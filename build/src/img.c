/* img.c — OCI image store: load (docker save / OCI layout), images, rmi.
 *
 * Layout under <base> (default /var/lib/minicontainer, MC_IMG_BASE overrides):
 *   images/<name>/rootfs        merged rootfs (layers applied in order)
 *   images/<name>/config.json   OCI config of the image
 *   images/<name>/meta          name/tag/arch/layers/created
 *   images/<name>/argv          NUL-separated Entrypoint+Cmd
 *   images/<name>/env           NUL-separated KEY=VALUE
 *
 * We deliberately keep the merged rootfs (no overlay yet): one image = one
 * directory, so `run --image` needs no mount and costs 0 MB of RAM.
 */
#include "common.h"
#include "jsmn.h"
#include <sys/statvfs.h>

#define IMG_BASE_DEFAULT "/var/lib/minicontainer"
#define IMG_JSON_MAX     (16 * 1024 * 1024)
#define IMG_MAX_TOKENS   65536

const char *img_base(void) {
    const char *e = getenv("MC_IMG_BASE");
    return (e && *e) ? e : IMG_BASE_DEFAULT;
}

/* "docker.io/library/alpine:3.19" -> "alpine-3.19" (valid_name charset). */
static void img_name(const char *ref, char *out, size_t n) {
    const char *p = strrchr(ref, '/');
    p = p ? p + 1 : ref;
    size_t o = 0;
    for (; *p && o + 1 < n; p++) {
        char c = *p;
        if (c == ':' || c == '/' || c == '@') c = '-';
        out[o++] = c;
    }
    out[o] = '\0';
    if (o == 0) fmt_path(out, n, "image");
}

/* OCI configs say amd64/arm64; the rest of the tool says x86_64/aarch64. */
static void arch_norm(const char *in, char *out, size_t n) {
    const char *m = in;
    if (strcmp(in, "amd64") == 0) m = "x86_64";
    else if (strcmp(in, "arm64") == 0) m = "aarch64";
    else if (strcmp(in, "arm") == 0) m = "armv7l";
    else if (strcmp(in, "386") == 0) m = "i386";
    snprintf(out, n, "%s", m);
}

static char *read_all(const char *path, size_t *len) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > IMG_JSON_MAX) { close(fd); return NULL; }
    char *buf = malloc((size_t)st.st_size + 1);
    if (!buf) { close(fd); return NULL; }
    ssize_t r = read(fd, buf, (size_t)st.st_size);
    close(fd);
    if (r != st.st_size) { free(buf); return NULL; }
    buf[r] = '\0';
    *len = (size_t)r;
    return buf;
}

/* Write the image's argv/env side files from its config.json. */
static void write_config_side(const char *js, jsmntok_t *t, int ntok, const char *dir) {
    (void)ntok;
    char path[PATH_MAX], buf[BUF_SIZE_HUGE];
    size_t o = 0;
    /* OCI config: {architecture, os, config:{Env,Cmd,Entrypoint,...}} */
    int cfg = json_obj_get(js, t, 0, "config");
    if (cfg < 0) cfg = 0;
    int ep = json_obj_get(js, t, cfg, "Entrypoint");
    int cmd = json_obj_get(js, t, cfg, "Cmd");
    for (int pass = 0; pass < 2; pass++) {
        int arr = pass == 0 ? ep : cmd;
        if (arr < 0) continue;
        for (int k = 0; k < t[arr].size; k++) {
            int e = json_arr_nth(t, arr, k);
            if (e < 0) break;
            size_t len = (size_t)(t[e].end - t[e].start);
            if (o + len + 1 >= sizeof buf) break;
            memcpy(buf + o, js + t[e].start, len);
            o += len;
            buf[o++] = '\0';
        }
    }
    if (o == 0) {                       /* no default command in the image */
        buf[0] = '/'; buf[1] = 'b'; buf[2] = 'i'; buf[3] = 'n'; buf[4] = '/';
        buf[5] = 's'; buf[6] = 'h'; buf[7] = '\0';
        o = 8;
    }
    fmt_path(path, sizeof path, "%s/argv", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, buf, o); (void)w; close(fd); }

    o = 0;
    int env = json_obj_get(js, t, cfg, "Env");
    if (env >= 0) {
        for (int k = 0; k < t[env].size; k++) {
            int e = json_arr_nth(t, env, k);
            if (e < 0) break;
            size_t len = (size_t)(t[e].end - t[e].start);
            if (o + len + 1 >= sizeof buf) break;
            memcpy(buf + o, js + t[e].start, len);
            o += len;
            buf[o++] = '\0';
        }
    }
    fmt_path(path, sizeof path, "%s/env", dir);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, buf, o); (void)w; close(fd); }
}

