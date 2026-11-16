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
    /* Best effort: the host-side prep already made it; a --read-only rootfs
     * makes this fail with EROFS/EACCES and the mount below reports properly. */
    if (mkdir("/proc", 0555) < 0 && errno != EEXIST && errno != EACCES &&
        errno != EPERM && errno != EROFS)
        die("mkdir /proc");
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        if (errno != EBUSY) die("mount /proc");
    }
}

/* P1.5: OCI mounts[] / --mount type:source:dest[:options].
 * Runs after chroot, so `dest` is a path inside the container. /proc, /dev and
 * /sys are already provided by the setup (mounting a tmpfs over /dev would
 * hide the device nodes we just created), so those destinations are skipped. */
/* mode 1: only bind mounts, applied BEFORE chroot with the host rootfs as
 * prefix (their source is a host path). mode 2: only non-bind mounts, applied
 * after chroot. */
static int spec_split(const char *spec, char *buf, size_t n,
                      char **type, char **src, char **dst, char **opts) {
    if (strlen(spec) >= n) return -1;
    snprintf(buf, n, "%s", spec);
    *type = buf;
    char *c = strchr(buf, ':');
    if (!c) return -1;
    *c++ = 0;
    *src = c;
    c = strchr(c, ':');
    if (!c) return -1;
    *c++ = 0;
    *dst = c;
    *opts = strchr(c, ':');
    if (*opts) { **opts = 0; (*opts)++; }
    return 0;
}

/* Absolute path with no '.'/'..' components; used before touching the host. */
static int safe_abs_path(const char *p) {
    if (!p || p[0] != '/') return 0;
    for (const char *s = p; *s;) {
        while (*s == '/') s++;
        if (!*s) break;
        const char *e = strchr(s, '/');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if ((l == 1 && s[0] == '.') || (l == 2 && s[0] == '.' && s[1] == '.')) return 0;
        s = e ? e + 1 : s + l;
    }
    return 1;
}

/* Create dst inside rootfs without following any symlink (parent is host
 * root, so a symlink escape would create host directories). */
