/* oci.c — OCI runtime bundle (config.json) -> internal run flags.
 *
 * Supported: process.args/env/cwd/user, hostname, root.path,
 *            linux.resources.{memory.limit,cpu.quota+period,pids.limit},
 *            mounts[] (type/source/destination/options).
 * Not supported (declared, warned): hooks, seccomp profiles, devices,
 * rlimits, uidMappings/gidMappings, apparmor/selinux, cgroupsPath.
 * We use our own userns mapping and seccomp filter.
 */
#include "common.h"
#include "jsmn.h"

static char g_env[16][BUF_SIZE_LARGE];
static char g_mnt[16][PATH_MAX * 2];
static char g_args[16][BUF_SIZE_SMALL];
static char g_vals[8][BUF_SIZE_LARGE];

static void warn_unsupported(const char *js, jsmntok_t *t) {
    static const char *keys[] = {
        "hooks", "seccomp", "devices", "rlimits", "uidMappings", "gidMappings",
        "apparmorProfile", "selinuxLabel", "capabilities", "cgroupsPath", "unified"
    };
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (json_obj_get(js, t, 0, keys[i]) >= 0)
            fprintf(stderr, "warning: OCI key '%s' not supported, ignored\n", keys[i]);
    }
}

int oci_to_argv(const char *bundle, char **av, int maxav) {
    char cfg[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(cfg, sizeof cfg, "%s/config.json", bundle);
    char *js;
    jsmntok_t *t;
    int nt;
    if (json_parse_file(cfg, &js, &t, &nt) < 0) {
        fprintf(stderr, "cannot read %s\n", cfg);
        return -1;
    }
    warn_unsupported(js, t);

    int an = 0;
    av[an++] = "minicontainer";
    av[an++] = "run";

    int proc = json_obj_get(js, t, 0, "process");
    if (proc < 0) { fprintf(stderr, "config.json missing process\n"); json_free(js, t); return -1; }

    /* linux.resources -> cgroup flags */
    int lx = json_obj_get(js, t, 0, "linux");
    int res = lx >= 0 ? json_obj_get(js, t, lx, "resources") : -1;
    if (res >= 0) {
        int mem = json_obj_get(js, t, res, "memory");
        int lim = mem >= 0 ? json_obj_get(js, t, mem, "limit") : -1;
        if (lim >= 0 && t[lim].type == JSMN_PRIMITIVE) {
            long v = atol(js + t[lim].start);
            if (v > 0) { snprintf(g_vals[0], sizeof g_vals[0], "--ram=%ld", v); av[an++] = g_vals[0]; }
        }
        int cpu = json_obj_get(js, t, res, "cpu");
        if (cpu >= 0) {
            int q = json_obj_get(js, t, cpu, "quota");
            int p = json_obj_get(js, t, cpu, "period");
            long quota = q >= 0 ? atol(js + t[q].start) : -1;
            long period = p >= 0 ? atol(js + t[p].start) : -1;
            if (quota > 0 && period > 0) {
                snprintf(g_vals[1], sizeof g_vals[1], "--cpu=%ld", quota * 100 / period);
                av[an++] = g_vals[1];
            }
        }
        int pids = json_obj_get(js, t, res, "pids");
        int plim = pids >= 0 ? json_obj_get(js, t, pids, "limit") : -1;
        if (plim >= 0 && t[plim].type == JSMN_PRIMITIVE) {
            long v = atol(js + t[plim].start);
            if (v > 0) { snprintf(g_vals[2], sizeof g_vals[2], "--pids=%ld", v); av[an++] = g_vals[2]; }
        }
    }

    /* hostname */
    int hn = json_obj_get(js, t, 0, "hostname");
    if (hn >= 0 && t[hn].type == JSMN_STRING) {
        char v[BUF_SIZE_SMALL];
        json_tok_copy(js, &t[hn], v, sizeof v);
        if (v[0]) {
            fmt_path(g_vals[3], sizeof g_vals[3], "--hostname=%s", v);
            av[an++] = g_vals[3];
        }
    }

    /* root.path: must stay inside the bundle */
    int root = json_obj_get(js, t, 0, "root");
    int rp = root >= 0 ? json_obj_get(js, t, root, "path") : -1;
    if (rp >= 0) {
        char v[PATH_MAX];
        json_tok_copy(js, &t[rp], v, sizeof v);
        if (strstr(v, "..") || v[0] == '/' || !v[0]) {
            fprintf(stderr, "OCI error: root.path invalid or unsafe\n");
            json_free(js, t);
            return -1;
        }
        fmt_path(g_vals[4], sizeof g_vals[4], "--rootfs=%s/%s", bundle, v);
        av[an++] = g_vals[4];
    }

    /* process.cwd / user */
    int cwd = json_obj_get(js, t, proc, "cwd");
    if (cwd >= 0 && t[cwd].type == JSMN_STRING) {
        char v[PATH_MAX];
        json_tok_copy(js, &t[cwd], v, sizeof v);
        if (v[0] && v[0] == '/' && !strstr(v, "..")) {
            fmt_path(g_vals[5], sizeof g_vals[5], "-w%s", v);
            av[an++] = g_vals[5];
        }
    }
    int user = json_obj_get(js, t, proc, "user");
    if (user >= 0) {
        int u = json_obj_get(js, t, user, "uid");
        int g = json_obj_get(js, t, user, "gid");
        long uid = u >= 0 ? atol(js + t[u].start) : 0;
        long gid = g >= 0 ? atol(js + t[g].start) : uid;
        if (uid != 0 || gid != 0) {
            snprintf(g_vals[6], sizeof g_vals[6], "--user=%ld:%ld", uid, gid);
            av[an++] = g_vals[6];
        }
    }

    /* process.env */
    int env = json_obj_get(js, t, proc, "env");
    if (env >= 0) {
        for (int k = 0; k < t[env].size && k < 16; k++) {
            int e = json_arr_nth(t, env, k);
            if (e < 0) break;
            char kv[BUF_SIZE_LARGE];
            json_tok_copy(js, &t[e], kv, sizeof kv);
            if (!strchr(kv, '=')) continue;
            fmt_path(g_env[k], sizeof g_env[0], "--env=%s", kv);
            av[an++] = g_env[k];
        }
    }

    /* mounts[]: type:source:destination[:opt,opt] */
    int ms = json_obj_get(js, t, 0, "mounts");
    if (ms >= 0) {
        for (int k = 0; k < t[ms].size && k < 16; k++) {
            int m = json_arr_nth(t, ms, k);
            if (m < 0) break;
            int ty = json_obj_get(js, t, m, "type");
            int so = json_obj_get(js, t, m, "source");
            int de = json_obj_get(js, t, m, "destination");
            if (de < 0) continue;
            char type[BUF_SIZE_SMALL] = "", src[PATH_MAX] = "", dst[PATH_MAX] = "";
            if (ty >= 0) json_tok_copy(js, &t[ty], type, sizeof type);
            if (so >= 0) json_tok_copy(js, &t[so], src, sizeof src);
            json_tok_copy(js, &t[de], dst, sizeof dst);
            if (dst[0] != '/' || strstr(dst, "..")) continue;
            char opts[BUF_SIZE_MEDIUM] = "";
            int op = json_obj_get(js, t, m, "options");
            if (op >= 0) {
                size_t o = 0;
                for (int q = 0; q < t[op].size && q < 8; q++) {
                    int e = json_arr_nth(t, op, q);
                    if (e < 0) break;
                    char ov[BUF_SIZE_SMALL];
                    json_tok_copy(js, &t[e], ov, sizeof ov);
                    int w = snprintf(opts + o, sizeof opts - o, "%s%s", o ? "," : "", ov);
                    if (w > 0) o += (size_t)w;
                }
            }
            if (fmt_path(g_mnt[k], sizeof g_mnt[0], "--mount=%s:%s:%s%s%s",
                         type[0] ? type : "bind", src, dst, opts[0] ? ":" : "", opts) < 0) {
                fprintf(stderr, "minicontainer: oci-run: mount spec qua dai, bo qua %s\n", dst);
                continue;
            }
            av[an++] = g_mnt[k];
        }
    }

    /* process.args */
    int args = json_obj_get(js, t, proc, "args");
    if (args < 0 || t[args].size == 0) {
        fprintf(stderr, "config.json missing process.args\n");
        json_free(js, t);
        return -1;
    }
    av[an++] = "--";
    for (int k = 0; k < t[args].size && k < 16 && an < maxav - 1; k++) {
        int e = json_arr_nth(t, args, k);
        if (e < 0) break;
        json_tok_copy(js, &t[e], g_args[k], sizeof g_args[0]);
        av[an++] = g_args[k];
    }
    av[an] = NULL;
    json_free(js, t);
    return an;
}
