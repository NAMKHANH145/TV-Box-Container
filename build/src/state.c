/* state.c — ps/stats/stop/rm: quan ly qua state dir + cgroupfs, khong daemon */
#include "common.h"

static int pid_running(int pid);

static int state_cgdir_valid(const char *path) {
    size_t base = strlen(CGROUP_BASE);
    if (!path || strncmp(path, CGROUP_BASE "/", base + 1) != 0 || !path[base + 1]) return 0;
    for (const char *p = path + base + 1; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == 0 || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return 0;
        p = end ? end + 1 : p + len;
    }
    return 1;
}

static int state_rootfs_valid(const char *path) {
    if (!path || path[0] != '/' || path[1] == '\0' || strcmp(path, "/") == 0) return 0;
    for (const char *p = path + 1; *p;) {
        const char *end = strchr(p, '/');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == 0 || (len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return 0;
        p = end ? end + 1 : p + len;
    }
    char actual[PATH_MAX];
    if (realpath(path, actual) && strcmp(actual, "/") == 0) return 0;
    return 1;
}

static int cgroup_has_pid(const char *cgdir, int pid) {
    char path[PATH_MAX + BUF_SIZE_SMALL], line[BUF_SIZE_SMALL];
    snprintf(path, sizeof path, "%s/cgroup.procs", cgdir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *end;
        long n = strtol(line, &end, 10);
        if (n == pid && (*end == '\n' || *end == '\0')) { found = 1; break; }
    }
    fclose(f);
    return found;
}

int state_relationship_valid(const char *cgdir, int pid) {
    if (!state_cgdir_valid(cgdir)) return 0;
    if (pid <= 0) return 0;
    if (access(cgdir, F_OK) != 0) {
        char proc_missing[PATH_MAX];
        snprintf(proc_missing, sizeof proc_missing, "/proc/%d", pid);
        if (access(proc_missing, F_OK) != 0) return 1;
        char stat_path[PATH_MAX], stat_buf[256], *state;
        snprintf(stat_path, sizeof stat_path, "/proc/%d/stat", pid);
        /* PID may exit between the access() above and this read: a vanished
         * process means the container is stopped, not corrupt state. */
        if (read_small(stat_path, stat_buf, sizeof stat_buf) < 0)
            return access(proc_missing, F_OK) != 0;
        state = strrchr(stat_buf, ')');
        return state && state[1] == ' ' && state[2] == 'Z';
    }
    {
        char actual[PATH_MAX], base[PATH_MAX];
        if (!realpath(cgdir, actual) || !realpath(CGROUP_BASE, base)) return 0;
        size_t n = strlen(base);
        if (strncmp(actual, base, n) != 0 || actual[n] != '/') return 0;
    }
    char proc[PATH_MAX];
    snprintf(proc, sizeof proc, "/proc/%d", pid);
    if (access(proc, F_OK) != 0) return 1; /* stopped state: no live relationship to check */
    return cgroup_has_pid(cgdir, pid);
}

/* Simple JSON output helpers */
static void json_print_kv_str(const char *key, const char *val, int *first) {
    if (!*first) printf(", ");
    *first = 0;
    printf("\"%s\": \"", key);
    for (const char *p = val; *p; p++) {
        switch (*p) {
            case '"': printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            default: putchar(*p);
        }
    }
    printf("\"");
}

static void json_print_kv_int(const char *key, long val, int *first) {
    if (!*first) printf(", ");
    *first = 0;
    printf("\"%s\": %ld", key, val);
}

/* A JSON string value without a key (for arrays). */
static void json_print_value(const char *val) {
    printf("\"");
    for (const char *p = val; *p; p++) {
        switch (*p) {
            case '"': printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            default: putchar(*p);
        }
    }
    printf("\"");
}

static void json_print_kv_arr(const char *key, char **arr, int n, int *first) {
    if (n <= 0) return;
    if (!*first) printf(", ");
    *first = 0;
    printf("\"%s\": [", key);
    for (int i = 0; i < n; i++) {
        if (i) printf(", ");
        json_print_value(arr[i]);
    }
    printf("]");
}

/* ministat <id> */
int do_stats(const char *id, int json) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[256], f[PATH_MAX + 16], buf[256], cg[PATH_MAX];
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) {
        fprintf(stderr, "khong co container '%s'\n", id);
        return 1;
    }
    int pid = atoi(buf);
    snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
    int alive = (access(f, F_OK) == 0);
    snprintf(f, sizeof f, "%s/cgdir", dir);
    if (read_small(f, cg, sizeof cg) == 0 && !state_relationship_valid(cg, pid)) {
        fprintf(stderr, "invalid persisted cgroup path\n");
        return 1;
    }

    if (json) {
        int first = 1;
        printf("{");
        json_print_kv_str("id", id, &first);
        json_print_kv_str("status", alive ? "RUNNING" : "STOPPED", &first);
        json_print_kv_int("pid", pid, &first);

        snprintf(f, sizeof f, "%s/cgdir", dir);
        if (read_small(f, cg, sizeof cg) == 0) {
            snprintf(f, sizeof f, "%s/memory.current", cg);
            if (read_small(f, buf, sizeof buf) == 0)
                json_print_kv_str("memory", buf, &first);
            snprintf(f, sizeof f, "%s/pids.current", cg);
            if (read_small(f, buf, sizeof buf) == 0)
                json_print_kv_str("pids", buf, &first);
            snprintf(f, sizeof f, "%s/cpu.stat", cg);
            int fd = open(f, O_RDONLY);
            if (fd >= 0) {
                ssize_t r = read(fd, buf, sizeof buf - 1);
                close(fd);
                if (r > 0) {
                    buf[r] = 0;
                    char *u = strstr(buf, "usage_usec");
                    char num[64];
                    if (u && sscanf(u, "usage_usec %63s", num) == 1)
                        json_print_kv_str("cpu", num, &first);
                }
            }
        }
        printf("}\n");
        return 0;
    }

    printf("id:      %s\nstatus:  %s\npid:     %d\n", id, alive ? "RUNNING" : "STOPPED", pid);
    snprintf(f, sizeof f, "%s/cgdir", dir);
    if (read_small(f, cg, sizeof cg) < 0) return 0;
    snprintf(f, sizeof f, "%s/memory.current", cg);
    if (read_small(f, buf, sizeof buf) == 0) printf("memory:  %s bytes\n", buf);
    snprintf(f, sizeof f, "%s/pids.current", cg);
    if (read_small(f, buf, sizeof buf) == 0) printf("pids:    %s\n", buf);
    snprintf(f, sizeof f, "%s/cpu.stat", cg);
    int fd = open(f, O_RDONLY);
    if (fd >= 0) {
        ssize_t r = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (r > 0) {
            buf[r] = 0;
            char *u = strstr(buf, "usage_usec");
            char num[64];
            if (u && sscanf(u, "usage_usec %63s", num) == 1)
                printf("cpu:     %s usec\n", num);
        }
    }
    return 0;
}

