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

/* ---- P3.6: OCI/Docker seccomp profile JSON -> BPF ----
 *
 * Supported: defaultAction, syscalls[].names, syscalls[].action. Argument
 * filters (args[]) are not enforced (documented subset); a syscall listed with
 * args gets its action applied unconditionally. Unknown syscall names are
 * skipped, which is safe when defaultAction denies but means such an entry is
 * not enforced when defaultAction allows. No external library, no malloc at
 * install time beyond the filter itself. */
#include <stdint.h>
#include "jsmn.h"

#define SC_EPERM (SECCOMP_RET_ERRNO | EPERM)

static uint32_t scmp_action(const char *a) {
    if (strcmp(a, "SCMP_ACT_ALLOW") == 0) return SECCOMP_RET_ALLOW;
    if (strncmp(a, "SCMP_ACT_ERRNO", 14) == 0) return SC_EPERM;
    if (strncmp(a, "SCMP_ACT_KILL", 13) == 0) return SECCOMP_RET_KILL_PROCESS;
    if (strcmp(a, "SCMP_ACT_TRAP") == 0) return SECCOMP_RET_TRAP;
    if (strcmp(a, "SCMP_ACT_LOG") == 0) return SECCOMP_RET_LOG;
    if (strncmp(a, "SCMP_ACT_TRACE", 14) == 0) return SECCOMP_RET_TRACE;
    return SC_EPERM;              /* NOTIFY/unknown: fail closed */
}

struct sc_name { const char *name; int nr; };

