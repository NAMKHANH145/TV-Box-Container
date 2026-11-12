/* exec.c — run additional command in running container.
 *
 * Use setns() into user/mnt/uts/ipc/net/pid of PID 1, then fork (PID ns only
 * applies to child), chroot to container root, install seccomp and exec.
 * No nsenter needed; just need root on host. */
#include "common.h"
#include "namespace_setup.h"

struct nsfd {
    const char *name;
    int fd;
};

static int open_ns(pid_t pid, const char *name) {
    char p[BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "/proc/%d/ns/%s", pid, name);
    return open(p, O_RDONLY | O_CLOEXEC);
}

/* Container uses --net=host (or other ns same as host): setns into host ns
 * after entering userns will EPERM, and is not needed. */
static int same_ns(pid_t pid, const char *name) {
    char a[BUF_SIZE_SMALL], b[BUF_SIZE_SMALL];
    struct stat sa, sb;
    snprintf(a, sizeof a, "/proc/self/ns/%s", name);
    snprintf(b, sizeof b, "/proc/%d/ns/%s", pid, name);
    if (stat(a, &sa) < 0 || stat(b, &sb) < 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int do_exec(const char *id, char **cmd) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[BUF_SIZE_LARGE], f[PATH_MAX + BUF_SIZE_SMALL], buf[BUF_SIZE_SMALL];
    char cg[PATH_MAX] = "";
    int seccomp_mode = 0;
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) {
        fprintf(stderr, "no container '%s'\n", id);
        return 1;
    }
    pid_t pid = atoi(buf);
    snprintf(f, sizeof f, "/proc/%d/ns/pid", pid);
    if (access(f, F_OK) != 0) {
        fprintf(stderr, "container '%s' not running (pid %d)\n", id, pid);
        return 1;
    }
    snprintf(f, sizeof f, "%s/cgdir", dir);
    read_small(f, cg, sizeof cg);
    snprintf(f, sizeof f, "%s/seccomp", dir);
    { char sm[BUF_SIZE_SMALL];
      if (read_small(f, sm, sizeof sm) != 0 ||
          (strcmp(sm, "0") != 0 && strcmp(sm, "1") != 0 && strcmp(sm, "2") != 0)) {
          fprintf(stderr, "invalid persisted seccomp mode\n"); return 1;
      }
      seccomp_mode = atoi(sm);
    }
    if (!state_relationship_valid(cg, pid)) {
        fprintf(stderr, "container state no longer matches its cgroup\n"); return 1;
    }

    int rootfd;
    {
        char rp[BUF_SIZE_SMALL];
        snprintf(rp, sizeof rp, "/proc/%d/root", pid);
        rootfd = open(rp, O_PATH | O_DIRECTORY | O_CLOEXEC);
    }
    struct nsfd ns[] = {
        { "user", -1 }, { "mnt", -1 }, { "uts", -1 },
        { "ipc", -1 }, { "net", -1 }, { "pid", -1 },
    };
    int ok = rootfd >= 0;
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        if (same_ns(pid, ns[i].name)) continue;
        ns[i].fd = open_ns(pid, ns[i].name);
        if (ns[i].fd < 0) ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "cannot open namespace of '%s': %s\n", id, strerror(errno));
        if (rootfd >= 0) close(rootfd);
        for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++)
            if (ns[i].fd >= 0) close(ns[i].fd);
        return 1;
    }

    /* Open before changing mnt ns: after setns(mnt) host paths gone. */
    int cgfd = -1;
    if (cg[0]) {
        snprintf(f, sizeof f, "%s/cgroup.procs", cg);
        cgfd = open(f, O_WRONLY | O_CLOEXEC);
    }

    /* user ns first (get caps in target userns), then remaining ns. */
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        if (ns[i].fd < 0) continue;
        if (setns(ns[i].fd, 0) < 0) {
            fprintf(stderr, "setns(%s): %s\n", ns[i].name, strerror(errno));
            return 1;
        }
        close(ns[i].fd);
        /* Host uid 0 not in container's uid_map (0 -> 65534),
         * so must setuid(0) to become root in target userns and keep caps. */
        if (strcmp(ns[i].name, "user") == 0) {
            if (setresgid(0, 0, 0) < 0 || setresuid(0, 0, 0) < 0) {
                fprintf(stderr, "setuid in userns: %s\n", strerror(errno));
                return 1;
            }
        }
    }

    pid_t p = fork();
    if (p < 0) die("fork exec");
    if (p == 0) {
        if (fchdir(rootfd) < 0) _exit(EXIT_SETUP_ERROR);
        close(rootfd);
        if (chroot(".") < 0) _exit(EXIT_SETUP_ERROR);
        if (chdir("/") < 0) _exit(EXIT_SETUP_ERROR);
        if (cgfd >= 0) close(cgfd);

        /* Mount /proc and install seccomp (same as child_fn) */
        if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
            if (errno != EBUSY) _exit(EXIT_SETUP_ERROR);
        }
        namespace_drop_capabilities();
        namespace_install_seccomp(seccomp_mode);
        execvp(cmd[0], cmd);
        fprintf(stderr, "execvp %s: %s\n", cmd[0], strerror(errno));
        _exit(EXIT_CHILD_ERROR);
    }

    close(rootfd);
    if (cgfd >= 0) {
        char v[BUF_SIZE_SMALL];
        int n = snprintf(v, sizeof v, "%d", p);
        if (write(cgfd, v, n) < 0) { /* best-effort: container resources */ }
        close(cgfd);
    }

    int status = 0;
    while (waitpid(p, &status, 0) < 0 && errno == EINTR)
        ;
    return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_SIGNAL_BASE + WTERMSIG(status);
}