/* ---- P3.2: persisted run config, used by inspect and start ---- */

int state_save_run_config(const char *dir, const struct cfg *c) {
    char path[PATH_MAX];
    if (fmt_path(path, sizeof path, "%s/config", dir) < 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "rootfs=%s\n", c->rootfs ? c->rootfs : "");
    fprintf(f, "image=%s\n", c->image ? c->image : "");
    fprintf(f, "net=%s\n", c->net ? c->net : "host");
    fprintf(f, "hostname=%s\n", c->hostname ? c->hostname : "");
    fprintf(f, "seccomp=%d\n", c->seccomp_mode);
    fprintf(f, "seccomp_profile=%s\n", c->seccomp_profile ? c->seccomp_profile : "");
    fprintf(f, "init=%d\n", c->init_enabled);
    fprintf(f, "emulate=%d\n", c->emulate);
    fprintf(f, "rm=%d\n", c->rm_after);
    fprintf(f, "restart=%s\n", c->restart ? c->restart : "no");
    fprintf(f, "ram=%ld\n", c->ram);
    fprintf(f, "cpu=%d\n", c->cpu);
    fprintf(f, "pids=%ld\n", c->pids);
    fprintf(f, "io=%ld\n", c->io);
    fprintf(f, "disk=%ld\n", c->max_disk);
    fprintf(f, "memory_high=%ld\n", c->memory_high);
    fprintf(f, "cpu_weight=%ld\n", c->cpu_weight);
    fprintf(f, "workdir=%s\n", c->workdir ? c->workdir : "");
    fprintf(f, "user=%s\n", c->user ? c->user : "");
    for (int i = 0; i < c->nenv; i++)
        if (!strchr(c->env[i], '\n')) fprintf(f, "env=%s\n", c->env[i]);
    for (int i = 0; i < c->nmounts; i++)
        if (!strchr(c->mounts[i], '\n')) fprintf(f, "mount=%s\n", c->mounts[i]);
    for (int i = 0; i < c->npublish; i++)
        if (!strchr(c->publish[i], '\n')) fprintf(f, "publish=%s\n", c->publish[i]);
    for (int i = 0; i < c->nulimits; i++)
        if (!strchr(c->ulimits[i], '\n')) fprintf(f, "ulimit=%s\n", c->ulimits[i]);
    if (fclose(f) != 0) return -1;

    if (fmt_path(path, sizeof path, "%s/argv", dir) < 0) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    int ok = 1;
    char **sav = c->base_cmd ? c->base_cmd : c->cmd;
    for (char **a = sav; a && *a; a++) {
        size_t n = strlen(*a) + 1;
        if (write(fd, *a, n) != (ssize_t)n) { ok = 0; break; }
    }
    close(fd);
    if (!ok) unlink(path);
    return ok ? 0 : -1;
}

