/* cli.c — parse run flags (both --opt=value and --opt value) */
#include "common.h"
#include <getopt.h>

/* print_usage is defined below */

static const struct option run_options[] = {
	{"ram", required_argument, NULL, 'r'},
	{"cpu", required_argument, NULL, 'c'},
	{"pids", required_argument, NULL, 'q'},
	{"io", required_argument, NULL, 'i'},
	{"disk", required_argument, NULL, 'k'},
	{"name", required_argument, NULL, 'n'},
	{"detach", no_argument, NULL, 'D'},
	{"read-only", no_argument, NULL, 'R'},
	{"seccomp", required_argument, NULL, 's'},
	{"health-cmd", required_argument, NULL, 'h'},
	{"rootfs", required_argument, NULL, 'o'},
	{"image", required_argument, NULL, 'I'},
	{"net", required_argument, NULL, 'N'},
	{"hostname", required_argument, NULL, 'H'},
	{"publish", required_argument, NULL, 'p'},
	{"env", required_argument, NULL, 'e'},
	{"workdir", required_argument, NULL, 'w'},
	{"user", required_argument, NULL, 'u'},
	{"mount", required_argument, NULL, 'M'},
	{"help", no_argument, NULL, 1},
	{NULL, 0, NULL, 0}
};

/* Numeric value must be valid: reject "--ram=abc" silently ignored (-1 = unset). */
static long need_bytes(const char *opt, const char *val) {
	long v = parse_bytes(val);
	if (v <= 0) {
		fprintf(stderr, "minicontainer: %s: invalid value '%s'\n", opt, val ? val : "");
		exit(1);
	}
	return v;
}

void parse_args(int argc, char **argv, struct cfg *cf) {
	int opt;
	int option_index = 0;
	const char *prog = argv[0];

	/* Skip "run" subcommand if present */
	if (argc > 1 && strcmp(argv[1], "run") == 0) {
		argc--;
		argv++;
	}

	/* Reset getopt */
	optind = 1;

	while ((opt = getopt_long(argc, argv, "+r:c:i:k:n:dDRs:h:o:I:N:H:P:p:e:w:u:M:", run_options, &option_index)) != -1) {
		switch (opt) {
			case 'r': cf->ram = need_bytes("--ram", optarg); break;
			case 'c': cf->cpu = (int)need_bytes("--cpu", optarg); break;
			case 'q': cf->pids = need_bytes("--pids", optarg); break;
			case 'i': cf->io = need_bytes("--io", optarg); break;
			case 'k': cf->max_disk = need_bytes("--disk", optarg); break;
			case 'd': cf->detach = 1; break;
			case 'n': cf->name = optarg; break;
			case 'D': cf->detach = 1; break;
			case 'R': cf->read_only = 1; break;
			case 's':
				if (strcmp(optarg, "strict") == 0) cf->seccomp_mode = 1;
				else if (strcmp(optarg, "off") == 0) cf->seccomp_mode = 2;
				else if (strcmp(optarg, "default") == 0) cf->seccomp_mode = 0;
				else { fprintf(stderr, "minicontainer: --seccomp must be default, strict, or off\n"); exit(1); }
				break;
			case 'h': cf->health_cmd = optarg; break;
			case 'o': cf->rootfs = optarg; break;
			case 'I': cf->image = optarg; break;
			case 'N': cf->net = optarg; break;
			case 'H': cf->hostname = optarg; break;
			case 'e':
				cf->env = realloc(cf->env, (cf->nenv + 2) * sizeof *cf->env);
				cf->env[cf->nenv++] = (char *)optarg;
				cf->env[cf->nenv] = NULL;
				break;
			case 'w': cf->workdir = optarg; break;
			case 'u': cf->user = optarg; break;
			case 'M':
				cf->mounts = realloc(cf->mounts, (cf->nmounts + 2) * sizeof *cf->mounts);
				cf->mounts[cf->nmounts++] = (char *)optarg;
				cf->mounts[cf->nmounts] = NULL;
				break;
			case 'p':
			case 'P':
				cf->publish = realloc(cf->publish, (cf->npublish + 2) * sizeof *cf->publish);
				cf->publish[cf->npublish++] = (char *)optarg;
				cf->publish[cf->npublish] = NULL;
				break;
			case 1:
				print_usage(prog);
				exit(0);
			case '?':
				fprintf(stderr, "minicontainer: unknown option or missing argument\n");
				print_usage(prog);
				exit(1);
			default:
				/* Unknown option or missing argument */
				exit(1);
		}
	}

	/* Remaining arguments are the command */
	if (optind < argc) {
		cf->cmd = &argv[optind];
	}
}

void print_usage(const char *prog) {
	fprintf(stderr, "Usage: %s run [--ram=30M] [--cpu=20] [--pids=64] [--io=1M] [--disk=100M] [--detach|-d] [--name=ID]\n"
		"                  [--net=host|isolated|bridge] [--rootfs=DIR | --image=NAME] [--hostname=NAME] [--health-cmd CMD]\n"
		"                  [--env K=V]... [-w DIR] [--user UID[:GID]] [--mount type:src:dst[:opts]]...\n"
		"                  [--read-only] [--seccomp=default|strict|off] [--publish HOST:CONTAINER | -p HOST:CONTAINER]\n"
		"       %s ps [--json] | stats <id> [--json] | stop <id> [--timeout N] | rm [-f] <id> | exec <id> <cmd> [args...] |\n"
		"       %s logs [-f] [--tail=N] <id> | gc [--dry-run] |\n"
		"       %s oci-run <bundle> | service create <name> <cmd> [args...] |\n"
		"       %s pull <profile> [--url=BASE] [--dest=DIR] [--sha256=SUM] [--require-signature] [--sig=PUBKEY] |\n"
		"       %s sdk init <dir> [--profile=P] [--ram=32M] | sdk pack <dir> [-o OUT] [--force] |\n"
		"       %s check [--json] |\n"
		"       %s load <file.tar> [--tag=NAME] | images [--json] | rmi <name> | save <name> -o <file.tar> |\n"
		"       %s build -t NAME[:TAG] [-f Dockerfile] [--no-cache] CONTEXT |\n"
		"       %s export <image> -o <bundle.tar.gz> | import <bundle> [--verify=PUBKEY] |\n"
		"       --version\n", prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}
