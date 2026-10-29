/* systemd.c — generate systemd unit file for service create */
#include "common.h"

int do_service_create(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s service create <name> <cmd> [args...]\n", argv[0]);
		return 1;
	}
	const char *name = argv[3];
	if (!valid_name(name)) {
		fprintf(stderr, "service name must contain only alphanumeric, dash, underscore\n");
		return 1;
	}
	struct cfg c = {
		.ram = 32*1024*1024, .cpu = 20, .pids = 64, .io = 1024*1024,
		.detach = 0, .net = "host", .rootfs = "/opt/rootfs",
		.hostname = "minicontainer-os", .name = NULL, .health_cmd = NULL
	};
	c.cmd = &argv[4];
	if (!c.cmd[0]) {
		fprintf(stderr, "service create needs at least one command\n");
		return 1;
	}

	char cmd[2048]; size_t o = 0;
	for (char **a = c.cmd; *a; a++) {
		if (o) cmd[o++] = ' ';
		size_t l = strlen(*a);
		if (o + l > sizeof cmd - 1) l = sizeof cmd - 1 - o;
		memcpy(cmd + o, *a, l); o += l;
	}
	cmd[o] = 0;

	char buf[4096];
	snprintf(buf, sizeof buf,
		"[Unit]\n"
		"Description=%s container (minicontainer)\n"
		"After=network.target\n\n"
		"[Service]\n"
		"Type=simple\n"
		"ExecStart=/usr/local/bin/minicontainer run --name=%s --ram=32M --cpu=20 --pids=64 --io=1M --rootfs=/opt/rootfs %s\n"
		"Restart=always\n"
		"RestartSec=5\n\n"
		"[Install]\n"
		"WantedBy=multi-user.target\n",
		name, name, cmd);

	char path[256];
	snprintf(path, sizeof path, "/etc/systemd/system/minicontainer-%s.service", name);
	write_file(path, buf);
	printf("%s service generated at %s\n", name, path);
	printf("Enable with: systemctl daemon-reload && systemctl enable --now minicontainer-%s.service\n", name);
	return 0;
}