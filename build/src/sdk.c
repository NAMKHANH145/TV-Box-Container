/* sdk.c — `minicontainer sdk init|pack`: skeleton du an + dong goi rootfs
 * thanh <profile>-rootfs.tar.gz dung dinh dang `pull` nhan.
 *
 * pack validate cay rootfs bang nftw (FTW_PHYS, khong duoi symlink) roi moi
 * goi tar voi argv tuc tinh (khong qua shell). Ghi kem <out>.sha256 de
 * `pull --sha256=$(cut -d' ' -f1 <out>.sha256)` dung duoc ngay.
 */
#include "common.h"
#include <ftw.h>

#define PKG_SUFFIX "-rootfs.tar.gz"

static int g_bad;   /* 1 = cay rootfs co duong dan nguy hiem */
static char g_root[PATH_MAX + 16];

/* Symlink duoc phep: target tuyet doi (/bin/busybox, van nam trong container)
 * hoac tuong doi nhung khong vuot qua goc rootfs (vi du usr/bin/x -> ../../bin/y).
 * Kiem tra lexically, khong cham filesystem. */
static void check_link(const char *path, const char *target) {
	if (target[0] == '/') return;   /* tuyet doi: luon nam trong rootfs cua container */
	size_t rl = strlen(g_root);
	if (strncmp(path, g_root, rl) != 0) {
		fprintf(stderr, "minicontainer: sdk pack: %s nam ngoai %s\n", path, g_root);
		g_bad = 1;
		return;
	}
	const char *rel = path + rl;
	while (*rel == '/') rel++;
	int depth = 0;
	for (const char *p = rel; *p; p++)
		if (*p == '/' && p[1]) depth++;
	const char *t = target;
	while (*t) {
		while (*t == '/') t++;
		const char *e = t;
		while (*e && *e != '/') e++;
		size_t len = (size_t)(e - t);
		if (len == 2 && t[0] == '.' && t[1] == '.') {
			if (--depth < 0) {
				fprintf(stderr, "minicontainer: sdk pack: symlink tro ra ngoai rootfs: %s -> %s\n", path, target);
				g_bad = 1;
				return;
			}
		} else if (len && !(len == 1 && t[0] == '.')) {
			depth++;
		}
		t = e;
	}
}

static int pack_cb(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
	(void)st; (void)ftw;
	if (flag != FTW_SL) return 0;
	char buf[PATH_MAX + 1];
	ssize_t n = readlink(path, buf, PATH_MAX);
	if (n < 0) {
		fprintf(stderr, "minicontainer: sdk pack: readlink %s: %s\n", path, strerror(errno));
		g_bad = 1;
		return 0;
	}
	buf[n] = '\0';
	check_link(path, buf);
	return 0;
}

/* "/a/b/" -> "b" (tra ve con tro trong chinh s, khong cap phat) */
static const char *base_name(const char *s, char *tmp, size_t n) {
	size_t len = strlen(s);
	while (len > 1 && s[len - 1] == '/') len--;
	if (len >= n) return NULL;
	memcpy(tmp, s, len);
	tmp[len] = '\0';
	const char *b = strrchr(tmp, '/');
	return b ? b + 1 : tmp;
}

static int mkdir_p(const char *path, mode_t mode) {
	char buf[PATH_MAX];
	if (snprintf(buf, sizeof buf, "%s", path) >= (int)sizeof buf) return -1;
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(buf, mode) < 0 && errno != EEXIST) return -1;
		*p = '/';
	}
	if (mkdir(buf, mode) < 0 && errno != EEXIST) return -1;
	return 0;
}

static int dir_is_empty(const char *path) {
	DIR *d = opendir(path);
	if (!d) return 1;   /* khong ton tai = coi nhu trong */
	struct dirent *e;
	int empty = 1;
	while ((e = readdir(d)))
		if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { empty = 0; break; }
	closedir(d);
	return empty;
}

