# Contributing

## Before you start

- Read `AGENTS.md` (project rules), then `README.md`, `COMPAT.md` and `PLAN_FINAL.md`.
- One binary, zero daemon, no new runtime dependency. A feature that needs a supervisor, an HTTP
  API or cgroup v1 is out of scope.
- Never commit IP addresses, passwords, private keys, built rootfs images or `build/buildroot/`.

## Build

```bash
cd build
make clean && make && make tools                 # aarch64 musl (default toolchain)
make clean && make CC=gcc STRIP=strip            # native x86_64 for tests
```

Both builds must be **0 warnings**. The release build uses `-Os` with section GC to stay under the
300 KB CI ceiling.

## Test

```bash
cd build
bash smoke-sdk.sh          # no root, no hardware
bash local-test.sh         # root on any Linux with cgroup v2
bash test.sh               # full matrix on the S905W (root)
```

Never run `test.sh` and `soak.sh` at the same time: their cleanup assertions are global. Any new
number must be written to `progress/VERIFICATION_LOG.md`; do not invent measurements. If hardware
was not available, say so explicitly.

## Commits and pull requests

- Small, focused commits; stage files by name (`git add path`), never `git add -A`.
- Do not force-push `main` or rewrite shared history.
- Explain *why* in the commit body; include the test command and result for user-visible changes.
- A pull request needs: build clean on both toolchains, a test case for new behavior, docs updated,
  and no secrets.

## Adding a seccomp syscall name

`build/src/seccomp.c` contains the JSON-profile name table and the `__NR_*` ifdefs for four
architectures. Add the name with the correct `#ifdef`, then rebuild every architecture in the CI
matrix. Do not switch the strict filter to a deny-list.

## Adding a recipe

Create `recipes/<name>/` with `Dockerfile`, README (build, run, verify, logs), and `expected.txt`.
Recipes use only documented CLI flags and must not embed host paths, addresses or credentials.

## Security

Report vulnerabilities privately per `SECURITY.md`. Do not include exploit details in a public
issue or pull request.