struct persisted_cfg {
    char lines[160][PATH_MAX];
    char *env[32], *mounts[32], *publish[MAX_PUBLISH], *ulimits[16];
    int nenv, nmounts, npublish, nulimits;
    char argbuf[BUF_SIZE_HUGE];
    char *argv[65];
    int argc;
};

static struct persisted_cfg g_pcfg;

static int load_run_config(const char *dir, struct cfg *c) {
    memset(c, 0, sizeof *c);
    c->ram = -1; c->cpu = -1; c->pids = 64; c->io = -1; c->max_disk = -1;
    c->net = "host";
    c->hostname = "minicontainer-os";
    c->restart = "no";

    char path[PATH_MAX];
    if (fmt_path(path, sizeof path, "%s/config", dir) < 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (n < 160 && fgets(g_pcfg.lines[n], PATH_MAX, f)) {
        char *line = g_pcfg.lines[n];
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        if (!line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        const char *k = line, *v = eq;
        if (strcmp(k, "rootfs") == 0) c->rootfs = v;
        else if (strcmp(k, "image") == 0) c->image = v[0] ? v : NULL;
        else if (strcmp(k, "net") == 0) c->net = v;
        else if (strcmp(k, "hostname") == 0) c->hostname = v;
        else if (strcmp(k, "seccomp") == 0) c->seccomp_mode = atoi(v);
        else if (strcmp(k, "seccomp_profile") == 0) c->seccomp_profile = v[0] ? v : NULL;
        else if (strcmp(k, "init") == 0) c->init_enabled = atoi(v);
        else if (strcmp(k, "emulate") == 0) c->emulate = atoi(v);
        else if (strcmp(k, "rm") == 0) c->rm_after = atoi(v);
        else if (strcmp(k, "restart") == 0) c->restart = v;
        else if (strcmp(k, "ram") == 0) c->ram = atol(v);
        else if (strcmp(k, "cpu") == 0) c->cpu = atoi(v);
        else if (strcmp(k, "pids") == 0) c->pids = atol(v);
        else if (strcmp(k, "io") == 0) c->io = atol(v);
        else if (strcmp(k, "disk") == 0) c->max_disk = atol(v);
        else if (strcmp(k, "memory_high") == 0) c->memory_high = atol(v);
        else if (strcmp(k, "cpu_weight") == 0) c->cpu_weight = atol(v);
        else if (strcmp(k, "workdir") == 0) c->workdir = v[0] ? v : NULL;
        else if (strcmp(k, "user") == 0) c->user = v[0] ? v : NULL;
        else if (strcmp(k, "env") == 0 && g_pcfg.nenv < 32) {
            c->env = g_pcfg.env;
            g_pcfg.env[g_pcfg.nenv++] = (char *)v;
        } else if (strcmp(k, "mount") == 0 && g_pcfg.nmounts < 32) {
            c->mounts = g_pcfg.mounts;
            g_pcfg.mounts[g_pcfg.nmounts++] = (char *)v;
        } else if (strcmp(k, "publish") == 0 && g_pcfg.npublish < MAX_PUBLISH) {
            c->publish = g_pcfg.publish;
            g_pcfg.publish[g_pcfg.npublish++] = (char *)v;
        } else if (strcmp(k, "ulimit") == 0 && g_pcfg.nulimits < 16) {
            c->ulimits = g_pcfg.ulimits;
            g_pcfg.ulimits[g_pcfg.nulimits++] = (char *)v;
        }
        n++;
    }
    fclose(f);
    c->nenv = g_pcfg.nenv;
    c->nmounts = g_pcfg.nmounts;
    c->npublish = g_pcfg.npublish;
    c->nulimits = g_pcfg.nulimits;

    if (fmt_path(path, sizeof path, "%s/argv", dir) < 0) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, g_pcfg.argbuf, sizeof g_pcfg.argbuf - 1);
    close(fd);
    if (r <= 0) return -1;
    g_pcfg.argbuf[r] = 0;
    int argc = 0;
    for (ssize_t i = 0; i < r && argc < 64; i += (ssize_t)strlen(g_pcfg.argbuf + i) + 1)
        g_pcfg.argv[argc++] = g_pcfg.argbuf + i;
    if (argc == 0) return -1;
    g_pcfg.argv[argc] = NULL;
    g_pcfg.argc = argc;
    c->cmd = g_pcfg.argv;
    return 0;
}

/* inspect <id>: JSON, same field names as `ps --json` for status/pid. */
int do_inspect(const char *id, int json) {
    (void)json;
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[BUF_SIZE_LARGE], f[PATH_MAX + BUF_SIZE_SMALL], buf[BUF_SIZE_SMALL];
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) {
        fprintf(stderr, "no container '%s'\n", id);
        return 1;
    }
    int pid = atoi(buf);
    snprintf(f, sizeof f, "/proc/%d/stat", pid);
    const char *status = "STOPPED";
    char st[BUF_SIZE_MEDIUM];
    if (read_small(f, st, sizeof st) == 0) {
        char *e = strrchr(st, ')');
        if (e && e[1] == ' ' && e[2] && e[2] != 'Z') status = "RUNNING";
    }

    struct cfg c;
    int have_cfg = load_run_config(dir, &c) == 0;
    if (!have_cfg) {
        memset(&c, 0, sizeof c);
        c.ram = -1; c.cpu = -1; c.pids = -1; c.io = -1; c.max_disk = -1;
        c.net = ""; c.hostname = ""; c.restart = "no";
    }

    char cg[PATH_MAX] = "";
    snprintf(f, sizeof f, "%s/cgdir", dir);
    read_small(f, cg, sizeof cg);

    int first = 1;
    printf("{");
    json_print_kv_str("id", id, &first);
    json_print_kv_str("status", status, &first);
    json_print_kv_int("pid", pid, &first);
    int have_argv = have_cfg && c.cmd && c.cmd[0];
    if (have_argv)
        json_print_kv_arr("argv", c.cmd, g_pcfg.argc, &first);
    json_print_kv_str("rootfs", c.rootfs ? c.rootfs : "", &first);
    json_print_kv_str("image", c.image ? c.image : "", &first);
    json_print_kv_str("net", c.net ? c.net : "", &first);
    json_print_kv_str("hostname", c.hostname ? c.hostname : "", &first);
    json_print_kv_int("seccomp", c.seccomp_mode, &first);
    json_print_kv_str("restart", c.restart ? c.restart : "no", &first);
    json_print_kv_str("cgroup", cg, &first);
    json_print_kv_int("memory", c.ram, &first);
    json_print_kv_int("memory_high", c.memory_high, &first);
    json_print_kv_int("cpu", c.cpu, &first);
    json_print_kv_int("cpu_weight", c.cpu_weight, &first);
    json_print_kv_int("pids", c.pids, &first);
    json_print_kv_int("io", c.io, &first);
    if (have_cfg) {
        json_print_kv_arr("publish", c.publish, c.npublish, &first);
        json_print_kv_arr("mounts", c.mounts, c.nmounts, &first);
        json_print_kv_arr("env", c.env, c.nenv, &first);
    }
    printf("}\n");
    return 0;
}

