/* pull.c — download and extract rootfs profile to /opt/rootfs-<profile>.
 * Everything uses direct argv (no shell) so URL/profile from
 * environment cannot inject commands. */
#include "common.h"
#include <sys/statvfs.h>
#include <sys/vfs.h>

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

#define PKG_SUFFIX "-rootfs.tar.gz"

static const char *const known_profiles[] = {
    "base-mini", "iot-gateway", "python-edge", "web-proxy"
};

static int is_known_profile(const char *p) {
    for (size_t i = 0; i < sizeof known_profiles / sizeof known_profiles[0]; i++)
        if (strcmp(p, known_profiles[i]) == 0) return 1;
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) { fprintf(stderr, "minicontainer: pull: open %s: %s\n", src, strerror(errno)); return -1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out < 0) { fprintf(stderr, "minicontainer: pull: write %s: %s\n", dst, strerror(errno)); close(in); return -1; }
    char buf[BUF_SIZE_HUGE];
    ssize_t r;
    while ((r = read(in, buf, sizeof buf)) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w <= 0) { close(in); close(out); return -1; }
            off += w;
        }
    }
    close(in);
    if (close(out) < 0) return -1;
    if (r < 0) return -1;
    return 0;
}

static int have_tool(const char *name) {
    char *const av[] = { (char *)name, (char *)"--version", NULL };
    return run_wait(av) == 0 ? 1 : 0;
}

static int fetch(const char *url, const char *dst) {
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        if (have_tool("/usr/bin/curl")) {
            char *const av[] = { "/usr/bin/curl", "-fsSL", "--max-time", "1800", "-o",
                (char *)dst, (char *)url, NULL };
            int rc = run_wait(av);
            if (rc == 0) return 0;
            fprintf(stderr, "minicontainer: pull: curl failed (exit %d): %s\n", rc, url);
            return -1;
        }
        if (have_tool("/usr/bin/wget")) {
            char *const av[] = { "/usr/bin/wget", "-q", "-T", "1800", "-O",
                (char *)dst, (char *)url, NULL };
            if (run_wait(av) == 0) return 0;
            fprintf(stderr, "minicontainer: pull: wget failed: %s\n", url);
            return -1;
        }
        fprintf(stderr, "minicontainer: pull: need curl or wget to fetch URL\n");
        return -1;
    }
    /* local file path (scp'd file to box also works this way) */
    return copy_file(url, dst);
}

/* Dangerous tar member: absolute path or contains ".." component */
static int member_unsafe(const char *line) {
    if (line[0] == '/') return 1;
    char copy[PATH_MAX + 1];
    if (strlen(line) >= sizeof copy) return 1;
    strcpy(copy, line);
    for (char *tok = strtok(copy, "/"); tok; tok = strtok(NULL, "/"))
        if (strcmp(tok, "..") == 0) return 1;
    return 0;
}

/* P0.4 limits: a rootfs has thousands of files, not millions. These caps stop
 * a tar bomb (huge entry count / gzip bomb) before it fills the eMMC. */
#define TAR_MAX_ENTRIES 200000
#define TAR_MAX_BYTES   (4LL * 1024 * 1024 * 1024)

struct tar_scan { long long entries, bytes; };

/* A relative link target is unsafe when it climbs above the rootfs
 * ("../../etc" from a top-level member), the same lexical rule `sdk pack`
 * uses. `depth` is the member's own directory depth: usr/bin/x -> ../../bin/y
 * resolves to bin/y and is fine. Absolute targets are normal in real images
 * (/bin/sh -> /bin/busybox) and stay inside. */
static int link_unsafe(const char *target, int depth) {
    if (target[0] == '/') return 0;
    char copy[PATH_MAX + 1];
    if (strlen(target) >= sizeof copy) return 1;
    strcpy(copy, target);
    for (char *tok = strtok(copy, "/"); tok; tok = strtok(NULL, "/")) {
        if (strcmp(tok, ".") == 0) continue;
        if (strcmp(tok, "..") == 0) { if (--depth < 0) return 1; }
        else depth++;
    }
    return 0;
}

static int scan_name(const char *line, void *v) {
    struct tar_scan *s = v;
    if (++s->entries > TAR_MAX_ENTRIES) {
        fprintf(stderr, "minicontainer: pull: refuse to extract, too many entries (>%d)\n", TAR_MAX_ENTRIES);
        return -1;
    }
    if (member_unsafe(line)) {
        fprintf(stderr, "minicontainer: pull: refuse to extract, tar contains dangerous path: %s\n", line);
        return -1;
    }
    return 0;
}

