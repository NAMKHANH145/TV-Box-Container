# CHANGELOG — minicontainer

## [1.4.0] - 2026-10-29

### Added
- **`--read-only`** (`ns.c: ro_setup()`): protects the µSD/eMMC card from wear.
  The child bind-mounts the rootfs onto itself inside its private mount
  namespace, mounts `tmpfs` on `/tmp` (32M), `/run` (8M) and `/dev/shm` (64M),
  and after `chroot` + `/proc` remounts that bind read-only, so writes to
  `/etc`, `/usr`, `/opt` fail with `EROFS` while the host's copy stays
  writable. Distributions that redirect `/var/log -> ../tmp` (Buildroot) are
  detected with `lstat` per path component and skipped — mounting onto a
  symlink shadows `/tmp` with a second tmpfs.
- **`pull <profile>`** (`build/src/pull.c`): fetches `<profile>-rootfs.tar.gz`
  with `curl`, `wget` or a local path (`--url=BASE`, or `MC_PULL_URL`), aborts
  when the destination filesystem has less than 4× the archive size free,
  verifies `--sha256` when given, rejects archives with absolute or `..`
  members, extracts into a staging dir and renames it into place (`--force`
  to replace an existing root, old copy removed only after success). Tools are
  launched with `execvp` and a literal argv, so `MC_PULL_URL` cannot inject a
  command. Warns when the destination is on tmpfs, where `--read-only` cannot
  work.
- `soak.sh` gained a `--read-only` cycle (6 cycle kinds now).
- Release artifacts of v1.3.0 (aarch64/armv7l/x86_64/riscv64 + `SHA256SUMS.txt`
  + `test.sh`) were downloaded from the tagged GitHub release: checksums
  verified, all four binaries report `minicontainer 1.3.0`, and the aarch64
  asset passed the full suite on the S905W box.

### Changed
- CI builds the release binaries with **musl** instead of glibc: the four
  cross toolchains come from the `muslcc/x86_64` Docker images (musl.cc
  blocks the IP ranges GitHub Actions runners use, so direct tarball downloads
  are not an option). Assets drop from 443,828–931,880 B to 100,704–125,392 B
  (aarch64 729,416 → 125,392; x86_64 931,880 → 100,704), and a
  new CI step fails the build if any binary exceeds 200 KB, which is how a
  glibc link would show up. Verified by workflow_dispatch run `40857971212`
  (all four arches, static-pie, stripped).

### Fixed
- `--read-only` now fails with an explicit message on a rootfs under `/tmp`:
  the kernel returns `EPERM` for a read-only bind remount of tmpfs, and a
  silent writable rootfs would be worse than an error.

### Verified
- **74/74 PASS** on the TV Box (8 read-only cases, 11 pull cases), binary
  136,712 B aarch64 musl static, zero compiler warnings with both the musl
  cross toolchain and glibc `gcc -static -Wall`.
- 12-iteration soak with the read-only cycle: 0 errors, 0 leaks. The 24 h soak
  is left for a separate run (`ITER=117000 bash soak.sh`).

## [1.3.0] - 2026-10-29

### Added
- **`exec <id> <cmd>`** (`build/src/exec.c`): run a second process inside a
  running container with `setns()` (user → net/uts/ipc → mnt → pid), re-rooting
  through an `O_PATH` fd on `/proc/<pid>/root`, then `fork()` + `execvp`. The
  child joins the container cgroup and installs the same seccomp filter, so it
  sees uid 0, PID ≠ 1, and cannot `mount`. Namespaces identical to the caller's
  are skipped (joining the host netns after the container userns would return
  EPERM). No `nsenter`/iproute2 dependency.
- **`--net=bridge`** (`build/src/bridge.c`): shared bridge `mcbr0`
  (10.88.0.1/16) plus one veth pair per container (`mcv<pid>`/`mcc<pid>`). The
  host side uses `ip` from iproute2; the container side renames to `eth0` and
  sets address/netmask/default route with ioctls only, so the rootfs needs no
  `ip`. The chosen address is recorded in `/run/minicontainer/<id>/ip`. No NAT:
  container↔container and container↔10.88.0.1 work out of the box; LAN access
  requires the host to route 10.88/16. Missing `ip`/kernel modules produce an
  explicit error instead of a silent host fallback.
