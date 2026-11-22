#!/usr/bin/env bash
# demo-v3.sh — HAL: ship 1 ứng kiểm tra phần cứng, self-detect board,
#               dịch tên thiết bị thật (ttyAML0 / ttyS0) thành khả năng logic chung.
# Cách chạy: ./build-local.sh (1 lần) rồi BOX=<IP_BOX> SSHPASS_FILE=<file> bash demo-v3.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$(dirname "$HERE")"
RES="$HERE/results-v3"
mkdir -p "$RES"

: "${BOX:?Cần đặt BOX=<IP_BOX>}"
: "${SSHPASS_FILE:?Cần đặt SSHPASS_FILE=<file>}"
RELEASE_URL="https://github.com/NAMKHANH145/TV-Box-Container/releases/download/v2.0.0"
# Ưu tiên binary build từ source (../build-local.sh); thiếu thì mới tải release.
LOCAL_ARM="$DEMO/minicontainer-linux-aarch64"
LOCAL_X86="$DEMO/minicontainer-linux-x86_64"
SO=(-q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=15)
MOUNTS=(--mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind)

# Mô tả từng bước: máy nào, làm gì, gõ lệnh gì, thấy gì.
declare -A NOTE=(
[P1]='MÁY  : Laptop
VIỆC : Dùng binary build từ source
GÕ   : $ ./minicontainer-linux-x86_64 --version
THẤY : 2.0.1'
[P2]='MÁY  : Laptop -> TV BOX
VIỆC : Cài binary, gửi 1 file hwprobe.py vào rootfs Buildroot có sẵn
GÕ   : $ tar czf hwprobe.tar.gz hwprobe.py && scp ... && tar xzf ...
THẤY : rootfs NAME=Buildroot, Python 3.14'
[A1]='MÁY  : TV BOX (Amlogic S905W)
VIỆC : Chạy hwprobe.py, tự thấy ttyAML0 → backend amlogic
GÕ   : $ minicontainer run --rootfs /opt/rootfs-python-edge ... /usr/bin/python3 /hwprobe.py
THẤY : UART=1 · status=OK (có UART → app chạy đúng)'
[A2]='MÁY  : TV BOX
VIỆC : Ép HW_BACKEND=pc_x86 dù board là Amlogic → phản diện
GÕ   : $ minicontainer run ... --env HW_BACKEND=pc_x86 /usr/bin/python3 /hwprobe.py
THẤY : UART=0 · missing=[uart] · status=WRONG_BACKEND'
[B1]='MÁY  : Laptop (x86_64)
VIỆC : CÙNG hwprobe.py: tự thấy ttyS* → pc_x86; rồi ép amlogic
GÕ   : $ minicontainer run --image=hwprobe-1 ... python /hwprobe.py
THẤY : auto UART=32 OK · ép amlogic UART=0 WRONG_BACKEND'
[Z]='VIỆC : Tổng kết 4 ca — log trong results-v3/'
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
echo "# MÁY: TV BOX (rootfs Buildroot) + Laptop                        #"
echo "############################################################"
echo "# ĐỌC KẾT QUẢ: backend=board được chọn | selected_by=auto/ép     #"
echo "# capabilities=UART/GPIO/I2C/nhiệt độ | missing=thieu           #"
echo "# status=OK (đúng board) hoặc WRONG_BACKEND (ép sai board)      #"
echo "############################################################"

step P1 "prepare" bash -c '
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
echo "--- rootfs Buildroot trên box ---"
'
do_p2() {
    box 'mkdir -p /root/demo-mc/v3'
    if [ -f "$LOCAL_ARM" ]; then
        put "$LOCAL_ARM" /root/minicontainer-linux-aarch64
        boxsh 'install -m755 /root/minicontainer-linux-aarch64 /usr/local/bin/minicontainer; /usr/local/bin/minicontainer --version'
    fi
    tar -czf "$HERE/hwprobe.tar.gz" -C "$HERE" hwprobe.py
    sha256sum "$HERE/hwprobe.tar.gz"
    put "$HERE/hwprobe.tar.gz" /root/demo-mc/v3/
    boxsh 'cd /root/demo-mc/v3; sha256sum hwprobe.tar.gz; tar xzf hwprobe.tar.gz; install -m755 hwprobe.py /opt/rootfs-python-edge/hwprobe.py; ls -l /opt/rootfs-python-edge/hwprobe.py; cat /opt/rootfs-python-edge/etc/alpine-release 2>/dev/null || cat /opt/rootfs-python-edge/etc/os-release 2>/dev/null | head -3'
}
step P2 "deploy-binary-va-hal-vao-rootfs" do_p2

if ! docker image inspect mc-demo-laptop >/dev/null 2>&1; then
    docker build -q -t mc-demo-laptop -f "$DEMO/Dockerfile.laptop" "$DEMO" >/dev/null
fi

###############################################################################
# A: BOX — rootfs Buildroot, tự chọn backend amlogic
###############################################################################

step A1 "box-auto-backend" boxsh '
MC=/usr/local/bin/minicontainer
OUT=$($MC run --rm --rootfs /opt/rootfs-python-edge \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  /usr/bin/python3 /hwprobe.py)
echo "$OUT"
echo "$OUT" | grep -q "\"status\": \"OK\""
echo "$OUT" | grep -q "\"backend\": \"amlogic\""
echo "=> XÁC NHẬN: box tự chọn amlogic, có UART, status OK"
'

step A2 "box-FORCED-wrong-backend" boxsh '
MC=/usr/local/bin/minicontainer
OUT=$($MC run --rm --rootfs /opt/rootfs-python-edge \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  --env HW_BACKEND=pc_x86 \
  /usr/bin/python3 /hwprobe.py)
echo "$OUT"
echo "$OUT" | grep -q "WRONG_BACKEND"
echo "$OUT" | grep -q "\"uart\": {\"count\": 0"
echo "=> XÁC NHẬN: ép pc_x86 trên board Amlogic → mất UART"
'

###############################################################################
# B: LAPTOP — image OCI, cùng HAL, tự chọn backend pc_x86
###############################################################################

step B1 "laptop-build-and-auto-backend" lap '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/amd64 python:3.12-alpine 2>&1 | tail -1
$MC build --no-cache --platform=linux/amd64 -t hwprobe-1 /demo/demo-v3/app
echo "--- tự detect trên laptop ---"
OUT=$($MC run --rm --image=hwprobe-1 \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  python /hwprobe.py)
echo "$OUT"
echo "$OUT" | grep -q "\"status\": \"OK\""
echo "$OUT" | grep -q "\"backend\": \"pc_x86\""
echo "=> XÁC NHẬN: laptop tự chọn pc_x86, có UART, status OK"
echo "--- ép sai backend amlogic trên laptop ---"
OUT2=$($MC run --rm --image=hwprobe-1 \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  --env HW_BACKEND=amlogic \
  python /hwprobe.py)
echo "$OUT2"
echo "$OUT2" | grep -q "WRONG_BACKEND"
echo "$OUT2" | grep -q "\"uart\": {\"count\": 0"
echo "=> XÁC NHẬN: ép amlogic trên laptop → mất UART"
'

step Z "summary" bash -c '
echo "=> BOX auto: amlogic/OK | BOX ép pc_x86: WRONG_BACKEND | LAPTOP auto: pc_x86/OK | LAPTOP ép amlogic: WRONG_BACKEND"
echo "Log: '"$RES"'"
'
