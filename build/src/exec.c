/* exec.c — chay them 1 lenh trong container dang chay.
 *
 * Dung setns() vao user/mnt/uts/ipc/net/pid cua PID 1, roi fork (PID ns chi
 * ap dung cho con), chroot ve root cua container, cai seccomp va exec.
 * Khong can nsenter; chi can quyen root tren host.
 */
#include "common.h"

struct nsfd {
	const char *name;
	int fd;
};

static int open_ns(pid_t pid, const char *name) {
	char p[64];
	snprintf(p, sizeof p, "/proc/%d/ns/%s", pid, name);
	return open(p, O_RDONLY | O_CLOEXEC);
}

/* Container dung --net=host (hoac ns khac trung host): setns vao ns cua
 * host sau khi da vao userns se EPERM, va cung khong can thiet. */
static int same_ns(pid_t pid, const char *name) {
	char a[64], b[64];
	struct stat sa, sb;
	snprintf(a, sizeof a, "/proc/self/ns/%s", name);
	snprintf(b, sizeof b, "/proc/%d/ns/%s", pid, name);
	if (stat(a, &sa) < 0 || stat(b, &sb) < 0) return 0;
	return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

int do_exec(const char *id, char **cmd) {
	char dir[256], f[PATH_MAX + 16], buf[64];
	char cg[PATH_MAX] = "";
	snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
	snprintf(f, sizeof f, "%s/pid", dir);
	if (read_small(f, buf, sizeof buf) < 0) {
		fprintf(stderr, "khong co container '%s'\n", id);
		return 1;
	}
	pid_t pid = atoi(buf);
	snprintf(f, sizeof f, "/proc/%d/ns/pid", pid);
	if (access(f, F_OK) != 0) {
		fprintf(stderr, "container '%s' khong chay (pid %d)\n", id, pid);
		return 1;
	}
	snprintf(f, sizeof f, "%s/cgdir", dir);
	read_small(f, cg, sizeof cg);

	int rootfd;
	{
		char rp[64];
		snprintf(rp, sizeof rp, "/proc/%d/root", pid);
		rootfd = open(rp, O_PATH | O_DIRECTORY | O_CLOEXEC);
	}
	struct nsfd ns[] = {
		{ "user", -1 }, { "mnt", -1 }, { "uts", -1 },
		{ "ipc", -1 }, { "net", -1 }, { "pid", -1 },
	};
	int ok = rootfd >= 0;
	for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
		if (same_ns(pid, ns[i].name)) continue;
		ns[i].fd = open_ns(pid, ns[i].name);
		if (ns[i].fd < 0) ok = 0;
	}
	if (!ok) {
		fprintf(stderr, "khong mo duoc namespace cua '%s': %s\n", id, strerror(errno));
		if (rootfd >= 0) close(rootfd);
		for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++)
			if (ns[i].fd >= 0) close(ns[i].fd);
		return 1;
	}

	/* Mo truoc khi doi mnt ns: sau setns(mnt) duong dan host khong con. */
	int cgfd = -1;
	if (cg[0]) {
		snprintf(f, sizeof f, "%s/cgroup.procs", cg);
		cgfd = open(f, O_WRONLY | O_CLOEXEC);
	}

	/* user ns truoc (lay cap trong userns dich), roi cac ns con lai. */
	for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
		if (ns[i].fd < 0) continue;
		if (setns(ns[i].fd, 0) < 0) {
			fprintf(stderr, "setns(%s): %s\n", ns[i].name, strerror(errno));
			return 1;
		}
		close(ns[i].fd);
		/* Host uid 0 khong nam trong uid_map cua container (0 -> 65534),
		 * nen phai setuid(0) de thanh root trong userns dich va giu cap. */
		if (strcmp(ns[i].name, "user") == 0) {
			if (setresgid(0, 0, 0) < 0 || setresuid(0, 0, 0) < 0) {
				fprintf(stderr, "setuid trong userns: %s\n", strerror(errno));
				return 1;
			}
		}
	}

	pid_t p = fork();
	if (p < 0) die("fork exec");
	if (p == 0) {
		if (fchdir(rootfd) < 0) _exit(126);
		close(rootfd);
		if (chroot(".") < 0) _exit(126);
		if (chdir("/") < 0) _exit(126);
		if (cgfd >= 0) close(cgfd);
		install_filter();
		execvp(cmd[0], cmd);
		fprintf(stderr, "execvp %s: %s\n", cmd[0], strerror(errno));
		_exit(127);
	}

	close(rootfd);
	if (cgfd >= 0) {
		char v[32];
		int n = snprintf(v, sizeof v, "%d", p);
		if (write(cgfd, v, n) < 0) { /* best-effort: container resources */ }
		close(cgfd);
	}

	int status = 0;
	while (waitpid(p, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