static int scan_verbose(const char *line, void *v) {
    struct tar_scan *s = v;
    char mode[16];
    if (sscanf(line, "%15s", mode) != 1) return 0;          /* "total N" */
    char type = mode[0];
    if (!strchr("-dlhcbps", type)) return 0;                /* header lines */
    if (type == 'c' || type == 'b' || type == 'p') {
        fprintf(stderr, "minicontainer: pull: refuse to extract, device/fifo member: %s\n", line);
        return -1;
    }
    long long size = 0;
    /* Device nodes print "1,3" here, so a failed parse simply means size 0. */
    if (sscanf(line, "%*s %*s %lld", &size) == 1 && size > 0) {
        s->bytes += size;
        if (s->bytes > TAR_MAX_BYTES) {
            fprintf(stderr, "minicontainer: pull: refuse to extract, uncompressed size > %lld MB\n",
                    TAR_MAX_BYTES / 1048576);
            return -1;
        }
    }
    /* GNU tar: "name link to target" for hardlinks, "name -> target" for symlinks. */
    const char *lt = strstr(line, " link to ");
    if (type == 'h' || lt) {
        const char *tgt = lt ? lt + 9 : "";
        if (member_unsafe(tgt) || tgt[0] == '\0') {
            fprintf(stderr, "minicontainer: pull: refuse to extract, hardlink escapes rootfs: %s\n", line);
            return -1;
        }
    } else if (type == 'l') {
        const char *arrow = strstr(line, " -> ");
        if (!arrow) return 0;
        /* member name sits after mode/owner/size/date/time: count its depth */
        int p = 0, q = 0, depth = 0;
        sscanf(line, "%15s%n", mode, &p);
        sscanf(line + p, " %*s %*s %*s %*s%n", &q);
        const char *nm = line + p + q;
        while (*nm == ' ') nm++;
        for (; *nm && *nm != ' '; nm++)
            if (*nm == '/') depth++;
        if (link_unsafe(arrow + 4, depth)) {
            fprintf(stderr, "minicontainer: pull: refuse to extract, symlink escapes rootfs: %s\n", line);
            return -1;
        }
    }
    return 0;
}

/* Stream a tar listing through a pipe: a fixed capture buffer silently
 * truncated large listings before, leaving most members unchecked. */
static int tar_scan(const char *pkg, int verbose, int (*fn)(const char *, void *), void *ctx) {
    int fds[2];
    if (pipe(fds) < 0) return -1;
    pid_t p = fork();
    if (p < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (p == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(EXIT_CHILD_ERROR);
        close(fds[1]);
        char *const av[] = { "/usr/bin/tar", verbose ? "-tvzf" : "-tzf", (char *)pkg, NULL };
        execv(av[0], av);
        _exit(EXIT_CHILD_ERROR);
    }
    close(fds[1]);
    FILE *f = fdopen(fds[0], "r");
    if (!f) { close(fds[0]); kill(p, SIGKILL); waitpid(p, NULL, 0); return -1; }
    char line[BUF_SIZE_HUGE];
    int rc = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n') line[--n] = '\0';
        else if (n == sizeof line - 1) {         /* member name longer than 64 KB */
            fprintf(stderr, "minicontainer: pull: refuse to extract, member name too long\n");
            rc = -1;
            break;
        }
        if (fn(line, ctx) < 0) { rc = -1; break; }
    }
    if (rc < 0) kill(p, SIGKILL);
    fclose(f);
    int st = 0;
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
    if (rc == 0 && (!WIFEXITED(st) || WEXITSTATUS(st) != 0)) rc = -1;
    return rc;
}

static int check_tar(const char *pkg) {
    struct tar_scan s = { 0, 0 };
    if (tar_scan(pkg, 0, scan_name, &s) < 0) {
        fprintf(stderr, "minicontainer: pull: cannot read tar listing\n");
        return -1;
    }
    if (s.entries == 0) { fprintf(stderr, "minicontainer: pull: tar empty\n"); return -1; }
    struct tar_scan v = { 0, 0 };
    if (tar_scan(pkg, 1, scan_verbose, &v) < 0) {
        fprintf(stderr, "minicontainer: pull: cannot inspect tar members\n");
        return -1;
    }
    return 0;
}

