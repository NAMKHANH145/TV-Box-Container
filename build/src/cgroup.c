/* cgroup.c — cgroup v2: tao, gan, don (API v2, khong v1) */
#include "common.h"
#include <sys/sysmacros.h>

void cgroup_setup(struct cfg *c, char *cgdir) {
	static int n = 0;
	/* Bat controller o cay cha truoc (tu dong, khoi nho bat tay).
	 * Ghi lai khi da bat la no-op, an toan. */
	write_file(CGROUP_BASE "/cgroup.subtree_control", "+cpu +memory +pids +io");

	/* Tao cgroup v2 truoc (can quyen root) */
	snprintf(cgdir, PATH_MAX, CGROUP_BASE "/minicontainer-%d-%d", getpid(), n++);
	if (mkdir(cgdir, 0755) < 0) die("mkdir cgroup");
	if (c->ram > 0) {
		char v[32]; snprintf(v, sizeof v, "%ld", c->ram);
		char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/memory.max", cgdir);
		write_file(p, v);
	}
	if (c->cpu > 0) {
		char v[32]; snprintf(v, sizeof v, "%d 100000", c->cpu * 1000);
		char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/cpu.max", cgdir);
		write_file(p, v);
	}
	if (c->pids > 0) {
		char v[32]; snprintf(v, sizeof v, "%ld", c->pids);
		char p[PATH_MAX + 16]; snprintf(p, sizeof p, "%s/pids.max", cgdir);
		write_file(p, v);
	}
	if (c->io > 0) {
		/* blk-throttle chi nhan whole-disk (vd 179:0), khong nhan partition.
		 * Thu device cua rootfs truoc, ENODEV thi roi ve minor=0. */
		struct stat st;
		if (stat(c->rootfs, &st) < 0) die("stat rootfs");
		char v[64], p[PATH_MAX + 16];
		snprintf(p, sizeof p, "%s/io.max", cgdir);
		snprintf(v, sizeof v, "%u:%u rbps=%ld wbps=%ld",
			major(st.st_dev), minor(st.st_dev), c->io, c->io);
		if (try_write_file(p, v) < 0) {
			if (errno != ENODEV) die("io.max");
			snprintf(v, sizeof v, "%u:0 rbps=%ld wbps=%ld",
				major(st.st_dev), c->io, c->io);
			write_file(p, v);
		}
	}

	/* Tao .oldroot truoc (parent la host-root; con trong userns
	 * la nobody ngoai host nen khong mkdir duoc tren dir root). */
	{
		char o[PATH_MAX + 16];
		snprintf(o, sizeof o, "%s/.oldroot", c->rootfs);
		if (mkdir(o, 0700) < 0 && errno != EEXIST) die("mkdir .oldroot");
	}
}

/* Gui signal cho moi tien trinh trong cgroup (doc cgroup.procs) */
void kill_pids_in_cgroup(const char *cgdir, int sig) {
	char p[PATH_MAX + 16];
	snprintf(p, sizeof p, "%s/cgroup.procs", cgdir);
	FILE *fp = fopen(p, "r");
	if (!fp) return;
	char lb[64];
	while (fgets(lb, sizeof lb, fp)) {
		pid_t pid = atoi(lb);
		if (pid > 0) kill(pid, sig);
	}
	fclose(fp);
}

/* cgroup.kill (kernel >= 5.14) giet ca cgroup trong 1 lan ghi;
 * kernel cu hon thi quet cgroup.procs. */
void cgroup_kill(const char *cgdir) {
	char p[PATH_MAX + 16];
	snprintf(p, sizeof p, "%s/cgroup.kill", cgdir);
	if (try_write_file(p, "1") == 0) return;
	kill_pids_in_cgroup(cgdir, SIGKILL);
}

/* cgroup rong (khong con pid) hoac khong doc duoc cgroup.procs */
int cgroup_empty(const char *cgdir) {
	char p[PATH_MAX + 16], buf[64];
	snprintf(p, sizeof p, "%s/cgroup.procs", cgdir);
	if (read_small(p, buf, sizeof buf) < 0) return 1;
	return buf[0] == 0;
}

/* Gan host-pid vao cgroup (clone() tra ve dung host PID) */
void cgroup_attach(const char *cgdir, pid_t pid) {
	char p[PATH_MAX + 16], v[32];
	snprintf(p, sizeof p, "%s/cgroup.procs", cgdir);
	snprintf(v, sizeof v, "%d", pid);
	write_file(p, v);
}

void cgroup_cleanup(struct cfg *c, const char *cgdir) {
	/* Don dep: mount /proc tu chet theo MNT ns; xoa .oldroot tren eMMC + cgroup */
	char o[PATH_MAX + 16];
	snprintf(o, sizeof o, "%s/.oldroot", c->rootfs);
	rmdir(o);
	/* cgroup.kill la bat dong bo: doi kernel giai phong pid roi moi rmdir */
	for (int i = 0; i < 20; i++) {
		if (rmdir(cgdir) == 0 || errno == ENOENT) break;
		usleep(100000);
	}
}
