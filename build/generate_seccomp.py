#!/usr/bin/env python3
"""generate_seccomp.py — Generate seccomp strict filter at build time.

Reads syscall tables per architecture and outputs seccomp_strict.c with ALLOW() macros.
The deny-list (default filter) remains hand-written in seccomp.c (small, stable).
"""

import sys
import os
import subprocess
import re

# Syscalls allowed in strict mode (common across architectures)
# This is the baseline list; arch-specific additions are handled below.
COMMON_ALLOW = {
    # Basic I/O
    'read', 'write', 'open', 'close', 'openat', 'close_range',
    # File ops
    'stat', 'fstat', 'lstat', 'newfstatat', 'statx', 'lseek', 'llseek',
    'access', 'faccessat', 'faccessat2', 'chmod', 'fchmod', 'fchmodat', 'fchmodat2',
    'chown', 'fchown', 'lchown', 'fchownat', 'umask',
    'mkdir', 'mkdirat', 'rmdir', 'unlink', 'unlinkat', 'rename', 'renameat', 'renameat2',
    'link', 'linkat', 'symlink', 'symlinkat', 'readlink', 'readlinkat',
    'mknod', 'mknodat',
    # Memory
    'mmap', 'mprotect', 'munmap', 'mremap', 'msync', 'madvise', 'brk',
    'memfd_create', 'fallocate', 'ftruncate', 'truncate',
    # Process
    'fork', 'vfork', 'clone', 'clone3', 'execve', 'execveat', 'exit', 'exit_group',
    'wait4', 'waitid', 'getpid', 'getppid', 'gettid', 'getuid', 'getgid',
    'geteuid', 'getegid', 'setuid', 'setgid', 'setreuid', 'setregid',
    'setresuid', 'setresgid', 'getresuid', 'getresgid', 'setpgid', 'getpgid',
    'setsid', 'getsid', 'set_tid_address',
    # Signals
    'rt_sigaction', 'rt_sigprocmask', 'rt_sigreturn', 'rt_sigpending',
    'rt_sigtimedwait', 'rt_sigqueueinfo', 'rt_sigsuspend', 'sigaltstack',
    'kill', 'tkill', 'tgkill',
    # Time
    'nanosleep', 'clock_nanosleep', 'clock_gettime', 'clock_getres',
    'clock_getcpuclockid', 'clock_adjtime', 'gettimeofday',
    'setitimer', 'getitimer', 'alarm', 'timer_create', 'timer_settime',
    'timer_gettime', 'timer_getoverrun', 'timer_delete', 'timerfd_create',
    'timerfd_settime', 'timerfd_gettime',
    # Scheduling
    'sched_yield', 'sched_setaffinity', 'sched_getaffinity',
    'sched_setparam', 'sched_getparam', 'sched_setscheduler', 'sched_getscheduler',
    'sched_get_priority_max', 'sched_get_priority_min', 'sched_rr_get_interval',
    # File descriptors
    'dup', 'dup2', 'dup3', 'fcntl', 'flock', 'ioctl', 'poll', 'ppoll',
    'select', 'pselect6', 'epoll_create1', 'epoll_ctl', 'epoll_wait',
    'epoll_pwait', 'epoll_pwait2', 'pipe', 'pipe2', 'eventfd', 'eventfd2',
    'signalfd', 'signalfd4', 'inotify_init', 'inotify_init1', 'inotify_add_watch',
    'inotify_rm_watch', 'pread64', 'pwrite64', 'readv', 'writev', 'preadv2', 'pwritev2',
    'copy_file_range', 'sendfile',
    # Networking
    'socket', 'connect', 'accept', 'accept4', 'bind', 'listen', 'getsockname',
    'getpeername', 'socketpair', 'sendto', 'recvfrom', 'sendmsg', 'recvmsg',
    'shutdown', 'setsockopt', 'getsockopt',
    # Sync primitives
    'futex', 'restart_syscall', 'set_robust_list', 'get_robust_list',
    # VFS
    'getdents', 'getdents64', 'getcwd', 'chdir', 'fchdir',
    # Extended attributes
    'getxattr', 'lgetxattr', 'fgetxattr', 'setxattr', 'lsetxattr', 'fsetxattr',
    'listxattr', 'llistxattr', 'flistxattr', 'removexattr', 'lremovexattr', 'fremovexattr',
    # System info
    'uname', 'sysinfo', 'times', 'getrlimit', 'setrlimit', 'getrusage',
    'prlimit64', 'umask', 'getpriority', 'setpriority',
    # Capabilities
    'capget', 'capset',
    # Prctl
    'prctl', 'arch_prctl',
    # Random
    'getrandom',
    # rseq
    'rseq',
    # File system (SAFE only - no mount/umount/pivot_root)
    'statfs', 'fstatfs', 'sync', 'fsync', 'fdatasync',
    # Namespaces (for nested containers)
    'unshare', 'setns',
    # Other
    'rt_sigqueueinfo', 'personality', 'utime', 'utimes', 'utimensat', 'futimesat',
    'splice', 'tee', 'vmsplice',
    'kcmp', 'finit_module',
    'quotactl', 'quotactl_fd',
    'landlock_create_ruleset', 'landlock_add_rule', 'landlock_restrict_self',
    'process_madvise', 'process_mrelease',
    'membarrier', 'userfaultfd', 'pkey_mprotect', 'pkey_alloc', 'pkey_free',
}

