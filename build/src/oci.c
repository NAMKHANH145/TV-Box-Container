/* oci.c — OCI bundle mode toi thieu: config.json subset -> flags noi bo.
 * Ho tro: process.args[], hostname, root.path, linux.resources:
 *   memory.limit (byte), cpu.quota/cpu.period, pids.limit.
 * KHONG ho tro: mounts, hooks, seccomp-OCI, devices, rlimits, uidMappings
 * (ta co userns/seccomp rieng). JSON parser nguyen thuy: khong escape. */
#include "common.h"

static int json_str_at(const char *p, const char *key, char *out, size_t n) {
	char k[64];
	snprintf(k, sizeof k, "\"%s\"", key);
	p = strstr(p, k);
	if (!p) return -1;
	p = strchr(p + strlen(k), ':');
	if (!p) return -1;
	p = strchr(p, '"');
	if (!p) return -1;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
	out[i] = 0;
	return (*p == '"') ? 0 : -1;
}

static long json_long_at(const char *p, const char *key) {
	char k[64];
	snprintf(k, sizeof k, "\"%s\"", key);
	p = strstr(p, k);
	if (!p) return -1;
	p = strchr(p + strlen(k), ':');
	if (!p) return -1;
	return atol(p + 1);
}

/* Kiem tra key OCI khong ho tro va canh bao */
static void oci_warn_unsupported(const char *jb) {
	const char *unsupported[] = {
		"mounts", "hooks", "seccomp", "devices", 
		"rlimits", "uidMappings", "gidMappings", "namespaces",
		"apparmor", "selinux", "capabilities", "noNewPrivileges",
		"cgroupsPath", "intelRdt", "unified", "cgroupns"
	};
	size_t n_unsup = sizeof(unsupported) / sizeof(unsupported[0]);
	for (size_t i = 0; i < n_unsup; i++) {
		const char *p = strstr(jb, unsupported[i]);
		/* Kiem tra xem key nay co nam trong object chinh khong (khong phai trong comment/string) */
		if (p) {
			/* Tim ':' truoc key de xac dinh la key object */
			const char *colon = p - 1;
			while (colon > jb && (*colon == ' ' || *colon == '\t' || *colon == '\n')) colon--;
			if (colon > jb && *colon == '"') {
				fprintf(stderr, "warning: OCI key '%s' not supported, ignored\n", unsupported[i]);
			}
		}
	}
}

/* args[] -> out[] (toi da max), tra so luong. Khong ho tro escape. */
static int json_args(const char *js, char *out, size_t stride, int max) {
	const char *p = strstr(js, "\"args\"");
	int n = 0;
	if (!p || !(p = strchr(p, '['))) return 0;
	p++;
	while (*p && *p != ']' && n < max) {
		while (*p && *p != '"' && *p != ']') p++;
		if (*p != '"') break;
		p++;
		size_t i = 0;
		while (*p && *p != '"' && i + 1 < stride) out[n * stride + i++] = *p++;
		out[n * stride + i] = 0;
		if (*p == '"') { p++; n++; }
	}
	return n;
}

int oci_to_argv(const char *bundle, char **av, int maxav) {
	static char jb[8192], vals[8][512], args[16][256];
	static char ochn[200], ocpath[200];
	char cfg[PATH_MAX + 16], *argp[16];
	int an = 0;
	snprintf(cfg, sizeof cfg, "%s/config.json", bundle);
	FILE *f = fopen(cfg, "r");
	if (!f) { fprintf(stderr, "khong doc duoc %s\n", cfg); return -1; }
	size_t r = fread(jb, 1, sizeof jb - 1, f);
	fclose(f);
	jb[r] = 0;
	oci_warn_unsupported(jb);
	av[an++] = "minicontainer";
	av[an++] = "run";
	/* limits */
	long mem = -1, plim = -1, quota = -1, period = -1;
	const char *lr = strstr(jb, "\"linux\"");
	if (lr) {
		const char *mm = strstr(lr, "\"memory\"");
		if (mm) mem = json_long_at(mm, "limit");
		const char *pp = strstr(lr, "\"pids\"");
		if (pp) plim = json_long_at(pp, "limit");
		const char *cc = strstr(lr, "\"cpu\"");
		if (cc) { quota = json_long_at(cc, "quota"); period = json_long_at(cc, "period"); }
	}
	if (mem > 0) { snprintf(vals[0], sizeof vals[0], "--ram=%ld", mem); av[an++] = vals[0]; }
	if (quota > 0 && period > 0) {
		snprintf(vals[1], sizeof vals[1], "--cpu=%ld", quota * 100 / period);
		av[an++] = vals[1];
	}
	if (plim > 0) { snprintf(vals[2], sizeof vals[2], "--pids=%ld", plim); av[an++] = vals[2]; }
	/* hostname + rootfs */
	if (json_str_at(jb, "hostname", ochn, sizeof ochn) == 0 && ochn[0]) {
		snprintf(vals[4], sizeof vals[4], "--hostname=%s", ochn);
		av[an++] = vals[4];
	}
	/* root.path: scope trong "root" (tranh nham path cua hooks) */
	const char *rt = strstr(jb, "\"root\"");
	if (rt && json_str_at(rt, "path", ocpath, sizeof ocpath) == 0 && ocpath[0]) {
		/* Validate root.path: reject path traversal */
		if (strstr(ocpath, "..") != NULL || ocpath[0] == '/' || strlen(ocpath) == 0) {
			fprintf(stderr, "OCI error: root.path invalid or unsafe\n");
			return -1;
		}
		snprintf(vals[6], sizeof vals[6], "--rootfs=%s/%s", bundle, ocpath);
		av[an++] = vals[6];
	}
	/* args */
	int na = json_args(jb, (char *)args, sizeof args[0], 16);
	if (na <= 0) { fprintf(stderr, "config.json thieu process.args\n"); return -1; }
	av[an++] = "--";
	for (int k = 0; k < na && an < maxav; k++) { argp[k] = args[k]; av[an++] = argp[k]; }
	av[an] = NULL;
	return an;
}
