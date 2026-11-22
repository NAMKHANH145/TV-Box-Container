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
[P1]='MÁY  : Laptop (terminal bạn đang đứng)
VIỆC : Chuẩn bị binary build từ source
GÕ   : $ ./build-local.sh
       $ ./minicontainer-linux-x86_64 --version
THẤY : minicontainer 2.0.1-dev (x86_64, glibc, static)'
[P2]='MÁY  : Laptop -> TV BOX (archive) + TV BOX
VIỆC : Cài binary aarch64, đóng gói hwprobe.py rồi giải nén vào rootfs Buildroot
GÕ   : $ scp minicontainer-linux-aarch64 root@<IP_BOX>:/root/
       $ ssh root@<IP_BOX> install -m755 /root/minicontainer-linux-aarch64 /usr/local/bin/minicontainer
       $ tar czf hwprobe.tar.gz -C . hwprobe.py
       $ scp hwprobe.tar.gz root@<IP_BOX>:/root/demo-mc/v3/
       $ ssh root@<IP_BOX> "cd /root/demo-mc/v3 && tar xzf hwprobe.tar.gz"
THẤY : 2.0.1-dev (aarch64, musl, static); rootfs NAME=Buildroot, Python 3.14'
[A1]='MÁY  : TV BOX
VIỆC : Chạy hwprobe.py trong rootfs Buildroot, cho phép đọc /dev và /sys thật
GÕ   : $ minicontainer run --rm --rootfs /opt/rootfs-python-edge \
         --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
         /usr/bin/python3 /hwprobe.py
THẤY : backend=amlogic, uart=ttyAML0, 2 GPIO, 1 I2C, ~55-65°C, status=OK'
[A2]='MÁY  : TV BOX
VIỆC : CỐ TÌNH ép backend pc_x86 trên box Amlogic -> phải báo sai
GÕ   : $ minicontainer run --rm --rootfs /opt/rootfs-python-edge \
         --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
         --env HW_BACKEND=pc_x86 /usr/bin/python3 /hwprobe.py
THẤY : uart count=0, missing=["uart"], status=WRONG_BACKEND'
[B1]='MÁY  : Laptop
VIỆC : Build image OCI cho laptop rồi chạy CÙNG hwprobe.py
GÕ   : $ minicontainer pull --platform=linux/amd64 python:3.12-alpine
       $ minicontainer build --no-cache --platform=linux/amd64 -t hwprobe-1 /demo/demo-v3/app
       $ minicontainer run --rm --image=hwprobe-1 \
         --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
         python /hwprobe.py
       $ minicontainer run --rm --image=hwprobe-1 ... --env HW_BACKEND=amlogic python /hwprobe.py
THẤY : auto -> backend=pc_x86, 32 ttyS, status=OK; ép amlogic -> WRONG_BACKEND'
[Z]='VIỆC : Tổng kết — log từng bước nằm trong results-v3/'
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
$MC run --rm --rootfs /opt/rootfs-python-edge \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  /usr/bin/python3 /hwprobe.py
'

step A2 "box-FORCED-wrong-backend" boxsh '
MC=/usr/local/bin/minicontainer
$MC run --rm --rootfs /opt/rootfs-python-edge \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  --env HW_BACKEND=pc_x86 \
  /usr/bin/python3 /hwprobe.py
'

###############################################################################
# B: LAPTOP — image OCI, cùng HAL, tự chọn backend pc_x86
###############################################################################

step B1 "laptop-build-and-auto-backend" lap '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/amd64 python:3.12-alpine 2>&1 | tail -1
$MC build --no-cache --platform=linux/amd64 -t hwprobe-1 /demo/demo-v3/app
echo "--- auto detect trên laptop ---"
$MC run --rm --image=hwprobe-1 \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  python /hwprobe.py
echo "--- ép sai backend amlogic trên laptop ---"
$MC run --rm --image=hwprobe-1 \
  --mount bind:/dev:/hostdev:ro,rbind --mount bind:/sys:/hostsys:ro,rbind \
  --env HW_BACKEND=amlogic \
  python /hwprobe.py
'

step Z "summary" bash -c '
echo "Demo v3 (HAL) xong. Log: '"$RES"'"
ls -1 '"$RES"'/*.log
'
