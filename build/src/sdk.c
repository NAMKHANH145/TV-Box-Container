/* sdk.c — `minicontainer sdk init|pack`: project skeleton + pack rootfs
 * into <profile>-rootfs.tar.gz in format accepted by `pull`.
 *
 * pack validates rootfs tree via nftw (FTW_PHYS, no symlink following) then
 * calls tar with direct argv (no shell). Also writes <out>.sha256 so
 * `pull --sha256=$(cut -d' ' -f1 <out>.sha256)` works immediately. */
#include "common.h"
#include <ftw.h>

#define PKG_SUFFIX "-rootfs.tar.gz"

static int g_bad;   /* 1 = rootfs tree has dangerous paths */
static char g_root[PATH_MAX + BUF_SIZE_SMALL];

/* Symlink allowed: absolute target (/bin/busybox, stays within container)
 * or relative but doesn't escape rootfs root (e.g. usr/bin/x -> ../../bin/y).
 * Check lexically, no filesystem access. */
static void check_link(const char *path, const char *target) {
    if (target[0] == '/') return;   /* absolute: always within container rootfs */
    size_t rl = strlen(g_root);
    if (strncmp(path, g_root, rl) != 0) {
        fprintf(stderr, "minicontainer: sdk pack: %s outside %s\n", path, g_root);
        g_bad = 1;
        return;
    }
    const char *rel = path + rl;
    while (*rel == '/') rel++;
    int depth = 0;
    for (const char *p = rel; *p; p++)
        if (*p == '/' && p[1]) depth++;
    const char *t = target;
    while (*t) {
        while (*t == '/') t++;
        const char *e = t;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - t);
        if (len == 2 && t[0] == '.' && t[1] == '.') {
            if (--depth < 0) {
                fprintf(stderr, "minicontainer: sdk pack: symlink escapes rootfs: %s -> %s\n", path, target);
                g_bad = 1;
                return;
            }
        } else if (len && !(len == 1 && t[0] == '.')) {
            depth++;
        }
        t = e;
    }
}

static int pack_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)st; (void)ftw;
    if (flag != FTW_SL) return 0;
    char buf[PATH_MAX + 1];
    ssize_t n = readlink(path, buf, PATH_MAX);
    if (n < 0) {
        fprintf(stderr, "minicontainer: sdk pack: readlink %s: %s\n", path, strerror(errno));
        g_bad = 1;
        return 0;
    }
    buf[n] = '\0';
    check_link(path, buf);
    return 0;
}

/* "/a/b/" -> "b" (returns pointer within s, no allocation) */
static const char *base_name(const char *s, char *tmp, size_t n) {
    size_t len = strlen(s);
    while (len > 1 && s[len - 1] == '/') len--;
    if (len >= n) return NULL;
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    const char *b = strrchr(tmp, '/');
    return b ? b + 1 : tmp;
}

static int mkdir_p(const char *path, mode_t mode) {
    char buf[PATH_MAX];
    if (snprintf(buf, sizeof buf, "%s", path) >= (int)sizeof buf) return -1;
    for (char *p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, mode) < 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(buf, mode) < 0 && errno != EEXIST) return -1;
    return 0;
}

static int dir_is_empty(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 1;   /* doesn't exist = treat as empty */
    struct dirent *e;
    int empty = 1;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { empty = 0; break; }
    closedir(d);
    return empty;
}

