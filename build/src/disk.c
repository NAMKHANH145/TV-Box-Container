/* disk.c — R2-1: disk quota watchdog + detached health checks.
 *
 * Kernel 6.18-meson64 has no ext4 project quota, so total capacity is
 * enforced in userspace: sample allocated blocks under the rootfs and
 * kill the container's cgroup once the delta over the baseline exceeds
 * --disk. Foreground mode folds the sampling into the existing wait
 * loop (zero extra process). Detached mode (-d) forks one small
 * watchdog that exits when the container does (no persistent daemon).
 * The same watchdog runs --health-cmd every 5s in detached mode.
 */
#include "common.h"
#include <ftw.h>

/* nftw callback to accumulate disk usage */
static long long g_du_total;
static int du_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)path; (void)ftw;
    /* du semantics: count blocks of files, directories, and symlinks */
    if (flag == FTW_F || flag == FTW_D || flag == FTW_SL)
        g_du_total += (long long)st->st_blocks * 512;
    return 0;
}

long long disk_usage(const char *path) {
    if (!path) return -1;
    g_du_total = 0;
    /* FTW_PHYS: do not follow symlinks (Alpine uses absolute links).
     * FTW_MOUNT: never cross into a bind mount under the rootfs. */
    if (nftw(path, du_cb, 16, FTW_PHYS | FTW_MOUNT) != 0)
        return -1;
    return g_du_total;
}

static void sleep_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Parse a command string into argv array. Modifies the input string.
 * Returns allocated argv array (NULL-terminated), caller must free. */
static char **parse_cmd(char *cmd, int *overflow) {
    int argc = 0;
    *overflow = 0;
    char **argv = calloc(64, sizeof(char *));
    if (!argv) return NULL;
    char *p = cmd;
    while (*p) {
        while (*p && (*p == ' ' || *p == '\t')) p++;
        if (!*p) break;
        if (argc >= 63) { *overflow = 1; break; }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argv;
}

static void cleanup_dirs(const char *cgdir, const char *rootfs) {
    for (int i = 0; i < 50; i++) {
        if (rmdir(cgdir) == 0 || errno == ENOENT) break;
        sleep_ms(100);
    }
    if (rootfs) {
        char o[PATH_MAX + BUF_SIZE_SMALL];
        snprintf(o, sizeof o, "%s/.oldroot", rootfs);
        rmdir(o);
    }
}

static void watchdog_fail(const char *cgdir, const char *rootfs, const char *wdf,
                          const char *statedir, const char *marker, const char *detail) {
    char mf[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(mf, sizeof mf, "%s/%s", statedir, marker);
    try_write_file(mf, detail);
    /* Aggressive cleanup: keep killing and waiting until cgroup is gone */
    for (int i = 0; i < 100; i++) {
        if (cgroup_empty(cgdir)) break;
        cgroup_kill(cgdir);
        sleep_ms(100);
    }
    for (int i = 0; i < 50; i++) {
        if (rmdir(cgdir) == 0 || errno == ENOENT) break;
        sleep_ms(100);
    }
    if (rootfs) {
        char o[PATH_MAX + BUF_SIZE_SMALL];
        snprintf(o, sizeof o, "%s/.oldroot", rootfs);
        rmdir(o);
    }
    unlink(wdf);
    _exit(0);
}

/* Run health_cmd via fork+execvp (no shell). Returns exit status or -1 on error. */
int run_health_cmd(const char *health_cmd) {
    if (!health_cmd) return -1;
    if (strpbrk(health_cmd, ";|&<>$`\n\r") != NULL) return -1;
    char *cmd_copy = strdup(health_cmd);
    if (!cmd_copy) return -1;
    int overflow = 0;
    char **argv = parse_cmd(cmd_copy, &overflow);
    if (!argv || !argv[0]) { free(argv); free(cmd_copy); return -1; }
    if (overflow) { free(argv); free(cmd_copy); return -1; }
    pid_t pid = fork();
    if (pid < 0) { free(argv); free(cmd_copy); return -1; }
    if (pid == 0) {
        setpgid(0, 0);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        execvp(argv[0], argv);
        _exit(EXIT_CHILD_ERROR);
    }
    int status = 0, waited_ok = 0;
    for (int ticks = 0; ticks < 100; ticks++) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) { waited_ok = 1; break; }
        if (waited < 0 && errno != EINTR) break;
        sleep_ms(100);
    }
    if (!waited_ok) { kill(-pid, SIGKILL); kill(pid, SIGKILL); waitpid(pid, &status, 0); }
    free(argv);
    free(cmd_copy);
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

void container_watchdog_detached(long limit, const char *rootfs, const char *cgdir,
                                 const char *statedir, long long baseline,
                                 const char *health_cmd) {
    char wdf[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(wdf, sizeof wdf, "%s/watchdog", statedir);

    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_DFL);   /* inherited handler belongs to the run parent */
    signal(SIGALRM, SIG_IGN);   /* parent's 1s tick must not wake/kill us */

    int empty_streak = 0;
    time_t next_health = time(NULL) + HEALTH_PERIOD_S;
    for (;;) {
        sleep_ms(DISK_PERIOD_MS);

        if (access(cgdir, F_OK) != 0) {   /* someone else already cleaned up */
            unlink(wdf);
            _exit(0);
        }

        if (limit > 0) {
            long long used = disk_usage(rootfs);
            if (used >= 0 && used - baseline > limit) {
                char detail[128];
                snprintf(detail, sizeof detail, "used=%lld limit=%ld\n", used - baseline, limit);
                watchdog_fail(cgdir, rootfs, wdf, statedir, "disk_exceeded", detail);
            }
        }

        time_t now = time(NULL);
        if (health_cmd && now >= next_health) {
            int hr = run_health_cmd(health_cmd);
            if (hr != 0) {
                char detail[PATH_MAX + BUF_SIZE_SMALL];
                snprintf(detail, sizeof detail, "cmd=%s rc=%d\n", health_cmd, hr);
                watchdog_fail(cgdir, rootfs, wdf, statedir, "health_failed", detail);
            }
            next_health = now + HEALTH_PERIOD_S;
        }

        /* Container gone on its own: clean cgroup + .oldroot and exit. */
        if (cgroup_empty(cgdir)) {
            if (++empty_streak >= 2) {
                cleanup_dirs(cgdir, rootfs);
                unlink(wdf);
                _exit(0);
            }
        } else {
            empty_streak = 0;
        }
    }
}