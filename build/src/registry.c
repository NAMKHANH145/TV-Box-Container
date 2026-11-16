/* registry.c — distribution-spec client: `pull <image>` from a registry.
 *
 * Flow, per the OCI distribution spec:
 *   GET /v2/                     -> 401 + WWW-Authenticate: Bearer realm=...
 *   GET <realm>?service=..&scope=repository:<repo>:pull -> {"token":...}
 *   GET /v2/<repo>/manifests/<tag>  -> manifest list or image manifest
 *   GET /v2/<repo>/blobs/<digest>   -> config + layers, into <stage>/blobs/sha256/
 *
 * The blobs land in the same layout `docker save` produces, so img_install()
 * applies them exactly like `load` does: one install path, one set of checks.
 *
 * TLS and HTTP are curl's job. A static musl binary has no libssl, and every
 * target (Ubuntu, Debian, Buildroot) already ships curl.
 */
#include "common.h"
#include "jsmn.h"
#include <ctype.h>
#include <sys/file.h>

#define REG_MAX_BLOB  (2LL * 1024 * 1024 * 1024)
#define REG_MAX_LAYERS 64
#define REG_MAX_TOKEN  8192   /* Docker Hub JWT is ~1.5 KB; leave room */

struct ref {
    char host[256];
    char repo[512];
    char tag[128];
    char digest[80];
};

/* ---- reference parsing ------------------------------------------------ */

static int parse_ref(const char *in, struct ref *r) {
    char buf[BUF_SIZE_LARGE];
    memset(r, 0, sizeof *r);
    if (!in || !*in || strlen(in) >= sizeof buf) return -1;
    strcpy(buf, in);

    char *at = strchr(buf, '@');
    if (at) {
        *at = '\0';
        fmt_path(r->digest, sizeof r->digest, "%s", at + 1);
    }
    /* A ':' after the last '/' is a tag; a ':' before it is a host port. */
    char *slash = strrchr(buf, '/');
    char *colon = strrchr(buf, ':');
    if (colon && (!slash || colon > slash)) {
        *colon = '\0';
        fmt_path(r->tag, sizeof r->tag, "%s", colon + 1);
    }

    char *fs = strchr(buf, '/');
    if (fs) {
        *fs = '\0';
        int is_host = strchr(buf, '.') || strchr(buf, ':') || strcmp(buf, "localhost") == 0;
        if (is_host) {
            fmt_path(r->host, sizeof r->host, "%s", buf);
            fmt_path(r->repo, sizeof r->repo, "%s", fs + 1);
        } else {
            fmt_path(r->repo, sizeof r->repo, "%s/%s", buf, fs + 1);
        }
    } else {
        fmt_path(r->repo, sizeof r->repo, "%s", buf);
    }
    if (!r->host[0]) fmt_path(r->host, sizeof r->host, "registry-1.docker.io");
    if (strcmp(r->host, "docker.io") == 0 || strcmp(r->host, "index.docker.io") == 0)
        fmt_path(r->host, sizeof r->host, "registry-1.docker.io");
    if (!strchr(r->repo, '/')) {                 /* official image: library/x */
        char t[BUF_SIZE_MEDIUM];
        fmt_path(t, sizeof t, "library/%s", r->repo);
        fmt_path(r->repo, sizeof r->repo, "%s", t);
    }
    if (!r->tag[0] && !r->digest[0]) fmt_path(r->tag, sizeof r->tag, "latest");
    return (r->repo[0] && (r->tag[0] || r->digest[0])) ? 0 : -1;
}

/* ---- HTTP via curl ---------------------------------------------------- */

