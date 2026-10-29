# CHANGELOG — minicontainer

## [1.2.0] - 2026-10-29

### Added
- **Detached health checks**: `run -d --health-cmd CMD` now forks the same
  self-exiting watchdog used by `--disk`. It runs `CMD` on the host every 5s,
  kills the container's cgroup on failure, and leaves a
  `/run/minicontainer/<id>/health_failed` marker. A healthy container is left
  running; the watchdog exits when the container does (still zero daemons).
- `rm` also removes the `health_failed` marker.

### Changed
- `disk_watchdog_detached()` generalized to `container_watchdog_detached()`
  (disk + health in one process per detached container).
- Detached watchdog ignores `SIGALRM`/`SIGHUP`/`SIGINT` inherited from the
  supervisor so the 1s tick cannot terminate it.

### Verified
- TV Box S905W, kernel 6.18.38-meson64: `test.sh` **39/39 PASS** (3 new
  detached-health cases: failure marker+killed, cgroup cleaned, healthy run
  not killed). Latency 65ms.

## [1.1.0] - 2026-10-29

### Added
- **R2-1 disk quota watchdog** (`build/src/disk.c`): kernel lacks ext4 project
  quota, so `--disk SIZE` now caps total new bytes under the rootfs (baseline
  delta, `du`/st_blocks semantics), sampled every 1s. Foreground/systemd mode
  enforces it inside the existing wait loop (0 extra processes); `-d` forks one
  small watchdog per container that self-cleans and exits with the container.
  Overflow kills the cgroup and returns 137; detached runs leave a
  `/run/minicontainer/<id>/disk_exceeded` marker.
- `stop`/`rm` now reap the whole cgroup (`cgroup.kill`), retry `rmdir`, clean
  `.oldroot`, and terminate the detached watchdog.
- `nftw`-based `disk_usage()` helper (counts file + directory + symlink blocks).

### Changed
- wait loop uses `alarm(1)` + blocking `waitpid` instead of 200ms polling:
  exit latency back to 51-68ms while still sampling disk/health every second.
- `--net=bridge` is rejected with a clear message instead of silently acting
  as host networking.
- `--ram/--cpu/--pids/--io/--disk` reject malformed values (`abc`, `0`, `1.5G`)
  instead of silently treating them as unset.
- `--version` derives arch/libc at compile time (aarch64/armv7l/x86_64/riscv64).
- Seccomp filter uses `__NR_*` + per-arch `AUDIT_ARCH_*` so multi-arch builds
  actually work (previous filter was aarch64-only).

### Fixed
- `ps --json` with an existing but empty state dir printed invalid JSON
  (`[` newline `[]`); now prints `[]`.
- Duplicate `--disk` validation and noisy `": Success"` suffix on validation
  errors (`die_msg`).
- Detached containers that exited on their own left an empty cgroup and
  `.oldroot` behind; both are now cleaned.

### Verified
- TV Box S905W, kernel 6.18.38-meson64: `test.sh` **36/36 PASS**.
- Disk tests: 1.4MB > 1MB → SIGKILL/137 in 3s; 0.6MB → exit 0; single 2MB file
  → truncated at 1MB (RLIMIT_FSIZE); detached violation → marker + STOPPED +
  0 leftover cgroups; health-cmd failure → kill in 7s.
- Latency: 51-68ms over 5 runs; IO throttle 3MB @1MB/s = 3.09s; CPU cap 20%.

## [1.0.0] - 2026-10-06

### Added
- **Core runtime**: `minicontainer run` with `--ram`, `--cpu`, `--pids`, `--io`, `--net`, `--rootfs`, `--hostname`, `--detach`/`-d`, `--name` flags
- **6 Namespaces**: `CLONE_NEWPID` (PID=1), `CLONE_NEWNS` (mount isolation), `CLONE_NEWUTS` (hostname), `CLONE_NEWIPC` (shared memory), `CLONE_NEWUSER` (root→nobody), `CLONE_NEWNET` (optional `--net=isolated`)
- **5 Cgroup v2 controllers**: `memory.max` (RAM hard limit → OOM-killer), `cpu.max` (CPU quota), `pids.max` (fork-bomb protection), `io.max` (disk I/O throttling), `cgroup.procs` (attach)
- **Security layers**: pivot_root (chroot-break resistant), user namespace (nobody outside), seccomp-BPF 21 syscall deny-list, mount propagation leak fix (MS_PRIVATE)
- **OCI bundle mode**: `oci-run` reads config.json subset and converts to flags
- **CLI management**: `ps`, `stats`, `stop`, `rm` (zero-daemon, reads state from /run)
- **JSON output**: `ps --json`, `stats <id> --json`
- **Version flag**: `--version`
- **Timeout support**: `stop --timeout N`

### Changed
- Fixed integer overflow in `parse_bytes` (strtoll + validation)
- Fixed path traversal vulnerability in state files
- Fixed FD leak (pipe2 with O_CLOEXEC)
- Fixed detach stdio redirect (dup2 to /dev/null)
- Fixed mount propagation leak (MS_REC|MS_PRIVATE)
- Fixed OCI parser with warning for unsupported keys
- Implemented signal forwarding (SIGINT/SIGTERM → child + cleanup)

### Fixed
- Pipe2 file descriptor leaks across execvp
- Detach stdio redirect preventing SSH session hang
- Build corruption from duplicate declarations
- Build warnings from unused static functions

### Known Limitations
- `--sys` not available (kernel prohibits sysfs in userns)
- No OCI mounts/hooks/seccomp-OCI support (subset only)
- `--health-cmd` not implemented
- `--net=bridge` not implemented
- No disk quota yet (only throttling)
- No `exec` into running container

### Test Results
- test.sh: 30 test cases (21 original + signal, cpu-auto, io-auto, JSON tests)
- All tests passing