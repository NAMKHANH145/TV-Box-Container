#!/bin/sh
# install.sh — one-command install for the released musl static binary.
#
#   curl -fsSL https://github.com/NAMKHANH145/TV-Box-Container/releases/latest/download/install.sh | sudo sh
#
# Rules (P6.1):
#   - detect arch, download the asset, verify SHA256 before installing
#   - never run sudo on your behalf; print the exact command instead
#   - refuse to replace a binary while containers are running (use --force)
#   - print the installed version and sha256 when done
set -eu

REPO=${MC_REPO:-NAMKHANH145/TV-Box-Container}
PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
BASE_URL=${MC_BASE_URL:-}
FORCE=0
DO_CHECK=1
MINISIGN_PUB=${MC_MINISIGN_PUB:-}

usage() {
    cat <<'EOF'
Usage: install.sh [options]

  --prefix=DIR       install prefix (default /usr/local -> DIR/bin/minicontainer)
  --dest=DIR         staging root for packaging (default empty)
  --base-url=URL     release directory (default: latest GitHub release)
  --version=X.Y.Z    install a pinned release tag instead of latest
  --force            replace even if containers are running
  --no-check         skip the post-install `minicontainer check`
  -h, --help         show this help

Environment: PREFIX, DESTDIR, MC_BASE_URL, MC_VERSION, MC_MINISIGN_PUB
EOF
}

need_arg() {
    [ $# -ge 2 ] && [ -n "$2" ] || { echo "install: $1 needs a value" >&2; exit 1; }
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix=*) PREFIX=${1#*=} ;;
        --prefix) need_arg "$1" "${2:-}"; PREFIX=$2; shift ;;
        --dest=*) DESTDIR=${1#*=} ;;
        --dest) need_arg "$1" "${2:-}"; DESTDIR=$2; shift ;;
        --base-url=*) BASE_URL=${1#*=} ;;
        --base-url) need_arg "$1" "${2:-}"; BASE_URL=$2; shift ;;
        --version=*) MC_VERSION=${1#*=} ;;
        --version) need_arg "$1" "${2:-}"; MC_VERSION=$2; shift ;;
        --force) FORCE=1 ;;
        --no-check) DO_CHECK=0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install: unknown option '$1'" >&2; usage >&2; exit 1 ;;
    esac
    shift
done

case "$(uname -s)" in
    Linux) ;;
    *) echo "install: minicontainer requires Linux" >&2; exit 1 ;;
esac
case "$(uname -m)" in
    x86_64|amd64) ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    armv7l|armv7*) ARCH=armv7l ;;
    riscv64) ARCH=riscv64 ;;
    *) echo "install: unsupported architecture $(uname -m) (x86_64, aarch64, armv7l, riscv64)" >&2; exit 1 ;;
esac

if [ -z "$BASE_URL" ]; then
    if [ -n "${MC_VERSION:-}" ]; then
        BASE_URL="https://github.com/$REPO/releases/download/v${MC_VERSION#v}"
    else
        BASE_URL="https://github.com/$REPO/releases/latest/download"
    fi
fi
ASSET="minicontainer-linux-$ARCH"

command -v curl >/dev/null 2>&1 || { echo "install: curl is required" >&2; exit 1; }
command -v sha256sum >/dev/null 2>&1 || { echo "install: sha256sum is required" >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/mc-install.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT TERM

echo "install: downloading $ASSET from $BASE_URL"
curl -fsSL "$BASE_URL/$ASSET" -o "$TMP/minicontainer" || {
    echo "install: cannot download $ASSET" >&2; exit 1; }
curl -fsSL "$BASE_URL/SHA256SUMS.txt" -o "$TMP/SHA256SUMS.txt" || {
    echo "install: cannot download SHA256SUMS.txt" >&2; exit 1; }

WANT=$(awk -v n="$ASSET" '$2 == n { print $1; exit }' "$TMP/SHA256SUMS.txt")
[ -n "$WANT" ] || { echo "install: SHA256SUMS.txt has no entry for $ASSET" >&2; exit 1; }
GOT=$(sha256sum "$TMP/minicontainer" | awk '{ print $1 }')
[ "$WANT" = "$GOT" ] || {
    echo "install: SHA256 mismatch for $ASSET" >&2
    echo "  expected: $WANT" >&2
    echo "  actual:   $GOT" >&2
    exit 1
}

if [ -n "$MINISIGN_PUB" ]; then
    command -v minisign >/dev/null 2>&1 || {
        echo "install: MC_MINISIGN_PUB set but minisign is not installed" >&2; exit 1; }
    curl -fsSL "$BASE_URL/$ASSET.sig" -o "$TMP/minicontainer.sig" || {
        echo "install: cannot download $ASSET.sig" >&2; exit 1; }
    minisign -Vm "$TMP/minicontainer" -x "$TMP/minicontainer.sig" -p "$MINISIGN_PUB" >/dev/null || {
        echo "install: minisign verification failed" >&2; exit 1; }
fi

if [ "$FORCE" -eq 0 ] && [ -d /run/minicontainer ]; then
    for d in /run/minicontainer/*; do
        [ -f "$d/pid" ] || continue
        p=$(cat "$d/pid" 2>/dev/null || true)
        if [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then
            echo "install: container '${d##*/}' is running; stop it or use --force" >&2
            exit 1
        fi
    done
fi

BINDIR="$DESTDIR$PREFIX/bin"
TARGET="$BINDIR/minicontainer"
if [ ! -d "$BINDIR" ]; then
    if ! mkdir -p "$BINDIR" 2>/dev/null; then
        echo "install: cannot create $BINDIR" >&2
        echo "  re-run with: curl -fsSL $BASE_URL/install.sh | sudo sh -s -- --prefix=$PREFIX" >&2
        exit 1
    fi
fi
if [ -e "$TARGET" ] && [ ! -w "$TARGET" ]; then
    echo "install: $TARGET is not writable" >&2
    echo "  re-run with: curl -fsSL $BASE_URL/install.sh | sudo sh -s -- --prefix=$PREFIX" >&2
    exit 1
fi
install -m 0755 "$TMP/minicontainer" "$TARGET.new.$$"
mv -f "$TARGET.new.$$" "$TARGET"

if [ -z "$DESTDIR" ] && [ "$(id -u)" -eq 0 ]; then
    mkdir -p /var/lib/minicontainer /run/minicontainer /var/log/minicontainer
fi

echo "install: installed $TARGET"
"$TARGET" version
echo "install: sha256 $GOT"

if [ "$DO_CHECK" -eq 1 ] && [ -z "$DESTDIR" ]; then
    if [ "$(id -u)" -eq 0 ]; then
        "$TARGET" check || echo "install: check reported missing requirements (see above)" >&2
    else
        echo "install: not root; run: sudo $TARGET check"
    fi
fi
echo "install: done"
