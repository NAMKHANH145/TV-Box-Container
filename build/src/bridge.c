/* bridge.c — R5-3: --net=bridge.
 *
 * Shared bridge `mcbr0` (10.88.0.1/16) + one veth pair per container.
 * Host uses `ip` (iproute2) to create/move veth into container netns; container
 * configures interface via ioctl (no `ip` needed in rootfs).
 * Has NAT (masquerade) for container Internet access, and --publish HOST:CONTAINER
 * (DNAT) to accept connections from LAN. Cleanup on stop/rm. No daemon. */
#include "common.h"
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/route.h>

static const char *tool_path(const char *name) {
    static char path[PATH_MAX];
    static const char *const dirs[] = { "/usr/sbin", "/usr/bin", "/sbin", "/bin" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dirs[i], name);
        if (access(path, X_OK) == 0) return path;
    }
    return NULL;
}

static int run_ip(char *const argv[]) {
    const char *path = tool_path("ip");
    if (!path) return -1;
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        execv(path, argv);
        _exit(EXIT_CHILD_ERROR);
    }
    int st;
    if (waitpid(p, &st, 0) < 0) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

/* iptables wrapper (for NAT/DNAT). Returns 0 if command OK. */
static int run_ipt(char *const argv[]) {
    const char *path = tool_path("iptables");
    if (!path) return -1;
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
        execv(path, argv);
        _exit(EXIT_CHILD_ERROR);
    }
    int st;
    if (waitpid(p, &st, 0) < 0) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

static int has_iptables(void) {
    char *av[] = { "/usr/sbin/iptables", "--version", NULL };
    return run_ipt(av) == 0;
}

static void enable_ip_forward(void) {
    int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
    if (fd >= 0) { if (write(fd, "1", 1) != 1) { /* best effort */ } close(fd); }
}

/* Find interface with default route (read /proc/net/route, no `ip` needed). */
static void default_iface(char *out, size_t n) {
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) { snprintf(out, n, "eth0"); return; }
    char line[BUF_SIZE_MEDIUM];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; } /* header */
    while (fgets(line, sizeof line, f)) {
        char iface[64]; unsigned dest;
        if (sscanf(line, "%63s %x", iface, &dest) == 2 && dest == 0) {
            snprintf(out, n, "%s", iface);
            fclose(f);
            return;
        }
    }
    fclose(f);
    snprintf(out, n, "eth0");
}

static void ipt_add(char *const av[]) { run_ipt(av); }  /* best-effort */

static void nat_setup(const char *container_ip, char **publish, int npublish) {
    if (!has_iptables()) {
        fprintf(stderr, "minicontainer: --net=bridge: no iptables -> container cannot reach Internet/NAT. "
                    "Install iptables to enable NAT + port mapping.\n");
        return;
    }
    enable_ip_forward();

    char uplink[64];
    default_iface(uplink, sizeof uplink);

    /* Masquerade entire bridge subnet -> uplink. */
    char *masq[] = { "iptables", "-t", "nat", "-A", "POSTROUTING",
                      "-s", "10.88.0.0/16", "-o", uplink, "-j", "MASQUERADE", NULL };
    ipt_add(masq);

    /* Allow forward for bridge subnet (both directions). */
    char *fwd_out[] = { "iptables", "-A", "FORWARD", "-s", "10.88.0.0/16", "-j", "ACCEPT", NULL };
    char *fwd_in[]  = { "iptables", "-A", "FORWARD", "-d", "10.88.0.0/16",
                        "-m", "state", "--state", "ESTABLISHED,RELATED", "-j", "ACCEPT", NULL };
    ipt_add(fwd_out);
    ipt_add(fwd_in);

    /* Port mapping: -p hostport:containerport (TCP). */
    for (int i = 0; i < npublish; i++) {
        char spec[BUF_SIZE_SMALL];
        snprintf(spec, sizeof spec, "%s", publish[i]);
        char *colon = strchr(spec, ':');
        if (!colon) {
            fprintf(stderr, "minicontainer: --publish '%s' invalid format (need HOST:CONTAINER)\n", publish[i]);
            continue;
        }
        *colon = '\0';
        const char *hp = spec;
        const char *cp = colon + 1;
        char *dnat[] = { "iptables", "-t", "nat", "-A", "PREROUTING",
                         "-p", "tcp", "--dport", (char *)hp,
                         "-j", "DNAT", "--to-destination", NULL, NULL, NULL };
        /* build "--to-destination IP:PORT" */
        static char todest[BUF_SIZE_MEDIUM];
        snprintf(todest, sizeof todest, "%s:%s", container_ip, cp);
        dnat[12] = todest;
        ipt_add(dnat);
        char *allow[] = { "iptables", "-A", "FORWARD", "-p", "tcp", "-d", (char *)container_ip,
                          "--dport", (char *)cp, "-j", "ACCEPT", NULL };
        ipt_add(allow);
    }
}