/* sha256sum of a blob must equal the digest it is filed under. */
static int verify_blob(const char *stage, const char *rel, const char *digest) {
    char full[PATH_MAX];
    if (fmt_path(full, sizeof full, "%s/%s", stage, rel) >= (int)sizeof full) return -1;
    char *const av[] = { "/usr/bin/sha256sum", full, NULL };
    char out[BUF_SIZE_XLARGE];
    if (run_capture(av, out, sizeof out) != 0) return -1;
    const char *want = strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : digest;
    if (strlen(want) != 64 || strncmp(out, want, 64) != 0) {
        fprintf(stderr, "minicontainer: load: digest mismatch for %s\n", rel);
        return -1;
    }
    return 0;
}

static int blob_rel(const char *stage, const char *digest, char *out, size_t n) {
    const char *hex = strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : digest;
    if (strlen(hex) != 64) return -1;
    if (fmt_path(out, n, "blobs/sha256/%s", hex) >= (int)n) return -1;
    char full[PATH_MAX];
    if (fmt_path(full, sizeof full, "%s/%s", stage, out) >= (int)sizeof full) return -1;
    return access(full, R_OK);
}

int img_load(const char *file, const char *tag_override) {
    char stage[] = "/var/tmp/mc-load-XXXXXX";
    if (!mkdtemp(stage)) {
        fprintf(stderr, "minicontainer: load: cannot create staging dir: %s\n", strerror(errno));
        return 1;
    }
    char *const tav[] = { "/usr/bin/tar", "-xf", (char *)file, "-C", stage, NULL };
    if (run_wait(tav) != 0) {
        fprintf(stderr, "minicontainer: load: cannot unpack %s\n", file);
        char *const rav[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(rav);
        return 1;
    }

    char mpath[PATH_MAX], ipath[PATH_MAX];
    fmt_path(mpath, sizeof mpath, "%s/manifest.json", stage);
    fmt_path(ipath, sizeof ipath, "%s/index.json", stage);

    char ref[BUF_SIZE_MEDIUM] = "", cfg_rel[PATH_MAX] = "";
    char layers[64][PATH_MAX];
    int nlayers = 0, rc = 1;

    if (access(mpath, R_OK) == 0) {
        /* docker save: [{"Config":"blobs/...","RepoTags":[...],"Layers":[...]}] */
        char *js; jsmntok_t *t; int nt;
        if (json_parse_file(mpath, &js, &t, &nt) < 0) {
            fprintf(stderr, "minicontainer: load: bad manifest.json\n");
            goto out;
        }
        int pick = 0, entries = t[0].type == JSMN_ARRAY ? t[0].size : 0;
        if (entries == 0) { fprintf(stderr, "minicontainer: load: empty manifest.json\n"); free(js); free(t); goto out; }
        if (tag_override) {
            for (int i = 0; i < entries; i++) {
                int e = json_arr_nth(t, 0, i);
                int tags = json_obj_get(js, t, e, "RepoTags");
                if (tags < 0) continue;
                for (int k = 0; k < t[tags].size; k++) {
                    int g = json_arr_nth(t, tags, k);
                    if (g >= 0 && json_tok_eq(js, &t[g], tag_override)) { pick = i; goto picked; }
                }
            }
            fprintf(stderr, "minicontainer: load: tag '%s' not in archive\n", tag_override);
            free(js); free(t); goto out;
        }
picked: {
            int e = json_arr_nth(t, 0, pick);
            int c = json_obj_get(js, t, e, "Config");
            if (c < 0) { fprintf(stderr, "minicontainer: load: no Config\n"); free(js); free(t); goto out; }
            json_tok_copy(js, &t[c], cfg_rel, sizeof cfg_rel);
            int tags = json_obj_get(js, t, e, "RepoTags");
            if (tag_override) fmt_path(ref, sizeof ref, "%s", tag_override);
            else if (tags >= 0 && t[tags].size > 0) {
                int g = json_arr_nth(t, tags, 0);
                if (g >= 0) json_tok_copy(js, &t[g], ref, sizeof ref);
            }
            int ls = json_obj_get(js, t, e, "Layers");
            if (ls < 0) { fprintf(stderr, "minicontainer: load: no Layers\n"); free(js); free(t); goto out; }
            for (int k = 0; k < t[ls].size && nlayers < 64; k++) {
                int g = json_arr_nth(t, ls, k);
                if (g >= 0) json_tok_copy(js, &t[g], layers[nlayers++], sizeof layers[0]);
            }
        }
        free(js); free(t);
    } else if (access(ipath, R_OK) == 0) {
        /* OCI layout: index.json -> image index -> manifest -> config/layers */
        char *js; jsmntok_t *t; int nt;
        if (json_parse_file(ipath, &js, &t, &nt) < 0) { fprintf(stderr, "minicontainer: load: bad index.json\n"); goto out; }
        int ms = json_obj_get(js, t, 0, "manifests");
        int m0 = json_arr_nth(t, ms, 0);
        int dg = json_obj_get(js, t, m0, "digest");
        char d1[BUF_SIZE_SMALL] = "", rel1[PATH_MAX];
        if (dg >= 0) json_tok_copy(js, &t[dg], d1, sizeof d1);
        if (t[0].type == JSMN_OBJECT && dg >= 0) {
            int ann = json_obj_get(js, t, m0, "annotations");
            int rn = ann >= 0 ? json_obj_get(js, t, ann, "org.opencontainers.image.ref.name") : -1;
            if (rn >= 0) json_tok_copy(js, &t[rn], ref, sizeof ref);
        }
        free(js); free(t);
        if (!d1[0] || blob_rel(stage, d1, rel1, sizeof rel1) < 0) { fprintf(stderr, "minicontainer: load: image index missing\n"); goto out; }

        char p1[PATH_MAX];
        fmt_path(p1, sizeof p1, "%s/%s", stage, rel1);
        if (json_parse_file(p1, &js, &t, &nt) < 0) { fprintf(stderr, "minicontainer: load: bad image index\n"); goto out; }
        int im = json_arr_nth(t, json_obj_get(js, t, 0, "manifests"), 0);
        int md = json_obj_get(js, t, im, "digest");
        char d2[BUF_SIZE_SMALL] = "";
        if (md >= 0) json_tok_copy(js, &t[md], d2, sizeof d2);
        free(js); free(t);
        char rel2[PATH_MAX];
        if (!d2[0] || blob_rel(stage, d2, rel2, sizeof rel2) < 0) { fprintf(stderr, "minicontainer: load: manifest missing\n"); goto out; }

        char p2[PATH_MAX];
        fmt_path(p2, sizeof p2, "%s/%s", stage, rel2);
        if (json_parse_file(p2, &js, &t, &nt) < 0) { fprintf(stderr, "minicontainer: load: bad manifest\n"); goto out; }
        int cf = json_obj_get(js, t, 0, "config");
        int cd = cf >= 0 ? json_obj_get(js, t, cf, "digest") : -1;
        char dcfg[BUF_SIZE_SMALL] = "";
        if (cd >= 0) json_tok_copy(js, &t[cd], dcfg, sizeof dcfg);
        int ls = json_obj_get(js, t, 0, "layers");
        if (ls >= 0) {
            for (int k = 0; k < t[ls].size && nlayers < 64; k++) {
                int e = json_arr_nth(t, ls, k);
                int ld = e >= 0 ? json_obj_get(js, t, e, "digest") : -1;
                if (ld < 0) continue;
                char d[BUF_SIZE_SMALL];
                json_tok_copy(js, &t[ld], d, sizeof d);
                if (blob_rel(stage, d, layers[nlayers], sizeof layers[0]) == 0) nlayers++;
            }
        }
        free(js); free(t);
        if (!dcfg[0] || blob_rel(stage, dcfg, cfg_rel, sizeof cfg_rel) < 0) {
            fprintf(stderr, "minicontainer: load: config missing\n");
            goto out;
        }
    } else {
        fprintf(stderr, "minicontainer: load: %s is not a docker-save or OCI archive\n", file);
        goto out;
    }

    if (!ref[0]) fmt_path(ref, sizeof ref, "loaded-image");
    char name[BUF_SIZE_MEDIUM];
    img_name(ref, name, sizeof name);
    if (!valid_name(name)) { fprintf(stderr, "minicontainer: load: bad image name '%s'\n", name); goto out; }

    /* digest check + disk preflight (P1.6) */
    long long total = 0;
    {
        char full[PATH_MAX];
        fmt_path(full, sizeof full, "%s/%s", stage, cfg_rel);
        struct stat st;
        if (stat(full, &st) == 0) total += (long long)st.st_size;
        const char *hex = strrchr(cfg_rel, '/');
        if (verify_blob(stage, cfg_rel, hex ? hex + 1 : cfg_rel) < 0) goto out;
    }
    for (int i = 0; i < nlayers; i++) {
        char full[PATH_MAX];
        fmt_path(full, sizeof full, "%s/%s", stage, layers[i]);
        struct stat st;
        if (stat(full, &st) == 0) total += (long long)st.st_size;
        const char *hex = strrchr(layers[i], '/');
        if (verify_blob(stage, layers[i], hex ? hex + 1 : layers[i]) < 0) goto out;
    }
    {
        char base[PATH_MAX], images[PATH_MAX];
        fmt_path(base, sizeof base, "%s", img_base());
        fmt_path(images, sizeof images, "%s/images", base);
        if (mkdir(base, 0755) < 0 && errno != EEXIST) {
            fprintf(stderr, "minicontainer: load: cannot create %s: %s\n", base, strerror(errno));
            goto out;
        }
        if (mkdir(images, 0755) < 0 && errno != EEXIST) {
            fprintf(stderr, "minicontainer: load: cannot create %s: %s\n", images, strerror(errno));
            goto out;
        }
        struct statvfs sv;
        if (statvfs(base, &sv) == 0) {
            unsigned long long free_b = (unsigned long long)sv.f_bavail * sv.f_frsize;
            unsigned long long need = (unsigned long long)total * 3;
            if (free_b < need) {
                fprintf(stderr, "minicontainer: load: not enough space (%.1f MB free, need ~%.1f MB)\n",
                        free_b / 1048576.0, need / 1048576.0);
                goto out;
            }
        }
    }

    char dir[PATH_MAX], rootfs[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name);
    fmt_path(rootfs, sizeof rootfs, "%s/rootfs", dir);
    {
        char *const rav[] = { "/usr/bin/rm", "-rf", dir, NULL };
        run_wait(rav);
    }
    if (mkdir(dir, 0755) < 0 || mkdir(rootfs, 0755) < 0) {
        fprintf(stderr, "minicontainer: load: cannot create %s: %s\n", dir, strerror(errno));
        goto out;
    }
    for (int i = 0; i < nlayers; i++) {
        char full[PATH_MAX];
        fmt_path(full, sizeof full, "%s/%s", stage, layers[i]);
        printf("load: layer %d/%d\n", i + 1, nlayers);
        if (layer_apply(full, rootfs) < 0) {
            fprintf(stderr, "minicontainer: load: layer %d failed\n", i + 1);
            char *const rav[] = { "/usr/bin/rm", "-rf", dir, NULL };
            run_wait(rav);
            goto out;
        }
    }

    char src[PATH_MAX], dst[PATH_MAX];
    fmt_path(src, sizeof src, "%s/%s", stage, cfg_rel);
    fmt_path(dst, sizeof dst, "%s/config.json", dir);
    {
        char *const cav[] = { "/usr/bin/cp", "-f", src, dst, NULL };
        if (run_wait(cav) != 0) {
            fprintf(stderr, "minicontainer: load: cannot store config.json\n");
            goto out;
        }
    }

    /* config: arch + default argv/env */
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(dst, &js, &t, &nt) == 0) {
        char arch[BUF_SIZE_SMALL] = "";
        int a = json_obj_get(js, t, 0, "architecture");
        if (a >= 0) {
            char raw[BUF_SIZE_SMALL];
            json_tok_copy(js, &t[a], raw, sizeof raw);
            arch_norm(raw, arch, sizeof arch);
        }
        write_config_side(js, t, nt, dir);
        char meta[PATH_MAX], line[BUF_SIZE_LARGE];
        fmt_path(meta, sizeof meta, "%s/meta", dir);
        fmt_path(line, sizeof line, "name=%s\ntag=%s\narch=%s\nlayers=%d\n", name, ref, arch, nlayers);
        write_file(meta, line);
        printf("load: %s -> %s (%s, %d layer%s)\n", ref, name,
               arch[0] ? arch : MC_ARCH, nlayers, nlayers == 1 ? "" : "s");
        if (arch[0] && strcmp(arch, MC_ARCH) != 0)
            fprintf(stderr, "minicontainer: load: canh bao: image la %s, may nay la %s\n", arch, MC_ARCH);
        free(js); free(t);
    } else {
        fprintf(stderr, "minicontainer: load: cannot parse stored config.json\n");
    }
    rc = 0;

out:
    {
        char *const rav[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(rav);
    }
    return rc;
}

int img_rootfs(const char *name, char *out, size_t n) {
    char dir[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name);
    if (access(dir, F_OK) != 0) return -1;
    if (fmt_path(out, n, "%s/rootfs", dir) >= (int)n) return -1;
    return access(out, F_OK) == 0 ? 0 : -1;
}

/* Re-export an image as a docker-save compatible archive (one merged layer).
 * Not byte-identical to the original, but `load` accepts it again. */
int img_save(const char *name, const char *out) {
    if (!valid_name(name)) { fprintf(stderr, "invalid image name\n"); return 1; }
    char dir[PATH_MAX];
    if (fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name) < 0 || access(dir, F_OK) != 0) {
        fprintf(stderr, "khong co image '%s'\n", name);
        return 1;
    }
    char stage[] = "/var/tmp/mc-save-XXXXXX";
    if (!mkdtemp(stage)) { fprintf(stderr, "minicontainer: save: mkdtemp: %s\n", strerror(errno)); return 1; }
    char blobs[PATH_MAX], rootfs[PATH_MAX], cfg[PATH_MAX];
    fmt_path(blobs, sizeof blobs, "%s/blobs/sha256", stage);
    fmt_path(rootfs, sizeof rootfs, "%s/rootfs", dir);
    fmt_path(cfg, sizeof cfg, "%s/config.json", dir);
    char *const mkd[] = { "/usr/bin/mkdir", "-p", blobs, NULL };
    if (run_wait(mkd) != 0) { fprintf(stderr, "minicontainer: save: mkdir failed\n"); return 1; }

    char layer[PATH_MAX];
    fmt_path(layer, sizeof layer, "%s/layer.tar", stage);
    char *const tav[] = { "/usr/bin/tar", "-cf", layer, "-C", rootfs,
                          "--numeric-owner", "--xattrs", "--xattrs-include=*", ".", NULL };
    if (run_wait(tav) != 0) { fprintf(stderr, "minicontainer: save: cannot archive rootfs\n"); return 1; }

    char lhex[65], chex[65];
    char *const shl[] = { "/usr/bin/sha256sum", layer, NULL };
    char outbuf[BUF_SIZE_XLARGE];
    if (run_capture(shl, outbuf, sizeof outbuf) != 0) return 1;
    memcpy(lhex, outbuf, 64); lhex[64] = 0;
    char *const shc[] = { "/usr/bin/sha256sum", cfg, NULL };
    if (run_capture(shc, outbuf, sizeof outbuf) != 0) return 1;
    memcpy(chex, outbuf, 64); chex[64] = 0;

    char dst[PATH_MAX];
    fmt_path(dst, sizeof dst, "%s/%s", blobs, lhex);
    char *const mvl[] = { "/usr/bin/mv", layer, dst, NULL };
    if (run_wait(mvl) != 0) return 1;
    fmt_path(dst, sizeof dst, "%s/%s", blobs, chex);
    char *const cpl[] = { "/usr/bin/cp", cfg, dst, NULL };
    if (run_wait(cpl) != 0) return 1;

    char tag[BUF_SIZE_MEDIUM] = "";
    char meta[PATH_MAX], line[BUF_SIZE_MEDIUM];
    fmt_path(meta, sizeof meta, "%s/meta", dir);
    FILE *f = fopen(meta, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *nl = strchr(line, '\n'); if (nl) *nl = 0;
            if (strncmp(line, "tag=", 4) == 0) snprintf(tag, sizeof tag, "%s", line + 4);
        }
        fclose(f);
    }
    if (!tag[0]) snprintf(tag, sizeof tag, "%s:latest", name);

    char mpath[PATH_MAX];
    fmt_path(mpath, sizeof mpath, "%s/manifest.json", stage);
    char mb[BUF_SIZE_XLARGE];
    snprintf(mb, sizeof mb,
             "[{\"Config\":\"blobs/sha256/%s\",\"RepoTags\":[\"%s\"],\"Layers\":[\"blobs/sha256/%s\"]}]\n",
             chex, tag, lhex);
    write_file(mpath, mb);

    char *const tav2[] = { "/usr/bin/tar", "-cf", (char *)out, "-C", stage, ".", NULL };
    int rc = run_wait(tav2);
    char *const rav[] = { "/usr/bin/rm", "-rf", stage, NULL };
    run_wait(rav);
    if (rc != 0) { fprintf(stderr, "minicontainer: save: cannot write %s\n", out); return 1; }
    printf("save: %s -> %s\n", name, out);
    return 0;
}

