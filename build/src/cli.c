/* cli.c — parse flags run (ca 2 dang --opt=value va --opt value) */
#include "common.h"

/* print_usage is defined below */

#define OPT1(name) (strcmp(argv[i], name) == 0 && i + 1 < argc && (val = argv[++i]))
#define OPT2(name, pre) (strncmp(argv[i], pre, strlen(pre)) == 0 && (val = argv[i] + strlen(pre)))

/* Gia tri so bat buoc hop le: tranh "--ram=abc" bi bo qua im lang (-1 = unset). */
static long need_bytes(const char *opt, const char *val) {
	long v = parse_bytes(val);
	if (v <= 0) {
		fprintf(stderr, "minicontainer: %s: gia tri khong hop le '%s'\n", opt, val ? val : "");
		exit(1);
	}
	return v;
}

void parse_args(int argc, char **argv, struct cfg *cf) {
	int i = 1;
	if (argc > 1 && strcmp(argv[1], "run") == 0) i = 2;
	const char *val = NULL;
	for (; i < argc; i++) {
		val = NULL;
		if (OPT1("--ram") || OPT2("--ram", "--ram=")) cf->ram = need_bytes("--ram", val);
		else if (OPT1("--cpu") || OPT2("--cpu", "--cpu=")) cf->cpu = (int)need_bytes("--cpu", val);
		else if (OPT1("--pids") || OPT2("--pids", "--pids=")) cf->pids = need_bytes("--pids", val);
		else if (OPT1("--io") || OPT2("--io", "--io=")) cf->io = need_bytes("--io", val);
		else if (OPT1("--disk") || OPT2("--disk", "--disk=")) cf->max_disk = need_bytes("--disk", val);
		else if (OPT1("--name") || OPT2("--name", "--name=")) cf->name = val;
		else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--detach") == 0) cf->detach = 1;
		else if (OPT1("--health-cmd") || OPT2("--health-cmd", "--health-cmd=")) cf->health_cmd = val;
		else if (OPT1("--rootfs") || OPT2("--rootfs", "--rootfs=")) cf->rootfs = val;
		else if (OPT1("--net") || OPT2("--net", "--net=")) cf->net = val;
		else if (OPT1("--hostname") || OPT2("--hostname", "--hostname=")) cf->hostname = val;
		else if (strcmp(argv[i], "--") == 0) { cf->cmd = &argv[++i]; break; }
		else { cf->cmd = &argv[i]; break; }
	}
}

void print_usage(const char *prog) {
	fprintf(stderr, "Usage: %s run [--ram=30M] [--cpu=20] [--pids=64] [--io=1M] [--disk=100M] [--detach|-d] [--name=ID]\n"
		"                  [--net=host|isolated] [--rootfs=DIR] [--hostname=NAME] [--health-cmd CMD]\n"
		"       %s ps [--json] | stats <id> [--json] | stop <id> [--timeout N] | rm [-f] <id> | oci-run <bundle> |\n"
		"       %s service create <name> <cmd> [args...] | --version\n", prog, prog, prog);
}