static void nat_teardown(const char *container_ip, char **publish, int npublish) {
    if (!has_iptables()) return;
    char uplink[64];
    default_iface(uplink, sizeof uplink);

    char *masq[] = { "iptables", "-t", "nat", "-D", "POSTROUTING",
                      "-s", "10.88.0.0/16", "-o", uplink, "-j", "MASQUERADE", NULL };
    run_ipt(masq);
    char *fwd_out[] = { "iptables", "-D", "FORWARD", "-s", "10.88.0.0/16", "-j", "ACCEPT", NULL };
    char *fwd_in[]  = { "iptables", "-D", "FORWARD", "-d", "10.88.0.0/16",
                        "-m", "state", "--state", "ESTABLISHED,RELATED", "-j", "ACCEPT", NULL };
    run_ipt(fwd_out);
    run_ipt(fwd_in);

    for (int i = 0; i < npublish; i++) {
        char buf[BUF_SIZE_SMALL];
        snprintf(buf, sizeof buf, "%s", publish[i]);
        char *colon = strchr(buf, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *hp = buf;
        const char *cp = colon + 1;
        static char todest[BUF_SIZE_MEDIUM];
        snprintf(todest, sizeof todest, "%s:%s", container_ip, cp);
        char *dnat[] = { "iptables", "-t", "nat", "-D", "PREROUTING",
                         "-p", "tcp", "--dport", (char *)hp,
                         "-j", "DNAT", "--to-destination", todest, NULL };
        run_ipt(dnat);
        char *allow[] = { "iptables", "-D", "FORWARD", "-p", "tcp", "-d", (char *)container_ip,
                          "--dport", (char *)cp, "-j", "ACCEPT", NULL };
        run_ipt(allow);
    }
}

static int ip_exists(const char *dev) {
    char *av[] = { "ip", "link", "show", (char *)dev, NULL };
    return run_ip(av) == 0;
}

static int ensure_bridge(void) {
    if (ip_exists(MC_BRIDGE)) return 0;
    char *add[] = { "ip", "link", "add", MC_BRIDGE, "type", "bridge", NULL };
    if (run_ip(add) < 0 && !ip_exists(MC_BRIDGE)) return -1;
    char *addr[] = { "ip", "addr", "add", MC_BR_CIDR, "dev", MC_BRIDGE, NULL };
    run_ip(addr);                       /* EADDRINUSE on race: ignore */
    char *up[] = { "ip", "link", "set", MC_BRIDGE, "up", NULL };
    run_ip(up);
    return ip_exists(MC_BRIDGE) ? 0 : -1;
}

/* IP assigned by host PID: unique while container lives, no pool needed. */
static void ip_for_pid(pid_t pid, char *out, size_t n) {
    int a = (pid >> 8) & 0xff, b = pid & 0xff;
    if (b == 0 || b == 255 || (a == 0 && b <= 1)) b = BRIDGE_IP_MIN_OCTET;
    snprintf(out, n, "10.88.%d.%d", a, b);
}

int bridge_setup(pid_t child, struct bridge_cfg *bc, char **publish, int npublish) {
    char host[IFNAMSIZ], peer[IFNAMSIZ];
    snprintf(host, sizeof host, "mcv%u", (unsigned)child);
    snprintf(peer, sizeof peer, "mcc%u", (unsigned)child);

    if (ensure_bridge() < 0) return -1;

    char *mk[] = { "ip", "link", "add", host, "type", "veth", "peer", "name", peer, NULL };
    if (run_ip(mk) < 0) return -1;

    char pidstr[BUF_SIZE_SMALL];
    snprintf(pidstr, sizeof pidstr, "%u", (unsigned)child);
    char *mv[] = { "ip", "link", "set", peer, "netns", pidstr, NULL };
    if (run_ip(mv) < 0) goto fail;
    char *master[] = { "ip", "link", "set", host, "master", MC_BRIDGE, NULL };
    if (run_ip(master) < 0) goto fail;
    char *up[] = { "ip", "link", "set", host, "up", NULL };
    if (run_ip(up) < 0) goto fail;

    memset(bc, 0, sizeof *bc);
    snprintf(bc->peer, sizeof bc->peer, "%s", peer);
    snprintf(bc->gw, sizeof bc->gw, "%s", MC_BR_GW);
    ip_for_pid(child, bc->ip, sizeof bc->ip);
    bc->prefix = MC_BR_PREFIX;
    nat_setup(bc->ip, publish, npublish);
    return 0;

fail:
    char *del[] = { "ip", "link", "del", host, NULL };
    run_ip(del);
    return -1;
}

/* Remove host side on error; container side disappears with netns. */
void bridge_teardown(pid_t child, char **publish, int npublish) {
    char host[IFNAMSIZ], peer[IFNAMSIZ];
    snprintf(host, sizeof host, "mcv%u", (unsigned)child);
    snprintf(peer, sizeof peer, "mcc%u", (unsigned)child);
    char ip[BUF_SIZE_SMALL];
    ip_for_pid(child, ip, sizeof ip);
    nat_teardown(ip, publish, npublish);
    /* Deleting either end removes the pair; try both in case the peer never
     * moved into the container netns (bridge_setup failed halfway). */
    char *delh[] = { "ip", "link", "del", host, NULL };
    if (run_ip(delh) < 0) {
        char *delp[] = { "ip", "link", "del", peer, NULL };
        run_ip(delp);
    }
}

static int set_addr(int fd, const char *ifname, const char *ip, int prefix) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &sin->sin_addr) != 1) return -1;
    if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) return -1;
    sin->sin_addr.s_addr = htonl(prefix >= 8 ? 0xffffffffU << (32 - prefix) : 0);
    if (ioctl(fd, SIOCSIFNETMASK, &ifr) < 0) return -1;
    return 0;
}

