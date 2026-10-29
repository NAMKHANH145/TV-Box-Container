/* pull.c — tai va giai nen rootfs profile ve /opt/rootfs-<profile>.
 * Moi thu dung execvp + argv tuc tinh (khong qua shell) nen URL/ten profile
 * lay tu bien moi truong khong the chen lenh. */
#include "common.h"
#include <sys/statvfs.h>
#include <sys/vfs.h>

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

#define PKG_SUFFIX "-rootfs.tar.gz"

static const char *const known_profiles[] = {
	"base-mini", "iot-gateway", "python-edge", "web-proxy"
};

static int is_known_profile(const char *p) {
	for (size_t i = 0; i < sizeof known_profiles / sizeof known_profiles[0]; i++)
		if (strcmp(p, known_profiles[i]) == 0) return 1;
	return 0;
}

/* chay lenh, tra ve exit code (khong bat output) */
static int run_wait(char *const av[]) {
	pid_t p = fork();
	if (p < 0) return -1;
	if (p == 0) {
		execvp(av[0], av);
		_exit(127);
	}
	int st;
	while (waitpid(p, &st, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED(st)) return -1;
	return WEXITSTATUS(st);
}

/* chay lenh, bat stdout vao out (cat ngan), tra ve exit code */
static int run_capture(char *const av[], char *out, size_t n) {
	int fds[2];
	if (pipe(fds) < 0) return -1;
	pid_t p = fork();
	if (p < 0) { close(fds[0]); close(fds[1]); return -1; }
	if (p == 0) {
		close(fds[0]);
		if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
		close(fds[1]);
		execvp(av[0], av);
		_exit(127);
	}
	close(fds[1]);
	size_t used = 0;
	for (;;) {
		ssize_t r = read(fds[0], out + used, n - used - 1);
		if (r <= 0) break;
		used += (size_t)r;
		if (used + 1 >= n) break;
	}
	out[used] = '\0';
	close(fds[0]);
	int st;
	while (waitpid(p, &st, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED(st)) return -1;
	return WEXITSTATUS(st);
}

static int copy_file(const char *src, const char *dst) {
	int in = open(src, O_RDONLY | O_CLOEXEC);
	if (in < 0) { fprintf(stderr, "minicontainer: pull: mo %s: %s\n", src, strerror(errno)); return -1; }
	int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (out < 0) { fprintf(stderr, "minicontainer: pull: ghi %s: %s\n", dst, strerror(errno)); close(in); return -1; }
	char buf[65536];
	ssize_t r;
	while ((r = read(in, buf, sizeof buf)) > 0) {
		ssize_t off = 0;
		while (off < r) {
			ssize_t w = write(out, buf + off, (size_t)(r - off));
			if (w <= 0) { close(in); close(out); return -1; }
			off += w;
		}
	}
	close(in);
	if (close(out) < 0) return -1;
	if (r < 0) return -1;
	return 0;
}

static int have_tool(const char *name) {
	char *const av[] = { (char *)name, (char *)"--version", NULL };
	return run_wait(av) == 0 ? 1 : 0;
}

static int fetch(const char *url, const char *dst) {
	if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
		if (have_tool("curl")) {
			char *const av[] = { "curl", "-fsSL", "--max-time", "1800", "-o",
				(char *)dst, (char *)url, NULL };
			int rc = run_wait(av);
			if (rc == 0) return 0;
			fprintf(stderr, "minicontainer: pull: curl loi (exit %d): %s\n", rc, url);
			return -1;
		}
		if (have_tool("wget")) {
			char *const av[] = { "wget", "-q", "-T", "1800", "-O",
				(char *)dst, (char *)url, NULL };
			if (run_wait(av) == 0) return 0;
			fprintf(stderr, "minicontainer: pull: wget loi: %s\n", url);
			return -1;
		}
		fprintf(stderr, "minicontainer: pull: can curl hoac wget de tai URL\n");
		return -1;
	}
	/* duong dan dia phuong (file scp san len box cung dung cach nay) */
	return copy_file(url, dst);
}

/* Thanh vien tar nguy hai: duong dan tuyet doi hoac co component ".." */
static int member_unsafe(const char *line) {
	if (line[0] == '/') return 1;
	char copy[PATH_MAX + 1];
	if (strlen(line) >= sizeof copy) return 1;
	strcpy(copy, line);
	for (char *tok = strtok(copy, "/"); tok; tok = strtok(NULL, "/"))
		if (strcmp(tok, "..") == 0) return 1;
	return 0;
}

static int check_tar(const char *pkg) {
	char *const av[] = { "tar", "-tzf", (char *)pkg, NULL };
	char out[65536];
	int rc = run_capture(av, out, sizeof out);
	if (rc != 0) { fprintf(stderr, "minicontainer: pull: khong doc duoc danh sach tar (tar loi %d)\n", rc); return -1; }
	if (out[0] == '\0') { fprintf(stderr, "minicontainer: pull: tar ban\n"); return -1; }
	char copy[65536];
	size_t len = strlen(out);
	if (len >= sizeof copy) len = sizeof copy - 1;
	memcpy(copy, out, len);
	copy[len] = '\0';
	for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
		if (member_unsafe(line)) {
			fprintf(stderr, "minicontainer: pull: tu choi giai nen, tar chua duong dan nguy hiem: %s\n", line);
			return -1;
		}
	}
	return 0;
}