static int sdk_init(int argc, char **argv) {
	const char *dir = NULL, *profile = NULL, *ram = "32M", *cpu = "20", *pids = "64",
	           *io = "1M", *disk = NULL, *net = "host", *cmd = "/bin/sh";
	int ro = 0;
	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (strncmp(a, "--profile=", 10) == 0) profile = a + 10;
		else if (strncmp(a, "--ram=", 6) == 0) ram = a + 6;
		else if (strncmp(a, "--cpu=", 6) == 0) cpu = a + 6;
		else if (strncmp(a, "--pids=", 7) == 0) pids = a + 7;
		else if (strncmp(a, "--io=", 5) == 0) io = a + 5;
		else if (strncmp(a, "--disk=", 7) == 0) disk = a + 7;
		else if (strncmp(a, "--net=", 6) == 0) net = a + 6;
		else if (strncmp(a, "--cmd=", 6) == 0) cmd = a + 6;
		else if (strcmp(a, "--read-only") == 0) ro = 1;
		else if (!dir && a[0] != '-') dir = a;
		else { fprintf(stderr, "minicontainer: sdk init: tham so khong hieu: %s\n", a); return 1; }
	}
	if (!dir) {
		fprintf(stderr, "Usage: %s sdk init <dir> [--profile=NAME] [--ram=32M] [--cpu=20]"
			" [--pids=64] [--io=1M] [--disk=100M] [--net=host] [--read-only] [--cmd=/bin/sh]\n", "minicontainer");
		return 1;
	}
	if (strcmp(net, "host") && strcmp(net, "isolated") && strcmp(net, "bridge")) {
		fprintf(stderr, "minicontainer: sdk init: --net chi duoc host, isolated hoac bridge\n");
		return 1;
	}
	if (!dir_is_empty(dir)) {
		fprintf(stderr, "minicontainer: sdk init: %s da ton tai va khong rong\n", dir);
		return 1;
	}

	char rootfs[PATH_MAX], conf[PATH_MAX], readme[PATH_MAX];
	if (snprintf(rootfs, sizeof rootfs, "%s/rootfs", dir) >= (int)sizeof rootfs ||
	    snprintf(conf, sizeof conf, "%s/mc.conf", dir) >= (int)sizeof conf ||
	    snprintf(readme, sizeof readme, "%s/README.md", dir) >= (int)sizeof readme) {
		fprintf(stderr, "minicontainer: sdk init: duong dan qua dai\n");
		return 1;
	}
	if (mkdir_p(rootfs, 0755) < 0) {
		fprintf(stderr, "minicontainer: sdk init: tao %s: %s\n", rootfs, strerror(errno));
		return 1;
	}

	char body[4096];
	int n = snprintf(body, sizeof body,
		"# minicontainer project — sinh boi: minicontainer sdk init\n"
		"# Chay:  minicontainer run --rootfs <dir>/rootfs $(grep ^cmd= mc.conf | cut -d= -f2-)\n"
		"profile=%s\n"
		"ram=%s\n"
		"cpu=%s\n"
		"pids=%s\n"
		"io=%s\n"
		"%sdisk=%s\n"
		"net=%s\n"
		"read-only=%d\n"
		"cmd=%s\n",
		profile ? profile : "base-mini", ram, cpu, pids, io,
		disk ? "" : "# ", disk ? disk : "100M",
		net, ro, cmd);
	if (n < 0 || n >= (int)sizeof body) {
		fprintf(stderr, "minicontainer: sdk init: mc.conf qua dai\n");
		return 1;
	}
	if (try_write_file(conf, body) < 0) {
		fprintf(stderr, "minicontainer: sdk init: ghi %s: %s\n", conf, strerror(errno));
		return 1;
	}
	if (try_write_file(readme,
		"# Du an minicontainer\n\n"
		"1. Chep rootfs vao `rootfs/` (vd: `tar -xzf base-mini-rootfs.tar.gz -C rootfs`)\n"
		"   hoac `minicontainer pull base-mini --dest=./rootfs` (dung --force neu da co).\n"
		"2. Sua `mc.conf` (ram/cpu/pids/io/disk/net/read-only/cmd).\n"
		"3. Chay: `minicontainer run --rootfs ./rootfs /bin/sh`\n"
		"4. Dong goi lai: `minicontainer sdk pack .` -> `<ten-thu-muc>-rootfs.tar.gz` + `.sha256`\n") < 0) {
		fprintf(stderr, "minicontainer: sdk init: ghi %s: %s\n", readme, strerror(errno));
		return 1;
	}
	printf("sdk: tao %s (rootfs/ + mc.conf + README.md)\n", dir);
	printf("     buoc tiep: chep rootfs vao %s/rootfs roi `minicontainer sdk pack %s`\n", dir, dir);
	return 0;
}

