/* bridge.c — R5-3: --net=bridge.
 *
 * Mot bridge chung `mcbr0` (10.88.0.1/16) + mot cap veth per container.
 * Host dung `ip` (iproute2) de tao/di veth vao netns cua container; container
 * tu cau hinh interface bang ioctl (khong can `ip` trong rootfs).
 * Khong co daemon, khong co NAT: container noi duoc voi host va cac
 * container khac cung bridge.
 */
#include "common.h"
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/route.h>

#define MC_BRIDGE   "mcbr0"
#define MC_BR_CIDR  "10.88.0.1/16"
#define MC_BR_GW    "10.88.0.1"
#define MC_BR_PREFIX 16

static int run_ip(char *const argv[]) {
	pid_t p = fork();
	if (p < 0) return -1;
	if (p == 0) {
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
		execvp("ip", argv);
		_exit(127);
	}
	int st;
	if (waitpid(p, &st, 0) < 0) return -1;
	return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
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
	run_ip(addr);                       /* EADDRINUSE khi race: bo qua */
	char *up[] = { "ip", "link", "set", MC_BRIDGE, "up", NULL };
	run_ip(up);
	return ip_exists(MC_BRIDGE) ? 0 : -1;
}

/* IP dat theo host PID: unique khi container con song, khong can pool. */
static void ip_for_pid(pid_t pid, char *out, size_t n) {
	int a = (pid >> 8) & 0xff, b = pid & 0xff;
	if (b == 0 || b == 255 || (a == 0 && b <= 1)) b = 2;
	snprintf(out, n, "10.88.%d.%d", a, b);
}

int bridge_setup(pid_t child, struct bridge_cfg *bc) {
	char host[IFNAMSIZ], peer[IFNAMSIZ];
	snprintf(host, sizeof host, "mcv%u", (unsigned)child);
	snprintf(peer, sizeof peer, "mcc%u", (unsigned)child);

	if (ensure_bridge() < 0) return -1;

	char *mk[] = { "ip", "link", "add", host, "type", "veth", "peer", "name", peer, NULL };
	if (run_ip(mk) < 0) return -1;

	char pidstr[16];
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
	return 0;

fail:
	char *del[] = { "ip", "link", "del", host, NULL };
	run_ip(del);
	return -1;
}

/* Xoa phia host khi bi loi giua chung; phia container tu mat theo netns. */
void bridge_teardown(pid_t child) {
	char host[IFNAMSIZ];
	snprintf(host, sizeof host, "mcv%u", (unsigned)child);
	char *del[] = { "ip", "link", "del", host, NULL };
	run_ip(del);
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