/* Returns 0 when curl ran; *code is the HTTP status (0 if unknown). */
static int http_get(const char *url, const char *dst, const char *tok,
                    const char *hdrf, const char *extra, int resume, long *code) {
    char auth[REG_MAX_TOKEN + 64] = "";
    if (tok && *tok && fmt_path(auth, sizeof auth, "Authorization: Bearer %s", tok) < 0) {
        fprintf(stderr, "minicontainer: pull: token qua dai\n");
        return -1;
    }

    char *av[24];
    int k = 0;
    av[k++] = "/usr/bin/curl";
    av[k++] = "-sS";
    av[k++] = "-L";
    av[k++] = "--max-time";
    av[k++] = "1800";
    if (resume) { av[k++] = "-C"; av[k++] = "-"; }
    av[k++] = "-o"; av[k++] = (char *)dst;
    av[k++] = "-D"; av[k++] = (char *)hdrf;
    av[k++] = "-w"; av[k++] = "%{http_code}";
    if (auth[0]) { av[k++] = "-H"; av[k++] = auth; }
    if (extra && *extra) { av[k++] = "-H"; av[k++] = (char *)extra; }
    av[k++] = (char *)url;
    av[k] = NULL;

    char out[BUF_SIZE_SMALL] = "";
    if (run_capture(av, out, sizeof out) != 0) return -1;
    if (getenv("MC_DEBUG_HTTP")) fprintf(stderr, "DBG http %s -> [%s]\n", url, out);
    if (code) *code = strtol(out, NULL, 10);
    return 0;
}

/* Pull one `name="value"` out of a WWW-Authenticate header (case-insensitive). */
static int hdr_param(const char *hdr, const char *name, char *out, size_t n) {
    char low[BUF_SIZE_XLARGE], pat[BUF_SIZE_SMALL];
    size_t i = 0;
    for (; hdr[i] && i + 1 < sizeof low; i++) low[i] = (char)tolower((unsigned char)hdr[i]);
    low[i] = '\0';
    fmt_path(pat, sizeof pat, "%s=\"", name);
    const char *p = strstr(low, pat);
    if (!p) return -1;
    p += strlen(pat);
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < n) out[o++] = *p++;
    out[o] = '\0';
    return o ? 0 : -1;
}

static int file_has(const char *p) { return access(p, R_OK) == 0; }

static int sha256_of(const char *path, char out[65]) {
    char *const av[] = { "/usr/bin/sha256sum", (char *)path, NULL };
    char buf[BUF_SIZE_XLARGE];
    if (run_capture(av, buf, sizeof buf) != 0) return -1;
    if (strlen(buf) < 64) return -1;
    memcpy(out, buf, 64);
    out[64] = '\0';
    return 0;
}

static long long fsize(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long long)st.st_size : -1;
}

/* ---- token flow ------------------------------------------------------- */

static int token_for(const struct ref *r, const char *dir, char *tok, size_t tn) {
    char url[PATH_MAX * 2], hdrf[PATH_MAX], bodyf[PATH_MAX];
    fmt_path(hdrf, sizeof hdrf, "%s/hdr", dir);
    fmt_path(bodyf, sizeof bodyf, "%s/body", dir);
    fmt_path(url, sizeof url, "https://%s/v2/", r->host);

    long code = 0;
    if (http_get(url, bodyf, NULL, hdrf, NULL, 0, &code) < 0) {
        fprintf(stderr, "minicontainer: pull: cannot reach https://%s (curl failed)\n", r->host);
        return -1;
    }
    if (code == 200) { tok[0] = '\0'; return 0; }   /* open registry */
    if (code != 401) {
        fprintf(stderr, "minicontainer: pull: %s/v2/ tra ve HTTP %ld\n", r->host, code);
        return -1;
    }

    char hdr[BUF_SIZE_XLARGE] = "";
    if (read_small(hdrf, hdr, sizeof hdr) != 0 || !hdr[0]) {
        fprintf(stderr, "minicontainer: pull: khong doc duoc WWW-Authenticate\n");
        return -1;
    }
    char realm[PATH_MAX] = "", service[BUF_SIZE_MEDIUM] = "", scope[BUF_SIZE_LARGE] = "";
    if (hdr_param(hdr, "realm", realm, sizeof realm) < 0) {
        fprintf(stderr, "minicontainer: pull: registry khong cho biet realm\n");
        return -1;
    }
    hdr_param(hdr, "service", service, sizeof service);
    /* Ask for exactly what we need; the challenge's scope is often empty. */
    fmt_path(scope, sizeof scope, "repository:%s:pull", r->repo);

    char turl[PATH_MAX * 2];
    fmt_path(turl, sizeof turl, "%s%cservice=%s&scope=%s",
             realm, strchr(realm, '?') ? '&' : '?', service, scope);
    if (http_get(turl, bodyf, NULL, hdrf, NULL, 0, &code) < 0 || code != 200) {
        fprintf(stderr, "minicontainer: pull: lay token that bai (HTTP %ld)\n", code);
        return -1;
    }

    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(bodyf, &js, &t, &nt) < 0) {
        fprintf(stderr, "minicontainer: pull: token khong phai JSON\n");
        return -1;
    }
    int k = json_obj_get(js, t, 0, "token");
    if (k < 0) k = json_obj_get(js, t, 0, "access_token");
    int rc = -1;
    if (k >= 0 && t[k].type == JSMN_STRING) {
        json_tok_copy(js, &t[k], tok, tn);
        rc = tok[0] ? 0 : -1;
    }
    json_free(js, t);
    if (rc < 0) fprintf(stderr, "minicontainer: pull: token rong\n");
    else if (getenv("MC_DEBUG_HTTP")) fprintf(stderr, "DBG token len=%zu\n", strlen(tok));
    return rc;
}

