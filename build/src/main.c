/* main.c — dispatch lenh: run/ps/stats/stop/rm/oci-run */
#include "common.h"

#define VERSION "1.4.0"

#if defined(__aarch64__)
#define MC_ARCH "aarch64"
#elif defined(__x86_64__)
#define MC_ARCH "x86_64"
#elif defined(__arm__)
#define MC_ARCH "armv7l"
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
#define MC_ARCH "riscv64"
#elif defined(__i386__)
#define MC_ARCH "i386"
#else
#define MC_ARCH "unknown"
#endif

#ifdef __GLIBC__
#define MC_LIBC "glibc"
#else
#define MC_LIBC "musl"
#endif

int main(int argc, char **argv) {
	if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
		printf("minicontainer %s (%s, %s, static)\n", VERSION, MC_ARCH, MC_LIBC);
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "stats") == 0) {
		int json = 0;
		const char *id = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--json") == 0) json = 1;
			else if (!id) id = argv[i];
		}
		if (!id) { fprintf(stderr, "Usage: %s stats [--json] <id>\n", argv[0]); return 1; }
		return do_stats(id, json);
	}
	if (argc >= 2 && strcmp(argv[1], "ps") == 0) {
		int json = 0;
		for (int i = 2; i < argc; i++)
			if (strcmp(argv[i], "--json") == 0) json = 1;
		return do_ps(json);
	}
	if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
		int timeout = 3;
		const char *id = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) timeout = atoi(argv[++i]);
			else if (!id) id = argv[i];
		}
		if (!id) { fprintf(stderr, "Usage: %s stop [--timeout N] <id>\n", argv[0]); return 1; }
		return do_stop(id, timeout);
	}
	if (argc >= 2 && strcmp(argv[1], "rm") == 0) {
		int force = 0;
		const char *id = NULL;
		if (argc >= 4 && strcmp(argv[2], "-f") == 0) { force = 1; id = argv[3]; }
		else if (argc >= 3) id = argv[2];
		if (!id) { fprintf(stderr, "Usage: %s rm [-f] <id>\n", argv[0]); return 1; }
		return do_rm(id, force);
	}
	if (argc >= 3 && strcmp(argv[1], "oci-run") == 0) {
		static char *av[40];
		int an = oci_to_argv(argv[2], av, 39);
		if (an < 0) return 1;
		argc = an;
		argv = av;
	}
	if (argc >= 4 && strcmp(argv[1], "exec") == 0)
		return do_exec(argv[2], &argv[3]);
	if (argc == 3 && strcmp(argv[1], "exec") == 0) {
		fprintf(stderr, "Usage: %s exec <id> <cmd> [args...]\n", argv[0]);
		return 1;
	}
	if (argc >= 2 && strcmp(argv[1], "service") == 0 && argc >= 4 && strcmp(argv[2], "create") == 0) {
		return do_service_create(argc, argv);
	}
	if (argc >= 3 && strcmp(argv[1], "pull") == 0)
		return do_pull(argc, argv);
	if (argc >= 2 && strcmp(argv[1], "pull") == 0) {
		fprintf(stderr, "Usage: %s pull <profile> [--url=BASE] [--dest=DIR] [--sha256=SUM] [--force]\n", argv[0]);
		return 1;
	}
	struct cfg c = { .ram = -1, .cpu = -1, .pids = 64, .io = -1, .max_disk = -1, .detach = 0, .name = NULL,
	                 .rootfs = "/opt/rootfs",
	                 .net = "host", .hostname = "minicontainer-os", .cmd = NULL };
	parse_args(argc, argv, &c);
	validate_input(&c);
	if (!c.cmd || !c.cmd[0]) {
		print_usage(argv[0]);
		return 1;
	}
	return do_run(&c);
}
