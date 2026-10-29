# minicontainer — Container siêu nhẹ cho TV Box

## Tổng quan

`minicontainer` là tool quản lý container siêu nhẹ, không daemon, viết bằng C (musl static), targeting thiết bị nhúng như TV Box (S905W, 1.8GB RAM). So sánh:

| Khía cạnh | minicontainer | Docker |
|-----------|--------------|--------|
| Binary size | ~100-137KB static, musl (v1.4.0) | dockerd ~131MB + containerd ~77MB |
| RAM idle | 0MB (zero-daemon) | ~207MB |
| Start latency | 51-68ms | 2.076s |
| Rootfs size | 1.3MB (Buildroot) | 8.5MB+ (Alpine) |
| Filesystem | chroot + userns (pivot_root not usable in userns) | cgroup namespace |
| Security | userns + seccomp 21 syscall | seccomp + apparmor/selinux |

## Quick Start

Repo: <https://github.com/NAMKHANH145/TV-Box-Container> · Task board: [`TASKS.md`](TASKS.md) · Rootfs profiles: [`profiles/README.md`](profiles/README.md)

### Build
```bash
cd build/
make              # default: musl static
make clean && make # clean rebuild
```

### Build tools (bomb, breakout) for testing
```bash
make tools
```

### Deploy to device
```bash
make deploy BOX=<IP_BOX> SSHPASS=<password>
```

### Run tests
```bash
bash test.sh   # on device, 74 test cases
ITER=117000 bash soak.sh   # ~24h leak/latency soak (default 100 iterations)
```

## Commands

```bash
# Run container (foreground)
minicontainer run --ram 30M --cpu 20 --pids 64 [--io 1M] [--disk 100M] [--net host|isolated|bridge] [--rootfs DIR] [--hostname NAME] [--health-cmd CMD] [--read-only] <cmd> [args...]

# Run detached
minicontainer run -d --name ID --ram 30M --cpu 20 /bin/sleep 60

# Run a second process inside an existing container (setns, not a new container)
minicontainer exec <id> /bin/sh -c 'id -u'

# List containers
minicontainer ps

# Stats for a container
minicontainer stats <id>
minicontainer stats <id> --json

# Stop container
minicontainer stop <id>
minicontainer stop <id> --timeout 1

# Remove container
minicontainer rm <id>
minicontainer rm -f <id>   # force remove

# OCI bundle mode
minicontainer oci-run <bundle-dir>

# systemd service generator (restart on crash, still zero daemon)
minicontainer service create <name> <cmd> [args...]

# Fetch a rootfs profile (curl/wget/local path) into /opt/rootfs-<profile>
minicontainer pull <profile> [--url=BASE] [--dest=DIR] [--sha256=SUM] [--force]

# Version
minicontainer --version
```

### Disk quota (`--disk`)

Kernel 6.18-meson64 has no ext4 project quota, so `--disk SIZE` is enforced
in userspace: the supervisor samples allocated bytes under the rootfs
(`du` semantics, baseline delta) once per second and kills the container's
cgroup on overflow (exit 137). In foreground/systemd mode this costs **no
extra process**; with `-d` it forks one small watchdog per container that
exits when the container does. `RLIMIT_FSIZE = SIZE` still caps any single
file immediately. Worst-case burst is roughly one sampling interval.

### Exec (`exec <id> <cmd>`)

`exec` enters the running container with `setns()` instead of cloning new
namespaces: it joins user → net/uts/ipc → mount → pid, re-establishes the
target root through an `O_PATH` fd on `/proc/<pid>/root`, then `fork()`s
(PID namespaces only affect children) and `execvp`s the command. Namespaces
identical to the caller's are skipped — joining the *host* netns after
entering the container's userns would fail with EPERM. The new process is
attached to the container cgroup, runs under the same seccomp filter, sees
PID 1 as the container init, and has uid 0 inside. No `nsenter`/iproute2
dependency.

### Networking (`--net`)

| Mode | Effect |
|------|--------|
| `host` (default) | no `CLONE_NEWNET`; shares host connectivity |
| `isolated` | new empty netns; only `lo` (brought up, so loopback services work) — no external reach |
| `bridge` | new netns with `eth0` on the shared `mcbr0` bridge (10.88.0.1/16) |