static int sdk_init(int argc, char **argv) {
    const char *dir = NULL, *profile = NULL, *ram = "32M", *cpu = "20", *pids = "64",
               *io = "1M", *disk = NULL, *net = "host", *cmd = "/bin/sh";
    int ro = 0;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--profile=", 10) == 0) profile = a + 10;
        else if (strncmp(a, "--ram=", 6) == 0) ram = a + 6;
        else if (strncmp(a, "--cpu=", 6) == 0) cpu = a + 6;
        else if (strncmp(a, "--pids=", 7) == 0) pids = a + 7;
        else if (strncmp(a, "--io=", 5) == 0) io = a + 5;
        else if (strncmp(a, "--disk=", 7) == 0) disk = a + 7;
        else if (strncmp(a, "--net=", 6) == 0) net = a + 6;
        else if (strncmp(a, "--cmd=", 6) == 0) cmd = a + 6;
        else if (strcmp(a, "--read-only") == 0) ro = 1;
        else if (!dir && a[0] != '-') dir = a;
        else { fprintf(stderr, "minicontainer: sdk init: unknown parameter: %s\n", a); return 1; }
    }
    if (!dir) {
        fprintf(stderr, "Usage: %s sdk init <dir> [--profile=NAME] [--ram=32M] [--cpu=20]"
            " [--pids=64] [--io=1M] [--disk=100M] [--net=host] [--read-only] [--cmd=/bin/sh]\n", "minicontainer");
        return 1;
    }
    if (strcmp(net, "host") && strcmp(net, "isolated") && strcmp(net, "bridge")) {
        fprintf(stderr, "minicontainer: sdk init: --net must be host, isolated, or bridge\n");
        return 1;
    }
    if (!dir_is_empty(dir)) {
        fprintf(stderr, "minicontainer: sdk init: %s already exists and not empty\n", dir);
        return 1;
    }

    char rootfs[PATH_MAX], conf[PATH_MAX], readme[PATH_MAX];
    if (snprintf(rootfs, sizeof rootfs, "%s/rootfs", dir) >= (int)sizeof rootfs ||
        snprintf(conf, sizeof conf, "%s/mc.conf", dir) >= (int)sizeof conf ||
        snprintf(readme, sizeof readme, "%s/README.md", dir) >= (int)sizeof readme) {
        fprintf(stderr, "minicontainer: sdk init: path too long\n");
        return 1;
    }
    if (mkdir_p(rootfs, 0755) < 0) {
        fprintf(stderr, "minicontainer: sdk init: create %s: %s\n", rootfs, strerror(errno));
        return 1;
    }

    char body[BUF_SIZE_XXLARGE];
    int n = snprintf(body, sizeof body,
        "# minicontainer project — generated by: minicontainer sdk init\n"
        "# Run:  minicontainer run --rootfs <dir>/rootfs $(grep ^cmd= mc.conf | cut -d= -f2-)\n"
        "profile=%s\n"
        "ram=%s\n"
        "cpu=%s\n"
        "pids=%s\n"
        "io=%s\n"
        "%sdisk=%s\n"
        "net=%s\n"
        "read-only=%d\n"
        "cmd=%s\n",
        profile ? profile : "base-mini", ram, cpu, pids, io,
        disk ? "" : "# ", disk ? disk : "100M",
        net, ro, cmd);
    if (n < 0 || n >= (int)sizeof body) {
        fprintf(stderr, "minicontainer: sdk init: mc.conf too long\n");
        return 1;
    }
    if (try_write_file(conf, body) < 0) {
        fprintf(stderr, "minicontainer: sdk init: write %s: %s\n", conf, strerror(errno));
        return 1;
    }
    if (try_write_file(readme,
        "# minicontainer project\n\n"
        "1. Copy rootfs into `rootfs/` (e.g. `tar -xzf base-mini-rootfs.tar.gz -C rootfs`)\n"
        "   or `minicontainer pull base-mini --dest=./rootfs` (use --force if exists).\n"
        "2. Edit `mc.conf` (ram/cpu/pids/io/disk/net/read-only/cmd).\n"
        "3. Run: `minicontainer run --rootfs ./rootfs /bin/sh`\n"
        "4. Pack again: `minicontainer sdk pack .` -> `<dir>-rootfs.tar.gz` + `.sha256`\n") < 0) {
        fprintf(stderr, "minicontainer: sdk init: write %s: %s\n", readme, strerror(errno));
        return 1;
    }
    printf("sdk: created %s (rootfs/ + mc.conf + README.md)\n", dir);
    printf("     next: copy rootfs into %s/rootfs then `minicontainer sdk pack %s`\n", dir, dir);
    return 0;
}

