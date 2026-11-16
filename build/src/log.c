/* log.c — json-file driver: 1 JSON object per line, rotate 2 x 1 MB.
 *
 * Detached containers get a tiny logger process (host ns) reading the two
 * pipes wired to the container's stdout/stderr.  It stamps, escapes and
 * frames each line; when the file passes 1 MB it renames it to
 * container.log.1 (old .1 is dropped), so the on-disk cost is capped at
 * 2 MB per container.  No daemon: the logger exits when both pipes close. */
#include "common.h"
#include <poll.h>

#define LOG_MAX_BYTES (1024 * 1024)

const char *log_dir_for(const char *statedir, char *out, size_t n) {
    if (snprintf(out, n, "%s/logs", statedir) >= (int)n) return NULL;
    if (mkdir(out, 0755) < 0 && errno != EEXIST) return NULL;
    return out;
}

static int log_open(const char *dir, char *path, size_t pn) {
    if (snprintf(path, pn, "%s/container.log", dir) >= (int)pn) return -1;
    return open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
}

static void log_rotate(int *fd, const char *path) {
    char old[PATH_MAX + 16];
    if (snprintf(old, sizeof old, "%s.1", path) < (int)sizeof old) {
        unlink(old);
        rename(path, old);          /* best effort: rotation must not kill logs */
    }
    close(*fd);
    *fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
}

/* Escape one byte range into a JSON string body.  Returns bytes written
 * (always < outn).  Invalid UTF-8 is passed through; JSON readers tolerate
 * it and rewriting it would corrupt app output. */
static size_t json_escape(const char *in, size_t n, char *out, size_t outn) {
    static const char hex[] = "0123456789abcdef";
    size_t o = 0;
    for (size_t i = 0; i < n && o + 8 < outn; i++) {
        unsigned char ch = (unsigned char)in[i];
        switch (ch) {
        case '"':  out[o++] = '\\'; out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        case '\t': out[o++] = '\\'; out[o++] = 't';  break;
        default:
            if (ch < 0x20) {
                out[o++] = '\\'; out[o++] = 'u'; out[o++] = '0'; out[o++] = '0';
                out[o++] = hex[ch >> 4]; out[o++] = hex[ch & 15];
            } else {
                out[o++] = (char)ch;
            }
        }
    }
    return o;
}

static long log_emit(int fd, const char *stream, const char *buf, size_t n) {
    char esc[BUF_SIZE_HUGE];
    size_t e = json_escape(buf, n, esc, sizeof esc);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    gmtime_r(&ts.tv_sec, &tmv);
    char tsbuf[64];
    strftime(tsbuf, sizeof tsbuf, "%Y-%m-%dT%H:%M:%S", &tmv);
    char line[BUF_SIZE_HUGE + 256];
    int l = snprintf(line, sizeof line,
                     "{\"log\":\"%.*s\",\"stream\":\"%s\",\"time\":\"%s.%09ldZ\"}\n",
                     (int)e, esc, stream, tsbuf, (long)ts.tv_nsec);
    if (l > 0 && write(fd, line, (size_t)l) != l) {
        fprintf(stderr, "minicontainer: log: write failed: %s\n", strerror(errno));
        return 0;
    }
    return l > 0 ? l : 0;
}

/* Logger loop: reads stdout/stderr pipes until both reach EOF.
 * Returns 0 when the container's output is fully drained. */
