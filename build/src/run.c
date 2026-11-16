/* run.c — orchestrate one run: cgroup -> clone -> maps -> go -> detach/wait */
#include "common.h"
#include <sys/epoll.h>

#ifndef __NR_pidfd_open
#if defined(__x86_64__)
#define __NR_pidfd_open 434
#elif defined(__aarch64__)
#define __NR_pidfd_open 424
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
#define __NR_pidfd_open 275
#elif defined(__arm__)
#define __NR_pidfd_open 434
#elif defined(__i386__)
#define __NR_pidfd_open 434
#endif
#endif

static inline int sys_pidfd_open(pid_t pid, unsigned int flags) {
    return syscall(__NR_pidfd_open, pid, flags);
}

static void restart_self(struct cfg *c) __attribute__((noreturn));
static void restart_keeper(const char *dir, const char *id);
static void rm_after_keeper(const char *dir, pid_t child);

static int pid_alive(pid_t pid) {
    char p[BUF_SIZE_SMALL], b[BUF_SIZE_MEDIUM];
    if (pid <= 0) return 0;
    snprintf(p, sizeof p, "/proc/%d/stat", pid);
    if (read_small(p, b, sizeof b) < 0) return 0;
    char *e = strrchr(b, ')');
    return e && e[1] == ' ' && e[2] && e[2] != 'Z';
}

static volatile pid_t g_child_pid = -1;
static volatile sig_atomic_t g_stop_sig;

/* Handler only does 2 async-signal-safe things: set flag + kill child.
 * fopen/rmdir/usleep/cgroup_cleanup MUST NOT be called in handler (not
 * async-signal-safe, can deadlock if signal arrives during malloc/stdio);
 * cleanup runs after waitpid in normal flow.
 * SA_RESTART: waitpid is restarted, but child was killed so it returns. */
static void on_stop(int sig) {
    g_stop_sig = sig;
    if (g_child_pid > 0) kill(g_child_pid, SIGKILL);
}