/* start <id>: replay the saved config. Detached only (state exists only for
 * containers that were started with -d). */
int do_start(const char *id) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[BUF_SIZE_LARGE], f[PATH_MAX + BUF_SIZE_SMALL], buf[BUF_SIZE_SMALL];
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) {
        fprintf(stderr, "no container '%s'\n", id);
        return 1;
    }
    int oldpid = atoi(buf);
    char cg[PATH_MAX] = "";
    snprintf(f, sizeof f, "%s/cgdir", dir);
    read_small(f, cg, sizeof cg);
    if (pid_running(oldpid) || (cg[0] && access(cg, F_OK) == 0 && !cgroup_empty(cg))) {
        fprintf(stderr, "'%s' is already running\n", id);
        return 1;
    }

    struct cfg c;
    if (load_run_config(dir, &c) < 0) {
        fprintf(stderr, "'%s' has no saved config (created by an older version?) \u2014 use rm + run\n", id);
        return 1;
    }
    if (!c.rootfs || !c.cmd || !c.cmd[0]) {
        fprintf(stderr, "'%s': saved config is incomplete\n", id);
        return 1;
    }
    c.name = id;
    c.detach = 1;
    c.reuse_state = 1;

    snprintf(f, sizeof f, "%s/pid", dir); unlink(f);
    snprintf(f, sizeof f, "%s/cgdir", dir); unlink(f);
    snprintf(f, sizeof f, "%s/watchdog", dir); unlink(f);
    /* Keep restart_pid: when `start` was invoked by the keeper itself, the
     * keeper is still alive and `stop` must be able to kill it. */
    snprintf(f, sizeof f, "%s/ip", dir); unlink(f);
    snprintf(f, sizeof f, "%s/publish", dir); unlink(f);
    snprintf(f, sizeof f, "%s/disk_exceeded", dir); unlink(f);
    snprintf(f, sizeof f, "%s/health_failed", dir); unlink(f);

    /* The old cgroup dir usually outlives its processes; remove it before
     * run creates a fresh one, otherwise `start` leaks one dir per replay. */
    if (cg[0] && access(cg, F_OK) == 0 && cgroup_empty(cg)) {
        for (int i = 0; i < 20 && rmdir(cg) < 0 && errno != ENOENT; i++) usleep(100000);
    }

    int rc = do_run(&c);
    if (rc == 0) printf("%s: started\n", id);
    return rc;
}