`bridge` creates a veth pair named `mcv<pid>`/`mcc<pid>`, moves the container
end into the netns, and configures it there with ioctls only (so the rootfs
needs no `ip`); the host end is created and bridged with `ip` from iproute2.
The address is derived from the host PID (`10.88.<pid/256>.<pid%256>`,
skipping reserved values) and recorded in `/run/minicontainer/<id>/ip`.
There is no NAT, so bridge containers reach the LAN only if the host routes
the 10.88/16 subnet; they talk to each other and to 10.88.0.1 directly. The
host veth disappears with the container netns. If `ip` is missing or the
kernel lacks `veth`/`bridge`, the run fails with an explicit error rather
than silently falling back.

### Read-only rootfs (`--read-only`)

TV Boxes boot from a µSD/eMMC card that wears out on repeated small writes, so
`--read-only` freezes the rootfs and sends all writes to RAM:

- the rootfs is bind-mounted onto itself inside the container's private mount
  namespace (MS_REC|MS_PRIVATE first, so nothing propagates back to the host)
  and that bind is remounted `ro` after `/proc` is mounted
- `tmpfs` goes on `/tmp` (32M), `/run` (8M) and `/dev/shm` (64M)
- a path the distribution redirects with a symlink (`/var/log -> ../tmp`) is
  skipped, not re-mounted: mounting onto a symlink would shadow `/tmp` with a
  second tmpfs. Its target is already tmpfs.

Writes to `/etc`, `/usr`, `/opt` inside the container then fail with `EROFS`
while the host's copy of the rootfs stays writable. Kernel 6.18 refuses a
read-only bind remount of a tmpfs (`EPERM`), so a rootfs that lives under
`/tmp` fails loudly instead of silently staying writable — keep profiles on
ext4 (e.g. `/opt/rootfs-<profile>`).

### Rootfs profiles (`pull`)

```bash
minicontainer pull web-proxy --url=https://images.example.com/rootfs   # http(s)
minicontainer pull iot-gateway --url=/root/mcpull --dest=/opt/rootfs-iot
minicontainer run --rootfs /opt/rootfs-web-proxy --net=bridge --read-only /usr/sbin/lighttpd -f /etc/lighttpd.conf
```

`pull` fetches `<profile>-rootfs.tar.gz` with `curl` (or `wget`, or a plain
local path), refuses the download if the destination filesystem lacks ~4× the
archive size, verifies `--sha256` when given, rejects archives containing
absolute or `..` members, extracts into a staging directory and renames it
into place (an existing destination is only replaced with `--force`). External
tools are started with `execvp` and a literal argv, so a URL from
`MC_PULL_URL` cannot inject shell commands. Archives are built by
`profiles/build.sh`; see [`profiles/README.md`](profiles/README.md).

### Exec / health interplay

`--health-cmd` runs on the host, so a check that needs to be inside the
container should use `exec` from a wrapper service instead.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Host (TV Box: Amlogic S905W, 1.8GB RAM, eMMC)          │
│  ┌───────────────────────────────────────────────────────┐ │
│  │  minicontainer (122KB, 0 daemon, zero memory overhead) │ │
│  │  ┌─────────────────────────────────────────────────┐ │ │
│  │  │  run.c: clone() + chroot + seccomp + cgroup     │ │ │
│  │  │  ns.c: CLONE_NEWPID/NEWNS/NEWUTS/NEWIPC/NEWUSER │ │ │
│  │  │  exec.c: setns() vao container dang chay       │ │ │
│  │  │  bridge.c: mcbr0 + veth, ioctls trong netns     │ │ │
│  │  │  cgroup.c: memory/cpu/pids/io v2                 │ │ │
│  │  │  disk.c: userspace disk quota watchdog (R2-1)    │ │ │
│  │  │  state.c: ps/stats/stop/rm (via /run filesystem) │ │ │
│  │  │  cli.c: parse flags (2 syntaxes)                │ │ │
│  │  │  oci.c: OCI bundle mode                         │ │ │
│  │  └─────────────────────────────────────────────────┘ │ │
│  │         ↓                                               │ │
│  │  Rootfs (/opt/rootfs: Buildroot 1.3MB, /opt/alpine: 8.5MB) │
│  │  ┌─────────────────────────────────────────────────┐ │ │
│  │  │ Container (PID=1, userns: root→nobody, seccomp) │ │ │
│  │  │  /proc  /dev/null,zero,urandom  /sys (empty)   │ │ │
│  │  │  RAM limit: 30M, CPU: 20%, PIDs: 64, I/O: 1M/s │ │ │
│  │  └─────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Use Cases

