/* namespace_setup.c — namespace setup functions extracted from ns.c */
#include "namespace_setup.h"
#include <sys/sysmacros.h>
#include <linux/capability.h>
#include <sys/syscall.h>

static int path_join(char *out, size_t n, const char *dir, const char *name) {
    size_t dl = strlen(dir), nl = strlen(name);
    if (dl + 1 + nl + 1 > n) return -1;
    memcpy(out, dir, dl);
    out[dl] = '/';
    memcpy(out + dl + 1, name, nl + 1);
    return 0;
}

static int dev_mknod(const char *dp, mode_t mode, int ma, int mi) {
    if (mknod(dp, S_IFCHR | mode, makedev(ma, mi)) == 0) return 0;
    int e = errno;
    if (e == EEXIST) {
        struct stat st;
        if (lstat(dp, &st) < 0) return errno;
        if (S_ISDIR(st.st_mode)) return EEXIST;
        if (unlink(dp) < 0) return errno;
        if (mknod(dp, S_IFCHR | mode, makedev(ma, mi)) == 0) return 0;
        e = errno;
    }
    return e;
}

static int dev_bind_fallback(const char *dp, const char *host, mode_t mode) {
    struct stat hs;
    if (stat(host, &hs) < 0 || !S_ISCHR(hs.st_mode)) return ENOENT;

    int fd = open(dp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (fd < 0 && errno == EEXIST) {
        struct stat ts;
        if (lstat(dp, &ts) < 0) return errno;
        if (S_ISDIR(ts.st_mode) || unlink(dp) < 0) return errno;
        fd = open(dp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    }
    if (fd < 0) return errno;
    close(fd);
    if (mount(host, dp, NULL, MS_BIND, NULL) == 0) return 0;
    return errno;
}

static int dev_setup_one(const char *dp, const char *name, mode_t mode,
                         int major_num, int minor_num) {
    int e = dev_mknod(dp, mode, major_num, minor_num);
    if (e != EPERM && e != EACCES) return e;

    static const char *const safe_hosts[] = {
        "/dev/null", "/dev/zero", "/dev/random", "/dev/urandom",
        "/dev/tty", "/dev/console"
    };
    size_t index = 0;
    if (!strcmp(name, "zero")) index = 1;
    else if (!strcmp(name, "random")) index = 2;
    else if (!strcmp(name, "urandom")) index = 3;
    else if (!strcmp(name, "tty")) index = 4;
    else if (!strcmp(name, "console")) index = 5;
    return dev_bind_fallback(dp, safe_hosts[index], mode);
}

static void dev_link(const char *devdir, const char *tgt, const char *name) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    if (path_join(p, sizeof p, devdir, name) < 0) return;
    unlink(p);
    if (symlink(tgt, p) < 0) return;
}

static int mkdir_p(char *path) {
    for (char *s = path + 1; *s; s++) {
        if (*s != '/') continue;
        *s = '\0';
        if (mkdir(path, 0755) < 0 && errno != EEXIST) return -1;
        *s = '/';
    }
    if (mkdir(path, 0755) < 0 && errno != EEXIST) return -1;
    return 0;
}

static const char *const rw_dirs[][2] = {
    { "tmp",      "mode=1777,size=32M" },
    { "run",      "mode=755,size=8M" },
    { "var/log",  "mode=755,size=16M" },
    { "dev/shm",  "mode=1777,size=64M" },
};

static int path_has_symlink(const char *rootfs, const char *rel) {
    char cur[PATH_MAX + BUF_SIZE_SMALL], nxt[PATH_MAX + BUF_SIZE_SMALL], copy[PATH_MAX + BUF_SIZE_SMALL];
    struct stat st;
    if (strlen(rel) >= sizeof copy) return 1;
    snprintf(cur, sizeof cur, "%s", rootfs);
    snprintf(copy, sizeof copy, "%s", rel);
    for (char *tok = strtok(copy, "/"); tok; tok = strtok(NULL, "/")) {
        if (path_join(nxt, sizeof nxt, cur, tok) < 0) return 1;
        if (lstat(nxt, &st) == 0 && S_ISLNK(st.st_mode)) return 1;
        snprintf(cur, sizeof cur, "%s", nxt);
    }
    return 0;
}

static void ro_setup(const char *rootfs) {
    char p[PATH_MAX + BUF_SIZE_SMALL];
    if (strlen(rootfs) > PATH_MAX) die("--read-only: rootfs path too long");
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fprintf(stderr, "minicontainer: --read-only: bind rootfs failed: %s\n",
            strerror(errno));
        die("--read-only not usable with this rootfs");
    }
    for (size_t i = 0; i < sizeof rw_dirs / sizeof rw_dirs[0]; i++) {
        if (path_has_symlink(rootfs, rw_dirs[i][0])) continue;
        if (path_join(p, sizeof p, rootfs, rw_dirs[i][0]) < 0) continue;
        if (mkdir_p(p) < 0) continue;
        if (mount("tmpfs", p, "tmpfs", MS_NOSUID | MS_NODEV | MS_STRICTATIME,
            rw_dirs[i][1]) < 0 && errno != EBUSY)
            fprintf(stderr, "minicontainer: --read-only: /%s: %s\n",
                rw_dirs[i][0], strerror(errno));
    }
}

