# minicontainer

[![Build and Release](https://github.com/NAMKHANH145/TV-Box-Container/actions/workflows/build.yml/badge.svg)](https://github.com/NAMKHANH145/TV-Box-Container/actions/workflows/build.yml)

A zero-daemon container runtime in C for small Linux devices — one static binary, no background
service, no containerd, no runc. It runs OCI/Docker images and rootfs tarballs on a 1.8 GB TV Box
(Amlogic S905W, aarch64, cgroup v2 only) and on ordinary x86_64 Linux.

**v2.0.0** · aarch64 musl binary **277 KB** · `0 MB` RAM when idle · MIT

## Why

| | minicontainer | Docker on the same box |
|---|---|---|
| Idle processes | **0** (nothing runs until you run a container) | dockerd + containerd |
| Engine size | **~280 KB** (one static binary) | hundreds of MB |
| Install | copy one file | package manager + daemon + service |
| Root filesystem switch | `chroot` (works under this kernel's userns) | overlayfs + pivot_root |
| Scope | run images, pull, build, logs, A/B update | full Docker platform |

It is deliberately small: namespaces + cgroup v2 + seccomp in ~6k lines of C. No HTTP API, no
management socket, no persistent supervisor.

## Quickstart (60 seconds)

```bash
# 1. install the release binary (supported: x86_64, aarch64, armv7l, riscv64)
curl -fsSL https://github.com/NAMKHANH145/TV-Box-Container/releases/latest/download/install.sh | sudo sh

# 2. check the kernel and permissions
sudo minicontainer check

# 3. run a container from an image
sudo minicontainer pull alpine:3.19
sudo minicontainer run --image=alpine-3.19 --ram=64M /bin/sh -c 'cat /etc/alpine-release'

# 4. detached + logs
sudo minicontainer run -d --name web --image=alpine-3.19 --ram=64M /bin/sleep 300
sudo minicontainer ps
sudo minicontainer logs web
sudo minicontainer stop web && sudo minicontainer rm web

# 5. serve something over the bridge (busybox httpd; ports < 1024 are not bindable)
sudo minicontainer pull busybox:latest
sudo minicontainer run -d --name web --image=busybox-latest --ram=32M --net=bridge -p 8080:8080 \
  /bin/sh -c 'mkdir -p /tmp/www && echo hello > /tmp/www/index.html && exec httpd -f -p 8080 -h /tmp/www'
curl http://127.0.0.1:8080/
sudo minicontainer stop web && sudo minicontainer rm web
```

Need nginx? It drops privileges (`user nginx;`) and fails with `initgroups: EPERM` under the
mapped-root userns. Use the shipped recipe instead:

```bash
sudo minicontainer pull nginx:alpine
sudo minicontainer build -t recipe-nginx:1 recipes/nginx
sudo minicontainer run -d --name web --image=recipe-nginx:1 --ram=64M --net=bridge -p 8080:8080 \
  nginx -g 'daemon off;'
curl -s http://127.0.0.1:8080/ | grep -o 'nginx recipe is running'
```

No network? Use a rootfs tarball or an OCI layout on disk:

```bash
sudo minicontainer pull base-mini --url=/srv/rootfs --dest=/opt/rootfs --sha256=<sum>
sudo minicontainer run --rootfs=/opt/rootfs /bin/sh
```

## What works

| Area | Commands |
|---|---|
| Containers | `run` (foreground/detached), `ps`, `stats`, `logs`, `exec`, `cp`, `stop`, `rm`, `start`, `restart`, `inspect` |
| Isolation | PID/MNT/UTS/IPC/USER/NET namespaces, seccomp, `--read-only`, `--init`, `--rm` |
| Resources | cgroup v2 `memory.max`/`memory.high`, `cpu.max`/`cpu.weight`, `pids.max`, `io.max`, disk watchdog, `--ulimit` |
| Networking | `host`, `isolated`, `bridge` (veth + NAT), TCP/UDP publish, container names in `/etc/hosts` |
| Images | `pull` from a registry or an OCI layout, `build` from a Dockerfile subset, `load`/`save`/`rmi`, `export`/`import` signed bundles |
| Filesystems | `-v host:container[:ro]`, `--mount bind|tmpfs|proc|devpts`, multiple OCI `mounts[]` |
| Operations | A/B rootfs update with health gate and rollback, signed packages, zstd delta, Prometheus textfile metrics, journald events, hardware watchdog feeder |
| Orchestration | `compose` (YAML subset, up to 16 services), `service create` systemd unit |
| Diagnostics | `check`, `check --json`, `version --json`, `--emulate` (qemu, slow) |

## CLI at a glance

```text
minicontainer run [--name=ID] [-d] [--rm] [--restart=no|on-failure|always] [--init]
                 [--rootfs=DIR | --image=NAME] [--ram=64M] [--cpu=20] [--pids=64]
                 [--memory-high=32M] [--cpu-weight=100] [--io=1M] [--disk=100M]
                 [--net=host|isolated|bridge] [-p 8080:80[/udp]] [-v SRC:DST[:ro]]
                 [--env=K=V] [-w DIR] [--read-only] [--seccomp=default|strict|FILE.json]
                 [--ulimit nofile=1024] [--health-cmd=CMD] [--emulate] CMD [ARGS...]
minicontainer ps|stats|logs|inspect|stop|rm|start|restart|exec|cp ...
minicontainer pull REF | pull --offline=DIR | images | load | save | rmi | gc
minicontainer build -t NAME:tag [-f Dockerfile] [--no-cache] [--platform=linux/arm64] CONTEXT
minicontainer export IMAGE -o BUNDLE | export [--arch=ARCH] IMAGE IMAGE... -o MULTI | import BUNDLE [--verify=KEY]
minicontainer update apply PKG [--verify=KEY] [--health-cmd=CMD] [--link=PATH]
minicontainer update status|rollback|current|diff|patch ...
minicontainer compose [-f FILE] up|down|ps
minicontainer metrics [--stdout] [--dir=DIR]
minicontainer watchdog [--pid=PID] [--timeout=30] [--device=/dev/watchdog]
minicontainer check [--json] | version [--json] | sdk init|pack | service create
```

## Compose-lite

A strict subset of `docker-compose.yml` (not a full YAML engine):

```yaml
services:
  api:
    image: python:3.12-alpine
    command: ["python", "-m", "http.server", "8000"]
    network: bridge
    memory: 64M
  web:
    image: nginx:alpine
    ports: ["8080:80"]
    network: bridge
    restart: always
```

```bash
sudo minicontainer compose -f compose.yml up
sudo minicontainer compose -f compose.yml ps
sudo minicontainer compose -f compose.yml down
```

Supported keys: `image`, `rootfs`, `command`, `ports`, `environment`, `volumes`, `network`
(`bridge`/`host`/`isolated`), `restart`, `memory`, `cpus`. No `depends_on`, `build`, secrets,
healthchecks or profiles — build images first, then reference them by name.

## A/B rootfs updates

A package is a tar (`.tar`, `.tar.gz`, `.tar.zst`) containing `rootfs/` and an optional
`manifest.json`:

```json
{ "arch": "aarch64", "version": "2.0.1" }
```

```bash
# health is checked on the staged tree BEFORE the inactive slot is touched
sudo minicontainer update apply /srv/rootfs-2.0.1.tar.gz \
  --verify=/etc/minicontainer/release.pub \
  --link=/opt/rootfs \
  --health-cmd='wget -qO- http://127.0.0.1:8080/health'

sudo minicontainer update status
sudo minicontainer update rollback   # switch back to the last good slot
```

Slots live under `/var/lib/minicontainer/update/slots/{A,B}`; `current` is an atomic symlink and
`/opt/rootfs` follows `current/rootfs`. A bad package leaves the running slot and the fallback
untouched. `--verify` checks a minisign Ed25519 signature (`pkg.sig`); packages with `..` members,
device nodes, a missing `/bin/sh`, or a foreign architecture are rejected.

Delta updates (requires `zstd` on the host):

```bash
minicontainer update diff old.tar.gz new.tar.gz -o update.patch.zst
minicontainer update patch old.tar.gz update.patch.zst -o new-restored.tar.gz
```

## Monitoring and watchdog

```bash
# Prometheus node_exporter textfile collector
sudo minicontainer metrics --dir=/var/lib/minicontainer/metrics
# -> minicontainer_up{id="web"} 1
#    minicontainer_memory_bytes{id="web"} ...
#    minicontainer_cpu_seconds_total{id="web"} ...

# Feed /dev/watchdog while a container is alive (box only; magic close on stop)
sudo minicontainer watchdog --pid="$(cat /run/minicontainer/web/pid)" --timeout=30
```

Events (`container start/stop/exit`, `update applied/rolled back`) are sent best-effort to
journald; there is no libsystemd dependency and no event is required for operation.

## Limits (read this before deploying)

- **Linux only**, **root required**, **cgroup v2 only** (no cgroup v1 fallback).
- Root switch uses **`chroot`**, not `pivot_root` (pivot_root returns `EPERM` under this kernel's
  user namespace).
- `--net=bridge` has **no DNS and no outbound NAT**; containers reach each other by name through
  `/etc/hosts`, and published ports work. Ports below 1024 cannot be bound inside the container.
- Images are OCI/Docker **subset**: no volumes-from, no full compose, no Kubernetes, no rootless.
- Docker seccomp JSON is supported through a **name table subset**, not every syscall.
- `--emulate` runs a foreign-arch rootfs through qemu-user; expect 4–20× slowdown and reduced
  seccomp isolation.
- Hardware numbers for the S905W (p50 latency, OOM behavior, watchdog reset) are **not yet
  measured**; CI numbers below are x86_64.

See [COMPAT.md](COMPAT.md) for a requirement-by-requirement table with verification commands.

## Support model

| Version | Status |
|---|---|
| latest (v2.0.x) | supported |
| previous minor (v1.5.x) | security fixes only |
| older | unsupported |

`supported` means the x86_64 test matrix and the release build pass. `experimental` means
build-verified only: **aarch64 runtime on the S905W, armv7l, riscv64, hardware watchdog reset,
journald delivery, and compose over bridge**. Deprecations are announced in
[CHANGELOG.md](CHANGELOG.md) one minor release before removal.

## Build and test

```bash
cd build
make                         # default: buildroot aarch64 musl toolchain
make CC=gcc STRIP=strip      # native glibc for the local test matrix
make tools                   # bomb/breakout helpers for test.sh

bash test.sh                 # full matrix on the box (root)
bash local-test.sh           # same matrix on any root Linux host
bash smoke-sdk.sh            # 11 checks, no root, no box (CI)
```

Current evidence: **176 PASS / 0 FAIL / 7 SKIP (183 total)** on x86_64 glibc, Ubuntu 24.04
privileged container, kernel 6.8.0-124, cgroup v2. The aarch64 musl binary is cross-built
(280,112 B, 0 warnings) but has not been executed on the TV Box yet. Details and every number are
in [progress/VERIFICATION_LOG.md](progress/VERIFICATION_LOG.md) and [PLAN_FINAL.md](PLAN_FINAL.md).

## Documentation

| File | Content |
|---|---|
| [COMPAT.md](COMPAT.md) | kernel/arch/feature matrix, each row with a verification command |
| [GUIDE.md](GUIDE.md) | detailed build, deploy, command and troubleshooting guide (Vietnamese) |
| [FINAL.md](FINAL.md) | end-to-end architecture, syscall flow, kernel traps, evidence |
| [PLAN_FINAL.md](PLAN_FINAL.md) | v2.0 plan (P0–P6), gates and measured results |
| [TASK.md](TASK.md) | work split, definition of done, handoff |
| [progress/](progress/) | phase logs, changelog, verification evidence |
| [recipes/](recipes/) | nginx, Node, Python, Mosquitto and a two-service gateway |
| [demo-cross-ship/](demo-cross-ship/) | 3 runnable demos (v1 two-way ship, v2 pip wheels per arch, v3 hardware HAL) with logs |
| [PLAN_NOV22.md](PLAN_NOV22.md) | plan: promote source packaging into a real `minicontainer pack/unpack` feature |

## Performance (measured 22 Nov 2026)

Same workload on both machines: start a container running `/bin/true`, N=20 runs per runtime,
invoked back-to-back. Docker used `alpine:3.19`; minicontainer used the same Alpine rootfs
(`/opt/rootfs` on the box, an extracted copy inside the privileged harness on the laptop).

| Metric | TV Box S905W (aarch64) | Laptop (x86_64) |
|---|---|---|
| minicontainer start, p50 / p95 / max | **64 / 77 / 95 ms** | **2 / 5 / 19 ms** |
| Docker start, p50 / p95 / max | 1641 / 1743 / 2541 ms | 568 / 593 / 600 ms |
| Speedup | **~26×** | **~284×** |
| minicontainer idle processes / RSS | **0 / 0 KB** | **0 / 0 KB** |
| Docker daemon idle RSS (dockerd + containerd + shims) | 250 MB (256.320 KB) | 120 MB (123.444 KB) |
| minicontainer RSS while container runs | 512 KB | 924 KB |
| Docker daemon RSS increase while container runs | +14.6 MB (14.988 KB) | +10.7 MB (10.912 KB) |
| CPU for one minicontainer run (real / sys) | 0.038 / 0.017 s | 0.002 / 0.002 s |
| CPU for one Docker run (real / user+sys) | 1.679 / 0.136 s | 0.560 / 0.000 s |
| minicontainer binary / state on disk | 284.208 B / 227 MB | 1.205.776 B / 8 KB |
| Docker images + containers on disk | 320.3 MB + 6.3 MB | 211.7 MB + 0 B |

Zero-daemon is measured, not assumed: no `minicontainer` process exists between runs, while
Docker keeps 120–250 MB resident. The 284 KB binary is the shipped musl artifact; the 1.2 MB
x86_64 build exists only for the laptop test harness.

Caveats: on the box both runtimes run directly as root; on the laptop minicontainer runs inside a
privileged `ubuntu:24.04` harness (no passwordless sudo) while Docker runs on the host. The box
Docker daemon was already serving the user's Portainer/Samba containers, so its baseline includes
those shims. These are cold-start latencies of a trivial container, not throughput benchmarks.
Raw numbers and commands: [progress/VERIFICATION_LOG.md](progress/VERIFICATION_LOG.md).

## Security

The attack surface is one setuid-root binary: namespaces + chroot + cgroup + seccomp, no network
listener, no daemon, no management socket. Report vulnerabilities privately as described in
[SECURITY.md](SECURITY.md). Do not open a public issue for an unpatched escape.

## License

MIT — see [LICENSE](LICENSE).