static void setup_signal_handlers(void) {
    struct sigaction sa = { .sa_handler = on_stop, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* 1s tick: wake waitpid to sample disk/health without polling. */
static volatile sig_atomic_t g_tick;
static void on_tick(int sig) { (void)sig; g_tick = 1; }

static void setup_tick(void) {
    struct sigaction sa = { .sa_handler = on_tick };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
}

/* Wait for child using pidfd + epoll (Linux 5.3+), fallback to waitpid+alarm. */
static int wait_child(pid_t child, int *status, long disk_limit, long long disk_base,
                      const char *rootfs, const char *cgdir, const char *health_cmd) {
    int pidfd = sys_pidfd_open(child, 0);
    int have_pidfd = (pidfd >= 0);

    int epfd = -1;
    if (have_pidfd) {
        epfd = epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) {
            close(pidfd);
            have_pidfd = 0;
        } else {
            struct epoll_event ev = { .events = EPOLLIN, .data.fd = pidfd };
            if (epoll_ctl(epfd, EPOLL_CTL_ADD, pidfd, &ev) < 0) {
                close(pidfd);
                close(epfd);
                have_pidfd = 0;
            }
        }
    }

    int disk_killed = 0;
    const int HEALTH_PERIOD = HEALTH_PERIOD_S;
    time_t next_health = time(NULL) + HEALTH_PERIOD;
    time_t next_disk = time(NULL) + 1;

    if (have_pidfd) {
        /* pidfd + epoll: wait for child exit with 1s timeout for periodic checks */
        struct epoll_event events[1];
        for (;;) {
            int n = epoll_wait(epfd, events, 1, 1000); /* 1s timeout */
            if (n > 0) {
                /* Child exited */
                if (waitpid(child, status, 0) == child) break;
            } else if (n < 0 && errno != EINTR) {
                /* epoll error, fallback to waitpid */
                have_pidfd = 0;
                break;
            }

            time_t now = time(NULL);

            if (disk_limit > 0 && now >= next_disk) {
                long long used = disk_usage(rootfs);
                if (used >= 0 && used - disk_base > disk_limit) {
                    fprintf(stderr, "minicontainer: disk quota exceeded: %lld bytes > %ld\n",
                            used - disk_base, disk_limit);
                    cgroup_kill(cgdir);
                    kill(child, SIGKILL);
                    waitpid(child, status, 0);
                    disk_killed = 1;
                    break;
                }
                next_disk = now + 1;
            }

            if (health_cmd && now >= next_health) {
                int hr = run_health_cmd(health_cmd);
                if (hr != 0) {
                    fprintf(stderr, "minicontainer: health check failed: %s\n", health_cmd);
                    kill(child, SIGTERM);
                    usleep(2000000);
                    cgroup_kill(cgdir);
                    waitpid(child, status, 0);
                    break;
                }
                next_health = now + HEALTH_PERIOD;
            }

            if (g_stop_sig) {
                cgroup_kill(cgdir);
                kill(child, SIGKILL);
                waitpid(child, status, 0);
                break;
            }
        }
    }

    if (!have_pidfd) {
        /* Fallback: waitpid blocking + alarm(1) */
        setup_tick();
        for (;;) {
            g_tick = 0;
            alarm(1);
            pid_t w = waitpid(child, status, 0);
            alarm(0);
            if (w == child) break;
            if (w < 0) {
                if (errno == ECHILD) break;
                if (errno != EINTR) die("waitpid");
            }

            time_t now = time(NULL);

            if (disk_limit > 0 && now >= next_disk) {
                long long used = disk_usage(rootfs);
                if (used >= 0 && used - disk_base > disk_limit) {
                    fprintf(stderr, "minicontainer: disk quota exceeded: %lld bytes > %ld\n",
                            used - disk_base, disk_limit);
                    cgroup_kill(cgdir);
                    kill(child, SIGKILL);
                    waitpid(child, status, 0);
                    disk_killed = 1;
                    break;
                }
                next_disk = now + 1;
            }

            if (health_cmd && now >= next_health) {
                int hr = run_health_cmd(health_cmd);
                if (hr != 0) {
                    fprintf(stderr, "minicontainer: health check failed: %s\n", health_cmd);
                    kill(child, SIGTERM);
                    usleep(2000000);
                    cgroup_kill(cgdir);
                    waitpid(child, status, 0);
                    break;
                }
                next_health = now + HEALTH_PERIOD;
            }
            usleep(1000);
        }
    }

    if (have_pidfd) {
        close(pidfd);
        close(epfd);
    }
    return disk_killed;
}

int do_run(struct cfg *c) {
    /* P1: run straight from the image store; --rootfs still wins if both. */
    static char imgroot[PATH_MAX];
    if (c->image && !c->rootfs) {
        if (img_rootfs(c->image, imgroot, sizeof imgroot) < 0) {
            fprintf(stderr, "minicontainer: khong co image '%s' (dung: images)\n", c->image);
            return EXIT_SETUP_ERROR;
        }
        c->rootfs = imgroot;
        if (!c->cmd || !c->cmd[0]) {
            int n = 0;
            char **av = img_argv(c->image, &n);
            if (!av || n == 0) {
                fprintf(stderr, "minicontainer: image '%s' khong co CMD, hay truyen lenh\n", c->image);
                return EXIT_SETUP_ERROR;
            }
            c->cmd = av;
        }
    }
    if (!c->rootfs) {
        fprintf(stderr, "minicontainer: can --rootfs hoac --image\n");
        return EXIT_SETUP_ERROR;
    }

    /* Mount targets and /etc/hosts must exist before the child (mapped root,
     * cannot write root-owned rootfs dirs) starts. */
    if (namespace_prepare_filesystem(c) < 0)
        return EXIT_SETUP_ERROR;

    /* P0.1: refuse a rootfs built for another arch before touching cgroups.
     * P3.8: --emulate opts into qemu-user instead (4-20x slower, needs
     * qemu-<arch>-static on the host; seccomp drops to the deny-list). */
    if (c->rootfs) {
        char rarch[BUF_SIZE_SMALL];
        char shp[PATH_MAX + BUF_SIZE_SMALL];
        snprintf(shp, sizeof shp, "%s/bin/sh", c->rootfs);
        if (elf_arch(shp, rarch, sizeof rarch) == 0 && strcmp(rarch, MC_ARCH) != 0) {
            if (!c->emulate) {
                fprintf(stderr, "minicontainer: rootfs la %s, may nay la %s\n", rarch, MC_ARCH);
                fprintf(stderr, "  -> dung ban <app>-%s-rootfs.tar.gz, hoac: build --platform=linux-%s\n",
                        MC_ARCH, MC_ARCH);
                fprintf(stderr, "  -> hoac chay voi --emulate (cham 4-20x)\n");
                return EXIT_SETUP_ERROR;
            }
            const char *q = NULL;
            if (strcmp(rarch, "aarch64") == 0) q = "qemu-aarch64-static";
            else if (strcmp(rarch, "x86_64") == 0) q = "qemu-x86_64-static";
            else if (strcmp(rarch, "armv7l") == 0) q = "qemu-arm-static";
            else if (strcmp(rarch, "riscv64") == 0) q = "qemu-riscv64-static";
            else if (strcmp(rarch, "i386") == 0) q = "qemu-i386-static";
            char qpath[PATH_MAX];
            int found = 0;
            if (q) {
                static const char *const dirs[] = { "/usr/bin", "/usr/local/bin", "/bin", "/usr/sbin" };
                for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
                    if (fmt_path(qpath, sizeof qpath, "%s/%s", dirs[i], q) == 0 &&
                        access(qpath, X_OK) == 0) { found = 1; break; }
                }
            }
            if (!found) {
                fprintf(stderr, "minicontainer: --emulate: rootfs la %s, khong tim thay %s\n",
                        rarch, q ? q : "qemu-user-static");
                fprintf(stderr, "  -> apt install qemu-user-static (hoac dung image dung arch)\n");
                return EXIT_SETUP_ERROR;
            }
            static char *eav[128];
            static char qemu_bind[] = "/dev/.mc-qemu";
            c->base_cmd = c->cmd;      /* saved for `start` replay */
            int n = 0;
            eav[n++] = qemu_bind;
            eav[n++] = "-L";
            eav[n++] = "/";
            for (char **a = c->cmd; a && *a && n < 127; a++) eav[n++] = *a;
            eav[n] = NULL;
            c->cmd = eav;
            if (fmt_path(c->emulate_arch, sizeof c->emulate_arch, "%s", qpath) < 0) {
                fprintf(stderr, "minicontainer: --emulate: qemu path too long\n");
                return EXIT_SETUP_ERROR;
            }
            if (c->seccomp_mode == 1 || c->seccomp_mode == SECCOMP_MODE_PROFILE) {
                fprintf(stderr, "minicontainer: --emulate: ha seccomp xuong deny-list (qemu can nhieu syscall)\n");
                c->seccomp_mode = 0;
            }
            fprintf(stderr, "minicontainer: --emulate: %s qua %s (cham 4-20x)\n", rarch, qpath);
        }
    }

    char cgdir[PATH_MAX];
    cgroup_setup(c, cgdir);

    /* R2-1: baseline before container writes; quota = additional amount. */
    long disk_limit = 0;
    long long disk_base = 0;
    if (c->max_disk > 0) {
        disk_base = disk_usage(c->rootfs);
        if (disk_base < 0)
            fprintf(stderr, "minicontainer: warning: cannot measure '%s', disabling disk watchdog (RLIMIT_FSIZE still active)\n", c->rootfs);
        else
            disk_limit = c->max_disk;
    }

    /* 2 sync pipes: ready (child->parent: entered userns) + go (parent->child: all done) */
    int readyfd[2], gofd[2];
    if (pipe2(readyfd, O_CLOEXEC) < 0 || pipe2(gofd, O_CLOEXEC) < 0) die("pipe");

    /* P0.2: detached containers write stdout/stderr into pipes so a logger
     * can frame them as json-file. Foreground keeps the tty untouched. */
    int lout[2] = { -1, -1 }, lerr[2] = { -1, -1 };
    if (c->detach) {
        if (pipe2(lout, O_CLOEXEC) < 0 || pipe2(lerr, O_CLOEXEC) < 0) die("pipe log");
    }
    char *stack = malloc(STACK_SIZE);
    if (!stack) die("malloc stack");

    int flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWUSER;
    if (strcmp(c->net, "isolated") == 0 || strcmp(c->net, "bridge") == 0)
        flags |= CLONE_NEWNET;

    struct child_arg carg = { .c = c, .go_rd = gofd[0], .ready_wr = readyfd[1],
                             .detach = c->detach,
                             .out_w = lout[1], .err_w = lerr[1] };
    memcpy(carg.cgdir, cgdir, sizeof cgdir);
    pid_t child = clone(child_fn, stack + STACK_SIZE, flags, &carg);
    if (child < 0) die("clone");
    close(gofd[0]); close(readyfd[1]); /* parent: read ready, write go */
    if (c->detach) { close(lout[1]); close(lerr[1]); }

    /* Setup signal handler to forward signals to child */
    g_child_pid = child;
    setup_signal_handlers();

    /* Wait for child to enter userns */
    {
        char b;
        if (read(readyfd[0], &b, 1) != 1) die("ready read");
        close(readyfd[0]);
    }

    userns_map(child);
    cgroup_attach(cgdir, child);

    /* --net=bridge: create veth, move into child netns, then "go" so child
     * can configure interface (child needs netns with veth already present). */
    struct bridge_cfg bc;
    int have_bc = 0;
    if (strcmp(c->net, "bridge") == 0) {
        if (bridge_setup(child, &bc, c->publish, c->npublish) < 0) {
            /* bridge_setup may have created a host veth before a later step failed. */
            bridge_teardown(child, c->publish, c->npublish);
            kill(child, SIGKILL);
            waitpid(child, NULL, 0);
            cgroup_cleanup(c, cgdir);
            die_msg("--net=bridge failed (requires iproute2 + root + veth/bridge module)");
        }
        have_bc = 1;
    }

    /* Tell child to proceed: write 'go' byte then serialized bridge_cfg */
    if (write(gofd[1], "x", 1) != 1) die("go write");
    if (have_bc && bridge_cfg_write(gofd[1], &bc) < 0) die("go bridge write");
    close(gofd[1]);

    /* Detach: no waitpid, no cgroup cleanup — write state and exit.
     * Child becomes init in its ns, lives in its cgroup/ns. No daemon. */
    if (c->detach) {
        char id[BUF_SIZE_SMALL], dir[BUF_SIZE_LARGE], f[PATH_MAX + BUF_SIZE_SMALL], v[BUF_SIZE_SMALL];
        mkdir(STATE_BASE, 0755);
        if (c->name) snprintf(id, sizeof id, "%s", c->name);
        else snprintf(id, sizeof id, "mc-%d", child);
        snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
        if (c->reuse_state) {
            if (mkdir(dir, 0755) < 0 && errno != EEXIST) die("mkdir state");
        } else {
            for (int n = 1; mkdir(dir, 0755) < 0; n++) {
                if (errno != EEXIST) die("mkdir state");
                if (c->name) snprintf(id, sizeof id, "%s-%d", c->name, n);
                else snprintf(id, sizeof id, "mc-%d-%d", child, n);
                snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
            }
        }
        snprintf(f, sizeof f, "%s/pid", dir); snprintf(v, sizeof v, "%d", child);
        write_file(f, v);
        snprintf(f, sizeof f, "%s/cgdir", dir);
        write_file(f, cgdir);
        /* Save cmd for ps display (space-joined) */
        {
            char cb[BUF_SIZE_LARGE]; size_t o = 0;
            for (char **a = c->cmd; *a && o < sizeof cb - 1; a++) {
                if (o) cb[o++] = ' ';
                size_t l = strlen(*a);
                if (o + l > sizeof cb - 1) l = sizeof cb - 1 - o;
                memcpy(cb + o, *a, l); o += l;
            }
            cb[o] = 0;
        snprintf(f, sizeof f, "%s/cmd", dir);
        write_file(f, cb);
        snprintf(f, sizeof f, "%s/seccomp", dir);
        { char sv[16];
          /* exec has no profile path after chroot; the deny-list is the
           * closest equivalent for `exec` into a profile-filtered container. */
          snprintf(sv, sizeof sv, "%d",
                   c->seccomp_mode == SECCOMP_MODE_PROFILE ? 0 : c->seccomp_mode);
          write_file(f, sv); }
        }
        snprintf(f, sizeof f, "%s/rootfs", dir);
        write_file(f, c->rootfs);
        /* P3.2/P3.12: replayable config for inspect/start. */
        if (state_save_run_config(dir, c) < 0)
            fprintf(stderr, "minicontainer: warning: cannot save run config in %s\n", dir);
        if (have_bc) {
            snprintf(f, sizeof f, "%s/ip", dir);
            write_file(f, bc.ip);
            /* Save publish list for stop/rm (different process) to clean up NAT ports. */
            if (c->npublish > 0) {
                char pb[BUF_SIZE_LARGE]; size_t o = 0;
                for (int i = 0; i < c->npublish && o < sizeof pb - 1; i++) {
                    if (o) pb[o++] = ' ';
                    size_t l = strlen(c->publish[i]);
                    if (o + l > sizeof pb - 1) l = sizeof pb - 1 - o;
                    memcpy(pb + o, c->publish[i], l); o += l;
                }
                pb[o] = 0;
                snprintf(f, sizeof f, "%s/publish", dir);
                write_file(f, pb);
            }
        }

        /* P0.2: json-file logger. One tiny process per detached container;
         * exits as soon as both output pipes reach EOF (no daemon). */
        if (lout[0] >= 0) {
            char ldir[BUF_SIZE_LARGE];
            if (!log_dir_for(dir, ldir, sizeof ldir)) {
                fprintf(stderr, "minicontainer: cannot create %s/logs: %s\n", dir, strerror(errno));
                kill(child, SIGKILL); waitpid(child, NULL, 0);
                cgroup_cleanup(c, cgdir);
                return 1;
            }
            pid_t lg = fork();
            if (lg == 0) {
                setsid();
                int nfd = open("/dev/null", O_RDWR);
                if (nfd >= 0) { dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2); if (nfd > 2) close(nfd); }
                log_logger(lout[0], lerr[0], ldir);
                _exit(0);
            }
            if (lg < 0) {
                fprintf(stderr, "minicontainer: cannot start log driver: %s\n", strerror(errno));
                kill(child, SIGKILL); waitpid(child, NULL, 0);
                cgroup_cleanup(c, cgdir);
                return 1;
            }
            close(lout[0]); close(lerr[0]);
        }

        /* Watchdog detached: 1 process/container, exits when container dies.
         * Only exists when using --disk or --health-cmd (no persistent daemon). */
        if (disk_limit > 0 || c->health_cmd) {
            pid_t wd = fork();
            if (wd == 0) {
                setsid();
                int nfd = open("/dev/null", O_RDWR);
                if (nfd >= 0) {
                    dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2);
                    if (nfd > 2) close(nfd);
                }
                container_watchdog_detached(disk_limit, c->rootfs, cgdir, dir,
                                            disk_base, c->health_cmd);
                _exit(0);
            }
            if (wd > 0) {
                snprintf(f, sizeof f, "%s/watchdog", dir);
                snprintf(v, sizeof v, "%d", wd);
                write_file(f, v);
            } else {
                fprintf(stderr, "minicontainer: warning: failed to create watchdog: %s\n", strerror(errno));
            }
        }
        /* Bridge reaper: clean up NAT + veth when detached container dies,
         * since parent has exited. Poll /proc (can't waitpid as reaper isn't parent). */
        if (have_bc) {
            pid_t rp = fork();
            if (rp == 0) {
                setsid();
                int nfd = open("/dev/null", O_RDWR);
                if (nfd >= 0) { dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2); if (nfd > 2) close(nfd); }
                for (;;) {
                    char sp[BUF_SIZE_LARGE], *p;
                    snprintf(sp, sizeof sp, "/proc/%d/stat", (int)child);
                    if (read_small(sp, sp, sizeof sp) != 0) break;  /* gone */
                    if ((p = strchr(sp, ')')) && p[2] == 'Z') break;  /* zombie */
                    sleep(1);
                }
                bridge_teardown(child, c->publish, c->npublish);
                _exit(0);
            }
        }

        /* P3.2: --restart keeper. No supervisor daemon exists for detached
         * runs, so one tiny process (only when the flag is used) waits for
         * the container to die and replays `start <id>`. `stop` kills it. */
        if (!c->reuse_state && c->restart && strcmp(c->restart, "no") != 0) {
            pid_t rk = fork();
            if (rk == 0) {
                restart_keeper(dir, id);
                _exit(0);
            }
            if (rk > 0) {
                snprintf(f, sizeof f, "%s/restart_pid", dir);
                snprintf(v, sizeof v, "%d", rk);
                write_file(f, v);
            } else {
                fprintf(stderr, "minicontainer: warning: cannot start restart keeper\n");
            }
        }

        /* P3.11: --rm keeper. Removes the state dir once the container exits;
         * never in the signal handler, always in this normal-flow process. */
        if (!c->reuse_state && c->rm_after) {
            pid_t rk = fork();
            if (rk == 0) {
                rm_after_keeper(dir, child);
                _exit(0);
            }
        }

        printf("%s\n", id);
        free(stack);
        return 0;
    }

    int status = 0;
    int disk_killed = wait_child(child, &status, disk_limit, disk_base,
                                 c->rootfs, cgdir, c->health_cmd);
    /* Child is reaped: never let the signal handler kill a recycled PID. */
    g_child_pid = -1;

    /* Ctrl-C / SIGTERM: handler killed child; clean up remaining processes
     * in cgroup then remove cgroup. */
    if (g_stop_sig) cgroup_kill(cgdir);
    cgroup_cleanup(c, cgdir);
    if (have_bc) bridge_teardown(child, c->publish, c->npublish);
    free(stack);
    if (g_stop_sig) return EXIT_SIGNAL_BASE + g_stop_sig;
    if (disk_killed) return EXIT_SIGNAL_BASE + SIGKILL;

    /* P3.2: --restart for foreground runs (detached uses the keeper above). */
    int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (c->restart && strcmp(c->restart, "no") != 0) {
        if (strcmp(c->restart, "always") == 0 || rc != 0) {
            fprintf(stderr, "minicontainer: restarting (%s)\n", c->restart);
            sleep(1);
            /* SIGTERM may arrive during the backoff; honor it instead of
             * exec'ing another instance the caller can no longer reach. */
            if (g_stop_sig) return EXIT_SIGNAL_BASE + g_stop_sig;
            restart_self(c);
        }
    }
    return rc;
}

