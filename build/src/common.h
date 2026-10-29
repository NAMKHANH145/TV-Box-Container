/* common.h — dinh nghia chung cho moi module minicontainer */
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

struct cfg {
	long ram;
	int cpu;
	long pids;
	long io;
	long max_disk;
	int detach;
	const char *health_cmd;
	const char *name;
	const char *rootfs;
	const char *net;
	const char *hostname;
	char **cmd;
};

struct child_arg {
	struct cfg *c;
	int go_rd;
	int ready_wr;
	int detach;
	char cgdir[PATH_MAX];
};

void die(const char *msg);
void die_msg(const char *msg);
long parse_bytes(const char *s);
int try_write_file(const char *path, const char *val);
void write_file(const char *path, const char *val);
int read_small(const char *path, char *buf, size_t n);
void install_filter (void);

int child_fn(void *arg);
void userns_map(pid_t child);

void cgroup_setup(struct cfg *c, char *cgdir);
void cgroup_attach(const char *cgdir, pid_t pid);
void cgroup_cleanup(struct cfg *c, const char *cgdir);
void cgroup_kill(const char *cgdir);
int cgroup_empty(const char *cgdir);

int do_run(struct cfg *c);

int do_stats(const char *id, int json);
int do_ps(int json);
int do_stop(const char *id, int timeout);
int do_rm(const char *id, int force);

int oci_to_argv(const char *bundle, char **av, int maxav);
void parse_args(int argc, char **argv, struct cfg *c);
void print_usage(const char *prog);

int valid_name(const char *s);
void validate_input(struct cfg *c);
void kill_pids_in_cgroup(const char *cgdir, int sig);

int do_service_create(int argc, char **argv);

#ifndef SYS_pivot_root
#define SYS_pivot_root 217
#endif

/* R2-1: userspace disk quota watchdog (kernel has no project quota).
 * disk_usage() sums allocated blocks (du semantics) under path.
 * The detached watchdog polls the rootfs, runs health_cmd every 5s, and
 * kills the cgroup on overflow or failed health check. */
long long disk_usage(const char *path);
void container_watchdog_detached(long limit, const char *rootfs, const char *cgdir,
                                 const char *statedir, long long baseline,
                                 const char *health_cmd);
