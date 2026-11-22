/* build.c — Dockerfile subset -> image in the store.
 *
 * Supported: FROM (with --platform=), ARG, ENV, WORKDIR, COPY, ADD (local),
 *            RUN, CMD, ENTRYPOINT, USER, EXPOSE, VOLUME, LABEL (ignored),
 *            comments and line continuations.
 * Not supported (warned): multi-stage, ONBUILD, HEALTHCHECK, SHELL,
 *            remote ADD, .dockerignore.
 *
 * Each RUN executes /bin/sh -c inside the build rootfs in a private mount
 * namespace (proc + /dev bind). Results are cached per instruction key so a
 * rebuild after the last step is cheap; --no-cache forces a fresh run.
 *
 * --platform only picks the target arch: RUN for a foreign arch needs
 * binfmt_misc + qemu-user, otherwise it fails with exit 127 and a hint.
 */
#include "common.h"
#include <ctype.h>

#define BUILD_MAX_ENV 32
#define BUILD_MAX_ARGS 16

struct bstate {
    char rootfs[PATH_MAX];
    char workdir[PATH_MAX];
    char user[BUF_SIZE_SMALL];
    char **env;            /* KEY=VALUE, NULL-terminated */
    int nenv;
    char *cmd[BUILD_MAX_ARGS + 1];      /* CMD/ENTRYPOINT words (static bufs) */
    int ncmd;
    char *entry[BUILD_MAX_ARGS + 1];
    int nentry;
    char base_key[32];
    char arch[BUF_SIZE_SMALL];          /* target arch of the image being built */
};

/* Read arch= out of an image's meta file. */
static int img_arch(const char *name, char *out, size_t n) {
    char meta[PATH_MAX], line[BUF_SIZE_MEDIUM];
    fmt_path(meta, sizeof meta, "%s/images/%s/meta", img_base(), name);
    FILE *f = fopen(meta, "r");
    if (!f) return -1;
    int rc = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "arch=", 5) == 0) {
            char *nl = strchr(line + 5, '\n');
            if (nl) *nl = 0;
            fmt_path(out, n, "%s", line + 5);
            rc = 0;
            break;
        }
    }
    fclose(f);
    return rc;
}

/* OCI platform name -> the name the rest of the tool prints. */
static void platform_arch(const char *p, char *out, size_t n) {
    const char *s = strchr(p, '/');
    s = s ? s + 1 : p;
    if (strcmp(s, "amd64") == 0) s = "x86_64";
    else if (strcmp(s, "arm64") == 0) s = "aarch64";
    else if (strcmp(s, "arm") == 0 || strcmp(s, "armv7") == 0) s = "armv7l";
    else if (strcmp(s, "386") == 0) s = "i386";
    fmt_path(out, n, "%s", s);
}

