/* ns.c — namespaces: clone child, chroot, user namespace map */
#include "common.h"

int child_fn(void *arg) {
	struct child_arg *a = arg;
	struct cfg *c = a->c;
	char byte;

	if (write(a->ready_wr, "r", 1) != 1) die("ready");
	close(a->ready_wr);
	if (read(a->go_rd, &byte, 1) != 1) die("sync");
	close(a->go_rd);

	if (setgid(0) < 0) die("setgid");
	if (setuid(0) < 0) die("setuid");

	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) die("make-private /");

	if (sethostname(c->hostname, strlen(c->hostname)) < 0) die("sethostname");

	/* chroot isolation (userns-only: root in ns maps to host-nobody) */
	{
		if (chroot(c->rootfs) < 0) die("chroot");
		if (chdir("/") < 0) die("chdir /");
		/* mount proc AFTER chroot */
		if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
			if (errno != EBUSY) die("mount /proc");
		}
	}

	/* Detach: cat stdio ve /dev/null TRUOC exec */
	if (a->detach) {
		int fd = open("/dev/null", O_RDWR);
		if (fd < 0) die("open /dev/null");
		dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
		if (fd > 2) close(fd);
	}

	/* RLIMIT_FSIZE hard cap -- best-effort disk quota */
	if (c->max_disk > 0) {
		struct rlimit rl = { .rlim_cur = (rlim_t)c->max_disk, .rlim_max = (rlim_t)c->max_disk };
		setrlimit(RLIMIT_FSIZE, &rl);
	}

	/* Khoa syscall nguy hiem SAU setup, TRUOC execvp */
	install_filter();

	execvp(c->cmd[0], c->cmd);
	die("execvp");
	return 1;
}

void userns_map(pid_t child) {
	char p[64], v[32];
	snprintf(p, sizeof p, "/proc/%d/setgroups", child);
	write_file(p, "deny");
	snprintf(p, sizeof p, "/proc/%d/uid_map", child);
	snprintf(v, sizeof v, "0 65534 1");
	write_file(p, v);
	snprintf(p, sizeof p, "/proc/%d/gid_map", child);
	write_file(p, v);
}