# Syscalls explicitly DENIED in strict mode (dangerous operations)
# These are removed from COMMON_ALLOW if present
STRICT_DENY = {
    'mount', 'umount2', 'pivot_root', 'reboot', 'kexec_load', 'kexec_file_load',
    'init_module', 'finit_module', 'delete_module', 'get_kernel_syms', 'query_module',
    'ptrace', 'clock_settime', 'settimeofday', 'bpf', 'perf_event_open',
    'fanotify_init', 'name_to_handle_at', 'open_by_handle_at',
    'keyctl', 'add_key', 'request_key', 'sethostname', 'setdomainname',
    'acct', 'swapon', 'swapoff', 'ioperm', 'iopl', 'vm86',
    'modify_ldt', 'profil', 'uselib', 'sysfs', 'syslog', 'adjtimex',
    'settimeofday', 'stime', 'nfsservctl', 'getpmsg', 'putpmsg',
    'vhangup', 'idle', 'syscall', 'ni_syscall',
}

# Architecture-specific syscall mappings (syscall name -> number)
# These are for architectures where the name differs or extra syscalls exist.
ARCH_SPECIAL = {
    'x86_64': {
        # x86_64 has some unique syscalls
        'open': 2,  # legacy
        'stat': 4,  # legacy
        'lstat': 5,  # legacy
        'fstat': 5,  # wait, fstat is 5 on x86_64? Let me check.
    },
    'aarch64': {
        # aarch64 uses the common names mostly
    },
    'riscv64': {
        # riscv64 uses the common names mostly
    },
}

def get_syscall_numbers(arch):
    """Get syscall numbers for the given architecture using the kernel headers."""
    # Try to compile a small program to extract syscall numbers
    # This is the most reliable way since syscall numbers vary by arch
    
    if arch == 'x86_64':
        # Use asm/unistd_64.h
        header = '/usr/include/asm/unistd_64.h'
    elif arch == 'aarch64':
        header = '/usr/include/asm/unistd.h'
    elif arch == 'riscv64':
        header = '/usr/include/asm/unistd.h'
    elif arch == 'arm':
        header = '/usr/include/asm/unistd.h'
    elif arch == 'i386':
        header = '/usr/include/asm/unistd_32.h'
    else:
        return {}
    
    if not os.path.exists(header):
        # Try alternative locations
        alt_headers = [
            f'/usr/include/asm-generic/unistd.h',
            f'/usr/src/linux-headers-*/include/asm/unistd.h',
        ]
        for h in alt_headers:
            import glob
            matches = glob.glob(h)
            if matches:
                header = matches[0]
                break
        else:
            print(f"Warning: Could not find syscall header for {arch}", file=sys.stderr)
            return {}
    
    syscalls = {}
    raw = {}
    pattern = re.compile(r'#define\s+__NR_?(\w+)\s+(\S+)')
    try:
        with open(header, 'r') as f:
            for line in f:
                m = pattern.match(line)
                if m:
                    raw[m.group(1).lower()] = m.group(2)
    except Exception as e:
        print(f"Warning: Failed to parse {header}: {e}", file=sys.stderr)

    # Headers define some numbers indirectly, e.g.
    #   #define __NR_mmap __NR3264_mmap
    #   #define __NR3264_mmap 222
    # Resolve those, otherwise mmap/fcntl/stat silently drop out of the list.
    def resolve(name, seen):
        value = raw.get(name)
        if value is None:
            return None
        if value.isdigit():
            return int(value)
        m = re.fullmatch(r'__NR_?(\w+)', value)
        if m:
            target = m.group(1).lower()
            if target not in seen:
                return resolve(target, seen | {name})
        return None

    for name in raw:
        num = resolve(name, set())
        if num is not None:
            syscalls[name] = num

    return syscalls