/* ps */
int do_ps(int json) {
    DIR *d = opendir(STATE_BASE);
    if (!d) {
        if (json) {
            printf("[]\n");
        } else {
            printf("khong co container nao\n");
        }
        return 0;
    }

    if (json) printf("[");
    struct dirent *e;
    int n = 0;
    int first = 1;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char dir[512], f[PATH_MAX + 16], buf[256], cmd[128] = "-";
        snprintf(dir, sizeof dir, STATE_BASE "/%s", e->d_name);
        snprintf(f, sizeof f, "%s/pid", dir);
        if (read_small(f, buf, sizeof buf) < 0) continue;
        int pid = atoi(buf);
        snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
        const char *st = (access(f, F_OK) == 0) ? "RUNNING" : "STOPPED";
        snprintf(f, sizeof f, "%s/cmd", dir);
        if (read_small(f, cmd, sizeof cmd) == 0 && strlen(cmd) > 40) {
            cmd[37] = '.'; cmd[38] = '.'; cmd[39] = '.'; cmd[40] = 0;
        }
        if (json) {
            if (!first) printf(",");
            first = 0;
            printf("\n  {\n");
            printf("    \"id\": \"%s\",\n", e->d_name);
            printf("    \"status\": \"%s\",\n", st);
            printf("    \"pid\": %d,\n", pid);
            char cmd_esc[512]; size_t esc_j = 0;
            for (const char *p = cmd; *p && esc_j + 2 < sizeof(cmd_esc) - 1; p++) {
                switch (*p) {
                    case '"': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = '"'; break;
                    case '\\': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = '\\'; break;
                    case '\n': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 'n'; break;
                    case '\r': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 'r'; break;
                    case '\t': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 't'; break;
                    default: cmd_esc[esc_j++] = *p;
                }
            }
            cmd_esc[esc_j] = '\0';
            printf("    \"command\": \"%s\"\n", cmd_esc);
            printf("  }");
        } else {
            if (strlen(cmd) > 40) {
                cmd[37] = '.'; cmd[38] = '.'; cmd[39] = '.'; cmd[40] = 0;
            }
            printf("%-16s %-8s %-8d %s\n", e->d_name, st, pid, cmd);
        }
        n++;
    }
    closedir(d);
    if (json) {
        if (first) printf("]\n");
        else printf("\n]\n");
    } else {
        if (!n) printf("(trong)\n");
    }
    return 0;
}

/* Helper: parse space-joined publish specs into NULL-terminated array */
static int parse_publish_array(const char *pubbuf, char **pubs, int max_pubs) {
    if (!pubbuf || !pubbuf[0]) return 0;
    char pubbuf_copy[512];
    snprintf(pubbuf_copy, sizeof pubbuf_copy, "%s", pubbuf);
    int np = 0;
    char *tok = strtok(pubbuf_copy, " ");
    while (tok && np < max_pubs - 1) { pubs[np++] = tok; tok = strtok(NULL, " "); }
    pubs[np] = NULL;
    return np;
}

/* --restart keeper (run.c restart_keeper) is a setsid() session leader, so its
 * whole process group goes with it. Killing only restart_pid leaves an already
 * forked `start <id>` child alive, which revives the container behind stop's
 * back (container + cgroup leak, invisible in `ps`). */
static void kill_restart_keeper(int rp) {
    if (rp <= 1) return;
    char p[64];
    if (getpgid(rp) == rp) kill(-rp, SIGTERM);
    kill(rp, SIGTERM);
    for (int i = 0; i < 20; i++) {
        snprintf(p, sizeof p, "/proc/%d/stat", rp);
        if (access(p, F_OK) != 0) return;
        usleep(100000);
    }
    if (getpgid(rp) == rp) kill(-rp, SIGKILL);
    kill(rp, SIGKILL);
}

