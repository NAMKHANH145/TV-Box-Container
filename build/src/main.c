/* main.c — command dispatch: run/ps/stats/stop/rm/oci-run */
#include "common.h"
#include <sys/prctl.h>

#define VERSION MC_VERSION

static int print_version(int json) {
    if (json)
        printf("{\"version\":\"%s\",\"arch\":\"%s\",\"libc\":\"%s\",\"static\":true,"
               "\"features\":[\"namespaces\",\"cgroup-v2\",\"seccomp\",\"bridge\","
               "\"registry-pull\",\"compose\",\"ab-update\",\"metrics\",\"watchdog\"]}\n",
               VERSION, MC_ARCH, MC_LIBC);
    else
        printf("minicontainer %s (%s, %s, static)\n", VERSION, MC_ARCH, MC_LIBC);
    return 0;
}

int main(int argc, char **argv) {
    /* Become a subreaper to reap descendant zombies and ensure cgroup cleanup */
    prctl(PR_SET_CHILD_SUBREAPER, 1);

    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        int json = argc >= 3 && strcmp(argv[2], "--json") == 0;
        return print_version(json);
    }
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        int json = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json = 1;
            else { fprintf(stderr, "Usage: %s version [--json]\n", argv[0]); return 1; }
        }
        return print_version(json);
    }
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        int json = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json = 1;
            else { fprintf(stderr, "Usage: %s check [--json]\n", argv[0]); return 1; }
        }
        return do_check(json);
    }
    if (argc >= 3 && (strcmp(argv[1], "update") == 0 || strcmp(argv[1], "watchdog") == 0 ||
                      strcmp(argv[1], "metrics") == 0 || strcmp(argv[1], "compose") == 0)) {
        if (strcmp(argv[1], "update") == 0) return do_update_cli(argc, argv);
        if (strcmp(argv[1], "watchdog") == 0) return do_watchdog(argc, argv);
        if (strcmp(argv[1], "metrics") == 0) return do_metrics(argc, argv);
        return do_compose(argc, argv);
    }
    if (argc >= 3 && strcmp(argv[1], "sdk") == 0)
        return do_sdk(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "sdk") == 0) {
        fprintf(stderr, "Usage: %s sdk init <dir> [flags]  |  sdk pack <dir> [-o OUT] [--force]\n", argv[0]);
        return 1;
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
            else { fprintf(stderr, "Usage: %s ps [--json]\n", argv[0]); return 1; }
        return do_ps(json);
    }
    if (argc >= 4 && strcmp(argv[1], "export") == 0) {
        const char *out = NULL;
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "-o", 2) == 0) {
                if (argv[i][2]) out = argv[i] + 2;
                else if (i + 1 < argc) out = argv[++i];
            } else if (strncmp(argv[i], "--output=", 9) == 0) out = argv[i] + 9;
        }
        if (!out) { fprintf(stderr, "Usage: %s export <image> -o <bundle.tar.gz>\n", argv[0]); return 1; }
        return do_export(argv[2], out);
    }
    if (argc >= 3 && strcmp(argv[1], "import") == 0) {
        const char *key = NULL;
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "--verify=", 9) == 0) key = argv[i] + 9;
            else if (strcmp(argv[i], "--verify") == 0 && i + 1 < argc) key = argv[++i];
        }
        return do_import(argv[2], key);
    }
    if (argc >= 2 && strcmp(argv[1], "build") == 0)
        return do_build(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "images") == 0) {
        int json = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json = 1;
            else { fprintf(stderr, "Usage: %s images [--json]\n", argv[0]); return 1; }
        }
        return do_images(json);
    }
    if (argc >= 2 && strcmp(argv[1], "load") == 0) {
        const char *file = NULL, *tag = NULL;
        for (int i = 2; i < argc; i++) {
            if (strncmp(argv[i], "--tag=", 6) == 0) tag = argv[i] + 6;
            else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) tag = argv[++i];
            else if (!file) file = argv[i];
            else { fprintf(stderr, "Usage: %s load <file.tar> [--tag=NAME]\n", argv[0]); return 1; }
        }
        if (!file) { fprintf(stderr, "Usage: %s load <file.tar> [--tag=NAME]\n", argv[0]); return 1; }
        return img_load(file, tag);
    }
    if (argc >= 3 && strcmp(argv[1], "save") == 0) {
        const char *out = NULL;
        for (int i = 3; i < argc; i++) {
            if (strncmp(argv[i], "-o", 2) == 0) {
                if (argv[i][2]) out = argv[i] + 2;
                else if (i + 1 < argc) out = argv[++i];
            } else if (strncmp(argv[i], "--output=", 9) == 0) out = argv[i] + 9;
        }
        if (!out) { fprintf(stderr, "Usage: %s save <name> -o <file.tar>\n", argv[0]); return 1; }
        return img_save(argv[2], out);
    }
    if (argc == 3 && strcmp(argv[1], "rmi") == 0)
        return do_rmi(argv[2]);
    if (argc == 2 && strcmp(argv[1], "rmi") == 0) {
        fprintf(stderr, "Usage: %s rmi <name>\n", argv[0]);
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "gc") == 0) {
        int dry = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") == 0) dry = 1;
            else { fprintf(stderr, "Usage: %s gc [--dry-run]\n", argv[0]); return 1; }
        }
        return do_gc(dry);
    }
    if (argc >= 2 && strcmp(argv[1], "logs") == 0) {
        int follow = 0, tail = 0;
        const char *id = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--follow") == 0) follow = 1;
            else if (strncmp(argv[i], "--tail=", 7) == 0) tail = atoi(argv[i] + 7);
            else if (!id) id = argv[i];
            else { fprintf(stderr, "Usage: %s logs [-f] [--tail=N] <id>\n", argv[0]); return 1; }
        }
        if (!id) { fprintf(stderr, "Usage: %s logs [-f] [--tail=N] <id>\n", argv[0]); return 1; }
        return do_logs(id, follow, tail);
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
    if (argc >= 3 && strcmp(argv[1], "inspect") == 0) {
        int json = 0;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json = 1;
            else { fprintf(stderr, "Usage: %s inspect <id> [--json]\n", argv[0]); return 1; }
        }
        return do_inspect(argv[2], json);
    }
    if (argc == 3 && strcmp(argv[1], "start") == 0)
        return do_start(argv[2]);
    if (argc == 3 && strcmp(argv[1], "restart") == 0) {
        int rc = do_stop(argv[2], 3);
        if (rc != 0) return rc;
        return do_start(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "cp") == 0)
        return do_cp(argv[2], argv[3]);
    if (argc >= 2 && strcmp(argv[1], "cp") == 0) {
        fprintf(stderr, "Usage: %s cp <id>:<path> <path>  |  cp <path> <id>:<path>\n", argv[0]);
        return 1;
    }
    if (argc >= 4 && strcmp(argv[1], "exec") == 0)
        return do_exec(argv[2], &argv[3]);
    if (argc == 3 && strcmp(argv[1], "exec") == 0) {
        fprintf(stderr, "Usage: %s exec <id> <cmd> [args...]\n", argv[0]);
        return 1;
    }
    if (argc >= 3 && strcmp(argv[1], "service") == 0 && strcmp(argv[2], "gc") == 0)
        return do_service_gc();
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
                     .rootfs = NULL, .image = NULL,
                     .net = "host", .hostname = "minicontainer-os", .cmd = NULL };
    c.orig_argc = argc;
    c.orig_argv = argv;
    parse_args(argc, argv, &c);
    if (!c.rootfs && !c.image) c.rootfs = "/opt/rootfs";
    validate_input(&c);
    if ((!c.cmd || !c.cmd[0]) && !c.image) {
        print_usage(argv[0]);
        return 1;
    }
    return do_run(&c);
}