void namespace_setup_rootfs(const char *rootfs, int read_only, const char *hostname) {
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) die("make-private /");
    if (sethostname(hostname, strlen(hostname)) < 0) die("sethostname");
    if (read_only) ro_setup(rootfs);
}

void namespace_setup_dev(const char *rootfs) {
    char d[PATH_MAX + BUF_SIZE_SMALL], t[PATH_MAX + BUF_SIZE_SMALL];
    int edevice = 0;
    if (strlen(rootfs) > PATH_MAX) die("/dev path too long");
    if (path_join(d, sizeof d, rootfs, "dev") < 0) die("/dev path too long");
    if (mkdir(d, 0755) < 0 && errno != EEXIST) die("mkdir /dev");
    if (mount("tmpfs", d, "tmpfs", MS_NOSUID | MS_STRICTATIME,
        "mode=755,size=1M") < 0 && errno != EBUSY)
        die("mount /dev");
#define NODE(name, mode, ma, mi) \
    do { if (path_join(t, sizeof t, d, name) == 0) { \
          int e = dev_setup_one(t, name, mode, ma, mi); \
          if (e && (!strcmp(name, "null") || !strcmp(name, "zero") || \
                    !strcmp(name, "random") || !strcmp(name, "urandom"))) { \
              edevice = e; \
              fprintf(stderr, "minicontainer: failed to create /dev/%s: %s\n", \
                      name, strerror(e)); \
          } else if (e) { \
              fprintf(stderr, "minicontainer: warning: optional /dev/%s unavailable: %s\n", \
                      name, strerror(e)); \
          } } } while (0)
    NODE("null", 0666, 1, 3);
    NODE("zero", 0666, 1, 5);
    NODE("random", 0666, 1, 8);
    NODE("urandom", 0666, 1, 9);
    NODE("tty", 0666, 5, 0);
    NODE("console", 0600, 5, 1);
#undef NODE
    path_join(t, sizeof t, d, "shm"); mkdir(t, 0777);
    path_join(t, sizeof t, d, "pts"); mkdir(t, 0755);
    dev_link(d, "/proc/self/fd", "fd");
    dev_link(d, "/proc/self/fd/0", "stdin");
    dev_link(d, "/proc/self/fd/1", "stdout");
    dev_link(d, "/proc/self/fd/2", "stderr");
    if (edevice) die("required device unavailable in container");
}

void namespace_setup_ro(const char *rootfs) {
    /* This is a no-op at this level; the actual ro_setup was called in namespace_setup_rootfs.
     * The remount happens in namespace_remount_ro after chroot. */
    (void)rootfs;
}

void namespace_chroot(const char *rootfs) {
    if (chroot(rootfs) < 0) die("chroot");
    if (chdir("/") < 0) die("chdir /");
}

void namespace_mount_proc(void) {
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        if (errno != EBUSY) die("mount /proc");
    }
}