#define SN(n) { #n, __NR_##n }
static const struct sc_name sc_names[] = {
#ifdef __NR_read
    SN(read),
#endif
#ifdef __NR_write
    SN(write),
#endif
#ifdef __NR_open
    SN(open),
#endif
#ifdef __NR_openat
    SN(openat),
#endif
#ifdef __NR_close
    SN(close),
#endif
#ifdef __NR_close_range
    SN(close_range),
#endif
#ifdef __NR_stat
    SN(stat),
#endif
#ifdef __NR_fstat
    SN(fstat),
#endif
#ifdef __NR_lstat
    SN(lstat),
#endif
#ifdef __NR_newfstatat
    SN(newfstatat),
#endif
#ifdef __NR_fstatat64
    SN(fstatat64),
#endif
#ifdef __NR_statx
    SN(statx),
#endif
#ifdef __NR_lseek
    SN(lseek),
#endif
#ifdef __NR_access
    SN(access),
#endif
#ifdef __NR_faccessat
    SN(faccessat),
#endif
#ifdef __NR_faccessat2
    SN(faccessat2),
#endif
#ifdef __NR_chmod
    SN(chmod),
#endif
#ifdef __NR_fchmod
    SN(fchmod),
#endif
#ifdef __NR_fchmodat
    SN(fchmodat),
#endif
#ifdef __NR_chown
    SN(chown),
#endif
#ifdef __NR_fchown
    SN(fchown),
#endif
#ifdef __NR_lchown
    SN(lchown),
#endif
#ifdef __NR_fchownat
    SN(fchownat),
#endif
#ifdef __NR_mkdir
    SN(mkdir),
#endif
#ifdef __NR_mkdirat
    SN(mkdirat),
#endif
#ifdef __NR_rmdir
    SN(rmdir),
#endif
#ifdef __NR_unlink
    SN(unlink),
#endif
#ifdef __NR_unlinkat
    SN(unlinkat),
#endif
#ifdef __NR_rename
    SN(rename),
#endif
#ifdef __NR_renameat
    SN(renameat),
#endif
#ifdef __NR_renameat2
    SN(renameat2),
#endif
#ifdef __NR_link
    SN(link),
#endif
#ifdef __NR_linkat
    SN(linkat),
#endif
#ifdef __NR_symlink
    SN(symlink),
#endif
#ifdef __NR_symlinkat
    SN(symlinkat),
#endif
#ifdef __NR_readlink
    SN(readlink),
#endif
#ifdef __NR_readlinkat
    SN(readlinkat),
#endif
#ifdef __NR_mknod
    SN(mknod),
#endif
#ifdef __NR_mknodat
    SN(mknodat),
#endif
#ifdef __NR_mmap
    SN(mmap),
#endif
#ifdef __NR_mprotect
    SN(mprotect),
#endif
#ifdef __NR_munmap
    SN(munmap),
#endif
#ifdef __NR_mremap
    SN(mremap),
#endif
#ifdef __NR_msync
    SN(msync),
#endif
#ifdef __NR_madvise
    SN(madvise),
#endif
#ifdef __NR_brk
    SN(brk),
#endif
#ifdef __NR_memfd_create
    SN(memfd_create),
#endif
#ifdef __NR_fallocate
    SN(fallocate),
#endif
#ifdef __NR_ftruncate
    SN(ftruncate),
#endif
#ifdef __NR_truncate
    SN(truncate),
#endif
#ifdef __NR_fork
    SN(fork),
#endif
#ifdef __NR_vfork
    SN(vfork),
#endif
#ifdef __NR_clone
    SN(clone),
#endif
#ifdef __NR_clone3
    SN(clone3),
#endif
#ifdef __NR_execve
    SN(execve),
#endif
#ifdef __NR_execveat
    SN(execveat),
#endif
#ifdef __NR_exit
    SN(exit),
#endif
#ifdef __NR_exit_group
    SN(exit_group),
#endif
#ifdef __NR_wait4
    SN(wait4),
#endif
#ifdef __NR_waitid
    SN(waitid),
#endif
#ifdef __NR_getpid
    SN(getpid),
#endif
#ifdef __NR_getppid
    SN(getppid),
#endif
#ifdef __NR_gettid
    SN(gettid),
#endif
#ifdef __NR_getuid
    SN(getuid),
#endif
#ifdef __NR_getgid
    SN(getgid),
#endif
#ifdef __NR_geteuid
    SN(geteuid),
#endif
#ifdef __NR_getegid
    SN(getegid),
#endif
#ifdef __NR_setuid
    SN(setuid),
#endif
#ifdef __NR_setgid
    SN(setgid),
#endif
#ifdef __NR_setreuid
    SN(setreuid),
#endif
#ifdef __NR_setregid
    SN(setregid),
#endif
#ifdef __NR_setresuid
    SN(setresuid),
#endif
#ifdef __NR_setresgid
    SN(setresgid),
#endif
#ifdef __NR_getresuid
    SN(getresuid),
#endif
#ifdef __NR_getresgid
    SN(getresgid),
#endif
#ifdef __NR_setpgid
    SN(setpgid),
#endif
#ifdef __NR_getpgid
    SN(getpgid),
#endif
#ifdef __NR_setsid
    SN(setsid),
#endif
#ifdef __NR_getsid
    SN(getsid),
#endif
#ifdef __NR_set_tid_address
    SN(set_tid_address),
#endif
#ifdef __NR_rt_sigaction
    SN(rt_sigaction),
#endif
#ifdef __NR_rt_sigprocmask
    SN(rt_sigprocmask),
#endif
#ifdef __NR_rt_sigreturn
    SN(rt_sigreturn),
#endif
#ifdef __NR_rt_sigpending
    SN(rt_sigpending),
#endif
#ifdef __NR_rt_sigtimedwait
    SN(rt_sigtimedwait),
#endif
#ifdef __NR_rt_sigqueueinfo
    SN(rt_sigqueueinfo),
#endif
#ifdef __NR_rt_sigsuspend
    SN(rt_sigsuspend),
#endif
#ifdef __NR_sigaltstack
    SN(sigaltstack),
#endif
#ifdef __NR_kill
    SN(kill),
#endif
#ifdef __NR_tkill
    SN(tkill),
#endif
#ifdef __NR_tgkill
    SN(tgkill),
#endif
#ifdef __NR_nanosleep
    SN(nanosleep),
#endif
#ifdef __NR_clock_nanosleep
    SN(clock_nanosleep),
#endif
#ifdef __NR_clock_gettime
    SN(clock_gettime),
#endif
#ifdef __NR_clock_getres
    SN(clock_getres),
#endif
#ifdef __NR_gettimeofday
    SN(gettimeofday),
#endif
#ifdef __NR_setitimer
    SN(setitimer),
#endif
#ifdef __NR_getitimer
    SN(getitimer),
#endif
#ifdef __NR_alarm
    SN(alarm),
#endif
#ifdef __NR_timer_create
    SN(timer_create),
#endif
#ifdef __NR_timer_settime
    SN(timer_settime),
#endif
#ifdef __NR_timer_gettime
    SN(timer_gettime),
#endif
#ifdef __NR_timer_getoverrun
    SN(timer_getoverrun),
#endif
#ifdef __NR_timer_delete
    SN(timer_delete),
#endif
#ifdef __NR_timerfd_create
    SN(timerfd_create),
#endif
#ifdef __NR_timerfd_settime
    SN(timerfd_settime),
#endif
#ifdef __NR_timerfd_gettime
    SN(timerfd_gettime),
#endif
#ifdef __NR_sched_yield
    SN(sched_yield),
#endif
#ifdef __NR_sched_setaffinity
    SN(sched_setaffinity),
#endif
#ifdef __NR_sched_getaffinity
    SN(sched_getaffinity),
#endif
#ifdef __NR_sched_setparam
    SN(sched_setparam),
#endif
#ifdef __NR_sched_getparam
    SN(sched_getparam),
#endif
#ifdef __NR_sched_setscheduler
    SN(sched_setscheduler),
#endif
#ifdef __NR_sched_getscheduler
    SN(sched_getscheduler),
#endif
#ifdef __NR_sched_get_priority_max
    SN(sched_get_priority_max),
#endif
#ifdef __NR_sched_get_priority_min
    SN(sched_get_priority_min),
#endif
#ifdef __NR_sched_rr_get_interval
    SN(sched_rr_get_interval),
#endif
#ifdef __NR_dup
    SN(dup),
#endif
#ifdef __NR_dup2
    SN(dup2),
#endif
#ifdef __NR_dup3
    SN(dup3),
#endif
#ifdef __NR_fcntl
    SN(fcntl),
#endif
#ifdef __NR_flock
    SN(flock),
#endif
#ifdef __NR_ioctl
    SN(ioctl),
#endif
#ifdef __NR_poll
    SN(poll),
#endif
#ifdef __NR_ppoll
    SN(ppoll),
#endif
#ifdef __NR_select
    SN(select),
#endif
#ifdef __NR_pselect6
    SN(pselect6),
#endif
#ifdef __NR_epoll_create1
    SN(epoll_create1),
#endif
#ifdef __NR_epoll_ctl
    SN(epoll_ctl),
#endif
#ifdef __NR_epoll_wait
    SN(epoll_wait),
#endif
#ifdef __NR_epoll_pwait
    SN(epoll_pwait),
#endif
#ifdef __NR_epoll_pwait2
    SN(epoll_pwait2),
#endif
#ifdef __NR_pipe
    SN(pipe),
#endif
#ifdef __NR_pipe2
    SN(pipe2),
#endif
#ifdef __NR_eventfd
    SN(eventfd),
#endif
#ifdef __NR_eventfd2
    SN(eventfd2),
#endif
#ifdef __NR_signalfd
    SN(signalfd),
#endif
#ifdef __NR_signalfd4
    SN(signalfd4),
#endif
#ifdef __NR_inotify_init
    SN(inotify_init),
#endif
#ifdef __NR_inotify_init1
    SN(inotify_init1),
#endif
#ifdef __NR_inotify_add_watch
    SN(inotify_add_watch),
#endif
#ifdef __NR_inotify_rm_watch
    SN(inotify_rm_watch),
#endif
#ifdef __NR_pread64
    SN(pread64),
#endif
#ifdef __NR_pwrite64
    SN(pwrite64),
#endif
#ifdef __NR_readv
    SN(readv),
#endif
#ifdef __NR_writev
    SN(writev),
#endif
#ifdef __NR_preadv2
    SN(preadv2),
#endif
#ifdef __NR_pwritev2
    SN(pwritev2),
#endif
#ifdef __NR_copy_file_range
    SN(copy_file_range),
#endif
#ifdef __NR_sendfile
    SN(sendfile),
#endif
#ifdef __NR_socket
    SN(socket),
#endif
#ifdef __NR_connect
    SN(connect),
#endif
#ifdef __NR_accept
    SN(accept),
#endif
#ifdef __NR_accept4
    SN(accept4),
#endif
#ifdef __NR_bind
    SN(bind),
#endif
#ifdef __NR_listen
    SN(listen),
#endif
#ifdef __NR_getsockname
    SN(getsockname),
#endif
#ifdef __NR_getpeername
    SN(getpeername),
#endif
#ifdef __NR_socketpair
    SN(socketpair),
#endif
#ifdef __NR_sendto
    SN(sendto),
#endif
#ifdef __NR_recvfrom
    SN(recvfrom),
#endif
#ifdef __NR_sendmsg
    SN(sendmsg),
#endif
#ifdef __NR_recvmsg
    SN(recvmsg),
#endif
#ifdef __NR_shutdown
    SN(shutdown),
#endif
#ifdef __NR_setsockopt
    SN(setsockopt),
#endif
#ifdef __NR_getsockopt
    SN(getsockopt),
#endif
#ifdef __NR_futex
    SN(futex),
#endif
#ifdef __NR_restart_syscall
    SN(restart_syscall),
#endif
#ifdef __NR_set_robust_list
    SN(set_robust_list),
#endif
#ifdef __NR_get_robust_list
    SN(get_robust_list),
#endif
#ifdef __NR_getdents
    SN(getdents),
#endif
#ifdef __NR_getdents64
    SN(getdents64),
#endif
#ifdef __NR_getcwd
    SN(getcwd),
#endif
#ifdef __NR_chdir
    SN(chdir),
#endif
#ifdef __NR_fchdir
    SN(fchdir),
#endif
#ifdef __NR_getxattr
    SN(getxattr),
#endif
#ifdef __NR_lgetxattr
    SN(lgetxattr),
#endif
#ifdef __NR_fgetxattr
    SN(fgetxattr),
#endif
#ifdef __NR_setxattr
    SN(setxattr),
#endif
#ifdef __NR_lsetxattr
    SN(lsetxattr),
#endif
#ifdef __NR_fsetxattr
    SN(fsetxattr),
#endif
#ifdef __NR_listxattr
    SN(listxattr),
#endif
#ifdef __NR_llistxattr
    SN(llistxattr),
#endif
#ifdef __NR_flistxattr
    SN(flistxattr),
#endif
#ifdef __NR_removexattr
    SN(removexattr),
#endif
#ifdef __NR_lremovexattr
    SN(lremovexattr),
#endif
#ifdef __NR_fremovexattr
    SN(fremovexattr),
#endif
#ifdef __NR_uname
    SN(uname),
#endif
#ifdef __NR_sysinfo
    SN(sysinfo),
#endif
#ifdef __NR_times
    SN(times),
#endif
#ifdef __NR_getrlimit
    SN(getrlimit),
#endif
#ifdef __NR_setrlimit
    SN(setrlimit),
#endif
#ifdef __NR_getrusage
    SN(getrusage),
#endif
#ifdef __NR_prlimit64
    SN(prlimit64),
#endif
#ifdef __NR_getpriority
    SN(getpriority),
#endif
#ifdef __NR_setpriority
    SN(setpriority),
#endif
#ifdef __NR_capget
    SN(capget),
#endif
#ifdef __NR_capset
    SN(capset),
#endif
#ifdef __NR_prctl
    SN(prctl),
#endif
#ifdef __NR_arch_prctl
    SN(arch_prctl),
#endif
#ifdef __NR_getrandom
    SN(getrandom),
#endif
#ifdef __NR_rseq
    SN(rseq),
#endif
#ifdef __NR_statfs
    SN(statfs),
#endif
#ifdef __NR_fstatfs
    SN(fstatfs),
#endif
#ifdef __NR_sync
    SN(sync),
#endif
#ifdef __NR_fsync
    SN(fsync),
#endif
#ifdef __NR_fdatasync
    SN(fdatasync),
#endif
#ifdef __NR_unshare
    SN(unshare),
#endif
#ifdef __NR_setns
    SN(setns),
#endif
#ifdef __NR_personality
    SN(personality),
#endif
#ifdef __NR_utime
    SN(utime),
#endif
#ifdef __NR_utimes
    SN(utimes),
#endif
#ifdef __NR_utimensat
    SN(utimensat),
#endif
#ifdef __NR_futimesat
    SN(futimesat),
#endif
#ifdef __NR_splice
    SN(splice),
#endif
#ifdef __NR_tee
    SN(tee),
#endif
#ifdef __NR_vmsplice
    SN(vmsplice),
#endif
#ifdef __NR_membarrier
    SN(membarrier),
#endif
#ifdef __NR_mlock
    SN(mlock),
#endif
#ifdef __NR_mlock2
    SN(mlock2),
#endif
#ifdef __NR_mlockall
    SN(mlockall),
#endif
#ifdef __NR_munlock
    SN(munlock),
#endif
#ifdef __NR_munlockall
    SN(munlockall),
#endif
#ifdef __NR_getcpu
    SN(getcpu),
#endif
#ifdef __NR_openat2
    SN(openat2),
#endif
#ifdef __NR_pidfd_open
    SN(pidfd_open),
#endif
#ifdef __NR_pidfd_send_signal
    SN(pidfd_send_signal),
#endif
#ifdef __NR_pidfd_getfd
    SN(pidfd_getfd),
#endif
#ifdef __NR_process_vm_readv
    SN(process_vm_readv),
#endif
#ifdef __NR_process_vm_writev
    SN(process_vm_writev),
#endif
#ifdef __NR_process_madvise
    SN(process_madvise),
#endif
#ifdef __NR_readahead
    SN(readahead),
#endif
#ifdef __NR_fadvise64
    SN(fadvise64),
#endif
#ifdef __NR_fadvise64_64
    SN(fadvise64_64),
#endif
#ifdef __NR_syncfs
    SN(syncfs),
#endif
#ifdef __NR_sync_file_range
    SN(sync_file_range),
#endif
#ifdef __NR_sched_getattr
    SN(sched_getattr),
#endif
#ifdef __NR_sched_setattr
    SN(sched_setattr),
#endif
#ifdef __NR_semget
    SN(semget),
#endif
#ifdef __NR_semop
    SN(semop),
#endif
#ifdef __NR_semctl
    SN(semctl),
#endif
#ifdef __NR_semtimedop
    SN(semtimedop),
#endif
#ifdef __NR_shmget
    SN(shmget),
#endif
#ifdef __NR_shmat
    SN(shmat),
#endif
#ifdef __NR_shmctl
    SN(shmctl),
#endif
#ifdef __NR_shmdt
    SN(shmdt),
#endif
#ifdef __NR_msgget
    SN(msgget),
#endif
#ifdef __NR_msgsnd
    SN(msgsnd),
#endif
#ifdef __NR_msgrcv
    SN(msgrcv),
#endif
#ifdef __NR_msgctl
    SN(msgctl),
#endif
#ifdef __NR_mq_open
    SN(mq_open),
#endif
#ifdef __NR_mq_unlink
    SN(mq_unlink),
#endif
#ifdef __NR_mq_timedsend
    SN(mq_timedsend),
#endif
#ifdef __NR_mq_timedreceive
    SN(mq_timedreceive),
#endif
#ifdef __NR_mq_notify
    SN(mq_notify),
#endif
#ifdef __NR_mq_getsetattr
    SN(mq_getsetattr),
#endif
#ifdef __NR_mbind
    SN(mbind),
#endif
#ifdef __NR_set_mempolicy
    SN(set_mempolicy),
#endif
#ifdef __NR_get_mempolicy
    SN(get_mempolicy),
#endif
#ifdef __NR_migrate_pages
    SN(migrate_pages),
#endif
#ifdef __NR_move_pages
    SN(move_pages),
#endif
#ifdef __NR_mincore
    SN(mincore),
#endif
#ifdef __NR_getgroups
    SN(getgroups),
#endif
#ifdef __NR_setgroups
    SN(setgroups),
#endif
#ifdef __NR_setfsuid
    SN(setfsuid),
#endif
#ifdef __NR_setfsgid
    SN(setfsgid),
#endif
#ifdef __NR_epoll_create
    SN(epoll_create),
#endif
#ifdef __NR_io_uring_setup
    SN(io_uring_setup),
#endif
#ifdef __NR_io_uring_enter
    SN(io_uring_enter),
#endif
#ifdef __NR_io_uring_register
    SN(io_uring_register),
#endif
#ifdef __NR_clock_adjtime
    SN(clock_adjtime),
#endif
#ifdef __NR_adjtimex
    SN(adjtimex),
#endif
#ifdef __NR_mount
    SN(mount),
#endif
#ifdef __NR_umount2
    SN(umount2),
#endif
#ifdef __NR_pivot_root
    SN(pivot_root),
#endif
#ifdef __NR_reboot
    SN(reboot),
#endif
#ifdef __NR_chroot
    SN(chroot),
#endif
#ifdef __NR_settimeofday
    SN(settimeofday),
#endif
#ifdef __NR_clock_settime
    SN(clock_settime),
#endif
#ifdef __NR_sethostname
    SN(sethostname),
#endif
#ifdef __NR_setdomainname
    SN(setdomainname),
#endif
#ifdef __NR_ptrace
    SN(ptrace),
#endif
#ifdef __NR_bpf
    SN(bpf),
#endif
#ifdef __NR_perf_event_open
    SN(perf_event_open),
#endif
#ifdef __NR_keyctl
    SN(keyctl),
#endif
#ifdef __NR_add_key
    SN(add_key),
#endif
#ifdef __NR_request_key
    SN(request_key),
#endif
#ifdef __NR_open_by_handle_at
    SN(open_by_handle_at),
#endif
#ifdef __NR_name_to_handle_at
    SN(name_to_handle_at),
#endif
#ifdef __NR_init_module
    SN(init_module),
#endif
#ifdef __NR_finit_module
    SN(finit_module),
#endif
#ifdef __NR_delete_module
    SN(delete_module),
#endif
};

