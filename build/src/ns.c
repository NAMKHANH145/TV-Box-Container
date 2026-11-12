/* ns.c — namespaces: clone child, chroot, user namespace map */
#include "common.h"
#include "namespace_setup.h"

int child_fn(void *arg) {
    struct child_arg *a = arg;
    struct cfg *c = a->c;
    char byte;

    if (write(a->ready_wr, "r", 1) != 1) die("ready");
    close(a->ready_wr);
    if (read(a->go_rd, &byte, 1) != 1) die("sync");

    /* --net=bridge: parent created veth and moved into our netns;
     * child configures interface via ioctl (no iproute2 needed in rootfs). */
    if (strcmp(c->net, "bridge") == 0) {
        struct bridge_cfg bc;
        if (bridge_cfg_read(a->go_rd, &bc) < 0) die("sync bridge");
        if (bridge_configure(&bc) < 0) die("bridge configure");
    }
    close(a->go_rd);

    /* New netns (isolated/bridge) starts with lo DOWN: everything connecting
     * to 127.0.0.1 will fail. Host uses host's lo, no need to touch. */
    if (strcmp(c->net, "host") != 0) netns_lo_up();

    /* Full namespace setup: rootfs, dev, chroot, proc, ro, seccomp */
    namespace_child_setup(c, a->detach);

    execvp(c->cmd[0], c->cmd);
    die("execvp");
    return 1;
}

void userns_map(pid_t child) {
    char p[BUF_SIZE_SMALL], v[BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "/proc/%d/setgroups", child);
    write_file(p, "deny");
    snprintf(p, sizeof p, "/proc/%d/uid_map", child);
    snprintf(v, sizeof v, UID_MAP_GID);
    write_file(p, v);
    snprintf(p, sizeof p, "/proc/%d/gid_map", child);
    write_file(p, v);
}