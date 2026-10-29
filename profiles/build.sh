#!/bin/bash
# Build one rootfs profile with the local Buildroot tree.
# Usage: ./profiles/build.sh <base-mini|iot-gateway|python-edge|web-proxy>
# Output: build/rootfs-<profile>/images/rootfs.tar.gz
set -euo pipefail

PROFILE=${1:?usage: $0 <profile>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BR="$ROOT/build/buildroot"
OUT="$ROOT/build/rootfs-$PROFILE"
DEF="$ROOT/profiles/$PROFILE.defconfig"

[ -f "$DEF" ] || { echo "unknown profile: $PROFILE" >&2; exit 1; }
[ -x "$BR/output/host/bin/aarch64-buildroot-linux-musl-gcc" ] || {
	echo "warning: existing toolchain not found, Buildroot will rebuild one (slow)" >&2
}

echo "== configuring $PROFILE =="
make -C "$BR" O="$OUT" BR2_DEFCONFIG="$DEF" defconfig

echo "== building $PROFILE (this can take hours) =="
make -C "$BR" O="$OUT" -j"$(nproc)"

echo "== done =="
ls -la "$OUT/images/rootfs.tar.gz"