/* stop <id>: TERM ca cgroup, cho timeout, KILL neu con sot, don cgroup/watchdog */
int do_stop(const char *id, int timeout) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[256], f[PATH_MAX + 16];
    char cg[PATH_MAX] = "", rootfs[PATH_MAX] = "", wdbuf[64] = "", ipbuf[64] = "", pubbuf[512] = "";
    int pid = 0;
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/cgdir", dir);
    if (read_small(f, cg, sizeof cg) < 0) {
        fprintf(stderr, "khong co container '%s'\n", id);
        return 1;
    }
    snprintf(f, sizeof f, "%s/pid", dir);
    { char pb[64]; if (read_small(f, pb, sizeof pb) == 0) pid = atoi(pb); }
    snprintf(f, sizeof f, "%s/rootfs", dir);
    read_small(f, rootfs, sizeof rootfs);
    snprintf(f, sizeof f, "%s/ip", dir);
    read_small(f, ipbuf, sizeof ipbuf);
    snprintf(f, sizeof f, "%s/publish", dir);
    read_small(f, pubbuf, sizeof pubbuf);
    snprintf(f, sizeof f, "%s/watchdog", dir);
    int wd = (read_small(f, wdbuf, sizeof wdbuf) == 0) ? atoi(wdbuf) : 0;
    /* Kill the --restart keeper BEFORE the container, otherwise it can see
     * the pid disappear and start the container right back up. */
    snprintf(f, sizeof f, "%s/restart_pid", dir);
    { char rb[BUF_SIZE_SMALL]; if (read_small(f, rb, sizeof rb) == 0) kill_restart_keeper(atoi(rb)); }
    unlink(f);

    if (!state_rootfs_valid(rootfs) || !state_relationship_valid(cg, pid)) {
        fprintf(stderr, "invalid persisted container state\n");
        return 1;
    }

    if (access(cg, F_OK) != 0) {
        printf("%s: da dung\n", id);
    } else {
        kill_pids_in_cgroup(cg, SIGTERM);
        int waited = 0;
        while (waited < timeout * 10 && !cgroup_empty(cg)) {
            usleep(100000);
            waited++;
        }
        if (!cgroup_empty(cg)) {
            cgroup_kill(cg);
            for (int i = 0; i < 50 && !cgroup_empty(cg); i++)
                usleep(100000);
        }
        for (int i = 0; i < 20; i++) {
            if (rmdir(cg) == 0 || errno == ENOENT) break;
            usleep(100000);
        }
        printf("%s: stopped\n", id);
    }

    if (rootfs[0]) {
        char o[PATH_MAX + 16];
        snprintf(o, sizeof o, "%s/.oldroot", rootfs);
        rmdir(o);
    }
    if (ipbuf[0] && pid > 0) {
        /* bridge container: don NAT + veth. */
        char *pubs[16];
        int np = parse_publish_array(pubbuf, pubs, 16);
        bridge_teardown(pid, pubs, np);
    }
    if (wd > 1) kill(wd, SIGTERM);
    snprintf(f, sizeof f, "%s/watchdog", dir);
    unlink(f);
    journal_event("container stop id=%s", id);
    return 0;
}