- `build/soak.sh` — repeated-run leak check (cgroups, mounts, `/run` state,
  MemAvailable delta), optional `ITER=`/`HOUR=`.
- **Loopback bring-up** (`netns_lo_up()` in `bridge.c`, called from `ns.c`): a
  fresh netns starts with `lo` DOWN, so every `127.0.0.1` connect inside a
  container failed (mosquitto clients, local health checks). `isolated` and
  `bridge` containers now have `lo` UP with the 127.0.0.0/8 route.
- **Container `/dev`** (`ns.c: dev_setup()`): a Buildroot rootfs only has device
  nodes when it was built as root, so the dynamic profiles shipped an empty
  `/dev` and every detached run died on `open /dev/null`. The child now mounts a
  private tmpfs on `<rootfs>/dev` before `chroot` and creates
  `null/zero/random/urandom/tty/console` plus the `fd`/`stdin`/`stdout`/`stderr`
  symlinks. `mknod` is refused inside this userns, so the nodes are
  bind-mounted from the host; a warning is printed if `/dev/null` still cannot
  be created.
- `profiles/` — 4 Buildroot profiles (`base-mini`, `web-proxy`, `iot-gateway`,
  `python-edge`) + `build.sh`; two dynamic profiles built and measured.

### Changed
- `release.sh` reads the version from `src/main.c`, stages binary + test/soak/
  bench + profiles + rootfs, and emits `SHA256SUMS.txt` for the tree and the
  tarball; CI attaches `SHA256SUMS.txt` to tagged releases.
- `--net` validation now accepts `host|isolated|bridge`.
- `rm` also clears the `ip` state file.

### Fixed
- `iot-gateway.defconfig` used the legacy `BR2_PACKAGE_CURL` symbol (build
  aborted with "legacy configuration"); now `BR2_PACKAGE_LIBCURL` +
  `BR2_PACKAGE_LIBCURL_CURL`.
- `profiles/build.sh` looked for the toolchain under a stale path and was not
  executable (rc=126).
- CI x86_64: `musl-gcc` from musl-tools has no kernel headers
  (`linux/audit.h: No such file`); the x86_64 matrix entry builds with
  `gcc -static`.

### Verified
- TV Box S905W, kernel 6.18.38-meson64: `test.sh` **54/54 PASS** (5 new `exec`
  cases, 6 new bridge cases: state IP, host→container ping, `eth0`+default
  route, container↔container ping, unique addresses, veth auto-cleanup; 4 new
  `/dev` cases incl. a rootfs with an empty `/dev`).
  `--net=host`/`isolated`, detach lifecycle and cgroup cleanup unaffected.
- Profiles on hardware: `web-proxy` 3 MB (lighttpd 1.4.85), `python-edge`
  24 MB (Python 3.14.8, `import serial, spidev` OK) and `iot-gateway` 15 MB
  (Mosquitto 2.1.2 + curl + sqlite3) run as real containers. A Mosquitto
  broker container on `--net=bridge` (`--ram=32M --cpu=20`, 2.5 MB RSS) received
  a publish from a second container and delivered it to a third one
  (`mosquitto_sub` output `tu-m2-qua-bridge`); `mosquitto_pub -h 127.0.0.1`
  works via `exec` now that `lo` is up. A `web-proxy` server answered
  `GET /` → HTTP 200 for a `python-edge` client container (528 KB RSS, 1 pid).
- Release gate: `test.sh` run 3 times back-to-back on the box → 54/54 each time
  (plus the `/dev` case → 55/55), no leftover cgroups, veth, state dirs or mounts.
- Soak smoke: `ITER=200` → 200 cycles in 146 s, 0 errors, 0 leak reports,
  MemAvailable +3 MB drift (no regression).

### Known limitations added
- Ports below 1024 cannot be bound inside a container: mapped uid 0 has no
  `CAP_NET_BIND_SERVICE` in the init user namespace (same as Docker with
  userns-remap). Use `--net=bridge`/`host` with a high port.

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