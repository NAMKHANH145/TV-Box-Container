# Contributing to minicontainer

minicontainer is a solo-maintained, course-born project. Contributions are welcome but
please read this first.

## Ground rules

- **Only the maintainer (Khánh) edits `build/src/**`, `build/test.sh`, `.github/**`.**
  External PRs that touch those paths will be closed unless discussed first (open an issue).
- PRs touching docs (`*.md`, `progress/`), profiles, or new test evidence are welcome.
- Never commit binaries, rootfs tarballs, `build/buildroot/`, or logs (see `.gitignore`).
- Never hard-code IP addresses or passwords — use `BOX=`, `SSHPASS_FILE=`, `MC_PULL_URL=`,
  and `<IP_BOX>` in docs.
- Do not force-push, do not rewrite `main` history.

## Before opening a PR

1. Build clean: `cd build && make clean && make` → 0 warnings, binary ≤ 200 KB.
2. Run `bash build/smoke-sdk.sh` (no root needed) → 11/11.
3. If you touched runtime code: run `bash build/test.sh` on a real aarch64 box with cgroup v2
   → expect `TONG=84 FAIL=0`.
4. Run `bash build/soak.sh` (at least `ITER=200`) and paste the final line into the PR.
5. Update `progress/VERIFICATION_LOG.md` and `TASK.md` with any new numbers.

## Reporting bugs

Use the issue template. Include: box model, kernel (`uname -r`), distro, exact command, output,
and whether it reproduces on a second run. For leaks, include `ls /sys/fs/cgroup/minicontainer-*`
and `ls /run/minicontainer`.

## Style

- C: `-O2 -Wall`, 0 warnings. Follow existing style (Vietnamese comments are OK).
- Shell: `set -euo pipefail`, quote variables, no `eval`.
- Docs: Vietnamese for user-facing; numbers must have evidence (log path + date).

## License

By contributing you agree your changes are released under the MIT License (see `LICENSE`).