/* ---- manifests -------------------------------------------------------- */

static const char *want_arch(const char *platform) {
    if (platform && *platform) {
        const char *s = strchr(platform, '/');
        return s ? s + 1 : platform;
    }
    if (strcmp(MC_ARCH, "aarch64") == 0) return "arm64";
    if (strcmp(MC_ARCH, "x86_64") == 0) return "amd64";
    if (strcmp(MC_ARCH, "armv7l") == 0) return "arm";
    if (strcmp(MC_ARCH, "i386") == 0) return "386";
    return MC_ARCH;
}

static const char *manifest_accept(void) {
    return "Accept: application/vnd.oci.image.index.v1+json,"
           " application/vnd.docker.distribution.manifest.list.v2+json,"
           " application/vnd.oci.image.manifest.v1+json,"
           " application/vnd.docker.distribution.manifest.v2+json";
}

static int get_manifest(const struct ref *r, const char *tok, const char *refstr,
                        const char *dir, const char *out, char *mt, size_t mt_n) {
    char url[PATH_MAX * 2], hdrf[PATH_MAX];
    fmt_path(hdrf, sizeof hdrf, "%s/hdr", dir);
    fmt_path(url, sizeof url, "https://%s/v2/%s/manifests/%s", r->host, r->repo, refstr);
    long code = 0;
    if (http_get(url, out, tok, hdrf, manifest_accept(), 0, &code) < 0) return -1;
    if (code != 200) {
        fprintf(stderr, "minicontainer: pull: manifest %s -> HTTP %ld\n", refstr, code);
        return -1;
    }
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(out, &js, &t, &nt) < 0) {
        fprintf(stderr, "minicontainer: pull: manifest khong phai JSON\n");
        return -1;
    }
    mt[0] = '\0';
    int m = json_obj_get(js, t, 0, "mediaType");
    if (m >= 0) json_tok_copy(js, &t[m], mt, mt_n);
    json_free(js, t);
    return 0;
}

/* Pick the digest for this machine out of a manifest list / image index. */
static int pick_platform(const char *path, const char *platform, char *out, size_t n) {
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(path, &js, &t, &nt) < 0) return -1;
    int arr = json_obj_get(js, t, 0, "manifests");
    const char *want = want_arch(platform);
    int rc = -1;
    if (arr >= 0) {
        for (int k = 0; k < t[arr].size; k++) {
            int e = json_arr_nth(t, arr, k);
            if (e < 0) break;
            int pl = json_obj_get(js, t, e, "platform");
            if (pl < 0) continue;
            int os = json_obj_get(js, t, pl, "os");
            int ar = json_obj_get(js, t, pl, "architecture");
            char a[BUF_SIZE_SMALL] = "", o[BUF_SIZE_SMALL] = "";
            if (os >= 0) json_tok_copy(js, &t[os], o, sizeof o);
            if (ar >= 0) json_tok_copy(js, &t[ar], a, sizeof a);
            if (o[0] && strcmp(o, "linux") != 0) continue;
            if (strcmp(a, want) != 0) continue;
            int dg = json_obj_get(js, t, e, "digest");
            if (dg >= 0) { json_tok_copy(js, &t[dg], out, n); rc = out[0] ? 0 : -1; }
            break;
        }
    }
    json_free(js, t);
    return rc;
}

static int is_index(const char *path) {
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(path, &js, &t, &nt) < 0) return 0;
    int rc = json_obj_get(js, t, 0, "manifests") >= 0;
    json_free(js, t);
    return rc;
}

/* ---- blobs ------------------------------------------------------------ */

