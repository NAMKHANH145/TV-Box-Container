#!/bin/bash
# R4-3/P6.6: build one release tarball with checksums and optional minisign signatures.
#   ./release.sh                    # native architecture
#   ARCH=aarch64 ./release.sh       # explicit target (requires a matching toolchain)
#   MINISIGN_KEY=~/.minisign/key ./release.sh
#     creates <tarball>.sig and minicontainer.sig (minisign Ed25519, same format
#     that `minicontainer update apply --verify=KEY` and `pull --sig=KEY` verify).
set -e
cd "$(dirname "$0")"
ROOT=$(CDPATH= cd -- .. && pwd)

VERSION=$(sed -n 's/.*#define MC_VERSION "\([^"]*\)".*/\1/p' src/common.h)
[ -n "$VERSION" ] || { echo "khong doc duoc MC_VERSION tu src/common.h"; exit 1; }
ARCH_EXPLICIT=0
if [ -n "${ARCH:-}" ]; then ARCH_EXPLICIT=1; else ARCH=$(uname -m); fi

echo "=== Building clean release (v$VERSION, requested arch $ARCH) ==="
make clean
make
echo "Build OK: $(ls -la minicontainer)"

# The default Makefile toolchain is aarch64 while `uname -m` may report x86_64,
# so trust the ELF header (e_machine at offset 18) instead of the host uname:
# a tarball named -linux-x86_64 must contain an x86_64 binary for install.sh.
elf_arch() {
  case "$(od -An -tx1 -j18 -N2 "$1" | tr -d ' \n')" in
    3e00) echo x86_64 ;;
    b700) echo aarch64 ;;
    2800) echo armv7l ;;
    f300) echo riscv64 ;;
    *) echo unknown ;;
  esac
}
BUILT_ARCH=$(elf_arch minicontainer)
if [ "$ARCH_EXPLICIT" -eq 1 ] && [ "$BUILT_ARCH" != "$ARCH" ]; then
  echo "release: LỖI — binary build ra $BUILT_ARCH nhưng ARCH=$ARCH; không đóng gói sai tên." >&2
  echo "  build đúng arch trước, ví dụ:" >&2
  echo "    make clean && make CC=aarch64-linux-gnu-gcc STRIP=aarch64-linux-gnu-strip" >&2
  exit 1
fi
if [ "$BUILT_ARCH" = unknown ]; then
  echo "release: không đọc được e_machine của minicontainer" >&2
  exit 1
fi
if [ "$BUILT_ARCH" != "$ARCH" ]; then
  echo "release: arch thực của binary là $BUILT_ARCH (uname nói $ARCH) — dùng $BUILT_ARCH"
  ARCH=$BUILT_ARCH
fi

STAGE="release/minicontainer-${VERSION}-linux-${ARCH}"
TARBALL="minicontainer-${VERSION}-linux-${ARCH}.tar.gz"

echo "=== Creating release directory ==="
rm -rf release
mkdir -p "$STAGE/rootfs" "$STAGE/profiles"

echo "=== Copying artifacts ==="
cp minicontainer test.sh smoke-sdk.sh bench.sh "$STAGE/"
cp "$ROOT/install.sh" "$STAGE/"
cp "$ROOT/README.md" "$ROOT/COMPAT.md" "$ROOT/CHANGELOG.md" "$ROOT/SECURITY.md" \
   "$ROOT/CONTRIBUTING.md" "$ROOT/REPRODUCIBLE.md" "$ROOT/LICENSE" "$STAGE/"
cp -a "$ROOT/recipes" "$STAGE/"
for d in "$ROOT"/profiles/*.defconfig; do cp "$d" "$STAGE/profiles/"; done
cp "$ROOT/profiles/README.md" "$STAGE/profiles/"

# rootfs: lay image Buildroot gan nhat (neu da don thi lay tarball da luu)
if [ -f buildroot/output/images/rootfs.tar.gz ]; then
  cp buildroot/output/images/rootfs.tar.gz "$STAGE/rootfs/rootfs-base.tar.gz"
elif [ -f rootfs-images/web-proxy-rootfs.tar.gz ]; then
  cp rootfs-images/*.tar.gz "$STAGE/rootfs/"
else
  echo "CHU Y: khong co rootfs image — dung profiles/build.sh <profile> de build"
fi

echo "=== Verification ==="
du -sh "$STAGE"
ls -la "$STAGE" "$STAGE/rootfs" "$STAGE/profiles"

echo "=== Checksums (SHA256) ==="
( cd "$STAGE" && sha256sum minicontainer install.sh test.sh smoke-sdk.sh rootfs/* profiles/*.defconfig 2>/dev/null | sort > SHA256SUMS.txt )
cat "$STAGE/SHA256SUMS.txt"

echo "=== Signing (optional) ==="
if [ -n "${MINISIGN_KEY:-}" ]; then
  command -v minisign >/dev/null || { echo "MINISIGN_KEY set but minisign not installed"; exit 1; }
  minisign -S -s "$MINISIGN_KEY" -m "$STAGE/minicontainer" -x "$STAGE/minicontainer.sig"
  echo "Signed $STAGE/minicontainer"
else
  echo "MINISIGN_KEY chua dat — bo qua .sig (van co SHA256SUMS)"
fi

echo "=== Creating tarball ==="
cd release
tar czf "../${TARBALL}" "minicontainer-${VERSION}-linux-${ARCH}/"
sha256sum "../${TARBALL}" | tee "../${TARBALL}.sha256"
if [ -n "${MINISIGN_KEY:-}" ]; then
  minisign -S -s "$MINISIGN_KEY" -m "../${TARBALL}" -x "../${TARBALL}.sig"
  echo "Signed ../${TARBALL}"
fi

echo "=== Release tarball: ${TARBALL} ==="
echo "Reproduce: cd build && make clean && make"
echo "Flags: -static -Os -Wall -ffunction-sections -fdata-sections -Wl,--gc-sections"
echo "Checksums: release/${STAGE}/SHA256SUMS.txt and ${TARBALL}.sha256"
