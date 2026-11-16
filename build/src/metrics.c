/* metrics.c — P4.4: node_exporter textfile + journald events.
 *
 * `metrics` reads the same state dirs as `ps` and emits Prometheus text:
 *   minicontainer_info{version,arch} 1
 *   minicontainer_up{id} 1|0
 *   minicontainer_memory_bytes{id}
 *   minicontainer_pids{id}
 *   minicontainer_cpu_seconds_total{id}
 * Atomic write (tmp + rename) so node_exporter never reads a partial file.
 *
 * Events go to systemd-journald's native socket, best effort: with no
 * journald (or a non-systemd host) the send simply fails and is ignored.
 * No libsystemd dependency. */
#include "common.h"
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>

static int metrics_dir_default(char *out, size_t n) {
    const char *e = getenv("MC_METRICS_DIR");
    if (!e || !*e) e = "/var/lib/minicontainer/metrics";
    return fmt_path(out, n, "%s", e);
}

static int mkpath_simple(const char *path, mode_t mode) {
    char b[PATH_MAX];
    if (strlen(path) >= sizeof b) return -1;
    snprintf(b, sizeof b, "%s", path);
    for (char *s = b + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(b, mode) < 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    if (mkdir(b, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

static long long read_ll(const char *path) {
    char b[BUF_SIZE_SMALL];
    if (read_small(path, b, sizeof b) < 0) return -1;
    return atoll(b);
}

static double read_cpu_seconds(const char *cgdir) {
    char p[PATH_MAX];
    if (fmt_path(p, sizeof p, "%s/cpu.stat", cgdir) < 0) return -1;
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    char line[BUF_SIZE_MEDIUM];
    double v = -1;
    while (fgets(line, sizeof line, f)) {
        long long usec;
        if (sscanf(line, "usage_usec %lld", &usec) == 1) { v = (double)usec / 1e6; break; }
    }
    fclose(f);
    return v;
}

static void emit_container(FILE *f, const char *id, const char *dir) {
    char p[PATH_MAX], b[BUF_SIZE_SMALL];
    pid_t pid = 0;
    fmt_path(p, sizeof p, "%s/pid", dir);
    if (read_small(p, b, sizeof b) == 0) pid = (pid_t)atoi(b);
    int up = pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
    fprintf(f, "minicontainer_up{id=\"%s\"} %d\n", id, up);

    char cg[PATH_MAX];
    cg[0] = 0;
    fmt_path(p, sizeof p, "%s/cgdir", dir);
    if (read_small(p, cg, sizeof cg) < 0) cg[0] = 0;
    if (!cg[0]) return;
    long long mem = read_ll((fmt_path(p, sizeof p, "%s/memory.current", cg), p));
    if (mem >= 0) fprintf(f, "minicontainer_memory_bytes{id=\"%s\"} %lld\n", id, mem);
    long long pids = read_ll((fmt_path(p, sizeof p, "%s/pids.current", cg), p));
    if (pids >= 0) fprintf(f, "minicontainer_pids{id=\"%s\"} %lld\n", id, pids);
    double cpu = read_cpu_seconds(cg);
    if (cpu >= 0) fprintf(f, "minicontainer_cpu_seconds_total{id=\"%s\"} %.6f\n", id, cpu);
}

int do_metrics(int argc, char **argv) {
    int to_stdout = 0;
    const char *dirarg = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--stdout") == 0) to_stdout = 1;
        else if (strncmp(argv[i], "--dir=", 6) == 0) dirarg = argv[i] + 6;
        else { fprintf(stderr, "Usage: minicontainer metrics [--stdout] [--dir=DIR]\n"); return 1; }
    }
    char dir[PATH_MAX];
    if (dirarg) fmt_path(dir, sizeof dir, "%s", dirarg);
    else metrics_dir_default(dir, sizeof dir);

    char tmp[PATH_MAX], out[PATH_MAX];
    FILE *f;
    if (to_stdout) {
        f = stdout;
    } else {
        if (mkpath_simple(dir, 0755) < 0) {
            fprintf(stderr, "metrics: cannot create %s: %s\n", dir, strerror(errno));
            return 1;
        }
        if (fmt_path(out, sizeof out, "%s/minicontainer.prom", dir) < 0 ||
            fmt_path(tmp, sizeof tmp, "%s/.minicontainer.prom.tmp", dir) < 0) return 1;
        f = fopen(tmp, "w");
        if (!f) {
            fprintf(stderr, "metrics: %s: %s\n", tmp, strerror(errno));
            return 1;
        }
    }

    fprintf(f, "# HELP minicontainer_info Build info\n");
    fprintf(f, "# TYPE minicontainer_info gauge\n");
    fprintf(f, "minicontainer_info{version=\"%s\",arch=\"%s\"} 1\n", MC_VERSION, MC_ARCH);
    fprintf(f, "# HELP minicontainer_up 1 if the container PID is alive\n");
    fprintf(f, "# TYPE minicontainer_up gauge\n");

    int count = 0;
    DIR *d = opendir(STATE_BASE);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            if (!valid_name(e->d_name)) continue;
            char sd[PATH_MAX];
            if (fmt_path(sd, sizeof sd, "%s/%s", STATE_BASE, e->d_name) < 0) continue;
            struct stat st;
            if (stat(sd, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            emit_container(f, e->d_name, sd);
            count++;
        }
        closedir(d);
    }

    if (to_stdout) {
        fflush(f);
        return 0;
    }
    if (fclose(f) != 0 || rename(tmp, out) < 0) {
        fprintf(stderr, "metrics: cannot write %s: %s\n", out, strerror(errno));
        unlink(tmp);
        return 1;
    }
    printf("metrics: %d container(s) -> %s\n", count, out);
    return 0;
}

/* Best-effort native journald datagram: no libsystemd, no dependency. */
void journal_event(const char *fmt, ...) {
    char msg[BUF_SIZE_XLARGE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (char *p = msg; *p; p++)
        if (*p == '\n') *p = ' ';

    char data[BUF_SIZE_LARGE + BUF_SIZE_XLARGE];
    int n = snprintf(data, sizeof data,
                     "MESSAGE=%s\nSYSLOG_IDENTIFIER=minicontainer\nPRIORITY=6\n", msg);
    if (n <= 0 || (size_t)n >= sizeof data) return;

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "/run/systemd/journal/socket");
    (void)sendto(fd, data, (size_t)n, MSG_NOSIGNAL, (struct sockaddr *)&sa, sizeof sa);
    close(fd);
}