static int get_blob(const struct ref *r, const char *tok, const char *digest,
                    const char *dir, const char *blobdir) {
    const char *hex = strncmp(digest, "sha256:", 7) == 0 ? digest + 7 : digest;
    if (strlen(hex) != 64) {
        fprintf(stderr, "minicontainer: pull: digest khong hop le: %s\n", digest);
        return -1;
    }
    char dst[PATH_MAX], url[PATH_MAX * 2], hdrf[PATH_MAX];
    fmt_path(dst, sizeof dst, "%s/%s", blobdir, hex);
    fmt_path(hdrf, sizeof hdrf, "%s/hdr", dir);

    char got[65];
    if (file_has(dst) && sha256_of(dst, got) == 0 && strcmp(got, hex) == 0) {
        printf("  blob %.12s.. [da co]\n", hex);
        return 0;
    }

    fmt_path(url, sizeof url, "https://%s/v2/%s/blobs/%s", r->host, r->repo, digest);
    long code = 0;
    if (http_get(url, dst, tok, hdrf, NULL, 1, &code) < 0 || (code != 200 && code != 206)) {
        /* Server may not do ranges; retry from scratch once. */
        unlink(dst);
        if (http_get(url, dst, tok, hdrf, NULL, 0, &code) < 0 || code != 200) {
            fprintf(stderr, "minicontainer: pull: tai blob %.12s.. that bai (HTTP %ld)\n", hex, code);
            unlink(dst);
            return -1;
        }
    }
    long long sz = fsize(dst);
    if (sz <= 0) {
        fprintf(stderr, "minicontainer: pull: blob rong: %.12s..\n", hex);
        unlink(dst);
        return -1;
    }
    if (sz > REG_MAX_BLOB) {
        fprintf(stderr, "minicontainer: pull: blob qua lon (%lld MB, tran %lld MB)\n",
                sz / 1048576, REG_MAX_BLOB / 1048576);
        unlink(dst);
        return -1;
    }
    if (sha256_of(dst, got) < 0 || strcmp(got, hex) != 0) {
        fprintf(stderr, "minicontainer: pull: sha256 sai cho blob %.12s..\n  expected: %s\n  actual  : %s\n",
                hex, hex, got);
        unlink(dst);
        return -1;
    }
    if (sz >= 1048576)
        printf("  blob %.12s.. %.1f MB\n", hex, sz / 1048576.0);
    else
        printf("  blob %.12s.. %.1f KB\n", hex, sz / 1024.0);
    return 0;
}

/* ---- entry point ------------------------------------------------------ */

