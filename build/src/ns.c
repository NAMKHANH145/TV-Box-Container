/* ns.c — namespaces: clone child, chroot, user namespace map */
#include "common.h"
#include <sys/sysmacros.h>

static int path_join(char *out, size_t n, const char *dir, const char *name) {
	size_t dl = strlen(dir), nl = strlen(name);
	if (dl + 1 + nl + 1 > n) return -1;
	memcpy(out, dir, dl);
	out[dl] = '/';
	memcpy(out + dl + 1, name, nl + 1);
	return 0;
}

static int dev_node(const char *dp, const char *hp, mode_t mode, int ma, int mi) {
	if (mknod(dp, S_IFCHR | mode, makedev(ma, mi)) == 0) return 0;
	int e = errno;
	if (e == EEXIST) {
		unlink(dp);
		if (mknod(dp, S_IFCHR | mode, makedev(ma, mi)) == 0) return 0;
		e = errno;
	}
	/* Mot so kernel tu choi mknod trong userns (CAP_MKNOD khong du): bind node
	 * cua host vao tmpfs /dev. Bind can file dich ton tai nen tao cot truoc. */
	int fd = open(hp, O_RDONLY);
	if (fd < 0) return e;
	close(fd);
	int tfd = open(dp, O_WRONLY | O_CREAT | O_NOFOLLOW, 0666);
	if (tfd >= 0) close(tfd);
	if (mount(hp, dp, NULL, MS_BIND, NULL) == 0) return 0;
	return e;
}

static void dev_link(const char *devdir, const char *tgt, const char *name) {
	char p[PATH_MAX + 64];
	if (path_join(p, sizeof p, devdir, name) < 0) return;
	unlink(p);
	symlink(tgt, p);
}

/* Rootfs Buildroot chi co /dev khi duoc tao bang root; profile dynamic o day
 * rong -> tao tmpfs /dev + cac node can thiet (null/urandom/...). Goi TRUOC
 * chroot, dung duong dan host cua rootfs (mount ns da MS_PRIVATE nen cac mount
 * nay khong thoat ra host). */
static void dev_setup(const char *rootfs) {
	char d[PATH_MAX + 64], t[PATH_MAX + 64], h[32];
	int enull = 0;
	if (strlen(rootfs) > PATH_MAX) return;
	if (path_join(d, sizeof d, rootfs, "dev") < 0) return;
	if (mkdir(d, 0755) < 0 && errno != EEXIST) return;
	if (mount("tmpfs", d, "tmpfs", MS_NOSUID | MS_STRICTATIME,
		"mode=755,size=1M") < 0 && errno != EBUSY)
		return;
#define NODE(name, mode, ma, mi) \
	do { if (path_join(t, sizeof t, d, name) == 0) { \
		     snprintf(h, sizeof h, "/dev/%s", name); \
		     int e = dev_node(t, h, mode, ma, mi); \
		     if (e && !strcmp(name, "null")) enull = e; } } while (0)
	NODE("null", 0666, 1, 3);
	NODE("zero", 0666, 1, 5);
	NODE("random", 0666, 1, 8);
	NODE("urandom", 0666, 1, 9);
	NODE("tty", 0666, 5, 0);
	NODE("console", 0600, 5, 1);
#undef NODE
	path_join(t, sizeof t, d, "shm"); mkdir(t, 0777);
	path_join(t, sizeof t, d, "pts"); mkdir(t, 0755);
	dev_link(d, "/proc/self/fd", "fd");
	dev_link(d, "/proc/self/fd/0", "stdin");
	dev_link(d, "/proc/self/fd/1", "stdout");
	dev_link(d, "/proc/self/fd/2", "stderr");
	if (enull) fprintf(stderr, "minicontainer: khong tao duoc /dev/null trong container: %s\n",
		strerror(enull));
}

int child_fn(void *arg) {
	struct child_arg *a = arg;
	struct cfg *c = a->c;
	char byte;

	if (write(a->ready_wr, "r", 1) != 1) die("ready");
	close(a->ready_wr);
	if (read(a->go_rd, &byte, 1) != 1) die("sync");

	/* --net=bridge: cha da move veth vao netns nay; con tu doi ten + gan IP
	 * bang ioctl (rootfs khong can iproute2). */
	if (strcmp(c->net, "bridge") == 0) {
		struct bridge_cfg bc;
		if (read(a->go_rd, &bc, sizeof bc) != (ssize_t)sizeof bc) die("sync bridge");
		if (bridge_configure(&bc) < 0) die("bridge configure");
	}
	close(a->go_rd);

	if (setgid(0) < 0) die("setgid");
	if (setuid(0) < 0) die("setuid");

	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) die("make-private /");

	if (sethostname(c->hostname, strlen(c->hostname)) < 0) die("sethostname");

	dev_setup(c->rootfs);

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