/* ship.c — export/import one image as a single signed bundle (P1.8).
 *
 * bundle.tar.gz
 *   manifest.json   {"name","arch","sha256","size","created"}
 *   image.tar       config.json + argv + env + meta + rootfs/
 *
 * `import` always verifies the sha256 of image.tar; with --verify KEY it also
 * checks <bundle>.sig (minisign/Ed25519) before touching the store. This is
 * how an image travels to a box with no network: scp one file.
 */
#include "common.h"
#include "jsmn.h"

int do_export(const char *image, const char *out) {
    if (!valid_name(image)) { fprintf(stderr, "invalid image name\n"); return 1; }
    char dir[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), image);
    if (access(dir, F_OK) != 0) {
        fprintf(stderr, "khong co image '%s'\n", image);
        return 1;
    }
    char stage[] = "/var/tmp/mc-exp-XXXXXX";
    if (!mkdtemp(stage)) { fprintf(stderr, "minicontainer: export: mkdtemp: %s\n", strerror(errno)); return 1; }

    char img_tar[PATH_MAX];
    fmt_path(img_tar, sizeof img_tar, "%s/image.tar", stage);
    char *const tav[] = { "/usr/bin/tar", "-cf", img_tar, "-C", dir,
                          "--numeric-owner", "--xattrs", "--xattrs-include=*", ".", NULL };
    if (run_wait(tav) != 0) {
        fprintf(stderr, "minicontainer: export: cannot archive image\n");
        char *const r1[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r1);
        return 1;
    }

    char hex[65] = "", buf[BUF_SIZE_XLARGE];
    char *const sh[] = { "/usr/bin/sha256sum", img_tar, NULL };
    if (run_capture(sh, buf, sizeof buf) != 0) return 1;
    memcpy(hex, buf, 64); hex[64] = 0;
    struct stat st;
    long long sz = (stat(img_tar, &st) == 0) ? (long long)st.st_size : 0;

    char arch[BUF_SIZE_SMALL] = MC_ARCH, name[BUF_SIZE_MEDIUM] = "";
    char meta[PATH_MAX], line[BUF_SIZE_MEDIUM];
    fmt_path(meta, sizeof meta, "%s/meta", dir);
    FILE *f = fopen(meta, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *nl = strchr(line, '\n'); if (nl) *nl = 0;
            if (strncmp(line, "arch=", 5) == 0) snprintf(arch, sizeof arch, "%.*s", (int)sizeof arch - 1, line + 5);
            else if (strncmp(line, "name=", 5) == 0) snprintf(name, sizeof name, "%.*s", (int)sizeof name - 1, line + 5);
        }
        fclose(f);
    }
    if (!name[0]) snprintf(name, sizeof name, "%s", image);

    char ts[BUF_SIZE_SMALL];
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tmv);

    char mpath[PATH_MAX], mb[BUF_SIZE_XLARGE];
    fmt_path(mpath, sizeof mpath, "%s/manifest.json", stage);
    snprintf(mb, sizeof mb,
             "{\n  \"name\": \"%s\",\n  \"arch\": \"%s\",\n  \"sha256\": \"%s\",\n"
             "  \"size\": %lld,\n  \"created\": \"%s\"\n}\n", name, arch, hex, sz, ts);
    write_file(mpath, mb);

    char *const tav2[] = { "/usr/bin/tar", "-czf", (char *)out, "-C", stage, ".", NULL };
    int rc = run_wait(tav2);
    char *const r2[] = { "/usr/bin/rm", "-rf", stage, NULL };
    run_wait(r2);
    if (rc != 0) { fprintf(stderr, "minicontainer: export: cannot write %s\n", out); return 1; }

    char sumf[PATH_MAX];
    if (fmt_path(sumf, sizeof sumf, "%s.sha256", out) == 0) {
        char sline[BUF_SIZE_LARGE], base[PATH_MAX];
        snprintf(base, sizeof base, "%s", out);
        const char *bn = strrchr(base, '/');
        bn = bn ? bn + 1 : base;
        char *const sh2[] = { "/usr/bin/sha256sum", (char *)out, NULL };
        if (run_capture(sh2, buf, sizeof buf) == 0) {
            char bhex[65];
            memcpy(bhex, buf, 64); bhex[64] = 0;
            if (fmt_path(sline, sizeof sline, "%s  %s\n", bhex, bn) == 0) write_file(sumf, sline);
        }
    }
    printf("export: %s -> %s (%.1f MB)\n", image, out, sz / 1048576.0);
    printf("  import tren may dich: minicontainer import %s\n", out);
    return 0;
}