static int sdk_pack(int argc, char **argv) {
    const char *dir = NULL, *out = NULL;
    int force = 0;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--output=", 9) == 0) out = a + 9;
        else if (strcmp(a, "--output") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(a, "-o") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(a, "--force") == 0) force = 1;
        else if (!dir && a[0] != '-') dir = a;
        else { fprintf(stderr, "minicontainer: sdk pack: unknown parameter: %s\n", a); return 1; }
    }
    if (!dir) {
        fprintf(stderr, "Usage: %s sdk pack <dir> [-o OUT.tar.gz] [--force]\n", "minicontainer");
        return 1;
    }

    /* <dir>/rootfs if exists, else <dir> itself */
    char root[PATH_MAX + BUF_SIZE_SMALL], sub[PATH_MAX + BUF_SIZE_SMALL];
    struct stat st;
    if (snprintf(sub, sizeof sub, "%s/rootfs", dir) >= (int)sizeof sub) {
        fprintf(stderr, "minicontainer: sdk pack: path too long\n");
        return 1;
    }
    if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode))
        snprintf(root, sizeof root, "%s", sub);
    else
        snprintf(root, sizeof root, "%s", dir);
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "minicontainer: sdk pack: %s not a directory\n", root);
        return 1;
    }
    if (strcmp(root, "/") == 0) {
        fprintf(stderr, "minicontainer: sdk pack: refuse to pack /\n");
        return 1;
    }

    g_bad = 0;
    snprintf(g_root, sizeof g_root, "%s", root);
    if (nftw(root, pack_cb, 16, FTW_PHYS | FTW_MOUNT) != 0) {
        fprintf(stderr, "minicontainer: sdk pack: cannot traverse %s: %s\n", root, strerror(errno));
        return 1;
    }
    if (g_bad) {
        fprintf(stderr, "minicontainer: sdk pack: refuse to pack (symlink escapes root)\n");
        return 1;
    }
    char sh[PATH_MAX + BUF_SIZE_SMALL];
    if (snprintf(sh, sizeof sh, "%s/bin/sh", root) >= (int)sizeof sh) {
        fprintf(stderr, "minicontainer: sdk pack: path too long\n");
        return 1;
    }
    if (lstat(sh, &st) != 0)
        fprintf(stderr, "minicontainer: sdk pack: warning: %s missing /bin/sh, container will not run\n", root);

    char base[PATH_MAX], defout[PATH_MAX + BUF_SIZE_SMALL];
    const char *bn = base_name(dir, base, sizeof base);
    if (!bn) { fprintf(stderr, "minicontainer: sdk pack: directory name too long\n"); return 1; }
    if (!out) {
        snprintf(defout, sizeof defout, "%s%s", bn, PKG_SUFFIX);
        out = defout;
    }
    if (!force && access(out, F_OK) == 0) {
        fprintf(stderr, "minicontainer: sdk pack: %s already exists (use --force to overwrite)\n", out);
        return 1;
    }

    char oparent[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(oparent, sizeof oparent, "%s", out);
    char *osl = strrchr(oparent, '/');
    if (osl && osl != oparent) {
        *osl = '\0';
        if (mkdir_p(oparent, 0755) < 0) {
            fprintf(stderr, "minicontainer: sdk pack: create %s: %s\n", oparent, strerror(errno));
            return 1;
        }
    }

    printf("sdk: packing %s -> %s\n", root, out);
    char *const tav[] = { "tar", "-czf", (char *)out, "-C", root,
                          "--numeric-owner", "--owner=0", "--group=0", ".", NULL };
    int rc = run_wait(tav);
    if (rc != 0) {
        fprintf(stderr, "minicontainer: sdk pack: tar failed (exit %d)\n", rc);
        unlink(out);
        return 1;
    }

    char sum[BUF_SIZE_XXLARGE];
    char *const sav[] = { "sha256sum", (char *)out, NULL };
    if (run_capture(sav, sum, sizeof sum) != 0 || strlen(sum) < 64) {
        fprintf(stderr, "minicontainer: sdk pack: cannot compute sha256 (need sha256sum)\n");
        return 1;
    }
    char hash[65];
    memcpy(hash, sum, 64);
    hash[64] = '\0';
    char sumfile[PATH_MAX + BUF_SIZE_SMALL];
    if (snprintf(sumfile, sizeof sumfile, "%s.sha256", out) >= (int)sizeof sumfile) {
        fprintf(stderr, "minicontainer: sdk pack: path too long\n");
        return 1;
    }
    char line[PATH_MAX + BUF_SIZE_LARGE];
    char outbase[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(outbase, sizeof outbase, "%s", out);
    const char *ob = strrchr(outbase, '/');
    ob = ob ? ob + 1 : outbase;
    snprintf(line, sizeof line, "%s  %s\n", hash, ob);
    if (try_write_file(sumfile, line) < 0) {
        fprintf(stderr, "minicontainer: sdk pack: write %s: %s\n", sumfile, strerror(errno));
        return 1;
    }

    struct stat os;
    long long sz = (stat(out, &os) == 0) ? (long long)os.st_size : -1;
    printf("sdk: done %s (%.1f MB)\n", out, sz / 1048576.0);
    printf("     %s\n", sumfile);
    /* `pull` looks for <profile>-rootfs.tar.gz, so profile must match filename. */
    char prof[PATH_MAX + BUF_SIZE_LARGE];
    snprintf(prof, sizeof prof, "%s", ob);
    size_t plen = strlen(prof), slen = strlen(PKG_SUFFIX);
    if (plen > slen && strcmp(prof + plen - slen, PKG_SUFFIX) == 0) prof[plen - slen] = '\0';
    else {
        fprintf(stderr, "minicontainer: sdk pack: warning: filename must end with %s for `pull` to work\n", PKG_SUFFIX);
        snprintf(prof, sizeof prof, "%s", bn);
    }
    char urldir[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(urldir, sizeof urldir, "%s", out);
    char *slash = strrchr(urldir, '/');
    if (slash && slash != urldir) *slash = '\0';
    else snprintf(urldir, sizeof urldir, ".");
    printf("     distribute: minicontainer pull %s --url=%s --sha256=%s\n", prof, urldir, hash);
    return 0;
}

int do_sdk(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: minicontainer sdk init <dir> [flags]  |  sdk pack <dir> [-o OUT] [--force]\n");
        return 1;
    }
    if (strcmp(argv[2], "init") == 0) return sdk_init(argc - 3, argv + 3);
    if (strcmp(argv[2], "pack") == 0) return sdk_pack(argc - 3, argv + 3);
    fprintf(stderr, "minicontainer: sdk: unsupported command: %s\n", argv[2]);
    return 1;
}