static int sdk_pack(int argc, char **argv) {
	const char *dir = NULL, *out = NULL;
	int force = 0;
	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if (strncmp(a, "--output=", 9) == 0) out = a + 9;
		else if (strcmp(a, "--output") == 0 && i + 1 < argc) out = argv[++i];
		else if (strcmp(a, "-o") == 0 && i + 1 < argc) out = argv[++i];
		else if (strcmp(a, "--force") == 0) force = 1;
		else if (!dir && a[0] != '-') dir = a;
		else { fprintf(stderr, "minicontainer: sdk pack: tham so khong hieu: %s\n", a); return 1; }
	}
	if (!dir) {
		fprintf(stderr, "Usage: %s sdk pack <dir> [-o OUT.tar.gz] [--force]\n", "minicontainer");
		return 1;
	}

	/* <dir>/rootfs neu co, khong thi chinh <dir> */
	char root[PATH_MAX + 16], sub[PATH_MAX + 16];
	struct stat st;
	if (snprintf(sub, sizeof sub, "%s/rootfs", dir) >= (int)sizeof sub) {
		fprintf(stderr, "minicontainer: sdk pack: duong dan qua dai\n");
		return 1;
	}
	if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode))
		snprintf(root, sizeof root, "%s", sub);
	else
		snprintf(root, sizeof root, "%s", dir);
	if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
		fprintf(stderr, "minicontainer: sdk pack: %s khong phai thu muc\n", root);
		return 1;
	}
	if (strcmp(root, "/") == 0) {
		fprintf(stderr, "minicontainer: sdk pack: tu choi dong goi /\n");
		return 1;
	}

	g_bad = 0;
	snprintf(g_root, sizeof g_root, "%s", root);
	if (nftw(root, pack_cb, 16, FTW_PHYS | FTW_MOUNT) != 0) {
		fprintf(stderr, "minicontainer: sdk pack: khong duyet duoc %s: %s\n", root, strerror(errno));
		return 1;
	}
	if (g_bad) {
		fprintf(stderr, "minicontainer: sdk pack: tu choi dong goi (symlink vuot root)\n");
		return 1;
	}
	char sh[PATH_MAX + 32];
	if (snprintf(sh, sizeof sh, "%s/bin/sh", root) >= (int)sizeof sh) {
		fprintf(stderr, "minicontainer: sdk pack: duong dan qua dai\n");
		return 1;
	}
	if (lstat(sh, &st) != 0)
		fprintf(stderr, "minicontainer: sdk pack: canh bao: %s khong co /bin/sh, container se khong chay duoc\n", root);

	char base[PATH_MAX], defout[PATH_MAX + 32];
	const char *bn = base_name(dir, base, sizeof base);
	if (!bn) { fprintf(stderr, "minicontainer: sdk pack: ten thu muc qua dai\n"); return 1; }
	if (!out) {
		snprintf(defout, sizeof defout, "%s%s", bn, PKG_SUFFIX);
		out = defout;
	}
	if (!force && access(out, F_OK) == 0) {
		fprintf(stderr, "minicontainer: sdk pack: %s da ton tai (dung --force de ghi de)\n", out);
		return 1;
	}

	char oparent[PATH_MAX + 64];
	snprintf(oparent, sizeof oparent, "%s", out);
	char *osl = strrchr(oparent, '/');
	if (osl && osl != oparent) {
		*osl = '\0';
		if (mkdir_p(oparent, 0755) < 0) {
			fprintf(stderr, "minicontainer: sdk pack: tao %s: %s\n", oparent, strerror(errno));
			return 1;
		}
	}

	printf("sdk: dong goi %s -> %s\n", root, out);
	char *const tav[] = { "tar", "-czf", (char *)out, "-C", root,
	                      "--numeric-owner", "--owner=0", "--group=0", ".", NULL };
	int rc = run_wait(tav);
	if (rc != 0) {
		fprintf(stderr, "minicontainer: sdk pack: tar loi (exit %d)\n", rc);
		unlink(out);
		return 1;
	}

	char sum[4096];
	char *const sav[] = { "sha256sum", (char *)out, NULL };
	if (run_capture(sav, sum, sizeof sum) != 0 || strlen(sum) < 64) {
		fprintf(stderr, "minicontainer: sdk pack: khong tinh duoc sha256 (can sha256sum)\n");
		return 1;
	}
	char hash[65];
	memcpy(hash, sum, 64);
	hash[64] = '\0';
	char sumfile[PATH_MAX + 64];
	if (snprintf(sumfile, sizeof sumfile, "%s.sha256", out) >= (int)sizeof sumfile) {
		fprintf(stderr, "minicontainer: sdk pack: duong dan qua dai\n");
		return 1;
	}
	char line[PATH_MAX + 160];
	char outbase[PATH_MAX + 64];
	snprintf(outbase, sizeof outbase, "%s", out);
	const char *ob = strrchr(outbase, '/');
	ob = ob ? ob + 1 : outbase;
	snprintf(line, sizeof line, "%s  %s\n", hash, ob);
	if (try_write_file(sumfile, line) < 0) {
		fprintf(stderr, "minicontainer: sdk pack: ghi %s: %s\n", sumfile, strerror(errno));
		return 1;
	}

	struct stat os;
	long long sz = (stat(out, &os) == 0) ? (long long)os.st_size : -1;
	printf("sdk: xong %s (%.1f MB)\n", out, sz / 1048576.0);
	printf("     %s\n", sumfile);
	/* `pull` tim <profile>-rootfs.tar.gz, nen profile phai khop ten file. */
	char prof[PATH_MAX + 96];
	snprintf(prof, sizeof prof, "%s", ob);
	size_t plen = strlen(prof), slen = strlen(PKG_SUFFIX);
	if (plen > slen && strcmp(prof + plen - slen, PKG_SUFFIX) == 0) prof[plen - slen] = '\0';
	else {
		fprintf(stderr, "minicontainer: sdk pack: canh bao: ten file phai ket thuc bang %s de `pull` tai duoc\n", PKG_SUFFIX);
		snprintf(prof, sizeof prof, "%s", bn);
	}
	char urldir[PATH_MAX + 32];
	snprintf(urldir, sizeof urldir, "%s", out);
	char *slash = strrchr(urldir, '/');
	if (slash && slash != urldir) *slash = '\0';
	else snprintf(urldir, sizeof urldir, ".");
	printf("     phan phoi: minicontainer pull %s --url=%s --sha256=%s\n", prof, urldir, hash);
	return 0;
}

int do_sdk(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage: minicontainer sdk init <dir> [flags]  |  sdk pack <dir> [-o OUT] [--force]\n");
		return 1;
	}
	if (strcmp(argv[2], "init") == 0) return sdk_init(argc - 3, argv + 3);
	if (strcmp(argv[2], "pack") == 0) return sdk_pack(argc - 3, argv + 3);
	fprintf(stderr, "minicontainer: sdk: lenh khong ho tro: %s\n", argv[2]);
	return 1;
}