int do_import(const char *bundle, const char *key) {
    if (key && verify_signature(bundle, key) < 0) return 1;

    char stage[] = "/var/tmp/mc-imp-XXXXXX";
    if (!mkdtemp(stage)) { fprintf(stderr, "minicontainer: import: mkdtemp: %s\n", strerror(errno)); return 1; }
    char *const tav[] = { "/usr/bin/tar", "-xf", (char *)bundle, "-C", stage, NULL };
    if (run_wait(tav) != 0) {
        fprintf(stderr, "minicontainer: import: cannot unpack %s\n", bundle);
        char *const r[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r);
        return 1;
    }

    char mpath[PATH_MAX];
    fmt_path(mpath, sizeof mpath, "%s/manifest.json", stage);
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(mpath, &js, &t, &nt) < 0) {
        fprintf(stderr, "minicontainer: import: bad manifest.json\n");
        char *const r[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r);
        return 1;
    }
    char name[BUF_SIZE_MEDIUM] = "", want[65] = "";
    int nk = json_obj_get(js, t, 0, "name");
    int sk = json_obj_get(js, t, 0, "sha256");
    if (nk >= 0) json_tok_copy(js, &t[nk], name, sizeof name);
    if (sk >= 0) json_tok_copy(js, &t[sk], want, sizeof want);
    json_free(js, t);
    if (!valid_name(name) || !want[0]) {
        fprintf(stderr, "minicontainer: import: manifest missing name/sha256\n");
        char *const r[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r);
        return 1;
    }

    char img_tar[PATH_MAX], hex[65] = "", buf[BUF_SIZE_XLARGE];
    fmt_path(img_tar, sizeof img_tar, "%s/image.tar", stage);
    char *const sh[] = { "/usr/bin/sha256sum", img_tar, NULL };
    if (run_capture(sh, buf, sizeof buf) != 0 || strlen(buf) < 64) {
        fprintf(stderr, "minicontainer: import: image.tar missing\n");
        char *const r[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r);
        return 1;
    }
    memcpy(hex, buf, 64); hex[64] = 0;
    if (strcmp(hex, want) != 0) {
        fprintf(stderr, "minicontainer: import: sha256 mismatch\n  expected: %s\n  actual  : %s\n", want, hex);
        char *const r[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r);
        return 1;
    }

    char dir[PATH_MAX];
    fmt_path(dir, sizeof dir, "%s/images/%s", img_base(), name);
    char *const r1[] = { "/usr/bin/rm", "-rf", dir, NULL };
    run_wait(r1);
    char *const mkd[] = { "/usr/bin/mkdir", "-p", dir, NULL };
    if (run_wait(mkd) != 0) {
        fprintf(stderr, "minicontainer: import: cannot create %s\n", dir);
        return 1;
    }
    char *const tav2[] = { "/usr/bin/tar", "-xf", img_tar, "-C", dir,
                           "--numeric-owner", "--xattrs", "--xattrs-include=*", NULL };
    if (run_wait(tav2) != 0) {
        fprintf(stderr, "minicontainer: import: extraction failed\n");
        char *const r[] = { "/usr/bin/rm", "-rf", dir, NULL };
        run_wait(r);
        char *const r2[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(r2);
        return 1;
    }
    char *const r3[] = { "/usr/bin/rm", "-rf", stage, NULL };
    run_wait(r3);
    printf("import: %s -> %s\n", bundle, name);
    printf("  run: minicontainer run --image=%s\n", name);
    return 0;
}
