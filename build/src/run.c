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
    char *stack = malloc(STACK_SIZE);
    if (!stack) die("malloc stack");

    int flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWUSER;
    if (strcmp(c->net, "isolated") == 0 || strcmp(c->net, "bridge") == 0)
        flags |= CLONE_NEWNET;

    struct child_arg carg = { .c = c, .go_rd = gofd[0], .ready_wr = readyfd[1],
                             .detach = c->detach };
    memcpy(carg.cgdir, cgdir, sizeof cgdir);
    pid_t child = clone(child_fn, stack + STACK_SIZE, flags, &carg);
    if (child < 0) die("clone");
    close(gofd[0]); close(readyfd[1]); /* parent: read ready, write go */

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
        for (int n = 1; mkdir(dir, 0755) < 0; n++) {
            if (errno != EEXIST) die("mkdir state");
            if (c->name) snprintf(id, sizeof id, "%s-%d", c->name, n);
            else snprintf(id, sizeof id, "mc-%d-%d", child, n);
            snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
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
        { char sv[16]; snprintf(sv, sizeof sv, "%d", c->seccomp_mode); write_file(f, sv); }
        }
        snprintf(f, sizeof f, "%s/rootfs", dir);
        write_file(f, c->rootfs);
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

        printf("%s\n", id);
        free(stack);
        return 0;
    }

    int status = 0;
    int disk_killed = wait_child(child, &status, disk_limit, disk_base,
                                 c->rootfs, cgdir, c->health_cmd);

    /* Ctrl-C / SIGTERM: handler killed child; clean up remaining processes
     * in cgroup then remove cgroup. */
    if (g_stop_sig) cgroup_kill(cgdir);
    cgroup_cleanup(c, cgdir);
    if (have_bc) bridge_teardown(child, c->publish, c->npublish);
    free(stack);
    if (g_stop_sig) return EXIT_SIGNAL_BASE + g_stop_sig;
    if (disk_killed) return EXIT_SIGNAL_BASE + SIGKILL;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
