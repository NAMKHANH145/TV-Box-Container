/* common.c — common utility functions */
#include "common.h"

void die(const char *msg) { perror(msg); exit(1); }

/* Parameter/validate error: don't print errno (avoid ": Success"). */
void die_msg(const char *msg) { fprintf(stderr, "minicontainer: %s\n", msg); exit(1); }

long parse_bytes(const char *s) {
    if (!s || !*s) return -1;
    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno || end == s || v < 0) return -1;
    long long mult = 1;
    if (*end) {
        switch (*end++) {
        case 'K': case 'k': mult = 1024LL; break;
        case 'M': case 'm': mult = 1024LL * 1024; break;
        case 'G': case 'g': mult = 1024LL * 1024 * 1024; break;
        default: return -1;
        }
        if (*end) return -1;
    }
    if (v > LONG_MAX / mult) return -1;
    return (long)(v * mult);
}

int try_write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    size_t len = strlen(val);
    ssize_t w = write(fd, val, len);
    int e = errno;
    close(fd);
    if (w != (ssize_t)len) { errno = e; return -1; }
    return 0;
}

void write_file(const char *path, const char *val) {
    if (try_write_file(path, val) < 0) die(path);
}

int read_small(const char *path, char *buf, size_t n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if (r <= 0) return -1;
    buf[r] = 0;
    while (r > 0 && (buf[r-1] == '\n' || buf[r-1] == ' ')) buf[--r] = 0;
    return 0;
}

static volatile sig_atomic_t g_alarm_fired = 0;
static void alarm_handler(int sig) { (void)sig; g_alarm_fired = 1; }

/* Run command with argv directly (no shell): used for tar/curl/sha256sum.
 * Timeout 30s via alarm. */
int run_wait(char *const av[]) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        /* These are privileged fixed helpers; never inherit a caller's PATH. */
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        execvp(av[0], av);
        _exit(EXIT_CHILD_ERROR);
    }
    struct sigaction sa = { .sa_handler = alarm_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    g_alarm_fired = 0;
    alarm(30);
    int st;
    while (waitpid(p, &st, 0) < 0) {
        if (errno == EINTR) {
            if (g_alarm_fired) {
                kill(p, SIGKILL);
                waitpid(p, &st, 0);
                alarm(0);
                return -1;
            }
            continue;
        }
        break;
    }
    alarm(0);
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

/* Like run_wait but capture stdout into out (truncated).
 * Timeout 30s via alarm. Returns -1 if output would exceed buffer. */
int run_capture(char *const av[], char *out, size_t n) {
    int fds[2];
    if (pipe(fds) < 0) return -1;
    pid_t p = fork();
    if (p < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (p == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(EXIT_CHILD_ERROR);
        close(fds[1]);
        setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
        execvp(av[0], av);
        _exit(EXIT_CHILD_ERROR);
    }
    close(fds[1]);
    struct sigaction sa = { .sa_handler = alarm_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    g_alarm_fired = 0;
    alarm(30);
    size_t used = 0;
    for (;;) {
        ssize_t r = read(fds[0], out + used, n - used - 1);
        if (r <= 0) break;
        used += (size_t)r;
        if (used + 1 >= n) {
            /* Output would exceed buffer - drain and return error */
            char drain[BUF_SIZE_LARGE];
            while (read(fds[0], drain, sizeof drain) > 0) {}
            close(fds[0]);
            alarm(0);
            /* Wait for child to avoid zombie */
            int st;
            waitpid(p, &st, 0);
            return -1;
        }
    }
    out[used] = '\0';
    close(fds[0]);
    int st;
    while (waitpid(p, &st, 0) < 0) {
        if (errno == EINTR) {
            if (g_alarm_fired) {
                kill(p, SIGKILL);
                waitpid(p, &st, 0);
                alarm(0);
                return -1;
            }
            continue;
        }
        break;
    }
    alarm(0);
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

int valid_name(const char *s) {
    if (!s || *s == '\0') return 0;
    if (strcmp(s, "..") == 0) return 0;
    for (const char *p = s; *p; p++)
        if (*p == '/' || *p < 0x20) return 0;
    return 1;
}

void validate_input(struct cfg *c) {
    if (c->net && strcmp(c->net, "host") != 0 && strcmp(c->net, "isolated") != 0
        && strcmp(c->net, "bridge") != 0)
        die_msg("--net must be host, isolated, or bridge");
    if (c->rootfs && strcmp(c->rootfs, "/") == 0)
        die_msg("--rootfs must be a path other than /");
    if (c->name && !valid_name(c->name))
        die_msg("--name invalid");
    if (c->cpu != -1 && (c->cpu < 1 || c->cpu > 100))
        die_msg("--cpu must be in range 1-100");
    if (c->ram != -1 && c->ram <= 0)
        die_msg("--ram must be > 0");
    if (c->pids != -1 && c->pids <= 0)
        die_msg("--pids must be > 0");
    if (c->io != -1 && c->io <= 0)
        die_msg("--io must be > 0");
    if (c->max_disk != -1 && c->max_disk <= 0)
        die_msg("--disk must be > 0");
    if (c->rootfs) {
        char p[PATH_MAX + BUF_SIZE_SMALL];
        snprintf(p, sizeof p, "%s/bin/sh", c->rootfs);
        struct stat st;
        /* lstat (don't follow) to accept Alpine's absolute symlinks
         * (link from bin/sh -> /bin/busybox) from host partition */
        if (lstat(p, &st) != 0) die_msg("--rootfs must contain /bin/sh");
        if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))
            die_msg("--rootfs must contain /bin/sh");
        char d[PATH_MAX + BUF_SIZE_SMALL];
        snprintf(d, sizeof d, "%s/bin", c->rootfs);
        struct stat ds;
        if (stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode))
            die_msg("--rootfs must have /bin");
    }
}
