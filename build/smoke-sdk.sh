#!/bin/bash
# smoke-sdk.sh — kiem tra `minicontainer sdk init|pack` KHONG can root,
# khong can namespace/cgroup. Chay duoc tren laptop va tren CI (x86_64).
#   bash build/smoke-sdk.sh [duong-dan-binary]
# Exit != 0 neu co FAIL. Khoi tao lai tu dau moi lan chay.
set -u
MC=${1:-$(cd "$(dirname "$0")" && pwd)/minicontainer}
[ -x "$MC" ] || { echo "THIEU: $MC (build truoc: cd build && make)"; exit 1; }
for tool in tar sha256sum; do
	command -v "$tool" >/dev/null || { echo "THIEU: $tool"; exit 1; }
done

SD=$(mktemp -d "${TMPDIR:-/tmp}/mcsmoke.XXXXXX")
trap 'rm -rf "$SD"' EXIT
PASS=0; FAIL=0
t() { # t <ten> <lenh...>
	local name="$1"; shift
	if "$@" >"$SD/t.log" 2>&1; then echo "PASS: $name"; PASS=$((PASS+1));
	else echo "FAIL: $name"; sed 's/^/      /' "$SD/t.log" | head -n 3; FAIL=$((FAIL+1)); fi
}

echo "=== sdk init ==="
t "init: tao skeleton (rootfs/ + mc.conf + README.md)" \
	sh -c "$MC sdk init $SD/proj --profile=base-mini >/dev/null && [ -f $SD/proj/mc.conf ] && [ -f $SD/proj/README.md ] && [ -d $SD/proj/rootfs ]"
t "init: mc.conf co ram mac dinh 32M" grep -q '^ram=32M' "$SD/proj/mc.conf"
t "init: tu choi thu muc khong rong" sh -c "! $MC sdk init $SD/proj >/dev/null 2>&1"

echo "=== sdk pack ==="
mkdir -p "$SD/proj/rootfs/bin" "$SD/proj/rootfs/usr/bin"
cp /bin/true "$SD/proj/rootfs/bin/sh" && chmod +x "$SD/proj/rootfs/bin/sh"
ln -sf ../../bin/sh "$SD/proj/rootfs/usr/bin/mc-rel"   # tuong doi, van trong rootfs -> hop le
ln -sf /bin/sh "$SD/proj/rootfs/bin/abs-link"          # tuyet doi -> hop le
t "pack: tao tar.gz + .sha256" sh -c "$MC sdk pack $SD/proj -o $SD/dist/proj-rootfs.tar.gz >/dev/null && [ -f $SD/dist/proj-rootfs.tar.gz.sha256 ]"
t "pack: member deu la tuong doi" sh -c "[ \$(tar -tzf $SD/dist/proj-rootfs.tar.gz | grep -c '^/') -eq 0 ]"
t "pack: giu quyen thuc thi" sh -c "tar -tzvf $SD/dist/proj-rootfs.tar.gz | grep -q '^-rwx.*bin/sh'"
t "pack: sha256 khop" sh -c "cd $SD/dist && sha256sum -c proj-rootfs.tar.gz.sha256 >/dev/null"
t "pack: giu symlink tuyet doi + tuong doi trong rootfs" sh -c "tar -tzf $SD/dist/proj-rootfs.tar.gz | grep -q './bin/abs-link' && tar -tzf $SD/dist/proj-rootfs.tar.gz | grep -q './usr/bin/mc-rel'"
t "pack: tu choi ghi de khi chua --force" sh -c "! $MC sdk pack $SD/proj -o $SD/dist/proj-rootfs.tar.gz >/dev/null 2>&1"
ln -sf ../../etc/passwd "$SD/proj/rootfs/escape"
t "pack: tu choi symlink vuot goc rootfs" sh -c "! $MC sdk pack $SD/proj -o $SD/dist/x-rootfs.tar.gz >/dev/null 2>&1"
rm -f "$SD/proj/rootfs/escape"

echo "=== round-trip pack -> pull ==="
t "pull tu thu muc local: sha256 khop + giai nen du" sh -c "SUM=\$(cut -d' ' -f1 $SD/dist/proj-rootfs.tar.gz.sha256); $MC pull proj --url=$SD/dist --dest=$SD/round --sha256=\$SUM >/dev/null && [ -x $SD/round/bin/sh ]"

echo "=== KET QUA: PASS=$PASS FAIL=$FAIL TONG=$((PASS+FAIL)) ==="
[ "$FAIL" -eq 0 ]