/* Re-exec this binary with the original argv (P3.2 foreground restart). */
static void restart_self(struct cfg *c) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (c->orig_argv && c->orig_argc > 1) {
        if (n > 0) {
            exe[n] = 0;
            execv(exe, c->orig_argv);
        }
        execvp(c->orig_argv[0], c->orig_argv);
    }
    fprintf(stderr, "minicontainer: cannot re-exec for --restart: %s\n", strerror(errno));
    exit(1);
}

/* P3.2: detached restart keeper. Polls the container PID, then asks this same
 * binary to replay the saved config. `stop` kills this process first. */
static void restart_keeper(const char *dir, const char *id) {
    setsid();
    int nfd = open("/dev/null", O_RDWR);
    if (nfd >= 0) { dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2); if (nfd > 2) close(nfd); }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return;
    exe[n] = 0;
    for (;;) {
        char f[PATH_MAX + BUF_SIZE_SMALL], b[BUF_SIZE_SMALL];
        if (fmt_path(f, sizeof f, "%s/pid", dir) < 0) return;
        if (read_small(f, b, sizeof b) < 0) { sleep(1); continue; }
        pid_t pid = (pid_t)atoi(b);
        while (pid_alive(pid)) sleep(1);
        char *av[] = { exe, "start", (char *)id, NULL };
        run_wait(av);
        sleep(1);
    }
}