int do_images(int json) {
    char base[PATH_MAX];
    fmt_path(base, sizeof base, "%s/images", img_base());
    DIR *d = opendir(base);
    if (!d) {
        if (json) printf("[]\n");
        else printf("(chua co image nao)\n");
        return 0;
    }
    struct dirent *e;
    int first = 1, count = 0;
    if (json) printf("[");
    else printf("%-24s %-10s %-12s %s\n", "IMAGE", "ARCH", "LAYERS", "SIZE");
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char dir[PATH_MAX], meta[PATH_MAX], line[BUF_SIZE_MEDIUM];
        fmt_path(dir, sizeof dir, "%s/%s", base, e->d_name);
        fmt_path(meta, sizeof meta, "%s/meta", dir);
        char arch[BUF_SIZE_SMALL] = "?", layers[BUF_SIZE_SMALL] = "?", tag[BUF_SIZE_MEDIUM] = "";
        FILE *f = fopen(meta, "r");
        if (f) {
            while (fgets(line, sizeof line, f)) {
                char *nl = strchr(line, '\n'); if (nl) *nl = 0;
                if (strncmp(line, "arch=", 5) == 0) fmt_path(arch, sizeof arch, "%s", line + 5);
                else if (strncmp(line, "layers=", 7) == 0) fmt_path(layers, sizeof layers, "%s", line + 7);
                else if (strncmp(line, "tag=", 4) == 0) fmt_path(tag, sizeof tag, "%s", line + 4);
            }
            fclose(f);
        }
        char rootfs[PATH_MAX];
        fmt_path(rootfs, sizeof rootfs, "%s/rootfs", dir);
        long long sz = disk_usage(rootfs);
        if (sz < 0) sz = 0;
        if (json) {
            printf("%s{\"name\":\"%s\",\"tag\":\"%s\",\"arch\":\"%s\",\"layers\":%s,\"size\":%lld}",
                   first ? "" : ",", e->d_name, tag, arch, layers, sz);
            first = 0;
        } else {
            printf("%-24s %-10s %-12s %.1f MB\n", e->d_name, arch, layers, sz / 1048576.0);
        }
        count++;
    }
    closedir(d);
    if (json) printf("]\n");
    else if (count == 0) printf("(chua co image nao)\n");
    return 0;
}

