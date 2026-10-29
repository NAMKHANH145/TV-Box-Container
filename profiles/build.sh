#!/bin/bash
# Build one rootfs profile with the local Buildroot tree.
# Usage: ./profiles/build.sh [--clean] <base-mini|iot-gateway|python-edge|web-proxy>
# Output: build/rootfs-<profile>/images/rootfs.tar.gz
#
# --clean       xoa build/rootfs-<profile> truoc khi build (giu nguyen
#               build/buildroot/output/host/bin: do la toolchain dung chung)
# MIN_FREE_GB   nguong dung luong trong con lai (mac dinh 4 GB); FORCE=1 de bo qua
set -euo pipefail

CLEAN=0
PROFILE=""
for a in "$@"; do
	case "$a" in
	--clean) CLEAN=1 ;;
	-*) echo "unknown flag: $a" >&2; exit 1 ;;
	*) [ -z "$PROFILE" ] && PROFILE="$a" || { echo "chi nhan 1 profile" >&2; exit 1; } ;;
	esac
done
[ -n "$PROFILE" ] || { echo "usage: $0 [--clean] <profile>" >&2; exit 1; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BR="$ROOT/build/buildroot"
OUT="$ROOT/build/rootfs-$PROFILE"
DEF="$ROOT/profiles/$PROFILE.defconfig"

[ -f "$DEF" ] || { echo "unknown profile: $PROFILE" >&2; exit 1; }

if [ "$CLEAN" = 1 ] && [ -d "$OUT" ]; then
	echo "== clean $OUT =="
	du -sh "$OUT" 2>/dev/null || true
	rm -rf "$OUT"
fi

# Buildroot can >2 GB cho mot profile; het dia giua build thi mat vai gio.
NEED_GB=${MIN_FREE_GB:-4}
AVAIL_GB=$(df -Pk "$ROOT" | awk 'NR==2 {print int($4/1048576)}')
if [ "${AVAIL_GB:-0}" -lt "$NEED_GB" ]; then
	echo "warning: chi con ${AVAIL_GB} GB trong $ROOT, can >= ${NEED_GB} GB (MIN_FREE_GB=... / FORCE=1)" >&2
	[ "${FORCE:-0}" = 1 ] || exit 1
fi

[ -x "$BR/output/host/bin/aarch64-buildroot-linux-musl-gcc" ] || {
	echo "warning: existing toolchain not found, Buildroot will rebuild one (slow)" >&2
}

echo "== configuring $PROFILE =="
make -C "$BR" O="$OUT" BR2_DEFCONFIG="$DEF" defconfig

echo "== building $PROFILE (this can take hours) =="
make -C "$BR" O="$OUT" -j"$(nproc)"

echo "== done =="
ls -la "$OUT/images/rootfs.tar.gz"
