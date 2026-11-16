# Security policy

## Supported versions

| Version | Security fixes |
|---|---|
| 2.0.x (latest) | yes |
| 1.5.x (N-1) | critical fixes only, until 2.1.0 |
| older | no |

## Reporting a vulnerability

Use GitHub's private vulnerability reporting:
<https://github.com/NAMKHANH145/TV-Box-Container/security/advisories/new>

Do **not** open a public issue for a container escape, privilege escalation, signature-verification
bypass, path traversal during image/package extraction, or a denial of service that affects the
host. Include:

- version and architecture (`minicontainer version --json`),
- kernel and distro (`uname -a`),
- the smallest reproduction you can share, with secrets removed,
- expected impact (host root? data disclosure? reboot?).

Best-effort response: acknowledgement within 72 hours, a fix or mitigation plan within 14 days for
confirmed high-severity issues. No bug bounty is offered.

## Security model

- The binary runs as root by design. There is no rootless mode.
- Isolation is namespaces + `chroot` + cgroup v2 + seccomp. It is **not** a VM and not a hardened
  multi-tenant sandbox.
- Container images and update packages are untrusted input: tar members with absolute paths or
  `..`, device nodes and fifos are rejected; update packages can be signed (minisign Ed25519) and
  the signature is checked before extraction.
- There is no network listener, management socket or background daemon. The only long-lived helper
  processes are per-container log, watchdog and restart keepers created only when the corresponding
  flag is used.
- Secrets must be passed with environment variables (`BOX`, `SSHPASS_FILE`, `MC_PULL_URL`); the
  repository and test scripts never contain credentials.

## Known limitations

- `--net=bridge` has no outbound NAT or DNS.
- Ports below 1024 cannot be bound in the container.
- `--emulate` runs foreign-arch code through qemu with a relaxed seccomp deny-list.
- Docker seccomp profiles are translated through a syscall-name subset.
- The hardware watchdog resets the board if the feeder stops; use `stop` for a clean shutdown.