static int mkdir_in_root(const char *rootfs, const char *dst, mode_t mode) {
    int fd = open(rootfs, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[PATH_MAX];
    if (strlen(dst) >= sizeof buf) { close(fd); return -1; }
    snprintf(buf, sizeof buf, "%s", dst);
    char *save = NULL;
    for (char *comp = strtok_r(buf, "/", &save); comp; comp = strtok_r(NULL, "/", &save)) {
        int next = openat(fd, comp, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            if (errno != ENOENT) { close(fd); return -1; }
            if (mkdirat(fd, comp, mode) < 0 && errno != EEXIST) { close(fd); return -1; }
            next = openat(fd, comp, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) { close(fd); return -1; }
        }
        close(fd);
        fd = next;
    }
    close(fd);
    return 0;
}

/* Called in the parent (host root) before clone: mount targets under a
 * root-owned rootfs cannot be created by the mapped-root child. Also makes
 * sure /etc/hosts exists so the child can bind its generated file over it. */
int namespace_prepare_filesystem(struct cfg *c) {
    /* /proc is a mount point, not something every image ships (busybox:latest
     * has none). Create it here, as the host-root parent: the child runs as
     * the mapped root (uid 0 -> 65534) and cannot mkdir inside a root-owned
     * image rootfs. */
    if (mkdir_in_root(c->rootfs, "/proc", 0555) < 0) {
        fprintf(stderr, "minicontainer: cannot create /proc mount point: %s\n", strerror(errno));
        return -1;
    }
    for (int i = 0; i < c->nmounts; i++) {
        char buf[PATH_MAX * 3], *type, *src, *dst, *opts;
        if (spec_split(c->mounts[i], buf, sizeof buf, &type, &src, &dst, &opts) < 0) {
            fprintf(stderr, "minicontainer: --mount wants type:src:dst[:opts]\n");
            return -1;
        }
        if (!strcmp(dst, "/proc") || !strcmp(dst, "/dev") || !strcmp(dst, "/sys")) continue;
        if (!safe_abs_path(dst)) {
            fprintf(stderr, "minicontainer: unsafe mount destination: %s\n", dst);
            return -1;
        }
        if (mkdir_in_root(c->rootfs, dst, 0755) < 0) {
            fprintf(stderr, "minicontainer: cannot create mount point %s: %s\n", dst, strerror(errno));
            return -1;
        }
    }
    if (c->net && strcmp(c->net, "bridge") == 0) {
        if (mkdir_in_root(c->rootfs, "/etc", 0755) < 0) {
            fprintf(stderr, "minicontainer: cannot create /etc for hosts: %s\n", strerror(errno));
            return -1;
        }
        char hp[PATH_MAX + BUF_SIZE_SMALL];
        if (path_join(hp, sizeof hp, c->rootfs, "etc/hosts") < 0) return -1;
        struct stat st;
        if (lstat(hp, &st) < 0 && errno == ENOENT) {
            int fd = open(hp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
            if (fd < 0) {
                fprintf(stderr, "minicontainer: cannot create /etc/hosts: %s\n", strerror(errno));
                return -1;
            }
            close(fd);
        }
    }
    return 0;
}

static int mount_spec_one(const char *spec, const char *prefix, int mode) {
    char buf[PATH_MAX * 3];
    char *type, *src, *dst, *opts;
    if (spec_split(spec, buf, sizeof buf, &type, &src, &dst, &opts) < 0) {
        fprintf(stderr, "minicontainer: --mount wants type:src:dst[:opts]\n");
        return -1;
    }

    int is_bind = strcmp(type, "bind") == 0;
    if ((mode == 1) != is_bind) return 0;

    if (!safe_abs_path(dst)) { fprintf(stderr, "minicontainer: unsafe mount destination: %s\n", dst); return -1; }
    if (strcmp(dst, "/proc") == 0 || strcmp(dst, "/dev") == 0 || strcmp(dst, "/sys") == 0)
        return 0;

    unsigned long flags = 0;
    char data[BUF_SIZE_MEDIUM] = "";
    size_t dl = 0;
    if (opts) {
        char ob[BUF_SIZE_MEDIUM];
        snprintf(ob, sizeof ob, "%s", opts);
        for (char *o = strtok(ob, ","); o; o = strtok(NULL, ",")) {
            if (strcmp(o, "ro") == 0) flags |= MS_RDONLY;
            else if (strcmp(o, "nosuid") == 0) flags |= MS_NOSUID;
            else if (strcmp(o, "nodev") == 0) flags |= MS_NODEV;
            else if (strcmp(o, "noexec") == 0) flags |= MS_NOEXEC;
            else if (strcmp(o, "rbind") == 0) flags |= MS_BIND | MS_REC;
            else if (strcmp(o, "bind") == 0) flags |= MS_BIND;
            else if (strcmp(o, "sync") == 0) flags |= MS_SYNCHRONOUS;
            else {
                int w = snprintf(data + dl, sizeof data - dl, "%s%s", dl ? "," : "", o);
                if (w > 0) dl += (size_t)w;
            }
        }
    }
    char target[PATH_MAX * 2];
    int tlen = snprintf(target, sizeof target, "%s%s", prefix, dst);
    if (tlen < 0 || tlen >= (int)sizeof target) {
        fprintf(stderr, "minicontainer: mount dest too long: %s\n", dst);
        return -1;
    }
    char dircopy[PATH_MAX * 2];
    snprintf(dircopy, sizeof dircopy, "%s", target);
    if (mkdir_p(dircopy) < 0) {
        fprintf(stderr, "minicontainer: cannot create mount point %s: %s\n", target, strerror(errno));
        return -1;
    }

    if (is_bind) {
        if (mount(src, target, NULL, MS_BIND | (flags & MS_REC), NULL) < 0) {
            fprintf(stderr, "minicontainer: mount --bind %s -> %s: %s\n", src, target, strerror(errno));
            return -1;
        }
        if (flags & MS_RDONLY) mount(NULL, target, NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL);
        return 0;
    }
    const char *fstype = type;
    const char *source = src[0] ? src : type;
    if (mount(source, target, fstype, flags, data[0] ? data : NULL) < 0) {
        fprintf(stderr, "minicontainer: mount -t %s %s: %s\n", fstype, target, strerror(errno));
        return -1;
    }
    return 0;
}

void namespace_apply_bind_mounts(struct cfg *c) {
    for (int i = 0; i < c->nmounts; i++) {
        if (mount_spec_one(c->mounts[i], c->rootfs, 1) < 0) die("bind mount from config");
    }
}

void namespace_apply_mounts(struct cfg *c) {
    for (int i = 0; i < c->nmounts; i++) {
        if (mount_spec_one(c->mounts[i], "", 2) < 0) die("mount from config");
    }
}

/* P3.1: --ulimit name=soft[:hard]. Only the names Docker commonly exposes. */
static int rlimit_resource(const char *name, int *res) {
    static const struct { const char *n; int r; } map[] = {
        { "nofile", RLIMIT_NOFILE }, { "fsize", RLIMIT_FSIZE },
        { "nproc", RLIMIT_NPROC },  { "core", RLIMIT_CORE },
        { "stack", RLIMIT_STACK },  { "cpu", RLIMIT_CPU },
        { "as", RLIMIT_AS },        { "memlock", RLIMIT_MEMLOCK },
        { "msgqueue", RLIMIT_MSGQUEUE }, { "nice", RLIMIT_NICE },
        { "rtprio", RLIMIT_RTPRIO },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(name, map[i].n) == 0) { *res = map[i].r; return 0; }
    return -1;
}

static void namespace_apply_ulimits(struct cfg *c) {
    for (int i = 0; i < c->nulimits; i++) {
        char buf[BUF_SIZE_MEDIUM];
        snprintf(buf, sizeof buf, "%s", c->ulimits[i]);
        char *eq = strchr(buf, '=');
        if (!eq) die("--ulimit wants name=soft[:hard]");
        *eq = 0;
        int res = 0;
        if (rlimit_resource(buf, &res) < 0)
            die("--ulimit: unknown limit name");
        char *soft = eq + 1, *hard = strchr(soft, ':');
        if (hard) *hard++ = 0;
        errno = 0;
        char *end = NULL;
        unsigned long long s = strtoull(soft, &end, 10);
        if (errno || end == soft || *end) die("--ulimit: bad soft value");
        unsigned long long h = hard ? strtoull(hard, &end, 10) : s;
        if (hard && (errno || end == hard || *end)) die("--ulimit: bad hard value");
        if (h == 0) h = s;
        struct rlimit rl = { .rlim_cur = (rlim_t)s, .rlim_max = (rlim_t)h };
        if (setrlimit(res, &rl) < 0) {
            fprintf(stderr, "minicontainer: --ulimit %s: %s\n", c->ulimits[i], strerror(errno));
            die("--ulimit");
        }
    }
}

void namespace_setup_identity(struct cfg *c) {
    if (c->workdir && c->workdir[0]) {
        if (chdir(c->workdir) < 0) {
            fprintf(stderr, "minicontainer: cannot chdir to '%s': %s\n", c->workdir, strerror(errno));
            die("workdir");
        }
    }
    if (c->user && c->user[0]) {
        char buf[BUF_SIZE_SMALL];
        snprintf(buf, sizeof buf, "%s", c->user);
        char *colon = strchr(buf, ':');
        long uid = atol(buf);
        long gid = colon ? atol(colon + 1) : uid;
        if (colon) *colon = 0;
        /* Only ids present in the userns map can be assumed. */
        if (setgid((gid_t)gid) < 0 || setuid((uid_t)uid) < 0) {
            fprintf(stderr, "minicontainer: cannot switch to %ld:%ld inside userns: %s\n",
                    uid, gid, strerror(errno));
            die("--user");
        }
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

void namespace_install_seccomp(int seccomp_mode, const char *profile) {
    if (seccomp_mode == 1) install_filter_strict();
    else if (seccomp_mode == 0) install_filter();
    else if (seccomp_mode == SECCOMP_MODE_PROFILE) {
        if (!profile || seccomp_install_profile(profile) < 0)
            die("--seccomp profile");
    }
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
    /* P3.8: bind a static qemu into /dev before chroot when --emulate. */
    namespace_setup_emulator(c);
    /* P3.6: a host-side profile path is unreachable after chroot. */
    if (c->seccomp_mode == SECCOMP_MODE_PROFILE && c->seccomp_profile) {
        char dst[PATH_MAX + BUF_SIZE_SMALL];
        if (path_join(dst, sizeof dst, c->rootfs, "dev/.mc-seccomp.json") < 0)
            die("--seccomp profile path too long");
        int in = open(c->seccomp_profile, O_RDONLY | O_CLOEXEC);
        int out = in >= 0 ? open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600) : -1;
        if (in < 0 || out < 0) {
            fprintf(stderr, "minicontainer: --seccomp: cannot copy %s: %s\n",
                    c->seccomp_profile, strerror(errno));
            die("--seccomp profile");
        }
        char b[BUF_SIZE_XXLARGE];
        ssize_t r;
        while ((r = read(in, b, sizeof b)) > 0) {
            for (ssize_t off = 0; off < r;) {
                ssize_t w = write(out, b + off, (size_t)(r - off));
                if (w < 0) { if (errno == EINTR) continue; r = -1; break; }
                off += w;
            }
            if (r < 0) break;
        }
        close(in);
        close(out);
        if (r < 0) die("--seccomp profile copy");
        c->seccomp_profile = "/dev/.mc-seccomp.json";
    }
    /* P3.7: build /etc/hosts while the host state dir is still visible. */
    namespace_prepare_hosts(c);
    /* -v / --mount bind: the source is a host path, so mount before chroot. */
    namespace_apply_bind_mounts(c);

    namespace_chroot(c->rootfs);
    namespace_setup_dns(c->net);
    namespace_setup_hosts(c->net);
    namespace_mount_proc();
    namespace_apply_mounts(c);

    if (c->read_only && namespace_remount_ro(c->rootfs) < 0)
        die("remount rootfs read-only");

    namespace_setup_identity(c);

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

    /* P3.1: rlimits before seccomp so setrlimit is never filtered. */
    namespace_apply_ulimits(c);

    /* Drop setup capabilities only after all mounts, mknod, and chroot work. */
    namespace_drop_capabilities();

    /* Install seccomp filter AFTER setup, BEFORE execvp */
    namespace_install_seccomp(c->seccomp_mode, c->seccomp_profile);
}

/* P3.7: bridge peers resolve by container name. The file is prepared before
 * chroot (state lives on the host) and bind-mounted over /etc/hosts after. */
void namespace_prepare_hosts(const struct cfg *c) {
    if (!c->net || strcmp(c->net, "bridge") != 0) return;

    char path[PATH_MAX + BUF_SIZE_SMALL];
    if (path_join(path, sizeof path, c->rootfs, "dev/.mc-hosts") < 0) return;

    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "minicontainer: cannot prepare /etc/hosts: %s\n", strerror(errno));
        return;
    }
    fputs("127.0.0.1 localhost\n::1 localhost\n", out);

    /* Existing rootfs /etc/hosts entries stay valid. */
    char hp[PATH_MAX + BUF_SIZE_SMALL];
    if (path_join(hp, sizeof hp, c->rootfs, "etc/hosts") == 0) {
        FILE *in = fopen(hp, "r");
        if (in) {
            char line[BUF_SIZE_MEDIUM];
            while (fgets(line, sizeof line, in))
                if (line[0] && line[0] != '\n') fputs(line, out);
            fclose(in);
        }
    }

    DIR *d = opendir(STATE_BASE);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            if (c->name && strcmp(e->d_name, c->name) == 0) continue;
            if (!valid_name(e->d_name)) continue;
            char ipf[PATH_MAX + BUF_SIZE_SMALL], ip[BUF_SIZE_SMALL];
            if (fmt_path(ipf, sizeof ipf, STATE_BASE "/%s/ip", e->d_name) < 0) continue;
            if (read_small(ipf, ip, sizeof ip) < 0) continue;
            fprintf(out, "%s %s\n", ip, e->d_name);
        }
        closedir(d);
    }
    if (fclose(out) != 0)
        fprintf(stderr, "minicontainer: cannot write /etc/hosts: %s\n", strerror(errno));
}

