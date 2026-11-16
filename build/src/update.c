/* update.c — package signature verification (P0.6).
 *
 * Packages are signed in minisign format (Ed25519).  Instead of vendoring
 * libsodium we decode the minisign armored key + signature ourselves and let
 * `openssl pkeyutl` do the curve maths: the key becomes a DER SPKI blob and
 * the signature stays a raw 64-byte Ed25519 signature.
 *
 *   pubkey : "untrusted comment: ...\n" + b64(alg[2] || keyid[8] || pk[32])
 *   .sig   : "untrusted comment: ...\n" + b64(alg[2] || keyid[8] || sig[64])
 *            "trusted comment: ...\n" + b64(global_sig[64])
 *
 * We verify the file signature (the part that covers the package bytes).
 */
#include "common.h"

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_decode(const char *in, unsigned char *out, size_t outn) {
    unsigned acc = 0;
    int bits = 0;
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r') break;
        int v = b64v((unsigned char)*p);
        if (v < 0) return -1;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= outn) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return (int)o;
}

static void b64_encode(const unsigned char *in, size_t n, char *out) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];  out[o++] = tab[v & 63];
        i += 3;
    }
    if (n - i == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = '='; out[o++] = '=';
    } else if (n - i == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];  out[o++] = '=';
    }
    out[o] = '\0';
}

/* First base64 line that is not a comment; returns decoded length or -1. */
static int read_armor(const char *path, unsigned char *out, size_t outn) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[BUF_SIZE_XLARGE];
    int n = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "untrusted comment:", 18) == 0) continue;
        if (strncmp(line, "trusted comment:", 16) == 0) break;
        n = b64_decode(line, out, outn);
        if (n > 0) break;
    }
    fclose(f);
    return n;
}

int verify_signature(const char *pkg, const char *keypath) {
    unsigned char pub[BUF_SIZE_MEDIUM], sig[BUF_SIZE_MEDIUM];
    int np = read_armor(keypath, pub, sizeof pub);
    if (np != 42) {
        fprintf(stderr, "minicontainer: verify: '%s' is not a minisign public key\n", keypath);
        return -1;
    }
    char sigpath[PATH_MAX + 8];
    if (snprintf(sigpath, sizeof sigpath, "%s.sig", pkg) >= (int)sizeof sigpath) return -1;
    int ns = read_armor(sigpath, sig, sizeof sig);
    if (ns != 74) {
        fprintf(stderr, "minicontainer: verify: missing or malformed %s\n", sigpath);
        return -1;
    }
    if (memcmp(pub + 2, sig + 2, 8) != 0) {
        fprintf(stderr, "minicontainer: verify: signature was made with a different key\n");
        return -1;
    }

    /* Ed25519 SubjectPublicKeyInfo: 302a300506032b6570032100 || pk */
    static const unsigned char der[] = {
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
    };
    unsigned char spki[sizeof der + 32];
    memcpy(spki, der, sizeof der);
    memcpy(spki + sizeof der, pub + 10, 32);
    char b64[BUF_SIZE_MEDIUM];
    b64_encode(spki, sizeof spki, b64);

    char pemfile[] = "/tmp/mc-key-XXXXXX", rawfile[] = "/tmp/mc-sig-XXXXXX";
    int kfd = mkstemp(pemfile);
    int sfd = mkstemp(rawfile);
    if (kfd < 0 || sfd < 0) {
        fprintf(stderr, "minicontainer: verify: cannot create temp file: %s\n", strerror(errno));
        if (kfd >= 0) { close(kfd); unlink(pemfile); }
        if (sfd >= 0) { close(sfd); unlink(rawfile); }
        return -1;
    }
    char pem[BUF_SIZE_MEDIUM];
    int pl = snprintf(pem, sizeof pem, "-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----\n", b64);
    int ok = 0;
    if (pl > 0 && write(kfd, pem, (size_t)pl) == pl &&
        write(sfd, sig + 10, 64) == 64) ok = 1;
    close(kfd);
    close(sfd);
    if (!ok) {
        fprintf(stderr, "minicontainer: verify: cannot write temp file\n");
        unlink(pemfile); unlink(rawfile);
        return -1;
    }

    char *const av[] = { "openssl", "pkeyutl", "-verify", "-pubin", "-inkey", pemfile,
                         "-rawin", "-in", (char *)pkg, "-sigfile", rawfile, NULL };
    int rc = run_wait(av);
    unlink(pemfile);
    unlink(rawfile);
    if (rc != 0) {
        fprintf(stderr, "minicontainer: verify: signature KHONG hop le cho %s\n", pkg);
        return -1;
    }
    printf("verify: chu ky hop le (%s)\n", keypath);
    return 0;
}

