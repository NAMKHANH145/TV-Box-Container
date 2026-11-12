#!/bin/sh
# Run the integration matrix on a normal Linux host.
# Requires root for user namespaces, mounts, cgroup v2 and networking.
set -eu

ROOT=${MC_LOCAL_ROOT:-/var/tmp/minicontainer-local}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MC=${MC:-"$SCRIPT_DIR/minicontainer"}
BR="$ROOT/rootfs"
ALP="$ROOT/alpine"

die() { echo "local-test: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "run as root: sudo MC=... $0"
[ -x "$MC" ] || die "binary not executable: $MC"
[ -x /bin/busybox ] || die "host /bin/busybox is required"

if [ "${KEEP_ROOT:-0}" != 1 ]; then rm -rf "$ROOT"; fi
mkdir -p "$BR" "$ALP"
for d in "$BR" "$ALP"; do
    mkdir -p "$d"/bin "$d"/etc "$d"/proc "$d"/sys "$d"/dev "$d"/tmp "$d"/run "$d"/var/log "$d"/dev/shm
    cp /bin/busybox "$d/bin/busybox"
    /bin/busybox --install -s "$d/bin"
    find "$d/bin" -type l -delete
    for applet in $(/bin/busybox --list); do
        [ "$applet" = busybox ] || ln -s busybox "$d/bin/$applet"
    done
    printf 'root:x:0:0:root:/root:/bin/sh\n' > "$d/etc/passwd"
    printf 'nameserver 192.0.2.1\n' > "$d/etc/resolv.conf"
    chmod 1777 "$d/tmp" "$d/dev/shm"
done

make -C "$SCRIPT_DIR" CC=gcc STRIP=strip tools >/dev/null
cp "$SCRIPT_DIR/bomb" "$SCRIPT_DIR/breakout" "$BR/"
cp "$SCRIPT_DIR/bomb" "$SCRIPT_DIR/breakout" "$ALP/"

MC="$MC" BR="$BR" ALP="$ALP" MC_LOCAL=1 IMG="$ROOT/no-images" \
    WPDEST="$ROOT/pulled" bash "$SCRIPT_DIR/test.sh"

rm -rf "$ROOT"