void namespace_setup_hosts(const char *net) {
    if (!net || strcmp(net, "bridge") != 0) return;
    struct stat st;
    if (lstat("/etc", &st) < 0 && errno == ENOENT && mkdir("/etc", 0755) < 0) {
        fprintf(stderr, "minicontainer: cannot create /etc for hosts: %s\n", strerror(errno));
        return;
    }
    if (lstat("/etc/hosts", &st) == 0 && S_ISLNK(st.st_mode)) {
        fprintf(stderr, "minicontainer: refusing symlinked /etc/hosts\n");
        return;
    }
    if (lstat("/etc/hosts", &st) < 0) {
        if (errno == ENOENT) {
            int fd = open("/etc/hosts", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
            if (fd < 0) {
                fprintf(stderr, "minicontainer: cannot create /etc/hosts: %s\n", strerror(errno));
                return;
            }
            close(fd);
        } else {
            fprintf(stderr, "minicontainer: cannot inspect /etc/hosts: %s\n", strerror(errno));
            return;
        }
    }
    if (mount("/dev/.mc-hosts", "/etc/hosts", NULL, MS_BIND, NULL) < 0)
        fprintf(stderr, "minicontainer: cannot install /etc/hosts: %s\n", strerror(errno));
}

/* P3.8: --emulate. do_run picks the qemu binary and stores its host path in
 * emulate_arch; bind it into the container dev tmpfs before chroot. */
void namespace_setup_emulator(const struct cfg *c) {
    if (!c->emulate_arch[0]) return;
    char dst[PATH_MAX + BUF_SIZE_SMALL];
    if (path_join(dst, sizeof dst, c->rootfs, "dev/.mc-qemu") < 0) return;
    int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0755);
    if (fd < 0) {
        fprintf(stderr, "minicontainer: --emulate: cannot create %s: %s\n", dst, strerror(errno));
        die("--emulate");
    }
    close(fd);
    if (mount(c->emulate_arch, dst, NULL, MS_BIND, NULL) < 0) {
        fprintf(stderr, "minicontainer: --emulate: bind %s: %s\n", c->emulate_arch, strerror(errno));
        die("--emulate");
    }
}