/* ---- P4: A/B rootfs slots, signed packages, health-gated rollback, delta ----
 *
 * Layout (base default /var/lib/minicontainer/update):
 *   slots/A/rootfs/            rootfs slot
 *   slots/A/manifest.json      optional package manifest (arch/version)
 *   current -> slots/A         atomic symlink flip
 *   previous                   last good slot
 *   link                       optional stable rootfs symlink path
 *
 * Package: tar[.gz|.zst] containing rootfs/ and optional manifest.json. */
#include "jsmn.h"

#define UPDATE_DEFAULT_BASE "/var/lib/minicontainer/update"

struct tar_check { int count; };

static int up_tar_name(const char *name, void *ctx) {
    struct tar_check *tc = ctx;
    if (name[0] == '/') return -1;
    for (const char *s = name; *s;) {
        const char *e = strchr(s, '/');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if (l == 2 && s[0] == '.' && s[1] == '.') return -1;
        s = e ? e + 1 : s + l;
    }
    if (++tc->count > 500000) return -1;
    return 0;
}

static int up_tar_type(const char *line, void *ctx) {
    (void)ctx;
    /* c/b/p = char/block device, fifo: never valid in an app rootfs. */
    if (line[0] == 'c' || line[0] == 'b' || line[0] == 'p') return -1;
    return 0;
}

static int check_package(const char *file) {
    struct tar_check tc = { 0 };
    if (tar_stream(file, 0, up_tar_name, &tc) < 0) {
        fprintf(stderr, "minicontainer: update: unsafe or unreadable package %s\n", file);
        return -1;
    }
    tc.count = 0;
    if (tar_stream(file, 1, up_tar_type, &tc) < 0) {
        fprintf(stderr, "minicontainer: update: package contains a device/fifo node\n");
        return -1;
    }
    return 0;
}

static int mkpath(const char *path, mode_t mode) {
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

static int rm_tree(const char *path) {
    char *const av[] = { "rm", "-rf", (char *)path, NULL };
    return run_wait(av);
}

static void update_base_path(char *out, size_t n, const char *arg) {
    const char *b = (arg && *arg) ? arg : getenv("MC_UPDATE_BASE");
    if (!b || !*b) b = UPDATE_DEFAULT_BASE;
    if (b[0] == '/') {
        snprintf(out, n, "%s", b);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) snprintf(cwd, sizeof cwd, ".");
        (void)fmt_path(out, n, "%s/%s", cwd, b);
    }
}

static void current_slot(const char *base, char *out, size_t n) {
    out[0] = 0;
    char cur[PATH_MAX], l[PATH_MAX];
    if (fmt_path(cur, sizeof cur, "%s/current", base) < 0) return;
    ssize_t r = readlink(cur, l, sizeof l - 1);
    if (r <= 0) return;
    l[r] = 0;
    const char *bn = strrchr(l, '/');
    bn = bn ? bn + 1 : l;
    if ((strcmp(bn, "A") == 0 || strcmp(bn, "B") == 0) && n >= 2) {
        out[0] = bn[0];
        out[1] = 0;
    }
}

