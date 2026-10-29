#!/bin/bash
# R4-3: Create release tarball
set -e
cd /home/namkhanh/TV-Box/build

VERSION="1.2.0"
ARCH="aarch64"
TARBALL="minicontainer-${VERSION}-linux-${ARCH}.tar.gz"

echo "=== Building clean release ==="
make clean
make
echo "Build OK: $(ls -la minicontainer)"

echo "=== Creating release directory ==="
rm -rf release
mkdir -p release/minicontainer-${VERSION}/rootfs
mkdir -p release/minicontainer-${VERSION}/bin

echo "=== Copying artifacts ==="
cp minicontainer release/minicontainer-${VERSION}/minicontainer
cp test.sh release/minicontainer-${VERSION}/test.sh
cp buildroot/output/images/rootfs.tar.gz release/minicontainer-${VERSION}/rootfs.tar.gz
cp ../profiles/iot-gateway.defconfig release/minicontainer-${VERSION}/iot-gateway.defconfig 2>/dev/null || true
cp ../README.md release/minicontainer-${VERSION}/README.md 2>/dev/null || true
cp ../CHANGELOG.md release/minicontainer-${VERSION}/CHANGELOG.md 2>/dev/null || true
cp ../GUIDE.md release/minicontainer-${VERSION}/GUIDE.md 2>/dev/null || true
cp ../BAOCAO.md release/minicontainer-${VERSION}/BAOCAO.md 2>/dev/null || true

echo "=== Verification ==="
echo "Binary: $(ls -la release/minicontainer-${VERSION}/minicontainer)"
echo "Rootfs: $(ls -la release/minicontainer-${VERSION}/rootfs.tar.gz)"
echo "Test.sh: $(ls -la release/minicontainer-${VERSION}/test.sh)"

echo "=== Creating tarball ==="
cd release
tar czf ../${TARBALL} minicontainer-${VERSION}/
ls -la ../${TARBALL}

# Checksum
echo "=== Checksum ==="
sha256sum ../${TARBALL}
echo "=== Release tarball created: ${TARBALL} ==="