/* rm <id>: xoa state (doi STOPPED truoc, tru khi -f), don cgroup con sot */
int do_rm(const char *id, int force) {
    if (!valid_name(id)) { fprintf(stderr, "invalid container id\n"); return 1; }
    char dir[256], f[PATH_MAX + 16], buf[256];
    char cg[PATH_MAX] = "", rootfs[PATH_MAX] = "", wdbuf[64] = "";
    char ipbuf[64] = "", pubbuf[512] = "";
    snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) {
        fprintf(stderr, "khong co container '%s'\n", id);
        return 1;
    }
    int pid = atoi(buf);
    snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
    int alive = 0;
    if (access(f, F_OK) == 0) {
        char st[256], *sp;
        snprintf(f, sizeof f, "/proc/%d/stat", pid);
        if (read_small(f, st, sizeof st) == 0 && (sp = strchr(st, ')')) && sp[2]) {
            if (sp[2] != 'Z') alive = 1;
        } else {
            alive = 1;
        }
    }
    /* Zombie container (state == 'Z') should be removable WITHOUT -f.
     * Also check cgroup emptiness as a secondary signal. */
    if (alive) {
        if (!force) {
            fprintf(stderr, "'%s' dang chay — stop truoc hoac rm -f\n", id);
            return 1;
        }
        do_stop(id, 3);
    }

    snprintf(f, sizeof f, "%s/cgdir", dir);
    read_small(f, cg, sizeof cg);
    snprintf(f, sizeof f, "%s/rootfs", dir);
    read_small(f, rootfs, sizeof rootfs);
    snprintf(f, sizeof f, "%s/ip", dir);
    read_small(f, ipbuf, sizeof ipbuf);
    snprintf(f, sizeof f, "%s/publish", dir);
    read_small(f, pubbuf, sizeof pubbuf);
    snprintf(f, sizeof f, "%s/watchdog", dir);
    int wd = (read_small(f, wdbuf, sizeof wdbuf) == 0) ? atoi(wdbuf) : 0;
    snprintf(f, sizeof f, "%s/restart_pid", dir);
    { char rb[BUF_SIZE_SMALL]; if (read_small(f, rb, sizeof rb) == 0) kill_restart_keeper(atoi(rb)); }
    unlink(f);

    if (!state_rootfs_valid(rootfs) || !state_relationship_valid(cg, pid)) {
        fprintf(stderr, "invalid persisted container state\n");
        return 1;
    }

    if (cg[0] && access(cg, F_OK) == 0) {
        /* rm means the container is going away: kill any orphan left behind
         * (its init may already be gone), otherwise the cgroup leaks. */
        cgroup_kill(cg);
        for (int i = 0; i < 50 && !cgroup_empty(cg); i++)
            usleep(100000);
        int gone = 0, last_errno = 0;
        for (int i = 0; i < 20; i++) {
            if (rmdir(cg) == 0 || errno == ENOENT) { gone = 1; break; }
            last_errno = errno;
            usleep(100000);
        }
        if (!gone)
            fprintf(stderr, "minicontainer: warning: cannot remove cgroup %s: %s\n",
                    cg, strerror(last_errno));
    }
    if (rootfs[0]) {
        char o[PATH_MAX + 16];
        snprintf(o, sizeof o, "%s/.oldroot", rootfs);
        rmdir(o);
    }
    if (ipbuf[0] && pid > 0) {
        /* bridge container: don NAT + veth. */
        char *pubs[16];
        int np = parse_publish_array(pubbuf, pubs, 16);
        bridge_teardown(pid, pubs, np);
    }
    if (wd > 1) kill(wd, SIGTERM);

    snprintf(f, sizeof f, "%s/pid", dir); unlink(f);
    snprintf(f, sizeof f, "%s/cgdir", dir); unlink(f);
    snprintf(f, sizeof f, "%s/cmd", dir); unlink(f);
    snprintf(f, sizeof f, "%s/rootfs", dir); unlink(f);
    snprintf(f, sizeof f, "%s/ip", dir); unlink(f);
    snprintf(f, sizeof f, "%s/publish", dir); unlink(f);
    snprintf(f, sizeof f, "%s/watchdog", dir); unlink(f);
    snprintf(f, sizeof f, "%s/restart_pid", dir); unlink(f);
    snprintf(f, sizeof f, "%s/seccomp", dir); unlink(f);
    snprintf(f, sizeof f, "%s/config", dir); unlink(f);
    snprintf(f, sizeof f, "%s/argv", dir); unlink(f);
    snprintf(f, sizeof f, "%s/disk_exceeded", dir); unlink(f);
    snprintf(f, sizeof f, "%s/health_failed", dir); unlink(f);
    log_purge(dir);
    rmdir(dir);
    journal_event("container remove id=%s", id);
    printf("%s: removed\n", id);
    return 0;
}

/* ---- P0.3: gc — reconcile state left behind by kill -9 / power loss ---- */

static int pid_running(int pid) {
    if (pid <= 0) return 0;
    char p[BUF_SIZE_SMALL], buf[BUF_SIZE_MEDIUM];
    snprintf(p, sizeof p, "/proc/%d/stat", pid);
    if (read_small(p, buf, sizeof buf) < 0) return 0;
    char *e = strrchr(buf, ')');
    return e && e[1] == ' ' && e[2] && e[2] != 'Z';
}

