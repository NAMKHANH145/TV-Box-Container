/* state.c — ps/stats/stop/rm: quan ly qua state dir + cgroupfs, khong daemon */
#include "common.h"

/* Simple JSON output helpers */
static void json_print_kv_str(const char *key, const char *val, int *first) {
	if (!*first) printf(", ");
	*first = 0;
	printf("\"%s\": \"", key);
	for (const char *p = val; *p; p++) {
		switch (*p) {
			case '"': printf("\\\""); break;
			case '\\': printf("\\\\"); break;
			case '\n': printf("\\n"); break;
			case '\r': printf("\\r"); break;
			case '\t': printf("\\t"); break;
			default: putchar(*p);
		}
	}
	printf("\"");
}

static void json_print_kv_int(const char *key, long val, int *first) {
	if (!*first) printf(", ");
	*first = 0;
	printf("\"%s\": %ld", key, val);
}

/* ministat <id> */
int do_stats(const char *id, int json) {
	char dir[256], f[PATH_MAX + 16], buf[256], cg[PATH_MAX];
	snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
	snprintf(f, sizeof f, "%s/pid", dir);
	if (read_small(f, buf, sizeof buf) < 0) {
		fprintf(stderr, "khong co container '%s'\n", id);
		return 1;
	}
	int pid = atoi(buf);
	snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
	int alive = (access(f, F_OK) == 0);
	
	if (json) {
		int first = 1;
		printf("{");
		json_print_kv_str("id", id, &first);
		json_print_kv_str("status", alive ? "RUNNING" : "STOPPED", &first);
		json_print_kv_int("pid", pid, &first);
		
		snprintf(f, sizeof f, "%s/cgdir", dir);
		if (read_small(f, cg, sizeof cg) == 0) {
			snprintf(f, sizeof f, "%s/memory.current", cg);
			if (read_small(f, buf, sizeof buf) == 0)
				json_print_kv_str("memory", buf, &first);
			snprintf(f, sizeof f, "%s/pids.current", cg);
			if (read_small(f, buf, sizeof buf) == 0)
				json_print_kv_str("pids", buf, &first);
			snprintf(f, sizeof f, "%s/cpu.stat", cg);
			int fd = open(f, O_RDONLY);
			if (fd >= 0) {
				ssize_t r = read(fd, buf, sizeof buf - 1);
				close(fd);
				if (r > 0) {
					buf[r] = 0;
					char *u = strstr(buf, "usage_usec");
					char num[64];
					if (u && sscanf(u, "usage_usec %63s", num) == 1)
						json_print_kv_str("cpu", num, &first);
				}
			}
		}
		printf("}\n");
		return 0;
	}
	
	printf("id:      %s\nstatus:  %s\npid:     %d\n", id, alive ? "RUNNING" : "STOPPED", pid);
	snprintf(f, sizeof f, "%s/cgdir", dir);
	if (read_small(f, cg, sizeof cg) < 0) return 0;
	snprintf(f, sizeof f, "%s/memory.current", cg);
	if (read_small(f, buf, sizeof buf) == 0) printf("memory:  %s bytes\n", buf);
	snprintf(f, sizeof f, "%s/pids.current", cg);
	if (read_small(f, buf, sizeof buf) == 0) printf("pids:    %s\n", buf);
	snprintf(f, sizeof f, "%s/cpu.stat", cg);
	int fd = open(f, O_RDONLY);
	if (fd >= 0) {
		ssize_t r = read(fd, buf, sizeof buf - 1);
		close(fd);
		if (r > 0) {
			buf[r] = 0;
			char *u = strstr(buf, "usage_usec");
			char num[64];
			if (u && sscanf(u, "usage_usec %63s", num) == 1)
				printf("cpu:     %s usec\n", num);
		}
	}
	return 0;
}

/* ps */
int do_ps(int json) {
	DIR *d = opendir(STATE_BASE);
	if (!d) { 
		if (json) {
			printf("[]\n");
		} else {
			printf("khong co container nao\n");
		}
		return 0; 
	}
	
	if (json) printf("[");
	struct dirent *e;
	int n = 0;
	int first = 1;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char dir[512], f[PATH_MAX + 16], buf[256], cmd[128] = "-";
		snprintf(dir, sizeof dir, STATE_BASE "/%s", e->d_name);
		snprintf(f, sizeof f, "%s/pid", dir);
		if (read_small(f, buf, sizeof buf) < 0) continue;
		int pid = atoi(buf);
		snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
		const char *st = (access(f, F_OK) == 0) ? "RUNNING" : "STOPPED";
		snprintf(f, sizeof f, "%s/cmd", dir);
		if (read_small(f, cmd, sizeof cmd) == 0 && strlen(cmd) > 40) {
			cmd[37] = '.'; cmd[38] = '.'; cmd[39] = '.'; cmd[40] = 0;
		}
		if (json) {
			if (!first) printf(",");
			first = 0;
			printf("\n  {\n");
			printf("    \"id\": \"%s\",\n", e->d_name);
			printf("    \"status\": \"%s\",\n", st);
			printf("    \"pid\": %d,\n", pid);
			char cmd_esc[512]; size_t esc_j = 0;
			for (const char *p = cmd; *p && esc_j + 2 < sizeof(cmd_esc) - 1; p++) {
				switch (*p) {
					case '"': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = '"'; break;
					case '\\': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = '\\'; break;
					case '\n': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 'n'; break;
					case '\r': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 'r'; break;
					case '\t': cmd_esc[esc_j++] = '\\'; cmd_esc[esc_j++] = 't'; break;
					default: cmd_esc[esc_j++] = *p;
				}
			}
			cmd_esc[esc_j] = '\0';
			printf("    \"command\": \"%s\"\n", cmd_esc);
			printf("  }");
		} else {
			if (strlen(cmd) > 40) {
				cmd[37] = '.'; cmd[38] = '.'; cmd[39] = '.'; cmd[40] = 0;
			}
			printf("%-16s %-8s %-8d %s\n", e->d_name, st, pid, cmd);
		}
		n++;
	}
	closedir(d);
	if (json) {
		if (first) printf("]\n");
		else printf("\n]\n");
	} else {
		if (!n) printf("(trong)\n");
	}
	return 0;
}

