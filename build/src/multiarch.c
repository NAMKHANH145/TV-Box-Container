/* multiarch.c — `export` voi nhieu image: 1 bundle, nhieu rootfs theo arch.
 * Luc import, img_load() se chon bien the khop MC_ARCH. Khong can qemu. */
#include "common.h"

static int meta_get(const char *dir, const char *key, char *out, size_t n) {
    char path[PATH_MAX], line[BUF_SIZE_MEDIUM];
    fmt_path(path, sizeof path, "%s/meta", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    out[0] = 0;
    size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        if (!strncmp(line, key, klen) && line[klen] == '=')
            snprintf(out, n, "%s", line + klen + 1);
    }
    fclose(f);
    return out[0] ? 0 : -1;
}

/* Dat ten chung cho moi bien the de import khong phu thuoc ten nguon. */
static void meta_set_name(const char *dir, const char *name) {
    char path[PATH_MAX], line[BUF_SIZE_MEDIUM], buf[BUF_SIZE_LARGE];
    fmt_path(path, sizeof path, "%s/meta", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t o = 0;
    int have = 0;
    while (fgets(line, sizeof line, f) && o + sizeof line < sizeof buf) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        if (!strncmp(line, "name=", 5)) {
            o += (size_t)snprintf(buf + o, sizeof buf - o, "name=%s\n", name);
            have = 1;
        } else
            o += (size_t)snprintf(buf + o, sizeof buf - o, "%s\n", line);
    }
    if (!have) o += (size_t)snprintf(buf + o, sizeof buf - o, "name=%s\n", name);
    fclose(f);
    write_file(path, buf);
}

int do_export_multi(int argc, char **argv) {
    char names[16][BUF_SIZE_MEDIUM];
    int n = 0;
    const char *out = NULL, *preferred = "";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-o")) {
            if (i + 1 >= argc) return 1;
            out = argv[++i];
        } else if (!strncmp(argv[i], "--arch=", 7)) {
            preferred = argv[i] + 7;          /* goi y arch dich, luu vao index */
        } else if (argv[i][0] != '-' && n < 16) {
            snprintf(names[n++], sizeof names[0], "%s", argv[i]);
        } else {
            fprintf(stderr, "Usage: %s export [--arch=ARCH] <image> <image>... -o OUT.tar.gz\n", argv[0]);
            return 1;
        }
    }
    if (n < 2 || !out) {
        fprintf(stderr, "Usage: %s export [--arch=ARCH] <image> <image>... -o OUT.tar.gz\n", argv[0]);
        return 1;
    }

    char common[BUF_SIZE_MEDIUM] = "";
    {
        char srcdir[PATH_MAX];
        fmt_path(srcdir, sizeof srcdir, "%s/images/%s", img_base(), names[0]);
        if (meta_get(srcdir, "name", common, sizeof common) < 0)
            snprintf(common, sizeof common, "%s", names[0]);
    }

    char stage[] = "/var/tmp/mc-multi-XXXXXX";
    if (!mkdtemp(stage)) {
        fprintf(stderr, "minicontainer: export: mkdtemp: %s\n", strerror(errno));
        return 1;
    }

    char index[BUF_SIZE_LARGE];
    size_t io = (size_t)snprintf(index, sizeof index,
                                 "{\"name\":\"%s\",\"preferred\":\"%s\",\"variants\":[",
                                 common, preferred);
    int rc = 1;
    for (int i = 0; i < n; i++) {
        char srcdir[PATH_MAX], arch[BUF_SIZE_SMALL] = "";
        fmt_path(srcdir, sizeof srcdir, "%s/images/%s", img_base(), names[i]);
        if (meta_get(srcdir, "arch", arch, sizeof arch) < 0 || !arch[0]) {
            fprintf(stderr, "minicontainer: export: '%s' has no arch\n", names[i]);
            goto out;
        }
        char tmpname[BUF_SIZE_MEDIUM], tmpdir[PATH_MAX];
        snprintf(tmpname, sizeof tmpname, "mc-multi-%d-%d", (int)getpid(), i);
        fmt_path(tmpdir, sizeof tmpdir, "%s/images/%s", img_base(), tmpname);
        char *const cp[] = { "/usr/bin/cp", "-a", srcdir, tmpdir, NULL };
        if (run_wait(cp) != 0) {
            fprintf(stderr, "minicontainer: export: cannot copy '%s'\n", names[i]);
            goto out;
        }
        meta_set_name(tmpdir, common);
        char variant[PATH_MAX];
        fmt_path(variant, sizeof variant, "%s/%s.tar", stage, arch);
        if (do_export(tmpname, variant) != 0) {
            char *const rm[] = { "/usr/bin/rm", "-rf", tmpdir, NULL };
            run_wait(rm);
            goto out;
        }
        char *const rm[] = { "/usr/bin/rm", "-rf", tmpdir, NULL };
        run_wait(rm);
        io += (size_t)snprintf(index + io, sizeof index - io,
                               "%s{\"arch\":\"%s\",\"path\":\"%s.tar\"}",
                               i ? "," : "", arch, arch);
        printf("export: variant %s -> %s\n", names[i], arch);
    }
    snprintf(index + io, sizeof index - io, "]}");
    char ipath[PATH_MAX];
    fmt_path(ipath, sizeof ipath, "%s/multiarch.json", stage);
    write_file(ipath, index);

    char *const tav[] = { "/usr/bin/tar", "-czf", (char *)out, "-C", stage, ".", NULL };
    rc = run_wait(tav);
    if (rc != 0) fprintf(stderr, "minicontainer: export: cannot write %s\n", out);
    else printf("export: %d bien the -> %s\n", n, out);

out:
    {
        char *const rmv[] = { "/usr/bin/rm", "-rf", stage, NULL };
        run_wait(rmv);
    }
    return rc;
}