static void read_previous(const char *base, char *out, size_t n) {
    char p[PATH_MAX];
    out[0] = 0;
    if (fmt_path(p, sizeof p, "%s/previous", base) < 0) return;
    if (read_small(p, out, n) < 0) out[0] = 0;
}

static void write_previous(const char *base, const char *slot) {
    char p[PATH_MAX];
    if (fmt_path(p, sizeof p, "%s/previous", base) < 0) return;
    write_file(p, slot ? slot : "");
}

/* Atomic symlink flip: write .current.tmp then rename over current. */
static int flip_current(const char *base, const char *slot) {
    char tmp[PATH_MAX], cur[PATH_MAX];
    if (fmt_path(tmp, sizeof tmp, "%s/.current.tmp", base) < 0 ||
        fmt_path(cur, sizeof cur, "%s/current", base) < 0) return -1;
    unlink(tmp);
    if (slot && slot[0]) {
        char target[BUF_SIZE_SMALL];
        snprintf(target, sizeof target, "slots/%s", slot);
        if (symlink(target, tmp) < 0) return -1;
        if (rename(tmp, cur) < 0) { int e = errno; unlink(tmp); errno = e; return -1; }
    } else {
        if (unlink(cur) < 0 && errno != ENOENT) return -1;
    }
    return 0;
}

static int ensure_link(const char *base, const char *linkpath) {
    if (!linkpath || !linkpath[0]) return 0;
    struct stat st;
    if (lstat(linkpath, &st) == 0 && !S_ISLNK(st.st_mode)) {
        fprintf(stderr, "minicontainer: update: %s exists and is not a symlink\n", linkpath);
        return -1;
    }
    char want[PATH_MAX], tmp[PATH_MAX];
    if (fmt_path(want, sizeof want, "%s/current/rootfs", base) < 0) return -1;
    if (fmt_path(tmp, sizeof tmp, "%s.tmp.%d", linkpath, (int)getpid()) < 0) return -1;
    unlink(tmp);
    if (symlink(want, tmp) < 0) return -1;
    if (rename(tmp, linkpath) < 0) { int e = errno; unlink(tmp); errno = e; return -1; }
    return 0;
}

static void read_link_setting(const char *base, char *out, size_t n) {
    char p[PATH_MAX];
    out[0] = 0;
    if (fmt_path(p, sizeof p, "%s/link", base) == 0) read_small(p, out, n);
}

static void read_manifest(const char *dir, char *arch, size_t an,
                          char *ver, size_t vn) {
    arch[0] = ver[0] = 0;
    char p[PATH_MAX];
    if (fmt_path(p, sizeof p, "%s/manifest.json", dir) < 0) return;
    char *js = NULL;
    jsmntok_t *t = NULL;
    int nt = 0;
    if (json_parse_file(p, &js, &t, &nt) < 0) return;
    int i = json_obj_get(js, t, 0, "arch");
    if (i >= 0) json_tok_copy(js, &t[i], arch, an);
    i = json_obj_get(js, t, 0, "version");
    if (i >= 0) json_tok_copy(js, &t[i], ver, vn);
    json_free(js, t);
}

static int update_health(const char *rootfs, const char *cmd) {
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) {
        fprintf(stderr, "minicontainer: update: cannot resolve /proc/self/exe\n");
        return -1;
    }
    exe[n] = 0;
    char *av[] = { exe, "run", "--rootfs", (char *)rootfs, "--net=host",
                   "--ram=64M", "--pids=64", "/bin/sh", "-c",
                   (char *)(cmd && *cmd ? cmd : "true"), NULL };
    return run_wait(av);
}

static const char *zstd_path(void) {
    static const char *const dirs[] = { "/usr/bin", "/bin", "/usr/local/bin" };
    static char p[PATH_MAX];
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        if (fmt_path(p, sizeof p, "%s/zstd", dirs[i]) == 0 && access(p, X_OK) == 0)
            return p;
    }
    return NULL;
}

