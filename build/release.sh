#!/bin/bash
# R4-3: tao release tarball kem checksum.
#   cach dung: ./release.sh   (hoac ARCH=riscv64 ./release.sh)
set -e
cd "$(dirname "$0")"

VERSION=$(sed -n 's/.*#define VERSION "\([^"]*\)".*/\1/p' src/main.c)
[ -n "$VERSION" ] || { echo "khong doc duoc VERSION tu src/main.c"; exit 1; }
ARCH="${ARCH:-$(uname -m)}"
STAGE="release/minicontainer-${VERSION}-linux-${ARCH}"
TARBALL="minicontainer-${VERSION}-linux-${ARCH}.tar.gz"

echo "=== Building clean release (v$VERSION, $ARCH) ==="
make clean
make
echo "Build OK: $(ls -la minicontainer)"

echo "=== Creating release directory ==="
rm -rf release
mkdir -p "$STAGE/rootfs" "$STAGE/profiles"

echo "=== Copying artifacts ==="
cp minicontainer test.sh smoke-sdk.sh bench.sh "$STAGE/"
for d in ../profiles/*.defconfig; do cp "$d" "$STAGE/profiles/"; done
cp ../profiles/README.md "$STAGE/profiles/"
for f in ../README.md ../GUIDE.md ../FINAL.md ../PLAN.md ../TASK.md ../LICENSE; do
  cp "$f" "$STAGE/" 2>/dev/null || true
done

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
( cd "$STAGE" && sha256sum minicontainer test.sh rootfs/* profiles/*.defconfig 2>/dev/null | sort > SHA256SUMS.txt )
cat "$STAGE/SHA256SUMS.txt"

echo "=== Creating tarball ==="
cd release
tar czf ../${TARBALL} minicontainer-${VERSION}-linux-${ARCH}/
sha256sum ../${TARBALL} | tee ../${TARBALL}.sha256
echo "=== Release tarball: ${TARBALL} ==="