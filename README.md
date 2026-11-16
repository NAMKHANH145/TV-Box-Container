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
minicontainer export IMAGE -o BUNDLE | import BUNDLE [--verify=KEY]
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

## Security

The attack surface is one setuid-root binary: namespaces + chroot + cgroup + seccomp, no network
listener, no daemon, no management socket. Report vulnerabilities privately as described in
[SECURITY.md](SECURITY.md). Do not open a public issue for an unpatched escape.

## License

MIT — see [LICENSE](LICENSE).