/* P3.11: --rm keeper. Normal-flow cleanup after the container exits. */
static void rm_after_keeper(const char *dir, pid_t child) {
    setsid();
    int nfd = open("/dev/null", O_RDWR);
    if (nfd >= 0) { dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2); if (nfd > 2) close(nfd); }
    while (pid_alive(child)) sleep(1);

    char f[PATH_MAX + BUF_SIZE_SMALL], cg[PATH_MAX] = "", rootfs[PATH_MAX] = "", b[BUF_SIZE_SMALL];
    if (fmt_path(f, sizeof f, "%s/cgdir", dir) == 0) read_small(f, cg, sizeof cg);
    if (fmt_path(f, sizeof f, "%s/rootfs", dir) == 0) read_small(f, rootfs, sizeof rootfs);
    if (fmt_path(f, sizeof f, "%s/watchdog", dir) == 0) {
        if (read_small(f, b, sizeof b) == 0) { int wd = atoi(b); if (wd > 1) kill(wd, SIGTERM); }
    }
    if (rootfs[0] && fmt_path(f, sizeof f, "%s/.oldroot", rootfs) == 0) rmdir(f);
    if (cg[0] && access(cg, F_OK) == 0) {
        cgroup_kill(cg);
        for (int i = 0; i < 50 && !cgroup_empty(cg); i++) usleep(100000);
        for (int i = 0; i < 20 && rmdir(cg) < 0 && errno != ENOENT; i++) usleep(100000);
    }
    static const char *const files[] = {
        "pid", "cgdir", "cmd", "rootfs", "ip", "publish", "watchdog",
        "restart_pid", "seccomp", "config", "argv", "disk_exceeded", "health_failed"
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
        if (fmt_path(f, sizeof f, "%s/%s", dir, files[i]) == 0) unlink(f);
    log_purge(dir);
    rmdir(dir);
}
