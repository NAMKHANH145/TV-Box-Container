#!/usr/bin/env bash
# demo-ver2.sh — FLOW LÀM VIỆC BÌNH THƯỜNG: một Dockerfile, lệnh `pip install` y nguyên.
#   - Build trên máy nào thì pip tự tải wheel của máy đó (không cần --platform trick).
#   - Ship nhầm bundle -> lỗi; build lại bằng CÙNG lệnh trên máy đích -> chạy.
# Cách chạy: ./build-local.sh (1 lần) rồi BOX=<IP_BOX> SSHPASS_FILE=<file> bash demo-ver2.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$(dirname "$HERE")"
RES="$HERE/results-normal"
mkdir -p "$RES"

: "${BOX:?Cần đặt BOX=<IP_BOX>}"
: "${SSHPASS_FILE:?Cần đặt SSHPASS_FILE=<file>}"
RELEASE_URL="https://github.com/NAMKHANH145/TV-Box-Container/releases/download/v2.0.0"
# Ưu tiên binary build từ source (../build-local.sh); thiếu thì mới tải release.
LOCAL_ARM="$DEMO/minicontainer-linux-aarch64"
LOCAL_X86="$DEMO/minicontainer-linux-x86_64"
SO=(-q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=15)

# Mô tả từng bước: máy nào, làm gì, gõ lệnh gì, thấy gì.
declare -A NOTE=(
[P1]='MÁY  : Laptop
VIỆC : init sinh Dockerfile; binary build từ source
GÕ   : $ minicontainer init /tmp/mc-demo-init --pip=Pillow
THẤY : 2.0.1 · Dockerfile khớp y hệt output init'
[P2]='MÁY  : Laptop -> TV BOX
VIỆC : Cài binary, gửi app/ bằng 1 file .tar.gz
GÕ   : $ tar czf app.tar.gz -C . app && scp app.tar.gz root@<IP_BOX>:...
THẤY : sha khớp, app/ có Dockerfile + app.py'
[A1]='MÁY  : TV BOX
VIỆC : Build + chạy native bằng pip install bình thường
GÕ   : $ minicontainer build --platform=linux/arm64 -t app-arm:1 /root/demo-mc/ver2-normal/app
THẤY : wheel aarch64 · e_machine=183 · cpu_ms ~12'
[A2]='MÁY  : TV BOX
VIỆC : Đóng gói image vừa chạy thành bundle
GÕ   : $ minicontainer export app-arm-1 -o app-arm-from-box.tar.gz
THẤY : bundle arm64 + sha256'
[A2b]='MÁY  : TV BOX -> Laptop
VIỆC : Gửi bundle về laptop
GÕ   : $ scp root@<IP_BOX>:.../app-arm-from-box.tar.gz results-normal/
THẤY : sha khớp'
[A3]='MÁY  : Laptop
VIỆC : Thử bundle arm trên laptop → phải thấy giả lập
GÕ   : $ minicontainer run --image=app-arm-1 python /app/app.py
THẤY : e_machine=183 nhưng cpu_ms ~4 → FAIL ĐỘ'
[A4]='MÁY  : Laptop
VIỆC : CÙNG Dockerfile, build native x86 rồi export
GÕ   : $ minicontainer build --platform=linux/amd64 -t app-x86:1 /demo/demo-ver2/app
THẤY : wheel x86_64 · e_machine=62 · cpu_ms ~0.6'
[B1]='MÁY  : Laptop
VIỆC : Kiểm tra bundle vừa export
GÕ   : $ sha256sum app-x86-from-laptop.tar.gz
THẤY : file ~28 MB + sha256'
[B1b]='MÁY  : Laptop -> TV BOX
VIỆC : Gửi bundle sang box
GÕ   : $ scp app-x86-from-laptop.tar.gz root@<IP_BOX>:...
THẤY : sha khớp'
[B2]='MÁY  : TV BOX
VIỆC : Thử bundle x86 trên box ARM → phải lỗi
GÕ   : $ minicontainer run --image=app-x86-1 python /app/app.py
THẤY : execvp: Exec format error → FAIL ĐỘ'
[B3]='MÁY  : TV BOX
VIỆC : Build native lại trên box bằng cùng Dockerfile
GÕ   : $ minicontainer build --platform=linux/arm64 -t app-arm:1 /root/demo-mc/ver2-normal/app
THẤY : wheel aarch64 · e_machine=183 → ĐÚNG'
[Z]='VIỆC : Xong — log trong results-normal/'
)