static int update_delta(int apply, const char *ref, const char *in, const char *out) {
    const char *z = zstd_path();
    if (!z) {
        fprintf(stderr, "minicontainer: update: zstd not found (apt install zstd)\n");
        return 1;
    }
    char refopt[PATH_MAX + 32];
    snprintf(refopt, sizeof refopt, "--patch-from=%s", ref);
    char *av[10];
    int n = 0;
    av[n++] = (char *)z;
    if (apply) av[n++] = "-d";
    av[n++] = refopt;
    av[n++] = (char *)in;
    av[n++] = "-f";
    av[n++] = "-o";
    av[n++] = (char *)out;
    av[n] = NULL;
    if (run_wait(av) != 0) {
        fprintf(stderr, "minicontainer: update: zstd %s failed\n", apply ? "patch" : "diff");
        return 1;
    }
    struct stat a, b;
    long long ra = stat(in, &a) == 0 ? (long long)a.st_size : 0;
    long long rb = stat(out, &b) == 0 ? (long long)b.st_size : 0;
    if (ra > 0 && rb > 0)
        printf("update %s: %s -> %s (%lld -> %lld bytes, %.1f%%)\n",
               apply ? "patch" : "diff", in, out, ra, rb, 100.0 * (double)rb / (double)ra);
    else
        printf("update %s: %s -> %s\n", apply ? "patch" : "diff", in, out);
    return 0;
}