static int sc_nr(const char *name) {
    for (size_t i = 0; i < sizeof sc_names / sizeof sc_names[0]; i++)
        if (strcmp(name, sc_names[i].name) == 0) return sc_names[i].nr;
    return -1;
}

int seccomp_install_profile(const char *path) {
    char *js = NULL;
    jsmntok_t *t = NULL;
    int nt = 0;
    if (json_parse_file(path, &js, &t, &nt) < 0) {
        fprintf(stderr, "minicontainer: seccomp: cannot parse %s\n", path);
        return -1;
    }
    if (nt < 1 || t[0].type != JSMN_OBJECT) {
        fprintf(stderr, "minicontainer: seccomp: %s is not a JSON object\n", path);
        json_free(js, t);
        return -1;
    }

    char act[BUF_SIZE_SMALL] = "SCMP_ACT_ERRNO";
    int da = json_obj_get(js, t, 0, "defaultAction");
    if (da >= 0) json_tok_copy(js, &t[da], act, sizeof act);
    uint32_t def = scmp_action(act);
    int default_allow = (def == SECCOMP_RET_ALLOW || def == SECCOMP_RET_LOG);
    if (def == SECCOMP_RET_LOG) def = SECCOMP_RET_ALLOW;

    struct sock_filter *f = malloc((2 * SECCOMP_PROFILE_MAX_RULES + 8) * sizeof *f);
    if (!f) { json_free(js, t); return -1; }
    int n = 0;
    f[n++] = (struct sock_filter)BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, arch));
    f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, MC_AUDIT_ARCH, 1, 0);
    f[n++] = (struct sock_filter)BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS);
    f[n++] = (struct sock_filter)BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr));

    int rules = 0, skipped = 0, unknown = 0;
    int arr = json_obj_get(js, t, 0, "syscalls");
    if (arr >= 0 && t[arr].type == JSMN_ARRAY) {
        for (int k = 0; k < t[arr].size && rules < SECCOMP_PROFILE_MAX_RULES; k++) {
            int ent = json_arr_nth(t, arr, k);
            if (ent < 0 || t[ent].type != JSMN_OBJECT) continue;
            int names = json_obj_get(js, t, ent, "names");
            int atok = json_obj_get(js, t, ent, "action");
            if (names < 0 || t[names].type != JSMN_ARRAY || atok < 0) { skipped++; continue; }
            char a[BUF_SIZE_SMALL] = "SCMP_ACT_ALLOW";
            json_tok_copy(js, &t[atok], a, sizeof a);
            uint32_t ret = scmp_action(a);
            if (ret == SECCOMP_RET_LOG) ret = SECCOMP_RET_ALLOW;
            /* Only rules that differ from the default are needed. */
            if (default_allow ? (ret == SECCOMP_RET_ALLOW) : (ret != SECCOMP_RET_ALLOW))
                continue;
            for (int j = 0; j < t[names].size && rules < SECCOMP_PROFILE_MAX_RULES; j++) {
                int ntok = json_arr_nth(t, names, j);
                if (ntok < 0) continue;
                char nm[BUF_SIZE_SMALL];
                json_tok_copy(js, &t[ntok], nm, sizeof nm);
                int nr = sc_nr(nm);
                if (nr < 0) { unknown++; continue; }
                f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, (uint32_t)nr, 0, 1);
                f[n++] = (struct sock_filter)BPF_STMT(BPF_RET + BPF_K, ret);
                rules++;
            }
        }
    }
    f[n++] = (struct sock_filter)BPF_STMT(BPF_RET + BPF_K, def);

    struct sock_fprog p = { .len = (unsigned short)n, .filter = f };
    int rc = 0;
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p)) {
        fprintf(stderr, "minicontainer: seccomp: install failed: %s\n", strerror(errno));
        rc = -1;
    }
    if (unknown)
        fprintf(stderr, "minicontainer: seccomp: %d syscall name(s) in profile not in our table (subset)\n", unknown);
    if (skipped)
        fprintf(stderr, "minicontainer: seccomp: %d entry(ies) without names/action skipped\n", skipped);
    free(f);
    json_free(js, t);
    return rc;
}

/* Strict allow-list filter — generated at build time by generate_seccomp.py.
 * The macro makes the included file emit the definition; compiled standalone
 * (e.g. CI compiling the whole src directory) it emits nothing, avoiding a
 * duplicate symbol. */
#define MC_SECCOMP_STRICT_INCLUDED 1
#if defined(__aarch64__)
#include "seccomp_strict_aarch64.c"
#elif defined(__x86_64__)
#include "seccomp_strict_x86_64.c"
#elif defined(__arm__)
#include "seccomp_strict_arm.c"
#elif defined(__riscv) && defined(__riscv_xlen) && __riscv_xlen == 64
#include "seccomp_strict_riscv64.c"
#elif defined(__i386__)
#include "seccomp_strict_i386.c"
#else
#error "minicontainer: unsupported architecture for strict seccomp filter"
#endif