static unsigned long long fnv(const char *s, unsigned long long h) {
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

static void key_of(char *out, size_t n, const char *parent, const char *ins) {
    unsigned long long h = 1469598103934665603ULL;
    h = fnv(parent, h);
    h = fnv("\n", h);
    h = fnv(ins, h);
    fmt_path(out, n, "%016llx", h);
}

/* Copy src -> dst with cp -a (preserves modes/symlinks/xattrs best effort). */
static int copy_path(const char *src, const char *dst) {
    char *const av[] = { "/usr/bin/cp", "-a", (char *)src, (char *)dst, NULL };
    return run_wait(av);
}

/* Run a shell command inside rootfs (private mount ns: proc + /dev). */
static int run_in_rootfs(struct bstate *b, const char *cmd) {
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        if (unshare(CLONE_NEWNS) < 0) {
            fprintf(stderr, "minicontainer: build: RUN can root (unshare: %s)\n", strerror(errno));
            _exit(125);
        }
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
        char pth[PATH_MAX], dv[PATH_MAX], rc[PATH_MAX];
        fmt_path(pth, sizeof pth, "%s/proc", b->rootfs);
        mkdir(pth, 0555);
        if (mount("proc", pth, "proc", 0, NULL) < 0 && errno != EBUSY) {
            fprintf(stderr, "minicontainer: build: mount proc: %s\n", strerror(errno));
            _exit(125);
        }
        fmt_path(dv, sizeof dv, "%s/dev", b->rootfs);
        mkdir(dv, 0755);
        if (mount("/dev", dv, NULL, MS_BIND | MS_REC, NULL) < 0) {
            fprintf(stderr, "minicontainer: build: bind /dev: %s\n", strerror(errno));
            _exit(125);
        }
        fmt_path(rc, sizeof rc, "%s/etc", b->rootfs);
        mkdir(rc, 0755);
        fmt_path(rc, sizeof rc, "%s/etc/resolv.conf", b->rootfs);
        /* Keep a real nameserver already shipped in the rootfs (e.g. via COPY):
         * the host often uses 127.0.0.53/127.0.0.11, which the runtime rejects. */
        int have_dns = 0;
        {
            FILE *rf = fopen(rc, "r");
            if (rf) {
                char rb[512] = "";
                size_t rn = fread(rb, 1, sizeof rb - 1, rf);
                fclose(rf);
                rb[rn] = '\0';
                have_dns = strstr(rb, "nameserver") && !strstr(rb, "127.0.0.");
            }
        }
        if (!have_dns) {
            char *const cpa[] = { "/usr/bin/cp", "-f", "/etc/resolv.conf", rc, NULL };
            run_wait(cpa);                   /* best effort: DNS for RUN */
        }
        if (chroot(b->rootfs) < 0) {
            fprintf(stderr, "minicontainer: build: chroot: %s\n", strerror(errno));
            _exit(125);
        }
        if (chdir(b->workdir[0] ? b->workdir : "/") < 0 && chdir("/") < 0) _exit(125);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        for (int i = 0; i < b->nenv; i++) {
            char kv[BUF_SIZE_LARGE];
            fmt_path(kv, sizeof kv, "%s", b->env[i]);
            char *eq = strchr(kv, '=');
            if (eq) { *eq = 0; setenv(kv, eq + 1, 1); }
        }
        execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    int st = 0;
    while (waitpid(p, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void env_set(struct bstate *b, const char *kv) {
    for (int i = 0; i < b->nenv; i++) {
        size_t l = strchr(b->env[i], '=') - b->env[i] + 1;
        if (strncmp(b->env[i], kv, l) == 0) {
            fmt_path(b->env[i], BUF_SIZE_LARGE, "%s", kv);
            return;
        }
    }
    if (b->nenv < BUILD_MAX_ENV) {
        b->env[b->nenv] = strdup(kv);
        b->nenv++;
        b->env[b->nenv] = NULL;
    }
}

/* Expand $VAR / ${VAR} from the build env. Unknown vars expand to "". */
static void expand(struct bstate *b, const char *in, char *out, size_t n) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < n; ) {
        if (*p != '$') { out[o++] = *p++; continue; }
        /* "$(...)" is shell command substitution: leave it for /bin/sh. */
        if (p[1] == '(') { out[o++] = *p++; continue; }
        const char *name = p + 1;
        int brace = (*name == '{');
        if (brace) name++;
        char key[BUF_SIZE_SMALL];
        size_t kl = 0;
        while ((*name && (isalnum((unsigned char)*name) || *name == '_')) && kl + 1 < sizeof key)
            key[kl++] = *name++;
        key[kl] = 0;
        if (brace && *name == '}') name++;
        const char *val = "";
        for (int i = 0; i < b->nenv; i++) {
            size_t l = strlen(key);
            if (strncmp(b->env[i], key, l) == 0 && b->env[i][l] == '=') { val = b->env[i] + l + 1; break; }
        }
        for (const char *v = val; *v && o + 1 < n; v++) out[o++] = *v;
        p = name;
    }
    out[o] = '\0';
}

/* Split "k=v" or "k v" ENV forms; returns 0 on success. */
static int parse_env_pair(struct bstate *b, const char *rest) {
    char kv[BUF_SIZE_LARGE];
    expand(b, rest, kv, sizeof kv);
    char *eq = strchr(kv, '=');
    if (!eq) {
        char *sp = strchr(kv, ' ');
        if (!sp) return -1;
        *sp = '=';
    }
    env_set(b, kv);
    return 0;
}

static int do_copy(struct bstate *b, const char *context, const char *args, int add) {
    char buf[BUF_SIZE_LARGE];
    fmt_path(buf, sizeof buf, "%s", args);
    char *parts[BUILD_MAX_ARGS + 1];
    int n = 0;
    for (char *t = strtok(buf, " \t"); t && n < BUILD_MAX_ARGS; t = strtok(NULL, " \t"))
        parts[n++] = t;
    if (n < 2) {
        fprintf(stderr, "minicontainer: build: COPY needs <src>... <dst>\n");
        return -1;
    }
    const char *dst = parts[n - 1];
    char dstfull[PATH_MAX];
    if (dst[0] == '/') fmt_path(dstfull, sizeof dstfull, "%s%s", b->rootfs, dst);
    else fmt_path(dstfull, sizeof dstfull, "%s/%s/%s", b->rootfs, b->workdir[0] ? b->workdir : "", dst);
    if (strstr(dstfull, "..")) { fprintf(stderr, "minicontainer: build: COPY dest escapes rootfs\n"); return -1; }

    int dir_dst = (dst[strlen(dst) - 1] == '/') || n > 2;
    if (dir_dst) {
        char *const mkd[] = { "/usr/bin/mkdir", "-p", dstfull, NULL };
        run_wait(mkd);
    } else {
        char parent[PATH_MAX];
        fmt_path(parent, sizeof parent, "%s", dstfull);
        char *sl = strrchr(parent, '/');
        if (sl) { *sl = 0; char *const mkd[] = { "/usr/bin/mkdir", "-p", parent, NULL }; run_wait(mkd); }
    }
    for (int i = 0; i < n - 1; i++) {
        if (strstr(parts[i], "..")) { fprintf(stderr, "minicontainer: build: COPY source escapes context\n"); return -1; }
        char src[PATH_MAX];
        fmt_path(src, sizeof src, "%s/%s", context, parts[i]);
        if (access(src, F_OK) != 0) {
            fprintf(stderr, "minicontainer: build: %s: no such file in context\n", parts[i]);
            return -1;
        }
        if (add && strncmp(parts[i], "http", 4) == 0) {
            fprintf(stderr, "minicontainer: build: remote ADD not supported, use COPY\n");
            return -1;
        }
        /* Docker copies the *contents* of a source directory, never the
         * directory itself: `COPY site/ /usr/share/nginx/html/` must land
         * index.html in html/, not html/site/. `cp -a dir/.` gives exactly
         * that; plain `cp -a dir` would nest one extra level. */
        char eff[PATH_MAX];
        struct stat sst;
        if (stat(src, &sst) == 0 && S_ISDIR(sst.st_mode))
            fmt_path(eff, sizeof eff, "%s/.", src);
        else
            fmt_path(eff, sizeof eff, "%s", src);
        if (copy_path(eff, dstfull) != 0) {
            fprintf(stderr, "minicontainer: build: cannot copy %s\n", parts[i]);
            return -1;
        }
    }
    return 0;
}

/* Split a JSON-ish array ["a","b"] or shell words. */
static int split_words(const char *s, char *out[], int max, char *storage, size_t sn) {
    int n = 0;
    size_t o = 0;
    const char *p = s;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        int quoted = 0;
        if (*p == '[') {
            p++;
            while (*p == ' ') p++;
            if (*p == '"') { quoted = 1; p++; }
        } else if (*p == '"') { quoted = 1; p++; }
        size_t start = o;
        while (*p && o + 1 < sn) {
            if (quoted && *p == '"') { p++; break; }
            if (!quoted && (*p == ' ' || *p == ']')) break;
            if (*p == '\\' && p[1]) p++;
            storage[o++] = *p++;
        }
        storage[o++] = '\0';
        out[n++] = storage + start;
        if (!quoted) { while (*p == ' ') p++; if (*p == ']') { p++; break; } }
        else { while (*p == ' ') p++; if (*p == ',') p++; if (*p == ']') { p++; break; } }
    }
    return n;
}

int do_build(int argc, char **argv) {
    const char *tag = NULL, *file = "Dockerfile", *context = ".", *platform = NULL;
    int no_cache = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) tag = argv[++i];
        else if (strncmp(argv[i], "-t", 2) == 0 && argv[i][2]) tag = argv[i] + 2;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) file = argv[++i];
        else if (strncmp(argv[i], "--platform=", 11) == 0) platform = argv[i] + 11;
        else if (strcmp(argv[i], "--platform") == 0 && i + 1 < argc) platform = argv[++i];
        else if (strcmp(argv[i], "--no-cache") == 0) no_cache = 1;
        else if (argv[i][0] != '-') context = argv[i];
    }
    if (!tag) {
        fprintf(stderr, "Usage: %s build -t NAME[:TAG] [-f Dockerfile] [--no-cache]"
                        " [--platform=linux/arm64] CONTEXT\n", argv[0]);
        return 1;
    }

    char dfpath[PATH_MAX];
    if (file[0] == '/') fmt_path(dfpath, sizeof dfpath, "%s", file);
    else fmt_path(dfpath, sizeof dfpath, "%s/%s", context, file);
    FILE *df = fopen(dfpath, "r");
    if (!df) { fprintf(stderr, "minicontainer: build: cannot read %s: %s\n", dfpath, strerror(errno)); return 1; }

    /* state */
    static struct bstate b;
    memset(&b, 0, sizeof b);
    fmt_path(b.arch, sizeof b.arch, "%s", MC_ARCH);
    if (platform && *platform) platform_arch(platform, b.arch, sizeof b.arch);
    b.env = malloc(sizeof(char *) * (BUILD_MAX_ENV + 1));
    if (!b.env) { fclose(df); return 1; }
    b.env[0] = NULL;
    static char entrystore[BUILD_MAX_ARGS * BUF_SIZE_SMALL];
    static char cmdstore[BUILD_MAX_ARGS * BUF_SIZE_SMALL];

    /* first pass: find FROM (needs to be first instruction) */
    char line[BUF_SIZE_XLARGE], logical[BUF_SIZE_JSON];
    int lineno = 0, have_base = 0;

    char work[] = "/var/tmp/mc-build-XXXXXX";
    if (!mkdtemp(work)) { fclose(df); fprintf(stderr, "minicontainer: build: mkdtemp: %s\n", strerror(errno)); return 1; }
    fmt_path(b.rootfs, sizeof b.rootfs, "%s/rootfs", work);
    mkdir(b.rootfs, 0755);

    int rc = 0;
    while (fgets(line, sizeof line, df)) {
        lineno++;
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        /* line continuation */
        while (line[strlen(line) ? strlen(line) - 1 : 0] == '\\') {
            line[strlen(line) - 1] = 0;
            char next[BUF_SIZE_XLARGE];
            if (!fgets(next, sizeof next, df)) break;
            lineno++;
            char *n2 = strchr(next, '\n'); if (n2) *n2 = 0;
            if (strlen(line) + strlen(next) + 1 >= sizeof logical) break;
            strcat(line, " ");
            strcat(line, next);
        }
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        char *rest = sp + 1;
        while (*rest == ' ') rest++;

        if (strcasecmp(line, "FROM") == 0) {
            /* FROM --platform=linux/arm64 base */
            if (strncmp(rest, "--platform=", 11) == 0) {
                char *sp2 = strchr(rest, ' ');
                if (sp2) {
                    *sp2 = '\0';
                    if (!platform) platform_arch(rest + 11, b.arch, sizeof b.arch);
                    rest = sp2 + 1;
                    while (*rest == ' ') rest++;
                }
            }
            char basev[PATH_MAX];
            expand(&b, rest, basev, sizeof basev);
            char *as = strcasestr(basev, " as ");
            if (as) *as = 0;
            char name[BUF_SIZE_MEDIUM];
            /* accept "alpine:3.19" or store name "alpine-3.19" */
            char tmp[BUF_SIZE_MEDIUM];
            fmt_path(tmp, sizeof tmp, "%s", basev);
            for (char *p = tmp; *p; p++) if (*p == ':' || *p == '/') *p = '-';
            fmt_path(name, sizeof name, "%s", tmp);
            char src[PATH_MAX];
            if (img_rootfs(name, src, sizeof src) < 0) {
                fprintf(stderr, "minicontainer: build: base image '%s' not found (load it first)\n", basev);
                rc = 1;
                break;
            }
            /* Store keys images by name only. Cross-building (requested != host)
             * from a local base is valid for COPY-only Dockerfiles; but building
             * a NATIVE image from a foreign base is almost always a stale ref
             * from a previous `pull --platform`, so fail loudly instead of
             * dying later with an opaque exec error. */
            if (platform && *platform && strcmp(b.arch, MC_ARCH) == 0) {
                char ba[BUF_SIZE_SMALL] = "";
                if (img_arch(name, ba, sizeof ba) == 0 && *ba && strcmp(ba, b.arch) != 0) {
                    fprintf(stderr,
                        "minicontainer: build: base '%s' is %s, requested %s; "
                        "pull it again with --platform=linux/%s\n",
                        basev, ba, b.arch, b.arch);
                    rc = 1;
                    break;
                }
            }
            char *const cpa[] = { "/usr/bin/cp", "-a", src, b.rootfs, NULL };
            /* cp -a src rootfs copies src INTO rootfs; use src/. form */
            char srcslash[PATH_MAX];
            fmt_path(srcslash, sizeof srcslash, "%s/.", src);
            char *const cpa2[] = { "/usr/bin/cp", "-a", srcslash, b.rootfs, NULL };
            (void)cpa;
            if (run_wait(cpa2) != 0) {
                fprintf(stderr, "minicontainer: build: cannot copy base rootfs\n");
                rc = 1;
                break;
            }
            fmt_path(b.base_key, sizeof b.base_key, "%s", name);
            have_base = 1;
            /* Without an explicit --platform the image inherits the base arch,
             * so building an aarch64 base on x86_64 stays aarch64. */
            if (!platform) {
                char ba[BUF_SIZE_SMALL];
                if (img_arch(name, ba, sizeof ba) == 0) fmt_path(b.arch, sizeof b.arch, "%s", ba);
            }
            printf("build: FROM %s (%s)\n", basev, b.arch);
            continue;
        }
        if (!have_base) {
            fprintf(stderr, "minicontainer: build: line %d: FROM must come first\n", lineno);
            rc = 1;
            break;
        }

        if (strcasecmp(line, "ARG") == 0) {
            char kv[BUF_SIZE_LARGE];
            expand(&b, rest, kv, sizeof kv);
            if (strchr(kv, '=')) env_set(&b, kv);
            continue;
        }
        if (strcasecmp(line, "ENV") == 0) {
            if (parse_env_pair(&b, rest) < 0) { fprintf(stderr, "minicontainer: build: bad ENV\n"); rc = 1; break; }
            continue;
        }
        if (strcasecmp(line, "WORKDIR") == 0) {
            char v[PATH_MAX];
            expand(&b, rest, v, sizeof v);
            if (v[0] != '/') {
                char tmp[PATH_MAX];
                fmt_path(tmp, sizeof tmp, "%s/%s", b.workdir[0] ? b.workdir : "", v);
                fmt_path(v, sizeof v, "%s", tmp);
            }
            fmt_path(b.workdir, sizeof b.workdir, "%s", v);
            char full[PATH_MAX];
            fmt_path(full, sizeof full, "%s%s", b.rootfs, v);
            char *const mkd[] = { "/usr/bin/mkdir", "-p", full, NULL };
            run_wait(mkd);
            printf("build: WORKDIR %s\n", v);
            continue;
        }
        if (strcasecmp(line, "COPY") == 0 || strcasecmp(line, "ADD") == 0) {
            char args[BUF_SIZE_LARGE];
            expand(&b, rest, args, sizeof args);
            if (do_copy(&b, context, args, strcasecmp(line, "ADD") == 0) < 0) { rc = 1; break; }
            printf("build: %s %s\n", line, args);
            continue;
        }
        if (strcasecmp(line, "RUN") == 0) {
            char cmd[BUF_SIZE_JSON];
            expand(&b, rest, cmd, sizeof cmd);
            char key[32], ckey[PATH_MAX];
            key_of(key, sizeof key, b.base_key, cmd);
            fmt_path(b.base_key, sizeof b.base_key, "%s", key);
            fmt_path(ckey, sizeof ckey, "%s/build-cache/%s", img_base(), key);
            /* Replaying a cached layer is cp -a (additive only): it cannot
             * reproduce removals. Never cache/replay a RUN that deletes. */
            int is_delete = strstr(cmd, "rm ") || strstr(cmd, " unlink") ||
                            strstr(cmd, "rmdir") || strstr(cmd, ": >");
            int hit = (!no_cache && !is_delete && access(ckey, F_OK) == 0);
            if (hit) {
                printf("build: RUN %s  [cache]\n", cmd);
                char *const cpa[] = { "/usr/bin/cp", "-a", ckey, b.rootfs, NULL };
                char srcslash[PATH_MAX];
                fmt_path(srcslash, sizeof srcslash, "%s/.", ckey);
                char *const cpa2[] = { "/usr/bin/cp", "-a", srcslash, b.rootfs, NULL };
                (void)cpa;
                if (run_wait(cpa2) != 0) { fprintf(stderr, "minicontainer: build: cache copy failed\n"); rc = 1; break; }
                continue;
            }
            printf("build: RUN %s\n", cmd);
            int st = run_in_rootfs(&b, cmd);
            if (st != 0) {
                fprintf(stderr, "minicontainer: build: RUN failed (exit %d)\n", st);
                if (strcmp(b.arch, MC_ARCH) != 0)
                    fprintf(stderr, "minicontainer: build: image la %s, may nay la %s — can"
                                    " binfmt_misc + qemu-%s de chay RUN (xem P3.8)\n",
                            b.arch, MC_ARCH, b.arch);
                rc = 1;
                break;
            }
            if (!no_cache && !is_delete) {
                char parent[PATH_MAX];
                fmt_path(parent, sizeof parent, "%s/build-cache", img_base());
                char *const mkd[] = { "/usr/bin/mkdir", "-p", parent, NULL };
                run_wait(mkd);
                char tmpc[PATH_MAX];
                fmt_path(tmpc, sizeof tmpc, "%s/.tmp-%d", parent, (int)getpid());
                char *const rav[] = { "/usr/bin/rm", "-rf", tmpc, NULL };
                run_wait(rav);
                if (copy_path(b.rootfs, tmpc) == 0) rename(tmpc, ckey);
            }
            continue;
        }
        if (strcasecmp(line, "CMD") == 0 || strcasecmp(line, "ENTRYPOINT") == 0) {
            char *dst_store = strcasecmp(line, "CMD") == 0 ? cmdstore : entrystore;
            char **dst = strcasecmp(line, "CMD") == 0 ? b.cmd : b.entry;
            int n = split_words(rest, dst, BUILD_MAX_ARGS, dst_store, sizeof cmdstore);
            dst[n] = NULL;
            if (strcasecmp(line, "CMD") == 0) b.ncmd = n; else b.nentry = n;
            continue;
        }
        if (strcasecmp(line, "USER") == 0) { fmt_path(b.user, sizeof b.user, "%s", rest); continue; }
        if (strcasecmp(line, "EXPOSE") == 0 || strcasecmp(line, "LABEL") == 0 ||
            strcasecmp(line, "VOLUME") == 0 || strcasecmp(line, "STOPSIGNAL") == 0) {
            continue;
        }
        if (strcasecmp(line, "HEALTHCHECK") == 0 || strcasecmp(line, "SHELL") == 0 ||
            strcasecmp(line, "ONBUILD") == 0) {
            fprintf(stderr, "warning: build: %s not supported, ignored\n", line);
            continue;
        }
        fprintf(stderr, "minicontainer: build: line %d: unknown instruction %s\n", lineno, line);
        rc = 1;
        break;
    }
    fclose(df);
    if (rc != 0 || !have_base) {
        char *const rav[] = { "/usr/bin/rm", "-rf", work, NULL };
        run_wait(rav);
        return rc ? rc : 1;
    }

    /* store the image */
    char ref[BUF_SIZE_MEDIUM];
    fmt_path(ref, sizeof ref, "%s", tag);
    char name[BUF_SIZE_MEDIUM];
    fmt_path(name, sizeof name, "%s", tag);
    for (char *p = name; *p; p++) if (*p == ':' || *p == '/') *p = '-';

    char dir[PATH_MAX], rootfs[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name);
    fmt_path(rootfs, sizeof rootfs, "%s/rootfs", dir);
    char *const rav[] = { "/usr/bin/rm", "-rf", dir, NULL };
    run_wait(rav);
    char *const mkd[] = { "/usr/bin/mkdir", "-p", dir, NULL };
    if (run_wait(mkd) != 0 || rename(b.rootfs, rootfs) < 0) {
        fprintf(stderr, "minicontainer: build: cannot store image: %s\n", strerror(errno));
        char *const rw[] = { "/usr/bin/rm", "-rf", work, NULL };
        run_wait(rw);
        return 1;
    }
    {
        char *const rw[] = { "/usr/bin/rm", "-rf", work, NULL };
        run_wait(rw);
    }

    /* argv = ENTRYPOINT + CMD */
    char path[PATH_MAX], buf[BUF_SIZE_HUGE];
    size_t o = 0;
    for (int pass = 0; pass < 2; pass++) {
        char **arr = pass == 0 ? b.entry : b.cmd;
        int n = pass == 0 ? b.nentry : b.ncmd;
        for (int i = 0; i < n; i++) {
            size_t l = strlen(arr[i]);
            if (o + l + 1 >= sizeof buf) break;
            memcpy(buf + o, arr[i], l); o += l; buf[o++] = '\0';
        }
    }
    if (o == 0) { memcpy(buf, "/bin/sh", 8); o = 8; }
    fmt_path(path, sizeof path, "%s/argv", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, buf, o); (void)w; close(fd); }

    o = 0;
    for (int i = 0; i < b.nenv; i++) {
        size_t l = strlen(b.env[i]);
        if (o + l + 1 >= sizeof buf) break;
        memcpy(buf + o, b.env[i], l); o += l; buf[o++] = '\0';
    }
    fmt_path(path, sizeof path, "%s/env", dir);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, buf, o); (void)w; close(fd); }

    char meta[PATH_MAX], line2[BUF_SIZE_LARGE];
    fmt_path(meta, sizeof meta, "%s/meta", dir);
    fmt_path(line2, sizeof line2, "name=%s\ntag=%s\narch=%s\nlayers=1\nbuilt=%ld\n",
             name, ref, b.arch, (long)time(NULL));
    write_file(meta, line2);
    printf("build: %s -> %s (%s)\n", ref, name, b.arch);
    printf("  run: minicontainer run --image=%s\n", name);
    for (int i = 0; i < b.nenv; i++) free(b.env[i]);
    free(b.env);
    return 0;
}