1. **Lightweight IoT gateway**: Run MQTT broker (Mosquitto) with 32MB RAM, 20% CPU
2. **Secure sandbox**: Test scripts without polluting host (`/etc`, `/bin` protected)
3. **Background services**: `run -d` with zero daemon, monitor via `stats`
4. **Disk-capped jobs**: `--disk 100M` stops runaway writers before they fill eMMC
5. **OCI bundle**: Run standard OCI bundles with `oci-run`

## Security

- **3-layer defense**: chroot (no host escape) → userns (nobody outside) → seccomp 21 syscalls blocked
- **Cgroup v2**: Hard memory limits via kernel OOM-killer
- **No daemon**: Minimal attack surface
- **Input validation**: `--cpu` 1-100, `--ram`/`--pids`/`--io`/`--disk` > 0, safe names only, malformed sizes rejected

## Files

```
build/
├── Makefile          # make, make tools, make deploy, make clean
├── test.sh           # 74 test cases
├── soak.sh           # leak/memory check over repeated runs (~24h with ITER=117000)
├── bench.sh          # latency & resource benchmark
├── release.sh        # staged tarball + SHA256SUMS (version read from src/main.c)
├── src/              # 14 C modules + common.h
│                     # (main, cli, common, ns, run, exec, bridge, cgroup,
│                     #  disk, seccomp, state, oci, systemd, pull)
├── buildroot/        # Buildroot source for building rootfs (gitignored)
├── oci-test/         # Sample OCI bundle
└── tvbox_defconfig   # Buildroot defconfig for rootfs
profiles/             # 4 rootfs profiles + build.sh + README
.github/workflows/    # multi-arch CI (4 architectures) + tagged release
```

## Build Requirements

- Host: Ubuntu 22.04+ (cross-compile with `aarch64-linux-gnu-gcc`)
- TV Box: Ubuntu 26.04 LTS, kernel 6.18.38-meson64, aarch64
- Build toolchain: `buildroot/output/host/bin/aarch64-buildroot-linux-musl-gcc`

## Known Limitations

- `--sys` not available (kernel prohibits sysfs in userns)
- No OCI mounts/hooks/seccomp-OCI support (subset only)
- `--health-cmd` runs on the host every 5s (not inside the container), in both
  foreground and `-d` mode; a failing check kills the cgroup and leaves a
  `health_failed` marker in detached mode
- `--disk` is a sampling watchdog (1s), not a kernel quota: a fast burst can
  overshoot by ~1s of writes; a single file is still hard-capped by RLIMIT_FSIZE
- Ports < 1024 cannot be bound inside a container (mapped root has no
  `CAP_NET_BIND_SERVICE` in the init userns) — run servers on 8080+
- `--net=bridge` has no NAT/DNS: bridge containers reach only the bridge
  subnet (10.88.0.0/16) and 10.88.0.1 unless the host routes/forwards it
- `exec` needs the container's PID still alive and read access to
  `/proc/<pid>/ns/*`; it does not copy env or working directory from the
  caller
- `--read-only` needs the rootfs on a real filesystem: the kernel rejects a
  read-only bind remount of tmpfs, so `--rootfs` under `/tmp` errors out
- `pull` does not verify a signature or a pinned key — pass `--sha256` for
  content you did not build yourself; no NAT/DNS for pulled profiles either
- Multi-arch binaries are built from portable seccomp (`__NR_*`); the tagged
  CI release publishes all four and they run (x86_64 native, armv7l/riscv64/
  aarch64 under qemu-user), but only aarch64 has been tested on real hardware

## License

MIT or Apache-2.0 (to be decided)