static int set_updown(int fd, const char *ifname, int up) {
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) return -1;
    ifr.ifr_flags = up ? (short)(ifr.ifr_flags | IFF_UP | IFF_RUNNING)
                       : (short)(ifr.ifr_flags & ~IFF_UP);
    return ioctl(fd, SIOCSIFFLAGS, &ifr) < 0 ? -1 : 0;
}

static int add_default_route(int fd, const char *ifname, const char *gw) {
    struct rtentry rt;
    memset(&rt, 0, sizeof rt);
    struct sockaddr_in *d = (struct sockaddr_in *)&rt.rt_dst;
    struct sockaddr_in *g = (struct sockaddr_in *)&rt.rt_gateway;
    struct sockaddr_in *m = (struct sockaddr_in *)&rt.rt_genmask;
    d->sin_family = g->sin_family = m->sin_family = AF_INET;
    if (inet_pton(AF_INET, gw, &g->sin_addr) != 1) return -1;
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    rt.rt_dev = (char *)ifname;
    return ioctl(fd, SIOCADDRT, &rt) < 0 ? -1 : 0;
}

int bridge_configure(const struct bridge_cfg *bc) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", bc->peer);
    snprintf(ifr.ifr_newname, IFNAMSIZ, "eth0");
    if (ioctl(fd, SIOCSIFNAME, &ifr) < 0) { close(fd); return -1; }

    int rc = set_addr(fd, "eth0", bc->ip, bc->prefix);
    if (rc == 0) rc = set_updown(fd, "eth0", 1);
    if (rc == 0) rc = add_default_route(fd, "eth0", bc->gw);
    close(fd);
    return rc;
}

/* New netns has lo DOWN: everything connecting to 127.0.0.1
 * (health check, same-container client) will fail. Bring lo up. */
int netns_lo_up(void) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int rc = set_updown(fd, "lo", 1);
    close(fd);
    return rc;
}

/* Serialize bridge_cfg over pipe (ABI-safe: length-prefixed fields, not raw struct) */
int bridge_cfg_write(int fd, const struct bridge_cfg *bc) {
    uint32_t len;
    ssize_t w;

    len = strlen(bc->peer);
    if (write(fd, &len, sizeof len) != sizeof len) return -1;
    if ((w = write(fd, bc->peer, len)) != (ssize_t)len) return -1;

    len = strlen(bc->ip);
    if (write(fd, &len, sizeof len) != sizeof len) return -1;
    if ((w = write(fd, bc->ip, len)) != (ssize_t)len) return -1;

    len = strlen(bc->gw);
    if (write(fd, &len, sizeof len) != sizeof len) return -1;
    if ((w = write(fd, bc->gw, len)) != (ssize_t)len) return -1;

    if (write(fd, &bc->prefix, sizeof bc->prefix) != sizeof bc->prefix) return -1;

    return 0;
}

int bridge_cfg_read(int fd, struct bridge_cfg *bc) {
    uint32_t len;
    ssize_t r;

    memset(bc, 0, sizeof *bc);

    if ((r = read(fd, &len, sizeof len)) != sizeof len) return -1;
    if (len >= sizeof bc->peer) return -1;
    if ((r = read(fd, bc->peer, len)) != (ssize_t)len) return -1;
    bc->peer[len] = '\0';

    if ((r = read(fd, &len, sizeof len)) != sizeof len) return -1;
    if (len >= sizeof bc->ip) return -1;
    if ((r = read(fd, bc->ip, len)) != (ssize_t)len) return -1;
    bc->ip[len] = '\0';

    if ((r = read(fd, &len, sizeof len)) != sizeof len) return -1;
    if (len >= sizeof bc->gw) return -1;
    if ((r = read(fd, bc->gw, len)) != (ssize_t)len) return -1;
    bc->gw[len] = '\0';

    if ((r = read(fd, &bc->prefix, sizeof bc->prefix)) != sizeof bc->prefix) return -1;

    return 0;
}
