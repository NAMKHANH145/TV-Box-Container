/* state.c — ps/stats/stop/rm: quan ly qua state dir + cgroupfs, khong daemon */
#include "common.h"

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
    snprintf(f, sizeof f, "%s/seccomp", dir); unlink(f);
    snprintf(f, sizeof f, "%s/disk_exceeded", dir); unlink(f);
    snprintf(f, sizeof f, "%s/health_failed", dir); unlink(f);
    rmdir(dir);
    printf("%s: removed\n", id);
    return 0;
}