int do_rmi(const char *name) {
    if (!valid_name(name)) { fprintf(stderr, "invalid image name\n"); return 1; }
    char dir[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name);
    if (access(dir, F_OK) != 0) {
        fprintf(stderr, "khong co image '%s'\n", name);
        return 1;
    }
    /* Refuse while a container uses this rootfs. */
    DIR *sd = opendir(STATE_BASE);
    if (sd) {
        struct dirent *e;
        char want[PATH_MAX];
        fmt_path(want, sizeof want, "%s/rootfs", dir);
        while ((e = readdir(sd))) {
            if (e->d_name[0] == '.') continue;
            char rf[PATH_MAX + 16], buf[PATH_MAX];
            fmt_path(rf, sizeof rf, STATE_BASE "/%s/rootfs", e->d_name);
            if (read_small(rf, buf, sizeof buf) == 0 && strcmp(buf, want) == 0) {
                fprintf(stderr, "image '%s' dang duoc container '%s' dung\n", name, e->d_name);
                closedir(sd);
                return 1;
            }
        }
        closedir(sd);
    }
    char *const av[] = { "/usr/bin/rm", "-rf", "--", dir, NULL };
    if (run_wait(av) != 0) {
        fprintf(stderr, "minicontainer: rmi: cannot remove %s\n", dir);
        return 1;
    }
    printf("%s: removed\n", name);
    return 0;
}

