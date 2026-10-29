/* seccomp.c — deny-list BPF viet tay (khong libseccomp).
 * Chan syscall nguy hiem, tra EPERM (app van song).
 * Dung __NR_* cua libc hien tai + AUDIT_ARCH theo kien truc de binary
 * build o dau cung chay dung (aarch64/armv7l/x86_64/riscv64).
 * Cai SAU setup, TRUOC execvp. */
#include "common.h"
#include <sys/syscall.h>

#if defined(__x86_64__)
#define MC_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define MC_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__arm__)
#define MC_AUDIT_ARCH AUDIT_ARCH_ARM
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
#define MC_AUDIT_ARCH AUDIT_ARCH_RISCV64
#elif defined(__i386__)
#define MC_AUDIT_ARCH AUDIT_ARCH_I386
#else
#error "minicontainer: unsupported architecture for seccomp filter"
#endif

#define DENY(nr) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ERRNO | EPERM)

void install_filter(void) {
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, MC_AUDIT_ARCH, 1, 0),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, nr)),
#ifdef __NR_mount
		DENY(__NR_mount),
#endif
#ifdef __NR_umount2
		DENY(__NR_umount2),
#endif
#ifdef __NR_pivot_root
		DENY(__NR_pivot_root),
#endif
#ifdef __NR_reboot
		DENY(__NR_reboot),
#endif
#ifdef __NR_kexec_load
		DENY(__NR_kexec_load),
#endif
#ifdef __NR_kexec_file_load
		DENY(__NR_kexec_file_load),
#endif
#ifdef __NR_init_module
		DENY(__NR_init_module),
#endif
#ifdef __NR_finit_module
		DENY(__NR_finit_module),
#endif
#ifdef __NR_delete_module
		DENY(__NR_delete_module),
#endif
#ifdef __NR_ptrace
		DENY(__NR_ptrace),
#endif
#ifdef __NR_clock_settime
		DENY(__NR_clock_settime),
#endif
#ifdef __NR_clock_adjtime
		DENY(__NR_clock_adjtime),
#endif
#ifdef __NR_settimeofday
		DENY(__NR_settimeofday),
#endif
#ifdef __NR_bpf
		DENY(__NR_bpf),
#endif
#ifdef __NR_perf_event_open
		DENY(__NR_perf_event_open),
#endif
#ifdef __NR_fanotify_init
		DENY(__NR_fanotify_init),
#endif
#ifdef __NR_name_to_handle_at
		DENY(__NR_name_to_handle_at),
#endif
#ifdef __NR_open_by_handle_at
		DENY(__NR_open_by_handle_at),
#endif
#ifdef __NR_keyctl
		DENY(__NR_keyctl),
#endif
#ifdef __NR_add_key
		DENY(__NR_add_key),
#endif
#ifdef __NR_request_key
		DENY(__NR_request_key),
#endif
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog p = { .len = sizeof f / sizeof f[0], .filter = f };
	/* NO_NEW_PRIVS: dieu kien cai filter + tiem suid-binary leo thang */
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) die("no_new_privs");
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p)) die("seccomp");
}