int do_update_cli(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "update") != 0) {
        fprintf(stderr, "Usage: minicontainer update <apply|status|rollback|current|diff|patch> ...\n");
        return 1;
    }
    const char *sub = argv[2];

    if (strcmp(sub, "apply") == 0) {
        const char *pkg = NULL, *key = NULL, *health = NULL;
        const char *base_arg = NULL, *link_arg = NULL;
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "--verify=", 9) == 0) key = argv[i] + 9;
            else if (strcmp(argv[i], "--verify") == 0 && i + 1 < argc) key = argv[++i];
            else if (strncmp(argv[i], "--health-cmd=", 13) == 0) health = argv[i] + 13;
            else if (strcmp(argv[i], "--health-cmd") == 0 && i + 1 < argc) health = argv[++i];
            else if (strncmp(argv[i], "--base=", 7) == 0) base_arg = argv[i] + 7;
            else if (strncmp(argv[i], "--link=", 7) == 0) link_arg = argv[i] + 7;
            else if (!pkg) pkg = argv[i];
            else { fprintf(stderr, "update apply: unexpected '%s'\n", argv[i]); return 1; }
        }
        if (!pkg) { fprintf(stderr, "Usage: minicontainer update apply <pkg.tar> [--verify=KEY] [--health-cmd=CMD] [--base=DIR] [--link=PATH]\n"); return 1; }
        if (key && *key && verify_signature(pkg, key) < 0) return 1;
        if (check_package(pkg) < 0) return 1;

        char base[PATH_MAX];
        update_base_path(base, sizeof base, base_arg);
        if (mkpath(base, 0755) < 0) { fprintf(stderr, "update: cannot create %s: %s\n", base, strerror(errno)); return 1; }

        char slots[PATH_MAX];
        if (fmt_path(slots, sizeof slots, "%s/slots", base) < 0 || mkpath(slots, 0755) < 0) return 1;

        char stage[PATH_MAX];
        if (fmt_path(stage, sizeof stage, "%s/.stage-XXXXXX", base) < 0 || !mkdtemp(stage)) {
            fprintf(stderr, "update: mkdtemp: %s\n", strerror(errno));
            return 1;
        }
        char *const tav[] = { "tar", "-xf", (char *)pkg, "-C", stage,
                              "--numeric-owner", "--xattrs", "--xattrs-include=*", NULL };
        char *const tav_plain[] = { "tar", "-xf", (char *)pkg, "-C", stage, "--numeric-owner", NULL };
        if (run_wait(tav) != 0 && run_wait(tav_plain) != 0) {
            fprintf(stderr, "update: cannot extract %s\n", pkg);
            rm_tree(stage);
            return 1;
        }

        char rootfs[PATH_MAX];
        if (fmt_path(rootfs, sizeof rootfs, "%s/rootfs", stage) < 0 || access(rootfs, F_OK) != 0) {
            fprintf(stderr, "update: package must contain rootfs/\n");
            rm_tree(stage);
            return 1;
        }
        char shp[PATH_MAX];
        if (fmt_path(shp, sizeof shp, "%s/bin/sh", rootfs) < 0 || access(shp, F_OK) != 0) {
            fprintf(stderr, "update: rootfs has no /bin/sh\n");
            rm_tree(stage);
            return 1;
        }
        char march[BUF_SIZE_SMALL] = "", mver[BUF_SIZE_SMALL] = "";
        read_manifest(stage, march, sizeof march, mver, sizeof mver);
        char rarch[BUF_SIZE_SMALL];
        if (elf_arch(shp, rarch, sizeof rarch) == 0) {
            if (strcmp(rarch, MC_ARCH) != 0) {
                fprintf(stderr, "update: rootfs is %s, this device is %s\n", rarch, MC_ARCH);
                rm_tree(stage);
                return 1;
            }
        } else if (march[0] && strcmp(march, MC_ARCH) != 0) {
            fprintf(stderr, "update: manifest arch %s != %s\n", march, MC_ARCH);
            rm_tree(stage);
            return 1;
        }

        /* Health-test the staged tree BEFORE touching the inactive slot.
         * Flipping first and rolling back is not enough in an A/B pair: the
         * inactive slot is the last good version, so overwriting it would
         * destroy the only fallback if the new image is bad. */
        char stageroot[PATH_MAX];
        if (fmt_path(stageroot, sizeof stageroot, "%s/rootfs", stage) < 0 ||
            update_health(stageroot, health) != 0) {
            fprintf(stderr, "minicontainer: update: health check failed, slot unchanged\n");
            rm_tree(stage);
            journal_event("update rejected health slot=none");
            return 1;
        }

        char old[BUF_SIZE_SMALL];
        current_slot(base, old, sizeof old);
        const char *target = strcmp(old, "A") == 0 ? "B" : "A";

        char slotdir[PATH_MAX];
        if (fmt_path(slotdir, sizeof slotdir, "%s/slots/%s", base, target) < 0) {
            rm_tree(stage);
            return 1;
        }
        rm_tree(slotdir);
        if (rename(stage, slotdir) < 0) {
            fprintf(stderr, "update: cannot install slot %s: %s\n", slotdir, strerror(errno));
            rm_tree(stage);
            return 1;
        }
        stage[0] = 0;

        write_previous(base, old);
        if (flip_current(base, target) < 0) {
            fprintf(stderr, "update: cannot flip current: %s\n", strerror(errno));
            return 1;
        }

        char linkbuf[PATH_MAX];
        read_link_setting(base, linkbuf, sizeof linkbuf);
        const char *lp = (link_arg && *link_arg) ? link_arg : (linkbuf[0] ? linkbuf : NULL);
        if (lp) {
            if (ensure_link(base, lp) < 0) {
                fprintf(stderr, "update: cannot point %s: %s\n", lp, strerror(errno));
                if (old[0]) flip_current(base, old);
                return 1;
            }
            if (link_arg && *link_arg) {
                char lf[PATH_MAX];
                if (fmt_path(lf, sizeof lf, "%s/link", base) == 0) write_file(lf, lp);
            }
        }

        printf("update: slot %s%s%s installed%s\n", target,
               mver[0] ? " version " : "", mver,
               old[0] ? "" : " (first slot)");
        printf("  rootfs: %s/rootfs\n", slotdir);
        if (lp) printf("  link:   %s -> %s/current/rootfs\n", lp, base);
        journal_event("update applied slot=%s version=%s", target, mver[0] ? mver : "?");
        return 0;
    }

    if (strcmp(sub, "status") == 0) {
        int json = 0;
        const char *base_arg = NULL;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json = 1;
            else if (strncmp(argv[i], "--base=", 7) == 0) base_arg = argv[i] + 7;
        }
        char base[PATH_MAX];
        update_base_path(base, sizeof base, base_arg);
        char cur[BUF_SIZE_SMALL], prev[BUF_SIZE_SMALL], link[PATH_MAX];
        current_slot(base, cur, sizeof cur);
        read_previous(base, prev, sizeof prev);
        read_link_setting(base, link, sizeof link);
        char ver[BUF_SIZE_SMALL] = "", march[BUF_SIZE_SMALL] = "";
        if (cur[0]) {
            char sd[PATH_MAX];
            fmt_path(sd, sizeof sd, "%s/slots/%s", base, cur);
            read_manifest(sd, march, sizeof march, ver, sizeof ver);
        }
        if (json) {
            printf("{\"base\":\"%s\",\"current\":\"%s\",\"previous\":\"%s\","
                   "\"link\":\"%s\",\"version\":\"%s\",\"arch\":\"%s\"}\n",
                   base, cur, prev, link, ver, march[0] ? march : MC_ARCH);
        } else {
            printf("base:     %s\ncurrent:  %s\nprevious: %s\nlink:     %s\nversion:  %s\narch:     %s\n",
                   base, cur[0] ? cur : "(none)", prev[0] ? prev : "(none)",
                   link[0] ? link : "(none)", ver[0] ? ver : "?",
                   march[0] ? march : MC_ARCH);
        }
        return 0;
    }

    if (strcmp(sub, "rollback") == 0) {
        const char *base_arg = NULL;
        for (int i = 3; i < argc; i++)
            if (strncmp(argv[i], "--base=", 7) == 0) base_arg = argv[i] + 7;
        char base[PATH_MAX];
        update_base_path(base, sizeof base, base_arg);
        char cur[BUF_SIZE_SMALL], prev[BUF_SIZE_SMALL];
        current_slot(base, cur, sizeof cur);
        read_previous(base, prev, sizeof prev);
        if (!prev[0]) {
            fprintf(stderr, "update: no previous slot to roll back to\n");
            return 1;
        }
        if (flip_current(base, prev) < 0) {
            fprintf(stderr, "update: rollback failed: %s\n", strerror(errno));
            return 1;
        }
        write_previous(base, cur);
        printf("update: rolled back %s -> %s\n", cur[0] ? cur : "none", prev);
        journal_event("update rolled back to slot=%s", prev);
        return 0;
    }

    if (strcmp(sub, "current") == 0) {
        const char *base_arg = NULL;
        for (int i = 3; i < argc; i++)
            if (strncmp(argv[i], "--base=", 7) == 0) base_arg = argv[i] + 7;
        char base[PATH_MAX];
        update_base_path(base, sizeof base, base_arg);
        char cur[BUF_SIZE_SMALL];
        current_slot(base, cur, sizeof cur);
        if (!cur[0]) {
            fprintf(stderr, "update: no active slot\n");
            return 1;
        }
        printf("%s/current/rootfs\n", base);
        return 0;
    }

    if (strcmp(sub, "diff") == 0 || strcmp(sub, "patch") == 0) {
        const char *a = NULL, *b = NULL, *out = NULL;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out = argv[++i]; continue; }
            if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2]) { out = argv[i] + 2; continue; }
            if (strncmp(argv[i], "--output=", 9) == 0) { out = argv[i] + 9; continue; }
            if (!a) a = argv[i];
            else if (!b) b = argv[i];
            else { fprintf(stderr, "update %s: unexpected '%s'\n", sub, argv[i]); return 1; }
        }
        if (!a || !b || !out) {
            fprintf(stderr, "Usage: minicontainer update %s <old> <%s> -o <out>\n",
                    sub, sub[0] == 'd' ? "new" : "patch");
            return 1;
        }
        return update_delta(sub[0] == 'p', a, b, out);
    }

    fprintf(stderr, "minicontainer: unknown update command '%s'\n", sub);
    return 1;
}
