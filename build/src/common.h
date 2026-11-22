/* common.h — common definitions for all minicontainer modules */
#pragma once
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define STACK_SIZE (1024 * 1024)
#define MC_VERSION "2.0.1"

/* Compile-time target arch/libc — used by `--version`, `check`, `sdk pack` and
 * the rootfs arch guard in run.c. Keep in sync with generate_seccomp.py. */
#if defined(__aarch64__)
#define MC_ARCH "aarch64"
#elif defined(__x86_64__)
#define MC_ARCH "x86_64"
#elif defined(__arm__)
#define MC_ARCH "armv7l"
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
#define MC_ARCH "riscv64"
#elif defined(__i386__)
#define MC_ARCH "i386"
#else
#define MC_ARCH "unknown"
#endif

#ifdef __GLIBC__
#define MC_LIBC "glibc"
#else
#define MC_LIBC "musl"
#endif

#define CGROUP_BASE "/sys/fs/cgroup"
#ifndef STATE_BASE
#define STATE_BASE "/run/minicontainer"
#endif

#define NAME_CHARS "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-"

/* Common buffer sizes */
#define BUF_SIZE_SMALL 64
#define BUF_SIZE_MEDIUM 256
#define BUF_SIZE_LARGE 512
#define BUF_SIZE_XLARGE 1024
#define BUF_SIZE_XXLARGE 4096
#define BUF_SIZE_HUGE 65536
#define BUF_SIZE_JSON 8192

/* Common exit codes */
#define EXIT_CHILD_ERROR 127
#define EXIT_SETUP_ERROR 126
#define EXIT_SIGNAL_BASE 128

/* Network constants */
#define DEFAULT_NET_PREFIX 16
#define MC_BRIDGE "mcbr0"
#define MC_BR_CIDR "10.88.0.1/16"
#define MC_BR_GW "10.88.0.1"
#define MC_BR_PREFIX 16

/* Disk watchdog periods */
#define DISK_PERIOD_MS 1000
#define HEALTH_PERIOD_S 5

/* Bridge IP allocation */
#define BRIDGE_IP_MIN_OCTET 2
#define BRIDGE_IP_MAX_OCTET 254

/* CPU quota conversion */
#define CPU_QUOTA_PERIOD 100000

/* Publish array limits */
#define MAX_PUBLISH 16

/* UID/GID mapping */
#define UID_MAP_GID "0 65534 1"

/* cgroup v2 configuration structure */
struct cgroup_v2_cfg {
    long ram;
    int cpu;
    long pids;
    long io;
    long memory_high;   /* P3.5: soft limit (memory.high) */
    long cpu_weight;    /* P3.5: cpu.weight 1..10000 */
    const char *rootfs;
};

struct cfg {
    long ram;
    int cpu;
    long pids;
    long io;
    long max_disk;
    int detach;
    int read_only;
    const char *health_cmd;
    const char *name;
    const char *rootfs;
    const char *image;   /* P1: run from the image store instead of --rootfs */
    const char *net;
    const char *hostname;
    char **cmd;
    char **env;          /* P1.5: KEY=VALUE list */
    int nenv;
    const char *workdir; /* -w / process.cwd */
    const char *user;    /* --user UID[:GID] */
    char **mounts;       /* P1.5: type:source:dest[:opts] */
    int nmounts;
    int seccomp_mode;   /* 0=default deny-list, 1=strict allow-list, 2=off */
    char **publish;      /* "hostport:containerport", NULL-terminated */
    int npublish;
    /* P3 */
    int rm_after;        /* --rm: drop state when the container exits */
    int init_enabled;    /* --init: PID 1 reaper for apps that do not reap */
    int emulate;         /* --emulate: run a foreign-arch rootfs through qemu */
    const char *restart; /* --restart=no|on-failure|always */
    long memory_high;    /* --memory-high: soft limit (memory.high) */
    long cpu_weight;     /* --cpu-weight: 1..10000 (cpu.weight) */
    char **ulimits;      /* --ulimit name=soft[:hard] */
    int nulimits;
    char **orig_argv;    /* untouched argv, saved so `start` can replay it */
    int orig_argc;
    char emulate_arch[BUF_SIZE_SMALL]; /* filled by do_run when --emulate */
    const char *seccomp_profile; /* --seccomp=FILE.json (OCI seccomp profile) */
    int reuse_state;     /* start: reuse the existing state dir of a stopped container */
    char **base_cmd;     /* original argv when --emulate wraps it with qemu */
};