step() {
    local id="$1" title="$2"; shift 2
    local log="$RES/${id}-${title}.log"
    {
        echo "===== $id: $title ====="
        echo "time: $(date -Is)"
        if [ -n "${NOTE[$id]:-}" ]; then
            echo
            printf '%s\n' "${NOTE[$id]}"
        fi
        echo
    } | tee "$log"
    "$@" 2>&1 | tee -a "$log"
}
box()  { sshpass -f "$SSHPASS_FILE" ssh "${SO[@]}" "root@${BOX}" "$@"; }
boxsh(){ box "set -eu; $1"; }
put()  { sshpass -f "$SSHPASS_FILE" scp "${SO[@]}" "$1" "root@${BOX}:$2"; }
get()  { sshpass -f "$SSHPASS_FILE" scp "${SO[@]}" "root@${BOX}:$1" "$2"; }
lap() {
    local s="$1"
    docker run --rm --privileged -e MC_SETUP="$LAP_SETUP" \
        -v "$DEMO":/demo -w /demo mc-demo-laptop \
        bash -c 'set -eu; eval "$MC_SETUP"; eval "$1"' _ "$s"
}
LAP_SETUP='
mkdir -p /sys/fs/cgroup/mc-host
for p in $(cat /sys/fs/cgroup/cgroup.procs); do echo "$p" > /sys/fs/cgroup/mc-host/cgroup.procs 2>/dev/null || true; done
echo "+cpu +memory +pids +io" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
mkdir -p /var/lib/minicontainer/images /run/minicontainer /var/log/minicontainer
install -m755 /demo/minicontainer-linux-x86_64 /usr/local/bin/minicontainer
'

echo "############################################################"
echo "# MÁY: TV BOX + Laptop                                          #"
echo "############################################################"

step P1 "init-sinh-du-an-va-binary" bash -c '
set -eu
cd "'$DEMO'"
if [ -x minicontainer-linux-x86_64 ]; then
  echo "--- binary build từ source (build-local.sh) ---"
  sha256sum minicontainer-linux-x86_64
else
  echo "--- không có binary local, tải release GitHub ---"
  curl -fsSL -o minicontainer-linux-x86_64 "'$RELEASE_URL'/minicontainer-linux-x86_64"
  curl -fsSL -o SHA256SUMS.txt "'$RELEASE_URL'/SHA256SUMS.txt"
  grep " minicontainer-linux-x86_64\$" SHA256SUMS.txt | sha256sum -c -
fi
chmod +x minicontainer-linux-x86_64
./minicontainer-linux-x86_64 --version
echo "--- người dùng gõ 1 lệnh: init --pip=Pillow ---"
rm -rf /tmp/mc-demo-init
./minicontainer-linux-x86_64 init /tmp/mc-demo-init --pip=Pillow
echo "--- Dockerfile do init sinh ra ---"
diff -q "'$HERE'/app/Dockerfile" /tmp/mc-demo-init/Dockerfile \
  && echo "=> Dockerfile demo = output cua init (khong viet tay)"
'

if ! docker image inspect mc-demo-laptop >/dev/null 2>&1; then
    docker build -q -t mc-demo-laptop -f "$DEMO/Dockerfile.laptop" "$DEMO" >/dev/null
fi

do_p2() {
    box 'mkdir -p /root/demo-mc/ver2-normal'
    if [ -f "$LOCAL_ARM" ]; then
        put "$LOCAL_ARM" /root/minicontainer-linux-aarch64
        boxsh 'install -m755 /root/minicontainer-linux-aarch64 /usr/local/bin/minicontainer; /usr/local/bin/minicontainer --version'
    fi
    boxsh 'MC=/usr/local/bin/minicontainer; for c in app-arm app-x86 ship-wrong build-right; do $MC rm -f $c >/dev/null 2>&1 || true; done'
    tar -czf "$DEMO/app.tar.gz" -C "$HERE" app
    sha256sum "$DEMO/app.tar.gz"
    put "$DEMO/app.tar.gz" /root/demo-mc/ver2-normal/
    boxsh 'cd /root/demo-mc/ver2-normal; sha256sum app.tar.gz; rm -rf app; tar xzf app.tar.gz; ls -l app'
}
step P2 "copy-app-to-box" do_p2

###############################################################################
# CHIỀU A: BOX -> LAPTOP
###############################################################################

step A1 "box-normal-build-and-run" boxsh '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/arm64 python:3.12-alpine 2>&1 | tail -1
$MC build --no-cache --platform=linux/arm64 -t app-arm:1 /root/demo-mc/ver2-normal/app 2>&1 | grep -E "Downloading pillow-|build:" | tail -4
$MC run -d --name app-arm --image=app-arm-1 --ram=128M --net=bridge python /app/app.py
sleep 8
OUT=$($MC logs app-arm); echo "$OUT"
echo "$OUT" | grep -q "\"elf_e_machine\": 183"
echo "$OUT" | grep -q "\"platform_machine\": \"aarch64\""
echo "=> PASS: pip tự tải wheel aarch64, chạy native trên box"
'