int log_logger(int ofd, int efd, const char *dir) {
    char path[PATH_MAX + 16];
    int fd = log_open(dir, path, sizeof path);
    if (fd < 0) _exit(0);              /* no log file: don't block the app */

    struct {
        int fd;
        const char *stream;
        char buf[BUF_SIZE_HUGE];
        size_t len;
        int open;
    } ch[2] = { { ofd, "stdout", {0}, 0, 1 }, { efd, "stderr", {0}, 0, 1 } };
    long long written = 0;
    char raw[BUF_SIZE_HUGE];

    while (ch[0].open || ch[1].open) {
        struct pollfd pfd[2];
        int n = 0, idx[2];
        for (int i = 0; i < 2; i++) {
            if (!ch[i].open) continue;
            pfd[n].fd = ch[i].fd;
            pfd[n].events = POLLIN;
            pfd[n].revents = 0;
            idx[n++] = i;
        }
        if (n == 0) break;
        if (poll(pfd, n, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int k = 0; k < n; k++) {
            if (!(pfd[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            int i = idx[k];
            ssize_t r = read(ch[i].fd, raw, sizeof raw);
            if (r <= 0) {
                /* EOF: flush a trailing partial line, then stop reading it. */
                if (ch[i].len) {
                    if (written >= LOG_MAX_BYTES) { log_rotate(&fd, path); written = 0; }
                    written += log_emit(fd, ch[i].stream, ch[i].buf, ch[i].len);
                    ch[i].len = 0;
                }
                close(ch[i].fd);
                ch[i].open = 0;
                continue;
            }
            for (ssize_t b = 0; b < r; b++) {
                if (ch[i].len == sizeof ch[i].buf) {   /* absurdly long line */
                    if (written >= LOG_MAX_BYTES) { log_rotate(&fd, path); written = 0; }
                    written += log_emit(fd, ch[i].stream, ch[i].buf, ch[i].len);
                    ch[i].len = 0;
                }
                ch[i].buf[ch[i].len++] = raw[b];
                if (raw[b] == '\n') {
                    if (written >= LOG_MAX_BYTES) { log_rotate(&fd, path); written = 0; }
                    written += log_emit(fd, ch[i].stream, ch[i].buf, ch[i].len);
                    ch[i].len = 0;
                }
            }
        }
    }
    close(fd);
    return 0;
}

/* Decode one stored line: {"log":"<body>","stream":"...","time":"..."}
 * Prints <body> verbatim.  Returns 0 on success, -1 if not our format. */
static int print_line(const char *line, size_t n, FILE *out) {
    static const char pre[] = "{\"log\":\"";
    if (n < sizeof pre - 1 || memcmp(line, pre, sizeof pre - 1) != 0) return -1;
    const char *p = line + sizeof pre - 1;
    const char *end = line + n;
    const char *tail = NULL;
    for (const char *q = p; q + 11 < end; q++) {
        if (memcmp(q, "\",\"stream\":\"", 11) == 0) { tail = q; break; }
    }
    if (!tail) return -1;
    for (const char *q = p; q < tail; q++) {
        if (*q != '\\') { fputc(*q, out); continue; }
        if (++q >= tail) break;
        switch (*q) {
        case 'n': fputc('\n', out); break;
        case 'r': fputc('\r', out); break;
        case 't': fputc('\t', out); break;
        case '"': fputc('"', out); break;
        case '\\': fputc('\\', out); break;
        case 'u': {
            if (q + 4 < tail) {
                char h[5] = { q[1], q[2], q[3], q[4], 0 };
                fputc((int)strtol(h, NULL, 16), out);
                q += 4;
            }
            break;
        }
        default: fputc(*q, out); break;
        }
    }
    return 0;
}

static void dump_file(const char *path, int tail) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0) { close(fd); return; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return; }
    ssize_t r = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (r <= 0) { free(buf); return; }
    buf[r] = 0;

    /* --tail=N: walk backwards to the Nth line from the end. */
    char *start = buf;
    if (tail > 0) {
        char *p = buf + r;
        int seen = 0;
        if (p > buf && p[-1] == '\n') p--;      /* ignore trailing newline */
        while (p > buf) {
            if (p[-1] == '\n' && ++seen == tail) break;
            p--;
        }
        start = p;
    }
    for (char *p = start; p < buf + r; ) {
        char *nl = memchr(p, '\n', (size_t)(buf + r - p));
        size_t n = nl ? (size_t)(nl - p) + 1 : (size_t)(buf + r - p);
        print_line(p, n, stdout);
        p += n;
    }
    free(buf);
}

static int count_lines(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    int n = 0;
    char buf[BUF_SIZE_XXLARGE];
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0)
        for (ssize_t i = 0; i < r; i++)
            if (buf[i] == '\n') n++;
    close(fd);
    return n;
}

int do_logs(const char *id, int follow, int tail) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[BUF_SIZE_LARGE], path[PATH_MAX + 16], old[PATH_MAX + 32];
    snprintf(dir, sizeof dir, STATE_BASE "/%s/logs", id);
    snprintf(path, sizeof path, "%s/container.log", dir);
    snprintf(old, sizeof old, "%s.1", path);
    if (access(path, F_OK) != 0 && access(old, F_OK) != 0) {
        fprintf(stderr, "khong co log cho '%s'\n", id);
        return 1;
    }
    /* --tail spans the rotated file and the live one, oldest first. */
    int ncur = count_lines(path);
    if (tail > 0 && tail <= ncur) {
        dump_file(path, tail);
    } else {
        dump_file(old, tail > 0 ? tail - ncur : 0);
        dump_file(path, 0);
    }

    if (!follow) return 0;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) fd = open(path, O_RDONLY | O_CREAT | O_CLOEXEC, 0640);
    if (fd < 0) return 0;
    off_t off = lseek(fd, 0, SEEK_END);
    struct stat cur;
    fstat(fd, &cur);
    char buf[BUF_SIZE_HUGE];
    char acc[BUF_SIZE_HUGE];
    size_t alen = 0;
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r > 0) {
            for (ssize_t i = 0; i < r; i++) {
                if (alen == sizeof acc) alen = 0;
                acc[alen++] = buf[i];
                if (buf[i] == '\n') {
                    print_line(acc, alen, stdout);
                    alen = 0;
                    fflush(stdout);
                }
            }
            off += r;
            continue;
        }
        struct stat st;
        if (stat(path, &st) == 0 && (st.st_ino != cur.st_ino || st.st_size < off)) {
            /* rotated/truncated: reopen and continue from the top */
            close(fd);
            fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) return 0;
            fstat(fd, &cur);
            off = 0;
            alen = 0;
            continue;
        }
        usleep(200000);
    }
}

/* Remove stored logs so `rm` can rmdir the state dir. */
void log_purge(const char *statedir) {
    char dir[BUF_SIZE_LARGE];
    if (snprintf(dir, sizeof dir, "%s/logs", statedir) >= (int)sizeof dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[PATH_MAX + 16];
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) < (int)sizeof p) unlink(p);
    }
    closedir(d);
    rmdir(dir);
}
