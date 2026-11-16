/* layer.c — apply one OCI layer (tar, optionally gzip/zstd) onto a rootfs.
 *
 * Two passes over the archive before extraction:
 *   1. names  -> apply whiteouts (.wh.<name>, .wh..wh..opq) from the host side
 *   2. verbose-> reject device nodes / fifos (a rootfs image never needs them;
 *                run creates /dev itself)
 * Then `tar -x` with --xattrs so security.capability survives (needed by ping).
 */
#include "common.h"

#define LAYER_MAX_ENTRIES 500000

struct lctx {
    const char *rootfs;
    long long entries;
};

static int rel_ok(const char *p) {
    if (!p || !*p || p[0] == '/') return 0;
    for (const char *q = p; *q;) {
        const char *end = strchr(q, '/');
        size_t len = end ? (size_t)(end - q) : strlen(q);
        if (len == 2 && q[0] == '.' && q[1] == '.') return 0;
        q = end ? end + 1 : q + len;
    }
    return 1;
}

/* rm -rf <rootfs>/<rel>; rel was validated to stay inside rootfs. */
static int wipe(const char *rootfs, const char *rel) {
    if (!rel_ok(rel)) return -1;
    char full[PATH_MAX * 2];
    if (snprintf(full, sizeof full, "%s/%s", rootfs, rel) >= (int)sizeof full) return -1;
    char *const av[] = { "/usr/bin/rm", "-rf", "--", full, NULL };
    return run_wait(av);
}

static int clear_dir(const char *rootfs, const char *rel) {
    char dir[PATH_MAX * 2];
    if (rel[0] && !rel_ok(rel)) return -1;
    if (rel[0]) {
        if (snprintf(dir, sizeof dir, "%s/%s", rootfs, rel) >= (int)sizeof dir) return -1;
    } else {
        snprintf(dir, sizeof dir, "%s", rootfs);
    }
    DIR *d = opendir(dir);
    if (!d) return 0;                       /* nothing to clear */
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child[PATH_MAX * 2];
        if (snprintf(child, sizeof child, "%s%s%s", rel[0] ? rel : "", rel[0] ? "/" : "",
                     e->d_name) >= (int)sizeof child) continue;
        wipe(rootfs, child);
    }
    closedir(d);
    return 0;
}

static int wh_cb(const char *line, void *v) {
    struct lctx *c = v;
    if (++c->entries > LAYER_MAX_ENTRIES) {
        fprintf(stderr, "minicontainer: layer: too many entries (>%d)\n", LAYER_MAX_ENTRIES);
        return -1;
    }
    if (!rel_ok(line)) {
        fprintf(stderr, "minicontainer: layer: unsafe path: %s\n", line);
        return -1;
    }
    const char *base = strrchr(line, '/');
    base = base ? base + 1 : line;
    size_t dlen = (size_t)(base - line);     /* includes trailing '/' when nested */

    if (strcmp(base, ".wh..wh..opq") == 0) {
        char dir[PATH_MAX];
        if (dlen == 0) return clear_dir(c->rootfs, "");
        if (dlen - 1 >= sizeof dir) return -1;
        memcpy(dir, line, dlen - 1);
        dir[dlen - 1] = '\0';
        return clear_dir(c->rootfs, dir);
    }
    if (strncmp(base, ".wh.", 4) == 0 && base[4]) {
        char target[PATH_MAX];
        if (dlen + strlen(base + 4) >= sizeof target) return -1;
        memcpy(target, line, dlen);
        strcpy(target + dlen, base + 4);
        if (wipe(c->rootfs, target) < 0) {
            fprintf(stderr, "minicontainer: layer: cannot apply whiteout %s\n", line);
            return -1;
        }
    }
    return 0;
}

static int type_cb(const char *line, void *v) {
    (void)v;
    char mode[16];
    if (sscanf(line, "%15s", mode) != 1) return 0;
    char t = mode[0];
    if (t == 'c' || t == 'b' || t == 'p') {
        fprintf(stderr, "minicontainer: layer: refuse device/fifo member: %s\n", line);
        return -1;
    }
    return 0;
}

int layer_apply(const char *blob, const char *rootfs) {
    struct lctx c = { rootfs, 0 };
    if (tar_stream(blob, 0, wh_cb, &c) < 0) return -1;
    if (tar_stream(blob, 1, type_cb, NULL) < 0) return -1;

    char *const av[] = { "/usr/bin/tar", "-xf", (char *)blob, "-C", (char *)rootfs,
                         "--exclude=.wh.*", "--xattrs", "--xattrs-include=*",
                         "--numeric-owner", NULL };
    int rc = run_wait(av);
    if (rc != 0) {
        fprintf(stderr, "minicontainer: layer: extraction failed (tar exit %d): %s\n", rc, blob);
        return -1;
    }
    return 0;
}