#define SECCOMP_MODE_PROFILE 3
#define SECCOMP_PROFILE_MAX_RULES 1400

struct child_arg {
    struct cfg *c;
    int go_rd;
    int ready_wr;
    int detach;
    int out_w;          /* detached: write end of stdout pipe, else -1 */
    int err_w;          /* detached: write end of stderr pipe, else -1 */
    char cgdir[PATH_MAX];
};

/* --net=bridge: parent tao veth + move vao netns, child tu cau hinh ioctl. */
struct bridge_cfg {
    char peer[16];
    char ip[16];
    char gw[16];
    int prefix;
};

void die(const char *msg);
void die_msg(const char *msg);
long parse_bytes(const char *s);
int try_write_file(const char *path, const char *val);
void write_file(const char *path, const char *val);
int read_small(const char *path, char *buf, size_t n);
/* Read ELF e_machine of a file into out ("aarch64", "x86_64", ...).
 * Returns 0 on success, -1 if missing/not ELF/unknown arch. */
int elf_arch(const char *path, char *out, size_t n);
int do_check(int json);
void install_filter (void);
void install_filter_strict(void);

int child_fn(void *arg);
void userns_map(pid_t child);

/* cgroup v2 API */
int cgroup_v2_create(const struct cgroup_v2_cfg *cfg, char *cgdir, size_t cgdir_len);
int cgroup_v2_set_limit(const char *cgdir, const char *controller, const char *value);
int cgroup_v2_attach(const char *cgdir, pid_t pid);
void cgroup_v2_kill(const char *cgdir);
void cgroup_v2_cleanup(const struct cgroup_v2_cfg *cfg, const char *cgdir);
int cgroup_v2_empty(const char *cgdir);

/* Legacy cgroup wrappers (call v2 API) */
void cgroup_setup(struct cfg *c, char *cgdir);
void cgroup_attach(const char *cgdir, pid_t pid);
void cgroup_cleanup(struct cfg *c, const char *cgdir);
void cgroup_kill(const char *cgdir);
int cgroup_empty(const char *cgdir);

int do_run(struct cfg *c);
int do_pull(int argc, char **argv);
int do_sdk(int argc, char **argv);

int do_stats(const char *id, int json);
int do_ps(int json);
int do_stop(const char *id, int timeout);
int do_rm(const char *id, int force);
int do_gc(int dry);
int do_inspect(const char *id, int json);
int do_start(const char *id);
int do_cp(const char *src, const char *dst);

/* P4: A/B rootfs update + rollback + delta, watchdog, metrics, compose-lite. */
int do_update_cli(int argc, char **argv);
int do_watchdog(int argc, char **argv);
int do_metrics(int argc, char **argv);
int do_compose(int argc, char **argv);
void journal_event(const char *fmt, ...);
int state_save_run_config(const char *dir, const struct cfg *c);
int state_open_pid(const char *id, pid_t *pid, char *dir, size_t dirn);

/* json.c — jsmn helpers (jsmntok_t is `struct jsmntok`, see jsmn.h). */
struct jsmntok;
int json_parse_file(const char *path, char **js, struct jsmntok **toks, int *ntok);
void json_free(char *js, struct jsmntok *toks);
int json_tok_eq(const char *js, const struct jsmntok *t, const char *s);
void json_tok_copy(const char *js, const struct jsmntok *t, char *out, size_t n);
int json_tok_skip(const struct jsmntok *t, int i);
int json_obj_get(const char *js, const struct jsmntok *t, int obj, const char *key);
int json_arr_nth(const struct jsmntok *t, int arr, int n);

