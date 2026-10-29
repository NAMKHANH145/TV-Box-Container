/* run.c — dieu phoi 1 lan chay: cgroup -> clone -> maps -> go -> detach/wait */
#include "common.h"

static volatile pid_t g_child_pid;
static volatile sig_atomic_t g_stop_sig;

/* Handler chi lam 2 viec async-signal-safe: ghi co + kill con.
 * fopen/rmdir/usleep/cgroup_cleanup KHONG duoc goi trong handler (khong
 * async-signal-safe, co the deadlock neu signal den giua malloc/stdio);
 * phan don dep chay sau waitpid trong ngu canh binh thuong.
 * SA_RESTART: waitpid duoc khoi phuc, nhung con da bi kill nen no tra ve. */
static void on_stop(int sig) {
	g_stop_sig = sig;
	if (g_child_pid > 0) kill(g_child_pid, SIGKILL);
}

static void setup_signal_handlers(void) {
	struct sigaction sa = { .sa_handler = on_stop, .sa_flags = SA_RESTART };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

/* Tick 1s: danh thuc waitpid de sample disk/health ma khong polling. */
static volatile sig_atomic_t g_tick;
static void on_tick(int sig) { (void)sig; g_tick = 1; }

static void setup_tick(void) {
	struct sigaction sa = { .sa_handler = on_tick };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
}

int do_run(struct cfg *c) {
	char cgdir[PATH_MAX];
	cgroup_setup(c, cgdir);

	/* R2-1: baseline truoc khi container ghi; quota = phan tang them. */
	long disk_limit = 0;
	long long disk_base = 0;
	if (c->max_disk > 0) {
		disk_base = disk_usage(c->rootfs);
		if (disk_base < 0)
			fprintf(stderr, "minicontainer: warning: khong do duoc '%s', tat watchdog dung luong (RLIMIT_FSIZE van bat)\n", c->rootfs);
		else
			disk_limit = c->max_disk;
	}

	/* 2 ong dong bo: ready (con->cha: da vao userns) + go (cha->con: xong het) */
	int readyfd[2], gofd[2];
	if (pipe2(readyfd, O_CLOEXEC) < 0 || pipe2(gofd, O_CLOEXEC) < 0) die("pipe");
	char *stack = malloc(STACK_SIZE);
	if (!stack) die("malloc stack");

	int flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWUSER;
	if (strcmp(c->net, "isolated") == 0 || strcmp(c->net, "bridge") == 0)
		flags |= CLONE_NEWNET;

	struct child_arg carg = { .c = c, .go_rd = gofd[0], .ready_wr = readyfd[1],
	                         .detach = c->detach };
	memcpy(carg.cgdir, cgdir, sizeof cgdir);
	pid_t child = clone(child_fn, stack + STACK_SIZE, flags, &carg);
	if (child < 0) die("clone");
	close(gofd[0]); close(readyfd[1]); /* cha: doc ready, viet go */

	/* Thiet lap signal handler de forward signal cho child */
	g_child_pid = child;
	setup_signal_handlers();
	setup_tick();

	/* Doi con vao userns xong */
	{
		char b;
		if (read(readyfd[0], &b, 1) != 1) die("ready read");
		close(readyfd[0]);
	}

	userns_map(child);
	cgroup_attach(cgdir, child);

	/* --net=bridge: tao veth, move vao netns con, roi moi "go" de con tu
	 * cau hinh interface (con can netns da co san veth cua no). */
	struct bridge_cfg bc;
	int have_bc = 0;
	if (strcmp(c->net, "bridge") == 0) {
		if (bridge_setup(child, &bc) < 0) {
			kill(child, SIGKILL);
			waitpid(child, NULL, 0);
			cgroup_cleanup(c, cgdir);
			die_msg("--net=bridge that bai (can iproute2 + quyen root + module veth/bridge)");
		}
		have_bc = 1;
	}

	/* Bao con chay tiep */
	if (write(gofd[1], "x", 1) != 1) die("go write");
	if (have_bc && write(gofd[1], &bc, sizeof bc) != (ssize_t)sizeof bc) die("go bridge write");
	close(gofd[1]);

	/* Detach: khong waitpid, khong don cgroup — ghi state roi thoat.
	 * Con mo coi ve init, song trong cgroup/ns cua no. Khong daemon. */
	if (c->detach) {
		char id[64], dir[256], f[512], v[64];
		mkdir(STATE_BASE, 0755);
		if (c->name) snprintf(id, sizeof id, "%s", c->name);
		else snprintf(id, sizeof id, "mc-%d", child);
		snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
		for (int n = 1; mkdir(dir, 0755) < 0; n++) {
			if (errno != EEXIST) die("mkdir state");
			if (c->name) snprintf(id, sizeof id, "%s-%d", c->name, n);
			else snprintf(id, sizeof id, "mc-%d-%d", child, n);
			snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
		}
		snprintf(f, sizeof f, "%s/pid", dir); snprintf(v, sizeof v, "%d", child);
		write_file(f, v);
		snprintf(f, sizeof f, "%s/cgdir", dir);
		write_file(f, cgdir);
		/* Luu cmd de ps hien thi (noi bang space) */
		{
			char cb[512]; size_t o = 0;
			for (char **a = c->cmd; *a && o < sizeof cb - 1; a++) {
				if (o) cb[o++] = ' ';
				size_t l = strlen(*a);
				if (o + l > sizeof cb - 1) l = sizeof cb - 1 - o;
				memcpy(cb + o, *a, l); o += l;
			}
			cb[o] = 0;
			snprintf(f, sizeof f, "%s/cmd", dir);
			write_file(f, cb);
		}
		snprintf(f, sizeof f, "%s/rootfs", dir);
		write_file(f, c->rootfs);
		if (have_bc) {
			snprintf(f, sizeof f, "%s/ip", dir);
			write_file(f, bc.ip);
		}

		/* Watchdog detached: 1 process/container, tu thoat khi container chet.
		 * Chi ton tai khi dung --disk hoac --health-cmd (khong co daemon thuong truc). */
		if (disk_limit > 0 || c->health_cmd) {
			pid_t wd = fork();
			if (wd == 0) {
				setsid();
				int nfd = open("/dev/null", O_RDWR);
				if (nfd >= 0) {
					dup2(nfd, 0); dup2(nfd, 1); dup2(nfd, 2);
					if (nfd > 2) close(nfd);
				}
				container_watchdog_detached(disk_limit, c->rootfs, cgdir, dir,
				                            disk_base, c->health_cmd);
				_exit(0);
			}
			if (wd > 0) {
				snprintf(f, sizeof f, "%s/watchdog", dir);
				snprintf(v, sizeof v, "%d", wd);
				write_file(f, v);
			} else {
				fprintf(stderr, "minicontainer: warning: khong tao duoc watchdog: %s\n", strerror(errno));
			}
		}
		printf("%s\n", id);
		free(stack);
		return 0;
	}

	int status = 0;
	int disk_killed = 0;
	const int HEALTH_PERIOD = 5;
	time_t next_health = time(NULL) + HEALTH_PERIOD;
	time_t next_disk = time(NULL) + 1;

	/* waitpid blocking + alarm(1): thoat ngay khi con chet, van sample
	 * disk/health moi giay (khong polling nong). */
	for (;;) {
		g_tick = 0;
		alarm(1);
		pid_t w = waitpid(child, &status, 0);
		alarm(0);
		if (w == child) break;
		if (w < 0) {
			if (errno == ECHILD) break; /* system() co the da reap */
			if (errno != EINTR) die("waitpid");
		}

		time_t now = time(NULL);

		if (disk_limit > 0 && now >= next_disk) {
			long long used = disk_usage(c->rootfs);
			if (used >= 0 && used - disk_base > disk_limit) {
				fprintf(stderr, "minicontainer: disk quota exceeded: %lld bytes > %ld\n",
				        used - disk_base, disk_limit);
				cgroup_kill(cgdir);
				kill(child, SIGKILL);
				waitpid(child, &status, 0);
				disk_killed = 1;
				break;
			}
			next_disk = now + 1;
		}

		if (c->health_cmd && now >= next_health) {
			int hr = system(c->health_cmd);
			if (hr != 0) {
				fprintf(stderr, "minicontainer: health check failed: %s\n", c->health_cmd);
				kill(child, SIGTERM);
				usleep(2000000);
				cgroup_kill(cgdir);
				waitpid(child, &status, 0);
				break;
			}
			next_health = now + HEALTH_PERIOD;
		}
		usleep(1000); /* tranh vong lap nong neu tick bi tre */
	}

	/* Ctrl-C / SIGTERM: handler da kill con; don cac tien trinh con chau
	 * con lai trong cgroup roi moi xoa. */
	if (g_stop_sig) cgroup_kill(cgdir);
	cgroup_cleanup(c, cgdir);
	if (have_bc) bridge_teardown(child);
	free(stack);
	if (g_stop_sig) return 128 + g_stop_sig;
	if (disk_killed) return 128 + SIGKILL;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