int do_pull_registry(const char *refstr, const char *platform) {
    struct ref r;
    if (parse_ref(refstr, &r) < 0) {
        fprintf(stderr, "minicontainer: pull: ten image khong hop le: %s\n", refstr);
        return 1;
    }
    if (!file_has("/usr/bin/curl")) {
        fprintf(stderr, "minicontainer: pull: can /usr/bin/curl de tai tu registry"
                        " (apt install curl)\n");
        return 1;
    }

    /* A fixed staging dir (not mkdtemp) is what makes P2.2 resume work: if the
     * link drops mid-blob, the next `pull` keeps the partial file and curl
     * continues from its size. The lock stops two pulls sharing it. */
    char base[PATH_MAX], dir[PATH_MAX], lockp[PATH_MAX];
    fmt_path(base, sizeof base, "%s", img_base());
    if (mkdir(base, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "minicontainer: pull: khong tao duoc %s: %s\n", base, strerror(errno));
        return 1;
    }
    fmt_path(dir, sizeof dir, "%s/.pull", base);
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "minicontainer: pull: khong tao duoc %s: %s\n", dir, strerror(errno));
        return 1;
    }
    fmt_path(lockp, sizeof lockp, "%s/lock", dir);
    int lfd = open(lockp, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lfd < 0 || flock(lfd, LOCK_EX | LOCK_NB) < 0) {
        fprintf(stderr, "minicontainer: pull: mot tien trinh pull khac dang chay (%s)\n", dir);
        if (lfd >= 0) close(lfd);
        return 1;
    }

    char blobdir[PATH_MAX];
    fmt_path(blobdir, sizeof blobdir, "%s/blobs/sha256", dir);
    char *const mkd[] = { "/usr/bin/mkdir", "-p", blobdir, NULL };
    if (run_wait(mkd) != 0) {
        fprintf(stderr, "minicontainer: pull: khong tao duoc %s\n", blobdir);
        close(lfd);
        return 1;
    }

    printf("pull: %s/%s:%s (%s)\n", r.host, r.repo,
           r.tag[0] ? r.tag : r.digest, want_arch(platform));

    char tok[REG_MAX_TOKEN] = "";
    int rc = 1;
    if (token_for(&r, dir, tok, sizeof tok) < 0) goto out;

    char mpath[PATH_MAX], m2[PATH_MAX], mt[BUF_SIZE_MEDIUM] = "";
    fmt_path(mpath, sizeof mpath, "%s/manifest.json", dir);
    const char *want = r.tag[0] ? r.tag : r.digest;
    if (get_manifest(&r, tok, want, dir, mpath, mt, sizeof mt) < 0) goto out;

    if (is_index(mpath)) {
        char pick[80] = "";
        if (pick_platform(mpath, platform, pick, sizeof pick) < 0) {
            fprintf(stderr, "minicontainer: pull: image khong co bien the cho %s\n", want_arch(platform));
            goto out;
        }
        printf("pull: chon %s -> %.19s..\n", want_arch(platform), pick + (strncmp(pick, "sha256:", 7) == 0 ? 7 : 0));
        fmt_path(m2, sizeof m2, "%s/manifest.arch.json", dir);
        if (get_manifest(&r, tok, pick, dir, m2, mt, sizeof mt) < 0) goto out;
        fmt_path(mpath, sizeof mpath, "%s", m2);
    }

    /* Image manifest: config digest + layer digests. */
    char *js; jsmntok_t *t; int nt;
    if (json_parse_file(mpath, &js, &t, &nt) < 0) {
        fprintf(stderr, "minicontainer: pull: manifest hong\n");
        goto out;
    }
    char cfg[80] = "";
    int cf = json_obj_get(js, t, 0, "config");
    int cd = cf >= 0 ? json_obj_get(js, t, cf, "digest") : -1;
    if (cd >= 0) json_tok_copy(js, &t[cd], cfg, sizeof cfg);
    char layers[REG_MAX_LAYERS][80];
    int nl = 0;
    int ls = json_obj_get(js, t, 0, "layers");
    if (ls >= 0) {
        for (int k = 0; k < t[ls].size && nl < REG_MAX_LAYERS; k++) {
            int e = json_arr_nth(t, ls, k);
            int ld = e >= 0 ? json_obj_get(js, t, e, "digest") : -1;
            if (ld >= 0) json_tok_copy(js, &t[ld], layers[nl++], sizeof layers[0]);
        }
    }
    json_free(js, t);
    if (!cfg[0] || nl == 0) {
        fprintf(stderr, "minicontainer: pull: manifest thieu config/layer\n");
        goto out;
    }

    if (get_blob(&r, tok, cfg, dir, blobdir) < 0) goto out;
    for (int i = 0; i < nl; i++)
        if (get_blob(&r, tok, layers[i], dir, blobdir) < 0) goto out;

    /* Hand the stage to img_install in the docker-save shape it already reads. */
    char man[PATH_MAX], mb[BUF_SIZE_HUGE];
    fmt_path(man, sizeof man, "%s/manifest.json", dir);
    size_t o = 0;
    const char *chex = strncmp(cfg, "sha256:", 7) == 0 ? cfg + 7 : cfg;
    o += (size_t)snprintf(mb + o, sizeof mb - o, "[{\"Config\":\"blobs/sha256/%s\",\"RepoTags\":[\"%s\"],\"Layers\":[",
                          chex, refstr);
    for (int i = 0; i < nl && o < sizeof mb; i++) {
        const char *hx = strncmp(layers[i], "sha256:", 7) == 0 ? layers[i] + 7 : layers[i];
        o += (size_t)snprintf(mb + o, sizeof mb - o, "%s\"blobs/sha256/%s\"", i ? "," : "", hx);
    }
    snprintf(mb + o, sizeof mb - o, "]}]\n");
    write_file(man, mb);

    rc = img_install(dir, NULL);

out:
    if (rc == 0) {
        char *const rmv[] = { "/usr/bin/rm", "-rf", dir, NULL };
        run_wait(rmv);
    } else {
        fprintf(stderr, "minicontainer: pull: giu %s de lan sau tai tiep\n", dir);
    }
    close(lfd);
    return rc;
}
