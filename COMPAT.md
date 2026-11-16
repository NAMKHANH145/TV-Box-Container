# Compatibility

`minicontainer` is a Linux runtime, not a portability layer. Every row below is a real requirement
with the command used to check it. If a mandatory row fails, `minicontainer check` exits non-zero.

## Mandatory

| Requirement | Why | Verify |
|---|---|---|
| Linux kernel ≥ 5.0 | user namespaces, cgroup v2, modern seccomp | `uname -r` |
| cgroup v2 mounted at `/sys/fs/cgroup` | memory/cpu/pids/io limits; **no cgroup v1 fallback** | `test -f /sys/fs/cgroup/cgroup.controllers && cat /sys/fs/cgroup/cgroup.controllers` |
| user namespaces enabled | uid/gid mapping for the container | `cat /proc/sys/user/max_user_namespaces` (or `kernel.unprivileged_userns_clone=1`) |
| root | `chroot`, cgroup creation and mount setup | `id -u` → `0` |
| `/bin/sh` in every rootfs/image | health checks, shell commands | `test -x /opt/rootfs/bin/sh` |
| xattr support on the data filesystem | preserve file capabilities from image layers | `setfattr -n user.test -v 1 /var/lib/test && getfattr -n user.test /var/lib/test` |
| writable `/run` and `/var/lib` | state and image store | `minicontainer check --json` |

## Feature requirements

| Feature | Requirement | Verify |
|---|---|---|
| `--net=bridge` | `ip` (iproute2) and `iptables` with NAT | `command -v ip iptables && iptables -t nat -L` |
| `pull registry` | `curl` and outbound HTTPS | `minicontainer check` (`curl` row) |
| `--seccomp=FILE.json` | JSON profile using syscall names in the built-in subset | `minicontainer run --seccomp=profile.json ...` |
| `update ... diff/patch` | `zstd` on the host | `command -v zstd` |
| `update apply --health-cmd` | a working rootfs and the same kernel features as `run` | `minicontainer update apply ... --health-cmd='true'` |
| hardware watchdog | `/dev/watchdog` exposed by the board | `test -c /dev/watchdog` |
| journald events | `/run/systemd/journal/socket` (optional) | `test -S /run/systemd/journal/socket` |
| `service create` | systemd | `systemctl --version` |
| OCI bundle | `ociVersion` subset, `process.args`, `env`, `cwd`, `mounts[]` | `minicontainer oci-run BUNDLE` |

## Architectures

| Arch | Binary | Status |
|---|---|---|
| x86_64 | musl static (CI) / glibc static (local tests) | supported; full local matrix passes |
| aarch64 | musl static (release) | built and cross-compiled; **runtime on S905W not yet verified** |
| armv7l | musl static (CI) | build-only, experimental |
| riscv64 | musl static (CI) | build-only, experimental |
| i386 | not released | build may work, unsupported |

## Root filesystem

| Item | Support |
|---|---|
| Buildroot rootfs | yes (primary target) |
| Alpine / musl | yes (absolute `/bin/sh → /bin/busybox` symlinks are handled) |
| glibc images | yes if the rootfs ships its own loader and libs |
| overlayfs | used by the image store; `chroot` is used at runtime |
| `pivot_root` | **not used** — returns `EPERM` under this kernel's userns |
| read-only rootfs | `--read-only`; writable paths become tmpfs, symlinks are skipped |
| device nodes inside images | rejected by image extraction; `/dev` is created by the runtime |

## Networking

| Item | Support |
|---|---|
| `--net=host` | yes (shares the host netns) |
| `--net=isolated` | yes (new netns, `lo` up) |
| `--net=bridge` | yes, 10.88.0.0/16, veth + iptables NAT |
| DNS between containers | **no**; use names in `/etc/hosts` (bridge) or IPs |
| outbound NAT from bridge | **no** |
| TCP/UDP publish | yes, `-p host:container[/udp]` |
| container port < 1024 | **no** (`CAP_NET_BIND_SERVICE` is not granted) |

## Explicitly unsupported

- cgroup v1 kernels, non-Linux hosts.
- Rootless operation (the runtime needs root; userns is used *inside* the container).
- Full Docker Compose, Kubernetes, Swarm, image push, buildkit.
- GPU, swap accounting, NUMA, device passthrough.
- Live migration, checkpoint/restore.
- Docker `default.json` seccomp profiles that reference syscalls outside the built-in name table.

## Verified environment

The numbers in this repository were produced on:

- x86_64, Ubuntu 24.04 privileged container, host kernel `6.8.0-124-generic`, cgroup v2.
- `build/local-test.sh`: **176 PASS / 0 FAIL / 7 SKIP (183 total)**.
- aarch64 musl cross-build: 280,112 bytes, 0 warnings.

The S905W (`6.18.38-meson64`, 1.8 GB RAM) is the target, but hardware-only rows above (watchdog
reset, reboot loops, p50) are still pending. See [progress/VERIFICATION_LOG.md](progress/VERIFICATION_LOG.md).
