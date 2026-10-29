# minicontainer — Container siêu nhẹ cho TV Box

## Tổng quan

`minicontainer` là tool quản lý container siêu nhẹ, không daemon, viết bằng C (musl static), targeting thiết bị nhúng như TV Box (S905W, 1.8GB RAM). So sánh:

| Khía cạnh | minicontainer | Docker |
|-----------|--------------|--------|
| Binary size | ~114KB static (v1.2.0) | dockerd ~131MB + containerd ~77MB |
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
bash test.sh  # on device, 39 test cases
```

## Commands

```bash
# Run container (foreground)
minicontainer run --ram 30M --cpu 20 --pids 64 [--io 1M] [--disk 100M] [--net host|isolated] [--rootfs DIR] [--hostname NAME] [--health-cmd CMD] <cmd> [args...]

# Run detached
minicontainer run -d --name ID --ram 30M --cpu 20 /bin/sleep 60

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

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Host (TV Box: Amlogic S905W, 1.8GB RAM, eMMC)          │
│  ┌───────────────────────────────────────────────────────┐ │
│  │  minicontainer (114KB, 0 daemon, zero memory overhead) │ │
│  │  ┌─────────────────────────────────────────────────┐ │ │
│  │  │  run.c: clone() + chroot + seccomp + cgroup     │ │ │
│  │  │  ns.c: CLONE_NEWPID/NEWNS/NEWUTS/NEWIPC/NEWUSER │ │ │
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
├── test.sh           # 39 test cases
├── src/              # 11 C modules (common, ns, cgroup, run, state, cli, oci, seccomp, disk, systemd, main)
├── buildroot/        # Buildroot source for building rootfs (gitignored)
├── oci-test/         # Sample OCI bundle
└── tvbox_defconfig   # Buildroot defconfig for rootfs
profiles/             # 4 rootfs profiles + build.sh + README
.github/workflows/    # multi-arch CI (4 architectures)
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
- `--net=bridge` not implemented; the flag is rejected instead of silently
  falling back to host networking
- No `exec` into running container (use `nsenter`)
- Multi-arch binaries are built from portable seccomp (`__NR_*`) but CI has not
  been exercised on armv7l/riscv64 hardware

## License

MIT or Apache-2.0 (to be decided)