step A2 "box-export-and-scp" boxsh '
set -eu
MC=/usr/local/bin/minicontainer
$MC export app-arm-1 -o /root/demo-mc/ver2-normal/app-arm-from-box.tar.gz
sha256sum /root/demo-mc/ver2-normal/app-arm-from-box.tar.gz
'
do_a2b() {
    get /root/demo-mc/ver2-normal/app-arm-from-box.tar.gz "$RES/app-arm-from-box.tar.gz"
    sha256sum "$RES/app-arm-from-box.tar.gz"
}
step A2b "scp-bundle-to-laptop" do_a2b

step A3 "laptop-shipped-arm-WRONG" lap '
set +e
MC=/usr/local/bin/minicontainer
$MC import /demo/demo-ver2/results-normal/app-arm-from-box.tar.gz
$MC rm -f ship-wrong >/dev/null 2>&1 || true
$MC run -d --name ship-wrong --image=app-arm-1 --ram=128M --net=bridge python /app/app.py 2>&1
sleep 8
OUT=$($MC logs ship-wrong 2>&1); echo "$OUT" | grep -oE "\"elf_e_machine\": [0-9]+|\"cpu_ms\": [0-9.]+" | tr "\\n" " "; echo
if echo "$OUT" | grep -q "\"elf_e_machine\": 183"; then
  echo "=> FAIL DO: ảnh từ box có .so aarch64; laptop phải giả lập (xem cpu_ms)."
else
  echo "=> FAIL DO: không chạy được trên laptop."
fi
'

step A4 "laptop-normal-build-and-run" lap '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/amd64 python:3.12-alpine 2>&1 | tail -1
$MC build --no-cache --platform=linux/amd64 -t app-x86:1 /demo/demo-ver2/app 2>&1 | grep -E "Downloading pillow-|build:" | tail -4
$MC rm -f build-right >/dev/null 2>&1 || true
$MC run -d --name build-right --image=app-x86-1 --ram=128M --net=bridge python /app/app.py
sleep 8
OUT=$($MC logs build-right); echo "$OUT"
echo "$OUT" | grep -q "\"elf_e_machine\": 62"
echo "$OUT" | grep -q "\"platform_machine\": \"x86_64\""
echo "=> PASS: pip tự tải wheel x86_64, chạy native trên laptop"
$MC export app-x86-1 -o /demo/demo-ver2/results-normal/app-x86-from-laptop.tar.gz
sha256sum /demo/demo-ver2/results-normal/app-x86-from-laptop.tar.gz
'

###############################################################################
# CHIỀU B: LAPTOP -> BOX
###############################################################################

step B1 "bundle-ready" bash -c '
sha256sum "'$RES'/app-x86-from-laptop.tar.gz"
'
do_b1b() {
    put "$RES/app-x86-from-laptop.tar.gz" /root/demo-mc/ver2-normal/
    box 'sha256sum /root/demo-mc/ver2-normal/app-x86-from-laptop.tar.gz'
}
step B1b "scp-bundle-to-box" do_b1b

step B2 "box-shipped-x86-WRONG" boxsh '
set +e
MC=/usr/local/bin/minicontainer
$MC import /root/demo-mc/ver2-normal/app-x86-from-laptop.tar.gz
$MC rm -f ship-wrong2 >/dev/null 2>&1 || true
$MC run -d --name ship-wrong2 --image=app-x86-1 --ram=128M --net=bridge python /app/app.py 2>&1
sleep 6
OUT=$($MC logs ship-wrong2 2>&1); echo "$OUT" | head -1
echo "=> FAIL DO: ảnh từ laptop có .so x86_64, box không chạy được"
'

step B3 "box-normal-build-and-run" boxsh '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/arm64 python:3.12-alpine 2>&1 | tail -1
$MC build --no-cache --platform=linux/arm64 -t app-arm:1 /root/demo-mc/ver2-normal/app 2>&1 | grep -E "Downloading pillow-|build:" | tail -4
$MC rm -f build-right2 >/dev/null 2>&1 || true
$MC run -d --name build-right2 --image=app-arm-1 --ram=128M --net=bridge python /app/app.py
sleep 8
OUT=$($MC logs build-right2); echo "$OUT"
echo "$OUT" | grep -q "\"elf_e_machine\": 183"
echo "=> PASS: pip tự tải wheel aarch64, chạy native trên TV Box"
'

step Z "summary" bash -c '
echo "Demo ver2 xong. Log: '"$RES"'"
'
