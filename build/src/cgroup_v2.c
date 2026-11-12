/* cgroup_v2.c — cgroup v2 implementation (API v2 only, no v1) */
#include "cgroup_v2.h"
#include <sys/sysmacros.h>

static int write_cgroup_file(const char *cgdir, const char *file, const char *val) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "%s/%s", cgdir, file);
    return try_write_file(p, val);
}

static int write_cgroup_file_die(const char *cgdir, const char *file, const char *val) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "%s/%s", cgdir, file);
    write_file(p, val);
    return 0;
}

int cgroup_v2_create(const struct cgroup_v2_cfg *cfg, char *cgdir, size_t cgdir_len) {
    /* Enable controllers at parent (idempotent) */
    write_cgroup_file_die(CGROUP_BASE, "cgroup.subtree_control", "+cpu +memory +pids +io");

    /* Create cgroup v2 directory */
    static int n = 0;
    snprintf(cgdir, cgdir_len, CGROUP_BASE "/minicontainer-%d-%d", getpid(), n++);
    if (mkdir(cgdir, 0755) < 0) return -1;

    if (cfg->ram > 0) {
        char v[BUF_SIZE_SMALL];
        snprintf(v, sizeof v, "%ld", cfg->ram);
        write_cgroup_file_die(cgdir, "memory.max", v);
    }
    if (cfg->cpu > 0) {
        char v[BUF_SIZE_SMALL];
        snprintf(v, sizeof v, "%d %d", cfg->cpu * 1000, CPU_QUOTA_PERIOD);
        write_cgroup_file_die(cgdir, "cpu.max", v);
    }
    if (cfg->pids > 0) {
        char v[BUF_SIZE_SMALL];
        snprintf(v, sizeof v, "%ld", cfg->pids);
        write_cgroup_file_die(cgdir, "pids.max", v);
    }
    if (cfg->io > 0) {
        struct stat st;
        if (stat(cfg->rootfs, &st) < 0) return -1;
        char v[BUF_SIZE_MEDIUM];
        snprintf(v, sizeof v, "%u:%u rbps=%ld wbps=%ld",
            major(st.st_dev), minor(st.st_dev), cfg->io, cfg->io);
        if (write_cgroup_file(cgdir, "io.max", v) < 0) {
            if (errno != ENODEV) return -1;
            snprintf(v, sizeof v, "%u:0 rbps=%ld wbps=%ld",
                major(st.st_dev), cfg->io, cfg->io);
            write_cgroup_file_die(cgdir, "io.max", v);
        }
    }

    /* Create .oldroot before chroot (parent is host-root) */
    char o[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(o, sizeof o, "%s/.oldroot", cfg->rootfs);
    if (mkdir(o, 0700) < 0 && errno != EEXIST) return -1;

    return 0;
}

int cgroup_v2_set_limit(const char *cgdir, const char *controller, const char *value) {
    if (strcmp(controller, "memory") == 0)
        return write_cgroup_file(cgdir, "memory.max", value);
    if (strcmp(controller, "cpu") == 0)
        return write_cgroup_file(cgdir, "cpu.max", value);
    if (strcmp(controller, "pids") == 0)
        return write_cgroup_file(cgdir, "pids.max", value);
    if (strcmp(controller, "io") == 0)
        return write_cgroup_file(cgdir, "io.max", value);
    errno = EINVAL;
    return -1;
}

int cgroup_v2_attach(const char *cgdir, pid_t pid) {
    char v[BUF_SIZE_SMALL];
    snprintf(v, sizeof v, "%d", pid);
    return write_cgroup_file(cgdir, "cgroup.procs", v);
}

void cgroup_v2_kill(const char *cgdir) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "%s/cgroup.kill", cgdir);
    try_write_file(p, "1");
    /* Always also kill via PID list as fallback */
    kill_pids_in_cgroup(cgdir, SIGKILL);
}

/* Send signal to all PIDs in cgroup (read cgroup.procs) */
void kill_pids_in_cgroup(const char *cgdir, int sig) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "%s/cgroup.procs", cgdir);
    FILE *fp = fopen(p, "r");
    if (!fp) return;
    char lb[BUF_SIZE_SMALL];
    while (fgets(lb, sizeof lb, fp)) {
        pid_t pid = atoi(lb);
        if (pid > 0) kill(pid, sig);
    }
    fclose(fp);
}

int cgroup_v2_empty(const char *cgdir) {
    char p[PATH_MAX + BUF_SIZE_SMALL], buf[BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "%s/cgroup.procs", cgdir);
    if (read_small(p, buf, sizeof buf) < 0) return 1;
    return buf[0] == 0;
}

/* Reap descendants reparented to us (PR_SET_CHILD_SUBREAPER). A zombie keeps
 * its PID in cgroup.procs, so rmdir() stays EBUSY until somebody reaps it. */
static void reap_orphans(void) {
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
}

void cgroup_v2_cleanup(const struct cgroup_v2_cfg *cfg, const char *cgdir) {
    /* Remove .oldroot */
    char o[PATH_MAX + BUF_SIZE_SMALL];
    snprintf(o, sizeof o, "%s/.oldroot", cfg->rootfs);
    rmdir(o);

    /* Kill, reap, retry rmdir. Bounded at ~5s: a task stuck in D state cannot
     * be killed anyway, and failing loudly beats hanging the caller. */
    int last_errno = 0;
    for (int i = 0; i < 50; i++) {
        if (rmdir(cgdir) == 0 || errno == ENOENT) return;
        last_errno = errno;
        cgroup_v2_kill(cgdir);
        reap_orphans();
        usleep(100000);
    }
    fprintf(stderr, "minicontainer: warning: cannot remove cgroup %s: %s\n",
            cgdir, strerror(last_errno));
}

/* Legacy wrappers for backward compatibility */
void cgroup_setup(struct cfg *c, char *cgdir) {
    struct cgroup_v2_cfg v2cfg = {
        .ram = c->ram,
        .cpu = c->cpu,
        .pids = c->pids,
        .io = c->io,
        .rootfs = c->rootfs,
    };
    if (cgroup_v2_create(&v2cfg, cgdir, PATH_MAX) < 0) die("cgroup_v2_create");
}

void cgroup_attach(const char *cgdir, pid_t pid) {
    if (cgroup_v2_attach(cgdir, pid) < 0) die("cgroup_v2_attach");
}

void cgroup_cleanup(struct cfg *c, const char *cgdir) {
    struct cgroup_v2_cfg v2cfg = { .rootfs = c->rootfs };
    cgroup_v2_cleanup(&v2cfg, cgdir);
}

void cgroup_kill(const char *cgdir) {
    cgroup_v2_kill(cgdir);
}

int cgroup_empty(const char *cgdir) {
    return cgroup_v2_empty(cgdir);
}