def generate_strict_filter(arch, output_file):
    """Generate seccomp_strict.c for the given architecture."""
    syscalls = get_syscall_numbers(arch)
    
    if not syscalls:
        print(f"Error: No syscall numbers found for {arch}", file=sys.stderr)
        return False
    
    # Filter to only allowed syscalls that exist on this arch, excluding strict deny list
    allowed = []
    missing = []
    for name in sorted(COMMON_ALLOW - STRICT_DENY):
        if name in syscalls:
            allowed.append((name, syscalls[name]))
        else:
            missing.append(name)
    
    if missing:
        print(f"Info: {len(missing)} syscalls not found on {arch}: {', '.join(missing[:10])}{'...' if len(missing) > 10 else ''}", file=sys.stderr)
    
    # Architecture audit constant
    arch_const = {
        'x86_64': 'AUDIT_ARCH_X86_64',
        'aarch64': 'AUDIT_ARCH_AARCH64',
        'arm': 'AUDIT_ARCH_ARM',
        'riscv64': 'AUDIT_ARCH_RISCV64',
        'i386': 'AUDIT_ARCH_I386',
    }.get(arch, 'AUDIT_ARCH_X86_64')
    
    # Generate the C file
    with open(output_file, 'w') as f:
        f.write(f'''/* seccomp_strict.c — STRICT allow-list filter for {arch} (GENERATED by generate_seccomp.py)
 * DO NOT EDIT MANUALLY. Regenerate with: python3 generate_seccomp.py {arch}
 * This file is #include'd by seccomp.c when building for {arch}.
 *
 * The body is guarded so a build system that compiles every C file in src/
 * (CI runs gcc over the whole directory; the image has no make) produces an
 * empty translation unit instead of a second definition of
 * install_filter_strict().
 */
#ifdef MC_SECCOMP_STRICT_INCLUDED

#include "common.h"
#include <sys/syscall.h>

''')
        f.write(f'#define MC_AUDIT_ARCH {arch_const}\n\n')
        
        f.write('''#define ALLOW(nr) \\
    BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1), \\
    BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)

void install_filter_strict(void) {
    struct sock_filter f[] = {
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, MC_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, nr)),
''')
        
        for name, num in allowed:
            f.write(f'#ifdef __NR_{name}\n')
            f.write(f'    ALLOW(__NR_{name}),\n')
            f.write(f'#endif\n')
        
        f.write('''        /* Blocked (not in list): mount, reboot, ptrace, bpf, kexec, init_module,
           open_by_handle_at, name_to_handle_at, sethostname, setdomainname, etc. */
        BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ERRNO | EPERM),
    };
    struct sock_fprog p = { .len = sizeof f / sizeof f[0], .filter = f };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) die("no_new_privs");
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p)) die("seccomp");
}
#endif /* MC_SECCOMP_STRICT_INCLUDED */
''')
    
    print(f"Generated {output_file} with {len(allowed)} allowed syscalls for {arch}")
    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 generate_seccomp.py <arch> [output_file]")
        print("Architectures: x86_64, aarch64, arm, riscv64, i386")
        sys.exit(1)
    
    arch = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else f'seccomp_strict_{arch}.c'
    
    success = generate_strict_filter(arch, output_file)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()