static int verify_sha256(const char *pkg, const char *want) {
	char *const av[] = { "sha256sum", (char *)pkg, NULL };
	char out[4096];
	if (run_capture(av, out, sizeof out) != 0) {
		fprintf(stderr, "minicontainer: pull: sha256sum chay that bai\n");
		return -1;
	}
	char got[65] = "";
	size_t j = 0;
	for (size_t i = 0; i < strlen(out) && j < 64; i++) {
		char c = out[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) break;
		got[j++] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
	}
	got[j] = '\0';
	if (j != 64) { fprintf(stderr, "minicontainer: pull: khong doc duoc sha256 tu sha256sum\n"); return -1; }
	const char *w = want;
	if (strncmp(w, "sha256:", 7) == 0) w += 7;
	char low[65];
	for (size_t i = 0; i < 64; i++) {
		char c = w[i];
		if (!c) { fprintf(stderr, "minicontainer: pull: --sha256 phai co 64 ky tu hex\n"); return -1; }
		low[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
	}
	low[64] = '\0';
	if (strcmp(got, low) != 0) {
		fprintf(stderr, "minicontainer: pull: sha256 khong khop\n  mong doi: %s\n  thyc te : %s\n", low, got);
		return -1;
	}
	printf("pull: sha256 khop\n");
	return 0;
}

static long long file_size(const char *p) {
	struct stat st;
	if (stat(p, &st) < 0) return -1;
	return (long long)st.st_size;
}

int do_pull(int argc, char **argv) {
	const char *profile = NULL, *base = NULL, *dest = NULL, *sha = NULL;
	int force = 0;
	for (int i = 2; i < argc; i++) {
		const char *a = argv[i];
		if (strncmp(a, "--url=", 6) == 0) base = a + 6;
		else if (strcmp(a, "--url") == 0 && i + 1 < argc) base = argv[++i];
		else if (strncmp(a, "--dest=", 7) == 0) dest = a + 7;
		else if (strcmp(a, "--dest") == 0 && i + 1 < argc) dest = argv[++i];
		else if (strncmp(a, "--sha256=", 9) == 0) sha = a + 9;
		else if (strcmp(a, "--sha256") == 0 && i + 1 < argc) sha = argv[++i];
		else if (strcmp(a, "--force") == 0) force = 1;
		else if (!profile && valid_name(a)) profile = a;
		else { fprintf(stderr, "minicontainer: pull: tham so khong hieu: %s\n", a); return 1; }
	}
	if (!profile) {
		fprintf(stderr, "Usage: %s pull <profile> [--url=BASE] [--dest=DIR] [--sha256=SUM] [--force]\n", argv[0]);
		fprintf(stderr, "  profile: base-mini | iot-gateway | python-edge | web-proxy\n");
		return 1;
	}
	if (!is_known_profile(profile))
		fprintf(stderr, "minicontainer: pull: '%s' khong phai profile chuan, van thu tai\n", profile);
	if (!base) {
		base = getenv("MC_PULL_URL");
		if (!base || !*base) {
			fprintf(stderr, "minicontainer: pull: thieu nguon. Dung --url=BASE (http(s):// hoac duong dan file)"
				" hoac dat MC_PULL_URL.\n");
			return 1;
		}
	}

	char url[PATH_MAX * 2];
	if ((size_t)snprintf(url, sizeof url, "%s/%s%s", base, profile, PKG_SUFFIX) >= sizeof url) {
		fprintf(stderr, "minicontainer: pull: duong dan qua dai\n");
		return 1;
	}
	char destbuf[PATH_MAX];
	if (dest) {
		if (strlen(dest) >= sizeof destbuf || !*dest) { fprintf(stderr, "minicontainer: pull: --dest khong hop le\n"); return 1; }
		snprintf(destbuf, sizeof destbuf, "%s", dest);
	} else {
		if ((size_t)snprintf(destbuf, sizeof destbuf, "%s-%s", "/opt/rootfs", profile) >= sizeof destbuf) {
			fprintf(stderr, "minicontainer: pull: duong dan qua dai\n");
			return 1;
		}
	}

	/* staging nam cung file he thong voi dest de rename duoc atomic */
	char parent[PATH_MAX];
	snprintf(parent, sizeof parent, "%s", destbuf);
	char *slash = strrchr(parent, '/');
	if (!slash || slash == parent) { fprintf(stderr, "minicontainer: pull: --dest phai la duong dan tuyet doi\n"); return 1; }
	*slash = '\0';
	char pkg[PATH_MAX], stage[PATH_MAX];
	if ((size_t)snprintf(pkg, sizeof pkg, "%s/.mc-pull-%d%s", parent, (int)getpid(), PKG_SUFFIX) >= sizeof pkg ||
	    (size_t)snprintf(stage, sizeof stage, "%s/.mc-pull-%d.unpacked", parent, (int)getpid()) >= sizeof stage) {
		fprintf(stderr, "minicontainer: pull: duong dan qua dai\n");
		return 1;
	}

	struct stat dst;
	int dest_exists = (stat(destbuf, &dst) == 0);
	if (dest_exists && !force) {
		fprintf(stderr, "minicontainer: pull: %s da ton tai (dung --force de ghi de)\n", destbuf);
		return 1;
	}

	printf("pull: tai %s\n", url);
	if (mkdir(parent, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "minicontainer: pull: tao %s: %s\n", parent, strerror(errno));
		return 1;
	}
	struct statfs sfs;
	if (statfs(parent, &sfs) == 0 && (unsigned long long)sfs.f_type == (unsigned long long)TMPFS_MAGIC)
		fprintf(stderr, "minicontainer: pull: %s nam tren tmpfs (RAM): --read-only khong dung"
			" duoc o day, va file mat khi khoi dong lai. Nen dung /opt/rootfs-<profile>.\n", parent);
	if (fetch(url, pkg) < 0) return 1;
	long long sz = file_size(pkg);
	if (sz <= 0) { fprintf(stderr, "minicontainer: pull: tai ve file rong\n"); unlink(pkg); return 1; }

	struct statvfs sv;
	if (statvfs(parent, &sv) == 0) {
		unsigned long long free_b = (unsigned long long)sv.f_bavail * sv.f_frsize;
		unsigned long long need = (unsigned long long)sz * 4;
		if (free_b < need) {
			fprintf(stderr, "minicontainer: pull: thieu cho tron (%.1f MB con trong, can ~%.1f MB)\n",
				free_b / 1048576.0, need / 1048576.0);
			unlink(pkg);
			return 1;
		}
	}
	printf("pull: %.1f MB\n", sz / 1048576.0);

	if (sha && verify_sha256(pkg, sha) < 0) { unlink(pkg); return 1; }
	if (check_tar(pkg) < 0) { unlink(pkg); return 1; }

	if (mkdir(stage, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "minicontainer: pull: tao %s: %s\n", stage, strerror(errno));
		unlink(pkg);
		return 1;
	}
	char *const tav[] = { "tar", "-xzf", pkg, "-C", stage, NULL };
	if (run_wait(tav) != 0) {
		fprintf(stderr, "minicontainer: pull: tar giai nen that bai\n");
		unlink(pkg);
		char *const rav[] = { "rm", "-rf", stage, NULL };
		run_wait(rav);
		return 1;
	}
	unlink(pkg);

	char final[PATH_MAX + 1];
	snprintf(final, sizeof final, "%s", destbuf);
	if (dest_exists) {
		char olddir[PATH_MAX + 8];
		snprintf(olddir, sizeof olddir, "%s.old", destbuf);
		char *const rav[] = { "rm", "-rf", olddir, NULL };
		run_wait(rav);
		if (rename(destbuf, olddir) < 0) {
			fprintf(stderr, "minicontainer: pull: doi %s: %s\n", destbuf, strerror(errno));
			char *const cav[] = { "rm", "-rf", stage, NULL };
			run_wait(cav);
			return 1;
		}
		if (rename(stage, final) < 0) {
			fprintf(stderr, "minicontainer: pull: doi %s -> %s: %s\n", stage, final, strerror(errno));
			rename(olddir, destbuf);
			return 1;
		}
		char *const rav2[] = { "rm", "-rf", olddir, NULL };
		run_wait(rav2);
	} else if (rename(stage, final) < 0) {
		fprintf(stderr, "minicontainer: pull: doi %s -> %s: %s\n", stage, final, strerror(errno));
		char *const cav[] = { "rm", "-rf", stage, NULL };
		run_wait(cav);
		return 1;
	}

	long long done = disk_usage(final);
	if (done < 0) done = file_size(final);
	printf("pull: xong %s (%.1f MB sau giai nen)\n", final, done / 1048576.0);
	printf("  chay: minicontainer run --rootfs %s <cmd>\n", final);
	return 0;
}
