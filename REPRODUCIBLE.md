# Reproducing a release build

The published binaries are static musl builds. This is the exact toolchain and command used by CI
and by `build/release.sh`.

## Toolchain

CI uses the maintainer-published Docker images (musl.cc blocks GitHub runner IPs):

```bash
docker run --rm -v "$PWD":/io -w /io/build \
  muslcc/x86_64:aarch64-linux-musl \
  sh -c 'gcc -static -Os -Wall -ffunction-sections -fdata-sections -Wl,--gc-sections \
         -o minicontainer src/*.c && strip -s minicontainer'
```

Replace `aarch64-linux-musl` with `x86_64-linux-musl`, `armv7l-linux-musleabihf` or
`riscv64-linux-musl` for the other assets. The Makefile uses the same flags; the Docker images do
not ship `make`, so CI invokes `gcc` directly.

## Local (Buildroot toolchain)

```bash
cd build
make clean && make && make tools
stat -c '%n %s bytes' minicontainer
sha256sum minicontainer
```

## Verify an artifact

```bash
curl -fsSLO https://github.com/NAMKHANH145/TV-Box-Container/releases/download/v2.0.0/SHA256SUMS.txt
curl -fsSLO https://github.com/NAMKHANH145/TV-Box-Container/releases/download/v2.0.0/minicontainer-linux-aarch64
sha256sum -c SHA256SUMS.txt --ignore-missing
chmod +x minicontainer-linux-aarch64
./minicontainer-linux-aarch64 version --json   # fails on x86_64: wrong arch on purpose
```

`install.sh` performs the same checksum verification before installing and optionally verifies a
minisign signature with `MC_MINISIGN_PUB=/path/release.pub`.

## Bit-for-bit expectations

The source, flags and toolchain image are pinned. `strip` and the compiler's build-id can still make
byte-identical output depend on the toolchain image digest, so the project publishes **SHA256SUMS**
as the authoritative identity rather than promising bit-reproducible builds. Rebuild, compare
`version --json`, `check --json`, the binary size, and run `build/local-test.sh` before trusting a
self-built replacement.
