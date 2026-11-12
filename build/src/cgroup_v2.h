/* cgroup_v2.h — cgroup v2 abstraction (API v2 only, no v1) */
#pragma once
#include "common.h"

/* Create cgroup v2 directory, enable controllers, apply limits.
 * Returns 0 on success, -1 on error (errno set). */
int cgroup_v2_create(const struct cgroup_v2_cfg *cfg, char *cgdir, size_t cgdir_len);

/* Set a single controller limit (memory.max, cpu.max, pids.max, io.max).
 * controller: "memory", "cpu", "pids", "io"
 * value: string value (e.g., "1000000", "20000 100000", "64", "8:0 rbps=1048576 wbps=1048576") */
int cgroup_v2_set_limit(const char *cgdir, const char *controller, const char *value);

/* Attach a PID to the cgroup (write to cgroup.procs). */
int cgroup_v2_attach(const char *cgdir, pid_t pid);

/* Kill all processes in cgroup (cgroup.kill if available, fallback to SIGKILL). */
void cgroup_v2_kill(const char *cgdir);

/* Cleanup: remove .oldroot and cgroup directory (with retry). */
void cgroup_v2_cleanup(const struct cgroup_v2_cfg *cfg, const char *cgdir);

/* Check if cgroup is empty (no PIDs). Returns 1 if empty, 0 if not, -1 on error. */
int cgroup_v2_empty(const char *cgdir);

/* Legacy wrappers for backward compatibility (call the new functions). */
void cgroup_setup(struct cfg *c, char *cgdir);
void cgroup_attach(const char *cgdir, pid_t pid);
void cgroup_cleanup(struct cfg *c, const char *cgdir);
void cgroup_kill(const char *cgdir);
int cgroup_empty(const char *cgdir);