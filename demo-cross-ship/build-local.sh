#!/usr/bin/env bash
# build-local.sh — build binary từ source cho demo (không dùng release GitHub).
#   aarch64 musl  -> minicontainer-linux-aarch64  (cho TV Box)
#   x86_64 static -> minicontainer-linux-x86_64   (cho laptop)
# Sau khi chạy, 3 demo tự dùng 2 file này ở bước P1.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(dirname "$HERE")/build"

cd "$SRC"

echo ">>> aarch64 musl (toolchain mặc định của Makefile)"
make clean >/dev/null 2>&1 || true
make >/dev/null
cp minicontainer "$HERE/minicontainer-linux-aarch64"

echo ">>> x86_64 static (gcc trên máy này)"
make clean >/dev/null 2>&1 || true
make CC=gcc STRIP=strip >/dev/null
cp minicontainer "$HERE/minicontainer-linux-x86_64"

echo ">>> khôi phục artifact aarch64 trong build/"
make clean >/dev/null 2>&1 || true
make >/dev/null

echo ">>> kết quả"
ls -l "$HERE/minicontainer-linux-aarch64" "$HERE/minicontainer-linux-x86_64"
sha256sum "$HERE/minicontainer-linux-aarch64" "$HERE/minicontainer-linux-x86_64"
"$HERE/minicontainer-linux-x86_64" --version
