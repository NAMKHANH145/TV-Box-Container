/* preflight.c — `check`: kiem tra moi truong truoc khi cai hoac chay.
 *
 * Khong tao namespace, khong can cgroup: chi doc /proc, /sys va thu 1 xattr.
 * Exit 0 neu du dieu kien bat buoc, 1 neu thieu (in ro thieu cai gi). */
#include "common.h"
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>

struct item {
    const char *name;
    char detail[BUF_SIZE_MEDIUM];
    int ok;
    int required;
};

static int file_has(const char *path, const char *needle) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    static char buf[BUF_SIZE_HUGE];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0) return 0;
    buf[r] = '\0';
    return strstr(buf, needle) != NULL;
}

static long read_long(const char *path) {
    char b[BUF_SIZE_SMALL];
    if (read_small(path, b, sizeof b) < 0) return -1;
    return atol(b);
}

/* xattr is what preserves file capabilities inside image layers. */
static int xattr_ok(const char *dir) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/.mc-xattr-%d", dir, (int)getpid());
    int fd = open(p, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return 0;
    close(fd);
    int ok = setxattr(p, "user.mc", "1", 1, 0) == 0;
    unlink(p);
    return ok;
}

static void human(const struct item *it, int n) {
    for (int i = 0; i < n; i++) {
        const char *st = it[i].ok ? "ok" : (it[i].required ? "THIEU" : "canh bao");
        printf("%-12s %-28s %s\n", it[i].name, it[i].detail, st);
    }
}

static void json_out(const struct item *it, int n) {
    printf("{");
    for (int i = 0; i < n; i++) {
        printf("%s\"%s\":{\"ok\":%s,\"required\":%s,\"detail\":\"%s\"}",
               i ? "," : "", it[i].name, it[i].ok ? "true" : "false",
               it[i].required ? "true" : "false", it[i].detail);
    }
    printf("}\n");
}

int do_check(int json) {
    struct item it[12];
    int n = 0;
    struct utsname u;
    if (uname(&u) != 0) { snprintf(u.release, sizeof u.release, "unknown"); }

    /* arch — always true: this binary is running on this machine */
    it[n] = (struct item){ .name = "arch", .ok = 1, .required = 1 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", MC_ARCH);

    /* kernel >= 5.0 (cgroup v2 + userns are usable) */
    int maj = 0, min = 0;
    sscanf(u.release, "%d.%d", &maj, &min);
    it[n] = (struct item){ .name = "kernel", .ok = maj >= 5, .required = 1 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", u.release);

    /* cgroup v2 */
    int cg = access("/sys/fs/cgroup/cgroup.controllers", R_OK) == 0;
    it[n] = (struct item){ .name = "cgroup v2", .ok = cg, .required = 1 };
    if (cg) {
        char b[BUF_SIZE_MEDIUM] = "";
        read_small("/sys/fs/cgroup/cgroup.controllers", b, sizeof b);
        b[strcspn(b, "\n")] = '\0';
        snprintf(it[n].detail, BUF_SIZE_MEDIUM, "%s", b[0] ? b : "yes");
    } else {
        snprintf(it[n].detail, BUF_SIZE_MEDIUM, "khong co (can cgroup v2)");
    }
    n++;

    /* user namespaces */
    long maxns = read_long("/proc/sys/user/max_user_namespaces");
    long clone_ok = read_long("/proc/sys/kernel/unprivileged_userns_clone");
    int ns = (maxns > 0) || (clone_ok == 1);
    it[n] = (struct item){ .name = "userns", .ok = ns, .required = 1 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", ns ? "enabled" : "disabled");

    /* overlayfs — khong bat buoc: image store giu rootfs da merge (khong overlay).
     * Nhieu kernel (vd. meson64 tren S905W) co overlay duoi dang module chua load,
     * nen thieu overlay la canh bao, khong phai ly do chan chay. */
    int ovl = file_has("/proc/filesystems", "overlay");
    it[n] = (struct item){ .name = "overlayfs", .ok = ovl, .required = 0 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", ovl ? "yes" : "khong co (khong dung)");

    /* xattr tren thu muc du lieu */
    const char *xdir = access("/var/lib", W_OK) == 0 ? "/var/lib" : "/tmp";
    int xa = xattr_ok(xdir);
    it[n] = (struct item){ .name = "xattr", .ok = xa, .required = 1 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s tren %s", xa ? "yes" : "no", xdir);

    /* root — chroot + cgroup deu can */
    int root = geteuid() == 0;
    it[n] = (struct item){ .name = "root", .ok = root, .required = 1 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", root ? "yes" : "no (chay bang sudo)");

    /* iptables — chi can cho --net=bridge */
    int ipt = access("/usr/sbin/iptables", X_OK) == 0 ||
              access("/sbin/iptables", X_OK) == 0 ||
              access("/usr/bin/iptables", X_OK) == 0;
    it[n] = (struct item){ .name = "iptables", .ok = ipt, .required = 0 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", ipt ? "yes" : "no");

    /* curl — chi can cho `pull <image>` tu registry (P2) */
    int curl = access("/usr/bin/curl", X_OK) == 0 || access("/usr/local/bin/curl", X_OK) == 0;
    it[n] = (struct item){ .name = "curl", .ok = curl, .required = 0 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%s", curl ? "yes" : "no (can cho pull registry)");

    /* RAM >= 256 MB */
    long mem_kb = -1;
    {
        char b[BUF_SIZE_MEDIUM];
        if (read_small("/proc/meminfo", b, sizeof b) == 0)
            sscanf(b, "MemTotal: %ld kB", &mem_kb);
    }
    it[n] = (struct item){ .name = "memory", .ok = mem_kb >= 262144, .required = 0 };
    if (mem_kb > 0) snprintf(it[n].detail, BUF_SIZE_MEDIUM, "%ld MB", mem_kb / 1024);
    else snprintf(it[n].detail, BUF_SIZE_MEDIUM, "unknown");
    n++;

    /* disk free >= 200 MB */
    struct statvfs vfs;
    const char *dpath = access("/var/lib", W_OK) == 0 ? "/var/lib" : "/";
    unsigned long long freeb = 0;
    if (statvfs(dpath, &vfs) == 0) freeb = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
    it[n] = (struct item){ .name = "disk free", .ok = freeb >= 200ULL * 1024 * 1024, .required = 0 };
    snprintf(it[n++].detail, BUF_SIZE_MEDIUM, "%llu MB tren %s", freeb / (1024 * 1024), dpath);

    if (json) {
        json_out(it, n);
    } else {
        printf("minicontainer %s — kiem tra moi truong\n\n", MC_VERSION);
        human(it, n);
        printf("\n");
        int bad = 0;
        for (int i = 0; i < n; i++)
            if (it[i].required && !it[i].ok) {
                printf("=> THIEU: %s\n", it[i].name);
                bad = 1;
            }
        if (!bad) printf("=> san sang chay container (arch %s)\n", MC_ARCH);
    }

    for (int i = 0; i < n; i++)
        if (it[i].required && !it[i].ok) return 1;
    return 0;
}