/* P0.6: verify <pkg>.sig (minisign format) against a public key file. */
int verify_signature(const char *pkg, const char *keypath);

/* P1: OCI image store (load / images / rmi / run --image). */
const char *img_base(void);
int img_load(const char *file, const char *tag_override);
int img_install(const char *stage, const char *tag_override);
int do_init(int argc, char **argv);
int do_build(int argc, char **argv);
int do_export(const char *image, const char *out);
int do_export_multi(int argc, char **argv);
int do_import(const char *bundle, const char *key);
int img_rootfs(const char *name, char *out, size_t n);
int do_images(int json);
int do_rmi(const char *name);
int img_save(const char *name, const char *out);
char **img_argv(const char *name, int *argc_out);
int img_apply_env(const char *name);
int layer_apply(const char *blob, const char *rootfs);

/* P2: distribution-spec registry client (pull <image> from a registry). */
int do_pull_registry(const char *ref, const char *platform);
int do_exec(const char *id, char **cmd);
int state_relationship_valid(const char *cgdir, int pid);

/* P0.2: json-file log driver (1 object/line, rotate 2 x 1 MB). */
const char *log_dir_for(const char *statedir, char *out, size_t n);
int log_logger(int ofd, int efd, const char *dir);
int do_logs(const char *id, int follow, int tail);
void log_purge(const char *statedir);

int oci_to_argv(const char *bundle, char **av, int maxav);
void parse_args(int argc, char **argv, struct cfg *c);
void print_usage(const char *prog);

int valid_name(const char *s);
int run_wait(char *const av[]);
int run_capture(char *const av[], char *out, size_t n);
/* snprintf into a path buffer; -1 when truncated. */
int fmt_path(char *out, size_t n, const char *fmt, ...);
/* Stream `tar -tf`/`-tvf` output through a callback (aborts on <0). */
int tar_stream(const char *file, int verbose, int (*fn)(const char *, void *), void *ctx);
void validate_input(struct cfg *c);
void kill_pids_in_cgroup(const char *cgdir, int sig);

int do_service_create(int argc, char **argv);
int do_service_gc(void);

/* R2-1: userspace disk quota watchdog (kernel has no project quota).
 * disk_usage() sums allocated blocks (du semantics) under path.
 * The detached watchdog polls the rootfs, runs health_cmd every 5s, and
 * kills the cgroup on overflow or failed health check. */
long long disk_usage(const char *path);
void container_watchdog_detached(long limit, const char *rootfs, const char *cgdir,
                                 const char *statedir, long long baseline,
                                 const char *health_cmd);
int run_health_cmd(const char *health_cmd);

/* R5-3: --net=bridge (mcbr0 + veth, khong daemon, khong NAT). */
int bridge_setup(pid_t child, struct bridge_cfg *bc, char **publish, int npublish);
void bridge_teardown(pid_t child, char **publish, int npublish);
int bridge_configure(const struct bridge_cfg *bc);
int netns_lo_up(void);
int netns_allow_low_ports(void);

/* Bridge config serialization over pipe (ABI-safe) */
int bridge_cfg_write(int fd, const struct bridge_cfg *bc);
int bridge_cfg_read(int fd, struct bridge_cfg *bc);

/* namespace_setup functions */
void namespace_setup_rootfs(const char *rootfs, int read_only, const char *hostname);
void namespace_setup_dev(const char *rootfs);
void namespace_setup_ro(const char *rootfs);
void namespace_chroot(const char *rootfs);
void namespace_mount_proc(void);
void namespace_apply_mounts(struct cfg *c);
void namespace_setup_identity(struct cfg *c);
int namespace_remount_ro(const char *rootfs);
void namespace_install_seccomp(int seccomp_mode, const char *profile);
void namespace_child_setup(struct cfg *c, int detach);
void namespace_setup_hosts(const char *net);
void namespace_setup_emulator(const struct cfg *c);
int namespace_prepare_filesystem(struct cfg *c);
int seccomp_install_profile(const char *path);
