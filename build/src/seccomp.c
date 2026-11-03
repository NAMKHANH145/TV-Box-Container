/* seccomp.c — BPF viet tay (khong libseccomp).
 * default: deny-list 21 syscall nguy hiem (EPERM).
 * strict:  allow-list ~120 syscall (SECCOMP_RET_ERRNO|EPERM cho cai khac).
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

#define ALLOW(nr) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)

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

void install_filter_strict(void) {
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, MC_AUDIT_ARCH, 1, 0),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, nr)),
#ifdef __NR_read
		ALLOW(__NR_read),
#endif
#ifdef __NR_write
		ALLOW(__NR_write),
#endif
#ifdef __NR_open
		ALLOW(__NR_open),
#endif
#ifdef __NR_openat
		ALLOW(__NR_openat),
#endif
#ifdef __NR_close
		ALLOW(__NR_close),
#endif
#ifdef __NR_stat
		ALLOW(__NR_stat),
#endif
#ifdef __NR_fstat
		ALLOW(__NR_fstat),
#endif
#ifdef __NR_lstat
		ALLOW(__NR_lstat),
#endif
#ifdef __NR_newfstatat
		ALLOW(__NR_newfstatat),
#endif
#ifdef __NR_lseek
		ALLOW(__NR_lseek),
#endif
#ifdef __NR_mmap
		ALLOW(__NR_mmap),
#endif
#ifdef __NR_mprotect
		ALLOW(__NR_mprotect),
#endif
#ifdef __NR_munmap
		ALLOW(__NR_munmap),
#endif
#ifdef __NR_brk
		ALLOW(__NR_brk),
#endif
#ifdef __NR_rt_sigaction
		ALLOW(__NR_rt_sigaction),
#endif
#ifdef __NR_rt_sigprocmask
		ALLOW(__NR_rt_sigprocmask),
#endif
#ifdef __NR_rt_sigreturn
		ALLOW(__NR_rt_sigreturn),
#endif
#ifdef __NR_ioctl
		ALLOW(__NR_ioctl),
#endif
#ifdef __NR_pread64
		ALLOW(__NR_pread64),
#endif
#ifdef __NR_readv
		ALLOW(__NR_readv),
#endif
#ifdef __NR_writev
		ALLOW(__NR_writev),
#endif
#ifdef __NR_access
		ALLOW(__NR_access),
#endif
#ifdef __NR_faccessat
		ALLOW(__NR_faccessat),
#endif
#ifdef __NR_pipe
		ALLOW(__NR_pipe),
#endif
#ifdef __NR_pipe2
		ALLOW(__NR_pipe2),
#endif
#ifdef __NR_select
		ALLOW(__NR_select),
#endif
#ifdef __NR_pselect6
		ALLOW(__NR_pselect6),
#endif
#ifdef __NR_poll
		ALLOW(__NR_poll),
#endif
#ifdef __NR_ppoll
		ALLOW(__NR_ppoll),
#endif
#ifdef __NR_sched_yield
		ALLOW(__NR_sched_yield),
#endif
#ifdef __NR_mremap
		ALLOW(__NR_mremap),
#endif
#ifdef __NR_msync
		ALLOW(__NR_msync),
#endif
#ifdef __NR_madvise
		ALLOW(__NR_madvise),
#endif
#ifdef __NR_dup
		ALLOW(__NR_dup),
#endif
#ifdef __NR_dup2
		ALLOW(__NR_dup2),
#endif
#ifdef __NR_dup3
		ALLOW(__NR_dup3),
#endif
#ifdef __NR_nanosleep
		ALLOW(__NR_nanosleep),
#endif
#ifdef __NR_clock_nanosleep
		ALLOW(__NR_clock_nanosleep),
#endif
#ifdef __NR_getitimer
		ALLOW(__NR_getitimer),
#endif
#ifdef __NR_alarm
		ALLOW(__NR_alarm),
#endif
#ifdef __NR_setitimer
		ALLOW(__NR_setitimer),
#endif
#ifdef __NR_getpid
		ALLOW(__NR_getpid),
#endif
#ifdef __NR_getppid
		ALLOW(__NR_getppid),
#endif
#ifdef __NR_getuid
		ALLOW(__NR_getuid),
#endif
#ifdef __NR_getgid
		ALLOW(__NR_getgid),
#endif
#ifdef __NR_geteuid
		ALLOW(__NR_geteuid),
#endif
#ifdef __NR_getegid
		ALLOW(__NR_getegid),
#endif
#ifdef __NR_gettid
		ALLOW(__NR_gettid),
#endif
#ifdef __NR_socket
		ALLOW(__NR_socket),
#endif
#ifdef __NR_connect
		ALLOW(__NR_connect),
#endif
#ifdef __NR_accept
		ALLOW(__NR_accept),
#endif
#ifdef __NR_accept4
		ALLOW(__NR_accept4),
#endif
#ifdef __NR_sendto
		ALLOW(__NR_sendto),
#endif
#ifdef __NR_recvfrom
		ALLOW(__NR_recvfrom),
#endif
#ifdef __NR_sendmsg
		ALLOW(__NR_sendmsg),
#endif
#ifdef __NR_recvmsg
		ALLOW(__NR_recvmsg),
#endif
#ifdef __NR_shutdown
		ALLOW(__NR_shutdown),
#endif
#ifdef __NR_bind
		ALLOW(__NR_bind),
#endif
#ifdef __NR_listen
		ALLOW(__NR_listen),
#endif
#ifdef __NR_getsockname
		ALLOW(__NR_getsockname),
#endif
#ifdef __NR_getpeername
		ALLOW(__NR_getpeername),
#endif
#ifdef __NR_socketpair
		ALLOW(__NR_socketpair),
#endif
#ifdef __NR_setsockopt
		ALLOW(__NR_setsockopt),
#endif
#ifdef __NR_getsockopt
		ALLOW(__NR_getsockopt),
#endif
#ifdef __NR_clone
		ALLOW(__NR_clone),
#endif
#ifdef __NR_fork
		ALLOW(__NR_fork),
#endif
#ifdef __NR_vfork
		ALLOW(__NR_vfork),
#endif
#ifdef __NR_execve
		ALLOW(__NR_execve),
#endif
#ifdef __NR_execveat
		ALLOW(__NR_execveat),
#endif
#ifdef __NR_exit
		ALLOW(__NR_exit),
#endif
#ifdef __NR_exit_group
		ALLOW(__NR_exit_group),
#endif
#ifdef __NR_wait4
		ALLOW(__NR_wait4),
#endif
#ifdef __NR_waitid
		ALLOW(__NR_waitid),
#endif
#ifdef __NR_kill
		ALLOW(__NR_kill),
#endif
#ifdef __NR_tkill
		ALLOW(__NR_tkill),
#endif
#ifdef __NR_tgkill
		ALLOW(__NR_tgkill),
#endif
#ifdef __NR_uname
		ALLOW(__NR_uname),
#endif
#ifdef __NR_getcwd
		ALLOW(__NR_getcwd),
#endif
#ifdef __NR_chdir
		ALLOW(__NR_chdir),
#endif
#ifdef __NR_fchdir
		ALLOW(__NR_fchdir),
#endif
#ifdef __NR_rename
		ALLOW(__NR_rename),
#endif
#ifdef __NR_renameat
		ALLOW(__NR_renameat),
#endif
#ifdef __NR_renameat2
		ALLOW(__NR_renameat2),
#endif
#ifdef __NR_mkdir
		ALLOW(__NR_mkdir),
#endif
#ifdef __NR_mkdirat
		ALLOW(__NR_mkdirat),
#endif
#ifdef __NR_rmdir
		ALLOW(__NR_rmdir),
#endif
#ifdef __NR_creat
		ALLOW(__NR_creat),
#endif
#ifdef __NR_link
		ALLOW(__NR_link),
#endif
#ifdef __NR_linkat
		ALLOW(__NR_linkat),
#endif
#ifdef __NR_unlink
		ALLOW(__NR_unlink),
#endif
#ifdef __NR_unlinkat
		ALLOW(__NR_unlinkat),
#endif
#ifdef __NR_symlink
		ALLOW(__NR_symlink),
#endif
#ifdef __NR_symlinkat
		ALLOW(__NR_symlinkat),
#endif
#ifdef __NR_readlink
		ALLOW(__NR_readlink),
#endif
#ifdef __NR_readlinkat
		ALLOW(__NR_readlinkat),
#endif
#ifdef __NR_chmod
		ALLOW(__NR_chmod),
#endif
#ifdef __NR_fchmod
		ALLOW(__NR_fchmod),
#endif
#ifdef __NR_fchmodat
		ALLOW(__NR_fchmodat),
#endif
#ifdef __NR_chown
		ALLOW(__NR_chown),
#endif
#ifdef __NR_fchown
		ALLOW(__NR_fchown),
#endif
#ifdef __NR_fchownat
		ALLOW(__NR_fchownat),
#endif
#ifdef __NR_lchown
		ALLOW(__NR_lchown),
#endif
#ifdef __NR_umask
		ALLOW(__NR_umask),
#endif
#ifdef __NR_gettimeofday
		ALLOW(__NR_gettimeofday),
#endif
#ifdef __NR_getrlimit
		ALLOW(__NR_getrlimit),
#endif
#ifdef __NR_getrusage
		ALLOW(__NR_getrusage),
#endif
#ifdef __NR_sysinfo
		ALLOW(__NR_sysinfo),
#endif
#ifdef __NR_times
		ALLOW(__NR_times),
#endif
#ifdef __NR_getpriority
		ALLOW(__NR_getpriority),
#endif
#ifdef __NR_setpriority
		ALLOW(__NR_setpriority),
#endif
#ifdef __NR_prctl
		ALLOW(__NR_prctl),
#endif
#ifdef __NR_arch_prctl
		ALLOW(__NR_arch_prctl),
#endif
#ifdef __NR_setrlimit
		ALLOW(__NR_setrlimit),
#endif
#ifdef __NR_sync
		ALLOW(__NR_sync),
#endif
#ifdef __NR_fsync
		ALLOW(__NR_fsync),
#endif
#ifdef __NR_fdatasync
		ALLOW(__NR_fdatasync),
#endif
#ifdef __NR_getdents
		ALLOW(__NR_getdents),
#endif
#ifdef __NR_getdents64
		ALLOW(__NR_getdents64),
#endif
#ifdef __NR_fcntl
		ALLOW(__NR_fcntl),
#endif
#ifdef __NR_flock
		ALLOW(__NR_flock),
#endif
#ifdef __NR_truncate
		ALLOW(__NR_truncate),
#endif
#ifdef __NR_ftruncate
		ALLOW(__NR_ftruncate),
#endif
#ifdef __NR_getgroups
		ALLOW(__NR_getgroups),
#endif
#ifdef __NR_setgroups
		ALLOW(__NR_setgroups),
#endif
#ifdef __NR_setuid
		ALLOW(__NR_setuid),
#endif
#ifdef __NR_setgid
		ALLOW(__NR_setgid),
#endif
#ifdef __NR_setreuid
		ALLOW(__NR_setreuid),
#endif
#ifdef __NR_setregid
		ALLOW(__NR_setregid),
#endif
#ifdef __NR_setresuid
		ALLOW(__NR_setresuid),
#endif
#ifdef __NR_setresgid
		ALLOW(__NR_setresgid),
#endif
#ifdef __NR_getresuid
		ALLOW(__NR_getresuid),
#endif
#ifdef __NR_getresgid
		ALLOW(__NR_getresgid),
#endif
#ifdef __NR_setpgid
		ALLOW(__NR_setpgid),
#endif
#ifdef __NR_getpgid
		ALLOW(__NR_getpgid),
#endif
#ifdef __NR_getsid
		ALLOW(__NR_getsid),
#endif
#ifdef __NR_setsid
		ALLOW(__NR_setsid),
#endif
#ifdef __NR_capget
		ALLOW(__NR_capget),
#endif
#ifdef __NR_capset
		ALLOW(__NR_capset),
#endif
#ifdef __NR_rt_sigpending
		ALLOW(__NR_rt_sigpending),
#endif
#ifdef __NR_rt_sigtimedwait
		ALLOW(__NR_rt_sigtimedwait),
#endif
#ifdef __NR_rt_sigqueueinfo
		ALLOW(__NR_rt_sigqueueinfo),
#endif
#ifdef __NR_rt_sigsuspend
		ALLOW(__NR_rt_sigsuspend),
#endif
#ifdef __NR_sigaltstack
		ALLOW(__NR_sigaltstack),
#endif
#ifdef __NR_utime
		ALLOW(__NR_utime),
#endif
#ifdef __NR_utimes
		ALLOW(__NR_utimes),
#endif
#ifdef __NR_utimensat
		ALLOW(__NR_utimensat),
#endif
#ifdef __NR_futimesat
		ALLOW(__NR_futimesat),
#endif
#ifdef __NR_statfs
		ALLOW(__NR_statfs),
#endif
#ifdef __NR_fstatfs
		ALLOW(__NR_fstatfs),
#endif
#ifdef __NR_getxattr
		ALLOW(__NR_getxattr),
#endif
#ifdef __NR_lgetxattr
		ALLOW(__NR_lgetxattr),
#endif
#ifdef __NR_fgetxattr
		ALLOW(__NR_fgetxattr),
#endif
#ifdef __NR_setxattr
		ALLOW(__NR_setxattr),
#endif
#ifdef __NR_lsetxattr
		ALLOW(__NR_lsetxattr),
#endif
#ifdef __NR_fsetxattr
		ALLOW(__NR_fsetxattr),
#endif
#ifdef __NR_listxattr
		ALLOW(__NR_listxattr),
#endif
#ifdef __NR_llistxattr
		ALLOW(__NR_llistxattr),
#endif
#ifdef __NR_flistxattr
		ALLOW(__NR_flistxattr),
#endif
#ifdef __NR_removexattr
		ALLOW(__NR_removexattr),
#endif
#ifdef __NR_lremovexattr
		ALLOW(__NR_lremovexattr),
#endif
#ifdef __NR_fremovexattr
		ALLOW(__NR_fremovexattr),
#endif
#ifdef __NR_time
		ALLOW(__NR_time),
#endif
#ifdef __NR_futex
		ALLOW(__NR_futex),
#endif
#ifdef __NR_sched_setaffinity
		ALLOW(__NR_sched_setaffinity),
#endif
#ifdef __NR_sched_getaffinity
		ALLOW(__NR_sched_getaffinity),
#endif
#ifdef __NR_set_tid_address
		ALLOW(__NR_set_tid_address),
#endif
#ifdef __NR_restart_syscall
		ALLOW(__NR_restart_syscall),
#endif
#ifdef __NR_timer_create
		ALLOW(__NR_timer_create),
#endif
#ifdef __NR_timer_settime
		ALLOW(__NR_timer_settime),
#endif
#ifdef __NR_timer_gettime
		ALLOW(__NR_timer_gettime),
#endif
#ifdef __NR_timer_getoverrun
		ALLOW(__NR_timer_getoverrun),
#endif
#ifdef __NR_timer_delete
		ALLOW(__NR_timer_delete),
#endif
#ifdef __NR_clock_gettime
		ALLOW(__NR_clock_gettime),
#endif
#ifdef __NR_clock_getres
		ALLOW(__NR_clock_getres),
#endif
#ifdef __NR_clock_nanosleep
		ALLOW(__NR_clock_nanosleep),
#endif
#ifdef __NR_epoll_wait
		ALLOW(__NR_epoll_wait),
#endif
#ifdef __NR_epoll_ctl
		ALLOW(__NR_epoll_ctl),
#endif
#ifdef __NR_epoll_pwait
		ALLOW(__NR_epoll_pwait),
#endif
#ifdef __NR_signalfd
		ALLOW(__NR_signalfd),
#endif
#ifdef __NR_timerfd_create
		ALLOW(__NR_timerfd_create),
#endif
#ifdef __NR_eventfd
		ALLOW(__NR_eventfd),
#endif
#ifdef __NR_fallocate
		ALLOW(__NR_fallocate),
#endif
#ifdef __NR_timerfd_settime
		ALLOW(__NR_timerfd_settime),
#endif
#ifdef __NR_timerfd_gettime
		ALLOW(__NR_timerfd_gettime),
#endif
#ifdef __NR_signalfd4
		ALLOW(__NR_signalfd4),
#endif
#ifdef __NR_eventfd2
		ALLOW(__NR_eventfd2),
#endif
#ifdef __NR_epoll_create1
		ALLOW(__NR_epoll_create1),
#endif
#ifdef __NR_pipe2
		ALLOW(__NR_pipe2),
#endif
#ifdef __NR_inotify_init
		ALLOW(__NR_inotify_init),
#endif
#ifdef __NR_inotify_init1
		ALLOW(__NR_inotify_init1),
#endif
#ifdef __NR_inotify_add_watch
		ALLOW(__NR_inotify_add_watch),
#endif
#ifdef __NR_inotify_rm_watch
		ALLOW(__NR_inotify_rm_watch),
#endif
#ifdef __NR_prlimit64
		ALLOW(__NR_prlimit64),
#endif
#ifdef __NR_getrandom
		ALLOW(__NR_getrandom),
#endif
#ifdef __NR_memfd_create
		ALLOW(__NR_memfd_create),
#endif
#ifdef __NR_statx
		ALLOW(__NR_statx),
#endif
#ifdef __NR_rseq
		ALLOW(__NR_rseq),
#endif
#ifdef __NR_close_range
		ALLOW(__NR_close_range),
#endif
#ifdef __NR_openat2
		ALLOW(__NR_openat2),
#endif
#ifdef __NR_faccessat2
		ALLOW(__NR_faccessat2),
#endif
#ifdef __NR_epoll_pwait2
		ALLOW(__NR_epoll_pwait2),
#endif
#ifdef __NR_copy_file_range
		ALLOW(__NR_copy_file_range),
#endif
#ifdef __NR_preadv2
		ALLOW(__NR_preadv2),
#endif
#ifdef __NR_pwritev2
		ALLOW(__NR_pwritev2),
#endif
#ifdef __NR_fchmodat2
		ALLOW(__NR_fchmodat2),
#endif
		/* Blocked (not in list): mount, reboot, ptrace, bpf, kexec, init_module,
		   open_by_handle_at, name_to_handle_at, sethostname, setdomainname, etc. */
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ERRNO | EPERM),
	};
	struct sock_fprog p = { .len = sizeof f / sizeof f[0], .filter = f };
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) die("no_new_privs");
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p)) die("seccomp");
}