/* stop <id>: TERM ca cgroup, cho timeout, KILL neu con sot, don cgroup/watchdog */
int do_stop(const char *id, int timeout) {
	char dir[256], f[PATH_MAX + 16];
	char cg[PATH_MAX] = "", rootfs[PATH_MAX] = "", wdbuf[64] = "";
	snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
	snprintf(f, sizeof f, "%s/cgdir", dir);
	if (read_small(f, cg, sizeof cg) < 0) {
		fprintf(stderr, "khong co container '%s'\n", id);
		return 1;
	}
	snprintf(f, sizeof f, "%s/rootfs", dir);
	read_small(f, rootfs, sizeof rootfs);
	snprintf(f, sizeof f, "%s/watchdog", dir);
	int wd = (read_small(f, wdbuf, sizeof wdbuf) == 0) ? atoi(wdbuf) : 0;

	if (access(cg, F_OK) != 0) {
		printf("%s: da dung\n", id);
	} else {
		kill_pids_in_cgroup(cg, SIGTERM);
		int waited = 0;
		while (waited < timeout * 10 && !cgroup_empty(cg)) {
			usleep(100000);
			waited++;
		}
		if (!cgroup_empty(cg)) {
			cgroup_kill(cg);
			for (int i = 0; i < 50 && !cgroup_empty(cg); i++)
				usleep(100000);
		}
		for (int i = 0; i < 20; i++) {
			if (rmdir(cg) == 0 || errno == ENOENT) break;
			usleep(100000);
		}
		printf("%s: stopped\n", id);
	}

	if (rootfs[0]) {
		char o[PATH_MAX + 16];
		snprintf(o, sizeof o, "%s/.oldroot", rootfs);
		rmdir(o);
	}
	if (wd > 1) kill(wd, SIGTERM);
	snprintf(f, sizeof f, "%s/watchdog", dir);
	unlink(f);
	return 0;
}

/* rm <id>: xoa state (doi STOPPED truoc, tru khi -f), don cgroup con sot */
int do_rm(const char *id, int force) {
	char dir[256], f[PATH_MAX + 16], buf[256];
	char cg[PATH_MAX] = "", rootfs[PATH_MAX] = "", wdbuf[64] = "";
	snprintf(dir, sizeof dir, STATE_BASE "/%s", id);
	snprintf(f, sizeof f, "%s/pid", dir);
	if (read_small(f, buf, sizeof buf) < 0) {
		fprintf(stderr, "khong co container '%s'\n", id);
		return 1;
	}
	int pid = atoi(buf);
	snprintf(f, sizeof f, "/proc/%d/cmdline", pid);
	if (access(f, F_OK) == 0) {
		if (!force) {
			fprintf(stderr, "'%s' dang chay — stop truoc hoac rm -f\n", id);
			return 1;
		}
		do_stop(id, 3);
	}

	snprintf(f, sizeof f, "%s/cgdir", dir);
	read_small(f, cg, sizeof cg);
	snprintf(f, sizeof f, "%s/rootfs", dir);
	read_small(f, rootfs, sizeof rootfs);
	snprintf(f, sizeof f, "%s/watchdog", dir);
	int wd = (read_small(f, wdbuf, sizeof wdbuf) == 0) ? atoi(wdbuf) : 0;

	if (cg[0] && access(cg, F_OK) == 0) {
		if (force) cgroup_kill(cg);
		for (int i = 0; i < 50 && !cgroup_empty(cg); i++)
			usleep(100000);
		for (int i = 0; i < 20; i++) {
			if (rmdir(cg) == 0 || errno == ENOENT) break;
			usleep(100000);
		}
	}
	if (rootfs[0]) {
		char o[PATH_MAX + 16];
		snprintf(o, sizeof o, "%s/.oldroot", rootfs);
		rmdir(o);
	}
	if (wd > 1) kill(wd, SIGTERM);

	snprintf(f, sizeof f, "%s/pid", dir); unlink(f);
	snprintf(f, sizeof f, "%s/cgdir", dir); unlink(f);
	snprintf(f, sizeof f, "%s/cmd", dir); unlink(f);
	snprintf(f, sizeof f, "%s/rootfs", dir); unlink(f);
	snprintf(f, sizeof f, "%s/watchdog", dir); unlink(f);
	snprintf(f, sizeof f, "%s/disk_exceeded", dir); unlink(f);
	snprintf(f, sizeof f, "%s/health_failed", dir); unlink(f);
	rmdir(dir);
	printf("%s: removed\n", id);
	return 0;
}