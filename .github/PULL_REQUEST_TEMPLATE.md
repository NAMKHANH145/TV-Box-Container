## What changed

<!-- One or two sentences: the problem and the fix. -->

## Verification

- [ ] `cd build && make clean && make CC=gcc STRIP=strip` — 0 warnings
- [ ] `cd build && make` — aarch64 musl cross-build, 0 warnings
- [ ] `bash build/test.sh` or `bash build/local-test.sh` — new count and failures reported below
- [ ] New behavior has a case in `build/test.sh` (if user-visible)
- [ ] Measured numbers copied into `progress/VERIFICATION_LOG.md` (no invented values)

Test result:

```text
PASS=... FAIL=... SKIP=... TONG=...
```

## Checklist

- [ ] No hard-coded IP addresses, passwords or private keys
- [ ] No binaries, buildroot output, logs or generated rootfs committed
- [ ] Docs updated (`README.md`, `COMPAT.md`, `PLAN_FINAL.md`) when behavior changed
- [ ] Scope stays inside the one-binary, zero-daemon design
