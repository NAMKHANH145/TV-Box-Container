# Changelog

All measured numbers refer to the environment recorded in
[progress/VERIFICATION_LOG.md](progress/VERIFICATION_LOG.md). Hardware-only claims are marked.

## 2.0.0 — 2026-11-16

The v2.0 scope from `PLAN_FINAL.md`: run Docker/OCI images unchanged on the box, still one static
binary under the 300 KB CI ceiling.

### Added

- **P0** OCI runtime-spec subset: `--env`, `-w`, `--user`, multiple `mounts[]`, `oci-run`, arch
  guard, json-file log driver with rotation, `gc`, minisign package verification.
- **P1** Image store: Docker-save `load`, `images`, `rmi`, `save`, whiteouts, `build` from a
  Dockerfile subset with cache, `export`/`import` one-file signed bundles.
- **P2** Registry client (distribution-spec): anonymous token flow, manifest lists, `--platform`,
  resumable blob download, `pull --offline` from an OCI layout.
- **P3** CLI parity: `inspect`, `start`, `restart`, `cp`, `--rm`, `--init`, `--restart`,
  `-v` bind mounts, `--ulimit`, `memory.high`/`cpu.weight`, low ports in a private netns, UDP
  publish, bridge `/etc/hosts` peer names, OCI seccomp JSON → BPF, `--emulate` (qemu, slow).
- **P4** Operations: health-gated A/B rootfs updates with atomic symlink flip and rollback,
  signed packages, zstd `--patch-from` delta, `/dev/watchdog` feeder, Prometheus textfile
  metrics, best-effort journald events, YAML-subset compose for up to 16 services.
- **P6** Adoption: `install.sh` for four architectures with SHA256 verification, English README,
  `COMPAT.md`, five recipes, issue/PR templates, security policy, CI matrix and local-test job.
- `version --json` for machine-readable bug reports.

### Changed

- Build defaults to `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`; the aarch64 musl
  binary is **277,400 bytes** with the CI toolchain (284,208 with the local Buildroot one);
  armv7l 260,092, riscv64 248,696, x86_64 253,136 — all under the 300 KB red line.
- Test matrix is **185 cases**: **184 PASS / 0 FAIL / 1 SKIP** on the S905W box (aarch64) and
  178 PASS / 0 FAIL / 7 SKIP in the Ubuntu 24.04 CI container; see the verification log.
- `install.sh` verified on all four architectures (right asset, SHA256 checked, binary runs).
- `minicontainer images` reports allocated size deduplicated by inode, so hardlink farms such as
  `busybox:latest` show 4.4 MB instead of 412 MB.
- `update apply` health checks now work from an A/B slot: the staging directory is world-traversable
  (mkdtemp's 0700 blocked the mapped container uid) and `/dev` is created host-side like `/proc`.
- New `build/sign-package.py` signs packages/bundles in the minisign armor the runtime verifies
  (pure Ed25519, `Ed`; the prehashed `ED` mode is documented as unsupported).
- `install.sh` refuses to replace the binary while containers are running unless `--force`.

### Fixed — found by running the five recipes (gate P6.4, 10/10 on two machines)

- `compose up` **segfaulted** on any service with `ports`/`environment`/`volumes`: the 2-D char
  arrays were cast to `char **`, so `pub_parse()` walked garbage pointers. Real pointer vectors now.
- Dockerfile `COPY dir/ /dst/` nested the directory (`dst/dir/`) instead of copying its contents;
  `build.c` now uses `cp -a <src>/.` for directory sources.
- `-p HOST:CONTAINER` was unreachable from the host itself: it now also DNATs in `OUTPUT`, masquerades
  the `127/8` source onto the bridge, and sets `route_localnet` on `mcbr0` only.
- Recipes: nginx needs `user root;` + `master_process off;` and mosquitto `user root` (single-uid
  user namespace refuses `initgroups`); the Python recipe no longer blocks on `getfqdn()` for ~5 s
  before `listen` when the image has no `/etc/resolv.conf`.

### Known limitations

- Hardware-only gates verified on S905W (2026-11-17): real `/dev/watchdog` reset (magic close +
  kill -9 feeder → board reset ~2 s), five clean reboots with no leftover cgroup/mcbr0/veth/rule/state,
  journald events visible. Still unverified: p50 latency, 6-image corpus, `nginx` + `redis` name
  resolution, 24 h soak.
- Stranger install test (2026-11-17): one-command install in a clean Ubuntu 24.04 container in 22 s,
  alpine `cat /etc/alpine-release` → 3.19.9, busybox httpd over the bridge → curl 200, total 174 s.
  Stock `nginx:alpine` does not run out-of-the-box (initgroups EPERM + non-writable rootfs); use
  `recipes/nginx`.
- Compose over `network: bridge` with a published port is covered by the gateway recipe on both
  machines; compose with `volumes`/`environment` lists still has no dedicated test case.
- A signed update package's success path is not covered by a fixture (unsigned/invalid packages
  are rejected; the shared minisign verifier is tested through `pull`/`import`).
- aarch64, armv7l and riscv64 binaries are cross-built; only x86_64 executes in CI.

## 1.5.4 — 2026-10-30

- 84-case hardware matrix green; signal handler made async-signal-safe; `sdk init/pack`, CI smoke
  job, four Buildroot profiles and release tarballs. Final v1 release.

## 1.0.0 — 2026-10-29

- First working runtime: namespaces, cgroup v2 limits, bridge, seccomp strict filter, logs,
  `ps`/`stats`/`stop`/`rm`, Buildroot rootfs profiles.