/* NUL-separated argv from the image config; argv[0] must be an absolute path
 * or a bare name (execvp resolves it inside the container PATH). */
char **img_argv(const char *name, int *argc_out) {
    char path[PATH_MAX];
    fmt_path(path, sizeof path, "%s/images/%s/argv", img_base(), name);
    size_t len = 0;
    char *buf = read_all(path, &len);
    if (!buf) return NULL;
    int n = 0;
    for (size_t i = 0; i < len; i++) if (buf[i] == '\0') n++;
    if (n == 0) { free(buf); return NULL; }
    char **av = malloc(sizeof(char *) * (size_t)(n + 1));
    if (!av) { free(buf); return NULL; }
    int k = 0;
    for (size_t i = 0; i < len && k < n; i++) {
        if (i == 0 || buf[i - 1] == '\0') av[k++] = buf + i;
    }
    av[k] = NULL;
    if (argc_out) *argc_out = k;
    return av;
}

/* Apply the image Env before exec (called in the child, before chroot). */
int img_apply_env(const char *name) {
    char path[PATH_MAX];
    fmt_path(path, sizeof path, "%s/images/%s/env", img_base(), name);
    size_t len = 0;
    char *buf = read_all(path, &len);
    if (!buf) return 0;                       /* no env: inherit host env */
    for (size_t i = 0; i < len; ) {
        char *kv = buf + i;
        size_t l = strlen(kv);
        if (l) {
            char *eq = strchr(kv, '=');
            if (eq) { *eq = '\0'; setenv(kv, eq + 1, 1); }
        }
        i += l + 1;
    }
    free(buf);
    return 0;
}
