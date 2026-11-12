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
    const char *net;
    const char *hostname;
    char **cmd;
    int seccomp_mode;   /* 0=default deny-list, 1=strict allow-list, 2=off */
    char **publish;      /* "hostport:containerport", NULL-terminated */
    int npublish;
};

struct child_arg {
    struct cfg *c;
    int go_rd;
    int ready_wr;
    int detach;
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
int do_exec(const char *id, char **cmd);
int state_relationship_valid(const char *cgdir, int pid);

int oci_to_argv(const char *bundle, char **av, int maxav);
void parse_args(int argc, char **argv, struct cfg *c);
void print_usage(const char *prog);

int valid_name(const char *s);
int run_wait(char *const av[]);
int run_capture(char *const av[], char *out, size_t n);
void validate_input(struct cfg *c);
void kill_pids_in_cgroup(const char *cgdir, int sig);

int do_service_create(int argc, char **argv);

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

/* Bridge config serialization over pipe (ABI-safe) */
int bridge_cfg_write(int fd, const struct bridge_cfg *bc);
int bridge_cfg_read(int fd, struct bridge_cfg *bc);

/* namespace_setup functions */
void namespace_setup_rootfs(const char *rootfs, int read_only, const char *hostname);
void namespace_setup_dev(const char *rootfs);
void namespace_setup_ro(const char *rootfs);
void namespace_chroot(const char *rootfs);
void namespace_mount_proc(void);
int namespace_remount_ro(const char *rootfs);
void namespace_install_seccomp(int seccomp_mode);
void namespace_child_setup(struct cfg *c, int detach);
