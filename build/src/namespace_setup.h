/* namespace_setup.h — namespace setup functions extracted from ns.c for reuse by exec.c */
#pragma once
#include "common.h"

/* Setup rootfs bind mount, hostname, and read-only preparation.
 * Must be called before chroot. */
void namespace_setup_rootfs(const char *rootfs, int read_only, const char *hostname);

/* Setup /dev tmpfs and device nodes.
 * Must be called before chroot, after MS_PRIVATE on /. */
void namespace_setup_dev(const char *rootfs);

/* Setup read-only remount of rootfs (bind mount + tmpfs overlays for writable dirs).
 * Must be called before chroot. */
void namespace_setup_ro(const char *rootfs);

/* Perform chroot to rootfs and chdir to /. */
void namespace_chroot(const char *rootfs);

/* Mount /proc inside the chroot. */
void namespace_mount_proc(void);

/* Remount rootfs read-only after /proc is mounted (inside chroot).
 * Returns 0 on success, -1 on error (with error message printed). */
int namespace_remount_ro(const char *rootfs);

/* Install seccomp filter based on mode:
 * 0 = default deny-list, 1 = strict allow-list, 2 = off. */
void namespace_install_seccomp(int seccomp_mode);
void namespace_drop_capabilities(void);

/* Full setup sequence for child_fn (convenience function).
 * Calls all the above in the correct order. */
void namespace_child_setup(struct cfg *c, int detach);
void namespace_setup_dns(const char *net);