void namespace_setup_dns(const char *net) {
    if (strcmp(net, "host") == 0 || strcmp(net, "isolated") == 0) return;

    /* This runs after chroot in the child's private mount namespace.  Never
     * touch the host's rootfs from the parent. */
    FILE *f = fopen("/etc/resolv.conf", "r");
    char buf[BUF_SIZE_MEDIUM] = {0};
    if (f) {
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        if (ferror(f)) n = 0;
        buf[n] = '\0';
        fclose(f);
    }
    if (!buf[0] || !strstr(buf, "nameserver") || strstr(buf, "127.0.0.")) {
        fprintf(stderr, "minicontainer: bridge DNS not configured; provide nameserver in rootfs /etc/resolv.conf\n");
        return;
    }

    /* Ensure the bind target exists without following a rootfs symlink. */
    struct stat st;
    if (lstat("/etc", &st) < 0 && errno == ENOENT && mkdir("/etc", 0755) < 0) {
        fprintf(stderr, "minicontainer: cannot create /etc for container DNS: %s\n",
                strerror(errno));
        return;
    }
    int resolv_rc = lstat("/etc/resolv.conf", &st);
    if (resolv_rc < 0 && errno == ENOENT) {
        int fd = open("/etc/resolv.conf", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
        if (fd < 0) {
            fprintf(stderr, "minicontainer: cannot create DNS mount target: %s\n",
                    strerror(errno));
            return;
        }
        close(fd);
    } else if (resolv_rc < 0) {
        fprintf(stderr, "minicontainer: cannot inspect /etc/resolv.conf: %s\n",
                strerror(errno));
        return;
    } else if (S_ISLNK(st.st_mode)) {
        fprintf(stderr, "minicontainer: refusing symlinked /etc/resolv.conf\n");
        return;
    }

    /* Put the replacement on /dev (a container-private tmpfs), then bind it
     * over resolv.conf so the rootfs inode is never changed. */
    f = fopen("/dev/.minicontainer-resolv.conf", "w");
    if (!f) {
        fprintf(stderr, "minicontainer: cannot configure container DNS: %s\n",
                strerror(errno));
        return;
    }
    int bad = fputs(buf, f) == EOF;
    if (fclose(f) != 0) bad = 1;
    if (bad)
        fprintf(stderr, "minicontainer: cannot write container DNS: %s\n",
                strerror(errno));
    else if (mount("/dev/.minicontainer-resolv.conf", "/etc/resolv.conf",
                   NULL, MS_BIND, NULL) < 0)
        fprintf(stderr, "minicontainer: cannot install container DNS: %s\n",
                strerror(errno));
}

int namespace_remount_ro(const char *rootfs) {
    if (mount(NULL, "/", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL) < 0) {
        if (errno == EPERM)
            fprintf(stderr, "minicontainer: --read-only: rootfs on tmpfs"
                " (kernel does not allow read-only bind remount on tmpfs)."
                " Use rootfs on ext4, e.g. /opt/rootfs.\n");
        return -1;
    }
    return 0;
}

void namespace_install_seccomp(int seccomp_mode) {
    if (seccomp_mode == 1) install_filter_strict();
    else if (seccomp_mode == 0) install_filter();
    /* mode 2 (off): no filter installed */
}

void namespace_drop_capabilities(void) {
#ifdef CAP_LAST_CAP
    for (int cap = 0; cap <= CAP_LAST_CAP; cap++)
        if (cap != CAP_NET_RAW && cap != CAP_NET_BIND_SERVICE &&
            prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) < 0 && errno != EINVAL)
            die("drop capability bounding set");
#endif
    struct __user_cap_header_struct hdr = {
        .version = _LINUX_CAPABILITY_VERSION_3, .pid = 0
    };
    struct __user_cap_data_struct caps[2] = {{0}, {0}};
    caps[0].permitted = caps[0].effective =
        (1U << CAP_NET_RAW) | (1U << CAP_NET_BIND_SERVICE);
    if (syscall(SYS_capset, &hdr, caps) < 0) die("clear capabilities");
    if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0 && errno != EINVAL)
        die("clear ambient capabilities");
}

void namespace_child_setup(struct cfg *c, int detach) {
    /* The parent maps uid 0 before releasing the child.  Become mapped root
     * before any operation requiring namespace capabilities. */
    if (setgid(0) < 0) die("setgid");
    if (setuid(0) < 0) die("setuid");

    namespace_setup_rootfs(c->rootfs, c->read_only, c->hostname);
    namespace_setup_dev(c->rootfs);

    namespace_chroot(c->rootfs);
    namespace_setup_dns(c->net);
    namespace_mount_proc();

    if (c->read_only && namespace_remount_ro(c->rootfs) < 0)
        die("remount rootfs read-only");

    /* Detach: redirect stdio to /dev/null BEFORE exec */
    if (detach) {
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) die("open /dev/null");
        dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
        if (fd > 2) close(fd);
    }

    /* RLIMIT_FSIZE hard cap -- best-effort disk quota */
    if (c->max_disk > 0) {
        struct rlimit rl = { .rlim_cur = (rlim_t)c->max_disk, .rlim_max = (rlim_t)c->max_disk };
        setrlimit(RLIMIT_FSIZE, &rl);
    }

    /* Drop setup capabilities only after all mounts, mknod, and chroot work. */
    namespace_drop_capabilities();

    /* Install seccomp filter AFTER setup, BEFORE execvp */
    namespace_install_seccomp(c->seccomp_mode);
}