/* Remove one state dir if nothing in it is alive. Returns 1 if it was stale. */
static int gc_state(const char *name, int dry) {
    char dir[BUF_SIZE_LARGE], f[PATH_MAX + 16], buf[BUF_SIZE_MEDIUM];
    char cg[PATH_MAX] = "", ipbuf[BUF_SIZE_SMALL] = "", pubbuf[BUF_SIZE_LARGE] = "";
    snprintf(dir, sizeof dir, STATE_BASE "/%s", name);
    snprintf(f, sizeof f, "%s/pid", dir);
    if (read_small(f, buf, sizeof buf) < 0) return 0;      /* not a container dir */
    int pid = atoi(buf);
    snprintf(f, sizeof f, "%s/cgdir", dir);   read_small(f, cg, sizeof cg);
    snprintf(f, sizeof f, "%s/ip", dir);      read_small(f, ipbuf, sizeof ipbuf);
    snprintf(f, sizeof f, "%s/publish", dir); read_small(f, pubbuf, sizeof pubbuf);
    snprintf(f, sizeof f, "%s/watchdog", dir);
    int wd = (read_small(f, buf, sizeof buf) == 0) ? atoi(buf) : 0;
    int cg_live = (cg[0] && access(cg, F_OK) == 0 && !cgroup_empty(cg));
    if (pid_running(pid) || cg_live) return 0;             /* still a live container */

    printf("gc: %s (pid %d da chet)%s\n", name, pid, dry ? " [dry-run]" : "");
    if (dry) return 1;

    if (cg[0] && state_cgdir_valid(cg) && access(cg, F_OK) == 0) {
        cgroup_kill(cg);
        for (int i = 0; i < 50 && !cgroup_empty(cg); i++) usleep(100000);
        for (int i = 0; i < 20 && rmdir(cg) < 0 && errno != ENOENT; i++) usleep(100000);
    }
    if (ipbuf[0] && pid > 0) {
        char *pubs[16];
        int np = parse_publish_array(pubbuf, pubs, 16);
        bridge_teardown(pid, pubs, np);
    }
    if (wd > 1) kill(wd, SIGTERM);
    snprintf(f, sizeof f, "%s/restart_pid", dir);
    { char rb[BUF_SIZE_SMALL]; if (read_small(f, rb, sizeof rb) == 0) { int rp = atoi(rb); if (rp > 1) kill(rp, SIGTERM); } }

    snprintf(f, sizeof f, "%s/pid", dir);           unlink(f);
    snprintf(f, sizeof f, "%s/cgdir", dir);         unlink(f);
    snprintf(f, sizeof f, "%s/cmd", dir);           unlink(f);
    snprintf(f, sizeof f, "%s/rootfs", dir);        unlink(f);
    snprintf(f, sizeof f, "%s/ip", dir);            unlink(f);
    snprintf(f, sizeof f, "%s/publish", dir);       unlink(f);
    snprintf(f, sizeof f, "%s/watchdog", dir);      unlink(f);
    snprintf(f, sizeof f, "%s/restart_pid", dir);   unlink(f);
    snprintf(f, sizeof f, "%s/seccomp", dir);       unlink(f);
    snprintf(f, sizeof f, "%s/config", dir);        unlink(f);
    snprintf(f, sizeof f, "%s/argv", dir);          unlink(f);
    snprintf(f, sizeof f, "%s/disk_exceeded", dir); unlink(f);
    snprintf(f, sizeof f, "%s/health_failed", dir); unlink(f);
    log_purge(dir);
    rmdir(dir);
    return 1;
}

/* Orphan cgroups: minicontainer-<pid>-<n> whose pid is gone. A fresh dir is
 * skipped for 60 s so gc can never race a `run` that is still setting up. */
static int gc_cgroups(int dry) {
    DIR *d = opendir(CGROUP_BASE);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "minicontainer-", 14) != 0) continue;
        int pid = atoi(e->d_name + 14);
        if (pid_running(pid)) continue;
        char cg[PATH_MAX];
        snprintf(cg, sizeof cg, CGROUP_BASE "/%s", e->d_name);
        struct stat st;
        if (stat(cg, &st) < 0) continue;
        time_t now = time(NULL);
        if (now - st.st_mtime < 60) continue;
        if (cgroup_empty(cg)) {
            printf("gc: cgroup rac %s%s\n", e->d_name, dry ? " [dry-run]" : "");
            if (!dry) rmdir(cg);
            n++;
            continue;
        }
        /* Non-empty but every member is dead/zombie: kill then remove. */
        int live = 0;
        char pf[PATH_MAX + 16];
        snprintf(pf, sizeof pf, "%s/cgroup.procs", cg);
        FILE *f = fopen(pf, "r");
        if (f) {
            char line[BUF_SIZE_SMALL];
            while (fgets(line, sizeof line, f))
                if (pid_running(atoi(line))) live = 1;
            fclose(f);
        }
        if (live) continue;
        printf("gc: cgroup mo coi %s%s\n", e->d_name, dry ? " [dry-run]" : "");
        if (!dry) {
            cgroup_kill(cg);
            for (int i = 0; i < 50 && !cgroup_empty(cg); i++) usleep(100000);
            for (int i = 0; i < 20 && rmdir(cg) < 0 && errno != ENOENT; i++) usleep(100000);
        }
        n++;
    }
    closedir(d);
    return n;
}

int do_gc(int dry) {
    int n = 0;
    DIR *d = opendir(STATE_BASE);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            if (!valid_name(e->d_name)) continue;
            n += gc_state(e->d_name, dry);
        }
        closedir(d);
    }
    n += gc_cgroups(dry);
    if (n == 0) printf("gc: khong co gi de don\n");
    else printf("gc: %d muc%s\n", n, dry ? " (dry-run, chua xoa)" : " da don");
    return 0;
}