static int verify_sha256(const char *pkg, const char *want) {
    char *const av[] = { "/usr/bin/sha256sum", (char *)pkg, NULL };
    char out[BUF_SIZE_XXLARGE];
    if (run_capture(av, out, sizeof out) != 0) {
        fprintf(stderr, "minicontainer: pull: sha256sum failed\n");
        return -1;
    }
    char got[65] = "";
    size_t j = 0;
    for (size_t i = 0; i < strlen(out) && j < 64; i++) {
        char c = out[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) break;
        got[j++] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    got[j] = '\0';
    if (j != 64) { fprintf(stderr, "minicontainer: pull: cannot read sha256 from sha256sum\n"); return -1; }
    const char *w = want;
    if (strncmp(w, "sha256:", 7) == 0) w += 7;
    char low[65];
    for (size_t i = 0; i < 64; i++) {
        char c = w[i];
        if (!c) { fprintf(stderr, "minicontainer: pull: --sha256 must have 64 hex chars\n"); return -1; }
        low[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    low[64] = '\0';
    if (strcmp(got, low) != 0) {
        fprintf(stderr, "minicontainer: pull: sha256 mismatch\n  expected: %s\n  actual  : %s\n", low, got);
        return -1;
    }
    printf("pull: sha256 matches\n");
    return 0;
}

static long long file_size(const char *p) {
    struct stat st;
    if (stat(p, &st) < 0) return -1;
    return (long long)st.st_size;
}

int do_pull(int argc, char **argv) {
    const char *profile = NULL, *base = NULL, *dest = NULL, *sha = NULL;
    const char *sigkey = NULL;
    int force = 0, require_sig = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--url=", 6) == 0) base = a + 6;
        else if (strcmp(a, "--url") == 0 && i + 1 < argc) base = argv[++i];
        else if (strncmp(a, "--dest=", 7) == 0) dest = a + 7;
        else if (strcmp(a, "--dest") == 0 && i + 1 < argc) dest = argv[++i];
        else if (strncmp(a, "--sha256=", 9) == 0) sha = a + 9;
        else if (strcmp(a, "--sha256") == 0 && i + 1 < argc) sha = argv[++i];
        else if (strncmp(a, "--sig=", 6) == 0) { sigkey = a + 6; require_sig = 1; }
        else if (strcmp(a, "--sig") == 0 && i + 1 < argc) { sigkey = argv[++i]; require_sig = 1; }
        else if (strcmp(a, "--require-signature") == 0) require_sig = 1;
        else if (strcmp(a, "--force") == 0) force = 1;
        else if (!profile && valid_name(a)) profile = a;
        else { fprintf(stderr, "minicontainer: pull: unknown parameter: %s\n", a); return 1; }
    }
    if (!profile) {
        fprintf(stderr, "Usage: %s pull <profile> [--url=BASE] [--dest=DIR] [--sha256=SUM]\n"
                        "                    [--require-signature] [--sig=PUBKEY] [--force]\n", argv[0]);
        fprintf(stderr, "  profile: base-mini | iot-gateway | python-edge | web-proxy\n");
        return 1;
    }
    if (!is_known_profile(profile))
        fprintf(stderr, "minicontainer: pull: '%s' not a standard profile, trying anyway\n", profile);
    if (!base) {
        base = getenv("MC_PULL_URL");
        if (!base || !*base) {
            fprintf(stderr, "minicontainer: pull: missing source. Use --url=BASE (http(s):// or file path)"
                " or set MC_PULL_URL.\n");
            return 1;
        }
    }

    char url[PATH_MAX * 2];
    if ((size_t)snprintf(url, sizeof url, "%s/%s%s", base, profile, PKG_SUFFIX) >= sizeof url) {
        fprintf(stderr, "minicontainer: pull: path too long\n");
        return 1;
    }
    char destbuf[PATH_MAX];
    if (dest) {
        if (strlen(dest) >= sizeof destbuf || !*dest) { fprintf(stderr, "minicontainer: pull: --dest invalid\n"); return 1; }
        snprintf(destbuf, sizeof destbuf, "%s", dest);
    } else {
        if ((size_t)snprintf(destbuf, sizeof destbuf, "%s-%s", "/opt/rootfs", profile) >= sizeof destbuf) {
            fprintf(stderr, "minicontainer: pull: path too long\n");
            return 1;
        }
    }

    /* staging on same filesystem as dest for atomic rename */
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", destbuf);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) { fprintf(stderr, "minicontainer: pull: --dest must be absolute path\n"); return 1; }
    *slash = '\0';
    char pkg[PATH_MAX], stage[PATH_MAX];
    if ((size_t)snprintf(pkg, sizeof pkg, "%s/.mc-pull-%d%s", parent, (int)getpid(), PKG_SUFFIX) >= sizeof pkg ||
        (size_t)snprintf(stage, sizeof stage, "%s/.mc-pull-%d.unpacked", parent, (int)getpid()) >= sizeof stage) {
        fprintf(stderr, "minicontainer: pull: path too long\n");
        return 1;
    }

    struct stat dst;
    int dest_exists = (stat(destbuf, &dst) == 0);
    if (dest_exists && !force) {
        fprintf(stderr, "minicontainer: pull: %s already exists (use --force to overwrite)\n", destbuf);
        return 1;
    }

    printf("pull: downloading %s\n", url);
    if (mkdir(parent, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "minicontainer: pull: create %s: %s\n", parent, strerror(errno));
        return 1;
    }
    struct statfs sfs;
    if (statfs(parent, &sfs) == 0 && (unsigned long long)sfs.f_type == (unsigned long long)TMPFS_MAGIC)
        fprintf(stderr, "minicontainer: pull: %s on tmpfs (RAM): --read-only not usable"
            " here, and files lost on reboot. Use /opt/rootfs-<profile>.\n", parent);
    if (fetch(url, pkg) < 0) return 1;
    long long sz = file_size(pkg);
    if (sz <= 0) { fprintf(stderr, "minicontainer: pull: downloaded empty file\n"); unlink(pkg); return 1; }

    /* P0.6: fetch the detached signature next to the package, verify, drop it. */
    if (require_sig) {
        const char *key = sigkey ? sigkey : getenv("MC_SIGN_KEY");
        if (!key || !*key) key = "/etc/minicontainer/minisign.pub";
        char sigurl[PATH_MAX * 2 + 8], sigdst[PATH_MAX + 8];
        if (snprintf(sigurl, sizeof sigurl, "%s.sig", url) >= (int)sizeof sigurl ||
            snprintf(sigdst, sizeof sigdst, "%s.sig", pkg) >= (int)sizeof sigdst) {
            fprintf(stderr, "minicontainer: pull: path too long\n");
            unlink(pkg);
            return 1;
        }
        if (fetch(sigurl, sigdst) < 0) {
            fprintf(stderr, "minicontainer: pull: --require-signature nhung khong tai duoc %s\n", sigurl);
            unlink(pkg);
            return 1;
        }
        int vr = verify_signature(pkg, key);
        unlink(sigdst);
        if (vr < 0) { unlink(pkg); return 1; }
    }

    struct statvfs sv;
    if (statvfs(parent, &sv) == 0) {
        unsigned long long free_b = (unsigned long long)sv.f_bavail * sv.f_frsize;
        unsigned long long need = (unsigned long long)sz * 4;
        if (free_b < need) {
            fprintf(stderr, "minicontainer: pull: insufficient space (%.1f MB free, need ~%.1f MB)\n",
                free_b / 1048576.0, need / 1048576.0);
            unlink(pkg);
            return 1;
        }
    }
    printf("pull: %.1f MB\n", sz / 1048576.0);

    if (sha && verify_sha256(pkg, sha) < 0) { unlink(pkg); return 1; }
    if (check_tar(pkg) < 0) { unlink(pkg); return 1; }

    if (mkdir(stage, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "minicontainer: pull: create %s: %s\n", stage, strerror(errno));
        unlink(pkg);
        return 1;
    }
    char *const tav[] = { "/usr/bin/tar", "-xzf", pkg, "-C", stage, NULL };
    if (run_wait(tav) != 0) {
        fprintf(stderr, "minicontainer: pull: tar extraction failed\n");
        unlink(pkg);
        char *const rav[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(rav);
        return 1;
    }
    unlink(pkg);

    char final[PATH_MAX + 1];
    snprintf(final, sizeof final, "%s", destbuf);
    if (dest_exists) {
        char olddir[PATH_MAX + 8];
        snprintf(olddir, sizeof olddir, "%s.old", destbuf);
        char *const rav[] = { "/usr/bin/rm", "-rf", olddir, NULL };
        run_wait(rav);
        if (rename(destbuf, olddir) < 0) {
            fprintf(stderr, "minicontainer: pull: rename %s: %s\n", destbuf, strerror(errno));
            char *const cav[] = { "/usr/bin/rm", "-rf", stage, NULL };
            run_wait(cav);
            return 1;
        }
        if (rename(stage, final) < 0) {
            fprintf(stderr, "minicontainer: pull: rename %s -> %s: %s\n", stage, final, strerror(errno));
            rename(olddir, destbuf);
            return 1;
        }
        char *const rav2[] = { "/usr/bin/rm", "-rf", olddir, NULL };
        run_wait(rav2);
    } else if (rename(stage, final) < 0) {
        fprintf(stderr, "minicontainer: pull: rename %s -> %s: %s\n", stage, final, strerror(errno));
        char *const cav[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(cav);
        return 1;
    }

    long long done = disk_usage(final);
    if (done < 0) done = file_size(final);
    printf("pull: done %s (%.1f MB after extraction)\n", final, done / 1048576.0);
    printf("  run: minicontainer run --rootfs %s <cmd>\n", final);
    return 0;
}
