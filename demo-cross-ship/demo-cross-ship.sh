#!/usr/bin/env bash
# demo-cross-ship.sh — demo ship container 2 chiều TV Box <-> Laptop bằng minicontainer
#
# Cách chạy (KHÔNG hard-code IP/mật khẩu):
#   ./build-local.sh            # 1 lần: build binary aarch64 + x86_64 từ source
#   BOX=<IP_BOX> SSHPASS_FILE=/path/to/pass.txt bash demo-cross-ship.sh
# Không chạy build-local.sh thì script tự tải binary từ release GitHub (v2.0.0).
#
# Biến bắt buộc:
#   BOX           địa chỉ Tailscale/LAN của TV Box
#   SSHPASS_FILE  file chứa mật khẩu SSH (chmod 600)
# Biến tuỳ chọn:
#   SKIP_CLEANUP=1   bỏ qua dọn rác đầu script
#   RELEASE_VER=2.0.0
set -euo pipefail

DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS="$DEMO_DIR/results"
mkdir -p "$RESULTS"

: "${BOX:?Cần đặt BOX=<IP_BOX>}"
: "${SSHPASS_FILE:?Cần đặt SSHPASS_FILE=<file chứa mật khẩu>}"
[ -r "$SSHPASS_FILE" ] || { echo "Không đọc được SSHPASS_FILE" >&2; exit 1; }

RELEASE_VER="${RELEASE_VER:-2.0.0}"
RELEASE_URL="https://github.com/NAMKHANH145/TV-Box-Container/releases/download/v${RELEASE_VER}"
# Ưu tiên binary build từ source (./build-local.sh); thiếu thì mới tải release.
LOCAL_ARM="$DEMO_DIR/minicontainer-linux-aarch64"
LOCAL_X86="$DEMO_DIR/minicontainer-linux-x86_64"
SCIP_OPTS=(-q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=15)
SSH_OPTS=(-q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=15)

# Mô tả từng bước: máy nào, làm gì, gõ lệnh gì, thấy gì.
# In ra trước khi chạy để người xem theo dõi được như đang gõ terminal.
declare -A NOTE=(
[P1]='MÁY  : Laptop (terminal bạn đang đứng)
VIỆC : Chuẩn bị binary minicontainer cho laptop
GÕ   : $ ./build-local.sh
       $ ./minicontainer-linux-x86_64 --version
THẤY : minicontainer 2.0.1-dev (x86_64, glibc, static)'
[P2]='MÁY  : TV BOX (qua SSH)
VIỆC : Dọn container/image cũ để demo bắt đầu sạch
GÕ   : $ minicontainer ps
       $ minicontainer rm -f <id>      (với từng container còn sót)
       $ minicontainer rmi <image>     (với từng image còn sót)
THẤY : containers: (trong) · images: 0 hoặc 1'
[P3]='MÁY  : Laptop -> TV BOX (archive)
VIỆC : Đóng gói app-box/ thành 1 file .tar.gz rồi giải nén trên box
GÕ   : $ tar czf app-box.tar.gz -C . app-box
       $ scp app-box.tar.gz root@<IP_BOX>:/root/demo-mc/
       $ ssh root@<IP_BOX> "cd /root/demo-mc && rm -rf app-box && tar xzf app-box.tar.gz"
THẤY : sha256 khớp, 3 file: Dockerfile, resolv.conf, weather.py'
[A1]='MÁY  : Laptop -> TV BOX (scp)
VIỆC : Chuyển binary aarch64 build từ source sang box
GÕ   : $ scp minicontainer-linux-aarch64 root@<IP_BOX>:/root/
       $ sha256sum /root/minicontainer-linux-aarch64
THẤY : sha256 của binary aarch64'
[A2]='MÁY  : TV BOX
VIỆC : Cài binary và kiểm tra môi trường
GÕ   : $ install -m755 /root/minicontainer-linux-aarch64 /usr/local/bin/minicontainer
       $ minicontainer --version
       $ minicontainer check
THẤY : 2.0.1-dev (aarch64, musl, static) + 11 dòng ok'
[A3]='MÁY  : TV BOX
VIỆC : Build app thời tiết cho arm64 rồi chạy
GÕ   : $ minicontainer pull --platform=linux/arm64 python:3.12-alpine
       $ minicontainer build --no-cache --platform=linux/arm64 -t weather-arm:1 /root/demo-mc/app-box
       $ minicontainer run -d --name weather-box --image=weather-arm-1 --ram=96M --net=bridge python /app/weather.py
       $ minicontainer logs weather-box
THẤY : JSON thời tiết Hà Nội, ran_on.arch = aarch64'
[A4]='MÁY  : TV BOX
VIỆC : Build CÙNG app đó cho x86_64, rồi gộp 2 arch thành 1 bundle
GÕ   : $ minicontainer pull --platform=linux/amd64 python:3.12-alpine
       $ minicontainer build --no-cache --platform=linux/amd64 -t weather-x86:1 /root/demo-mc/app-box
       $ minicontainer export --arch=x86_64 weather-arm-1 weather-x86-1 -o /root/demo-mc/weather-multi.tar.gz
THẤY : 1 file chứa aarch64.tar + x86_64.tar + multiarch.json'
[A5]='MÁY  : TV BOX -> Laptop (scp)
VIỆC : Chỉ gửi 1 bundle duy nhất sang laptop
GÕ   : $ scp root@<IP_BOX>:/root/demo-mc/weather-multi.tar.gz results/
THẤY : sha256 trùng khớp với bên box'
[A6]='MÁY  : Laptop (trong harness ubuntu:24.04)
VIỆC : Import bundle — runtime TỰ CHỌN đúng biến thể x86_64 cho máy này
GÕ   : $ minicontainer import /demo/results/weather-multi.tar.gz
       $ minicontainer run -d --name weather-from-box --image=weather-arm-1 --ram=96M --net=bridge python /app/weather.py
       $ minicontainer logs weather-from-box
THẤY : "chon bien the x86_64", chạy native, cùng nhiệt độ/độ ẩm'
[B1]='MÁY  : Laptop
VIỆC : Xác nhận binary x86_64 dùng đúng bản build từ source
GÕ   : $ ./minicontainer-linux-x86_64 --version
THẤY : 2.0.1-dev (x86_64, glibc, static)'
[B2]='MÁY  : Laptop (trong harness)
VIỆC : Kiểm tra môi trường chạy container
GÕ   : $ minicontainer check
THẤY : 12 dòng ok'
[B3]='MÁY  : Laptop
VIỆC : Build app GitHub cho x86 chạy thử, build bản arm, xuất 1 bundle đa arch + 1 bản x86 để thử sai
GÕ   : $ minicontainer pull --platform=linux/amd64 python:3.12-alpine
       $ minicontainer build --no-cache --platform=linux/amd64 -t repoinfo-x86:1 /demo/app-laptop
       $ minicontainer run -d --name repoinfo-local --image=repoinfo-x86-1 --ram=96M --net=bridge python /app/repoinfo.py
       $ minicontainer logs repoinfo-local
       $ minicontainer pull --platform=linux/arm64 python:3.12-alpine
       $ minicontainer build --no-cache --platform=linux/arm64 -t repoinfo-arm:1 /demo/app-laptop
       $ minicontainer export repoinfo-x86-1 -o /demo/results/repoinfo-x86-only.tar.gz
       $ minicontainer export --arch=aarch64 repoinfo-arm-1 repoinfo-x86-1 -o /demo/results/repoinfo-multi.tar.gz
THẤY : log x86_64; 1 bundle đa arch + 1 bundle chỉ x86_64'
[B4]='MÁY  : Laptop -> TV BOX (scp)
VIỆC : Gửi 1 bundle đa arch + 1 bundle x86 (phản diện)
GÕ   : $ scp repoinfo-multi.tar.gz repoinfo-x86-only.tar.gz root@<IP_BOX>:/root/demo-mc/
THẤY : sha256 trùng khớp'
[B5]='MÁY  : TV BOX
VIỆC : Thử bundle chỉ x86 (phải lỗi), rồi import bundle đa arch — runtime tự chọn aarch64
GÕ   : $ minicontainer import /root/demo-mc/repoinfo-x86-only.tar.gz
       $ minicontainer run --image=repoinfo-x86-1 python /app/repoinfo.py      -> Exec format error
       $ minicontainer import /root/demo-mc/repoinfo-multi.tar.gz             -> chon bien the aarch64
       $ minicontainer run -d --name repoinfo-box --image=repoinfo-arm-1 python /app/repoinfo.py
THẤY : FAIL ĐỘ x86, rồi OK native aarch64, cùng repo/stars'
[Z]='VIỆC : Tổng kết — log từng bước nằm trong results/'
)

step() {
    local id="$1" title="$2"; shift 2
    local log="$RESULTS/${id}-${title}.log"
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

box() {
    sshpass -f "$SSHPASS_FILE" ssh "${SSH_OPTS[@]}" "root@${BOX}" "$@"
}
box_sh() {
    box "set -eu; $1"
}
from_box() {
    sshpass -f "$SSHPASS_FILE" scp "${SCIP_OPTS[@]}" "root@${BOX}:$1" "$2"
}
put_box() {
    sshpass -f "$SSHPASS_FILE" scp "${SCIP_OPTS[@]}" "$1" "root@${BOX}:$2"
}
laptop() {
    local script="$1"
    docker run --rm --privileged -e MC_SETUP="$LAPTOP_SETUP" \
        -v "$DEMO_DIR":/demo -w /demo mc-demo-laptop \
        bash -c 'set -eu; eval "$MC_SETUP"; eval "$1"' _ "$script"
}

LAPTOP_SETUP='
mkdir -p /sys/fs/cgroup/mc-host
for p in $(cat /sys/fs/cgroup/cgroup.procs); do
  echo "$p" > /sys/fs/cgroup/mc-host/cgroup.procs 2>/dev/null || true
done
echo "+cpu +memory +pids +io" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
mkdir -p /var/lib/minicontainer/images /run/minicontainer /var/log/minicontainer
install -m755 /demo/minicontainer-linux-x86_64 /usr/local/bin/minicontainer
'

echo "############################################################"
echo "# MÁY: TV BOX (aarch64) và Laptop (x86_64, harness Docker)  #"
echo "############################################################"

step P1 "binary-tu-source-hoac-release-x86_64" bash -c "
cd '$DEMO_DIR'
if [ -x '$LOCAL_X86' ]; then
  echo '--- dùng binary build từ source (demo-cross-ship/build-local.sh) ---'
  sha256sum '$LOCAL_X86'
else
  echo '--- không có binary local, tải release GitHub ---'
  curl -fsSL -o minicontainer-linux-x86_64 '$RELEASE_URL/minicontainer-linux-x86_64'
  curl -fsSL -o SHA256SUMS.txt '$RELEASE_URL/SHA256SUMS.txt'
  grep ' minicontainer-linux-x86_64\$' SHA256SUMS.txt | sha256sum -c -
fi
chmod +x minicontainer-linux-x86_64
./minicontainer-linux-x86_64 --version
"

if ! docker image inspect mc-demo-laptop >/dev/null 2>&1; then
    docker build -q -t mc-demo-laptop -f "$DEMO_DIR/Dockerfile.laptop" "$DEMO_DIR" >/dev/null
fi

if [ "${SKIP_CLEANUP:-0}" != 1 ]; then
    step P2 "clean-box-junk" box_sh '
MC=/usr/local/bin/minicontainer
[ -x "$MC" ] || MC=/bin/true
for id in $($MC ps 2>/dev/null | awk "\$2 == \"RUNNING\" || \$2 == \"STOPPED\" {print \$1}"); do $MC rm -f "$id" >/dev/null 2>&1 || true; done
rm -f /root/mc-test*.log /root/mc-test*.done /root/rb.log /root/box-*.sh /root/box-*.log \
      /root/extra.sh /root/extra.log /root/dod*.sh /root/dod*.log /root/ship-*.sh /root/ship-*.log \
      /root/box-all* /root/task-svc-*.tar.gz /root/task-svc-*.sha256 \
      /root/gate-box.sh /root/gate-box.log /root/gate-box.progress /root/recipe-gate.sh \
      /root/final-*.log /root/test-v15*.log /root/soak-v152.log /root/minicontainer \
      /root/SHA256SUMS.txt /root/minicontainer-linux-aarch64 /root/"&&"
rm -rf /root/tasksvc /root/recipes /root/tests /root/demo-mc
mkdir -p /root/demo-mc
for i in $($MC images 2>/dev/null | awk "NR>1{print \$1}"); do $MC rmi "$i" >/dev/null 2>&1 || true; done
echo "containers: $($MC ps 2>&1 | head -1)"
echo "images: $($MC images 2>/dev/null | grep -vc "IMAGE\|chua co image")"
'
fi

do_p3() {
    box 'mkdir -p /root/demo-mc'
    tar -czf "$DEMO_DIR/app-box.tar.gz" -C "$DEMO_DIR" app-box
    sha256sum "$DEMO_DIR/app-box.tar.gz"
    put_box "$DEMO_DIR/app-box.tar.gz" /root/demo-mc/
    box 'set -eu; cd /root/demo-mc; sha256sum app-box.tar.gz; rm -rf app-box; tar xzf app-box.tar.gz; ls -l app-box'
}
step P3 "copy-app-to-box" do_p3

###############################################################################
# CHIỀU A: TV BOX -> LAPTOP (chương trình lấy thời tiết từ Open-Meteo)
###############################################################################

do_a1() {
    if [ -f "$LOCAL_ARM" ]; then
        echo "--- dùng binary build từ source ---"
        put_box "$LOCAL_ARM" /root/minicontainer-linux-aarch64
        box 'sha256sum /root/minicontainer-linux-aarch64'
    else
        echo "--- không có binary local, tải release GitHub trên box ---"
        box_sh "
cd /root
curl -fsSL -o minicontainer-linux-aarch64 '$RELEASE_URL/minicontainer-linux-aarch64'
curl -fsSL -o SHA256SUMS.txt '$RELEASE_URL/SHA256SUMS.txt'
grep ' minicontainer-linux-aarch64\$' SHA256SUMS.txt | sha256sum -c -
sha256sum minicontainer-linux-aarch64
"
    fi
}
step A1 "box-binary-tu-source-hoac-release" do_a1

step A2 "box-install-and-check" box_sh '
install -m755 /root/minicontainer-linux-aarch64 /usr/local/bin/minicontainer
/usr/local/bin/minicontainer --version
/usr/local/bin/minicontainer check
'

step A3 "box-build-and-run-weather" box_sh '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/arm64 python:3.12-alpine
$MC build --no-cache --platform=linux/arm64 -t weather-arm:1 /root/demo-mc/app-box
$MC run -d --name weather-box --image=weather-arm-1 --ram=96M --net=bridge python /app/weather.py
sleep 12
$MC logs weather-box
'

step A4 "box-build-x86-and-export" box_sh '
set -eu
MC=/usr/local/bin/minicontainer
$MC pull --platform=linux/amd64 python:3.12-alpine
$MC build --no-cache --platform=linux/amd64 -t weather-x86:1 /root/demo-mc/app-box
$MC export --arch=x86_64 weather-arm-1 weather-x86-1 -o /root/demo-mc/weather-multi.tar.gz
sha256sum /root/demo-mc/weather-multi.tar.gz
ls -lh /root/demo-mc/weather-multi.tar.gz
tar -tzf /root/demo-mc/weather-multi.tar.gz | grep -E "multiarch.json|aarch64.tar|x86_64.tar"
'

do_a5() {
    from_box /root/demo-mc/weather-multi.tar.gz "$RESULTS/weather-multi.tar.gz"
    sha256sum "$RESULTS/weather-multi.tar.gz"
    tar -tzf "$RESULTS/weather-multi.tar.gz"
}
step A5 "scp-bundle-box-to-laptop" do_a5

step A6 "laptop-import-and-confirm" laptop '
MC=/usr/local/bin/minicontainer
echo "--- phiên bản ---"; $MC --version
echo "--- import bundle đa kiến trúc ---"; $MC import /demo/results/weather-multi.tar.gz | tee /dev/stderr | grep -q "chon bien the x86_64"
echo "--- chạy ---"; $MC run -d --name weather-from-box --image=weather-arm-1 --ram=96M --net=bridge python /app/weather.py
sleep 12
echo "--- log ---"; OUT=$($MC logs weather-from-box); echo "$OUT"
echo "$OUT" | grep -q "\"program\": \"weather.py\""
echo "$OUT" | grep -q "\"arch\": \"x86_64\""
echo "=> XÁC NHẬN: chạy trên laptop (x86_64), cùng chương trình weather.py"
'

###############################################################################
# CHIỀU B: LAPTOP -> TV BOX (chương trình lấy thông tin repo từ GitHub API)
###############################################################################

step B1 "laptop-binary-tu-source-hoac-release" bash -c "
cd '$DEMO_DIR'
if [ -x '$LOCAL_X86' ]; then
  echo '--- dùng binary build từ source ---'
  sha256sum '$LOCAL_X86'
else
  curl -fsSL -o minicontainer-linux-x86_64 '$RELEASE_URL/minicontainer-linux-x86_64'
  curl -fsSL -o SHA256SUMS.txt '$RELEASE_URL/SHA256SUMS.txt'
  grep ' minicontainer-linux-x86_64\$' SHA256SUMS.txt | sha256sum -c -
fi
chmod +x minicontainer-linux-x86_64
./minicontainer-linux-x86_64 --version
"

step B2 "laptop-check" laptop '
/usr/local/bin/minicontainer check
'

step B3 "laptop-build-run-x86-and-export-arm" laptop '
set -eu
MC=/usr/local/bin/minicontainer
echo "--- base x86 + build + chạy thử trên laptop ---"
$MC pull --platform=linux/amd64 python:3.12-alpine
$MC build --no-cache --platform=linux/amd64 -t repoinfo-x86:1 /demo/app-laptop
$MC run -d --name repoinfo-local --image=repoinfo-x86-1 --ram=96M --net=bridge python /app/repoinfo.py
sleep 12
OUT=$($MC logs repoinfo-local); echo "$OUT"
echo "$OUT" | grep -q "\"program\": \"repoinfo.py\""
echo "--- base arm64 + build cho box + export ---"
$MC pull --platform=linux/arm64 python:3.12-alpine
$MC build --no-cache --platform=linux/arm64 -t repoinfo-arm:1 /demo/app-laptop
$MC export repoinfo-x86-1 -o /demo/results/repoinfo-x86-only.tar.gz
$MC export --arch=aarch64 repoinfo-arm-1 repoinfo-x86-1 -o /demo/results/repoinfo-multi.tar.gz
sha256sum /demo/results/repoinfo-x86-only.tar.gz /demo/results/repoinfo-multi.tar.gz
tar -tzf /demo/results/repoinfo-multi.tar.gz | grep -E "multiarch.json|aarch64.tar|x86_64.tar"
'

do_b4() {
    put_box "$RESULTS/repoinfo-x86-only.tar.gz" /root/demo-mc/
    put_box "$RESULTS/repoinfo-multi.tar.gz" /root/demo-mc/
    box 'sha256sum /root/demo-mc/repoinfo-x86-only.tar.gz /root/demo-mc/repoinfo-multi.tar.gz'
}
step B4 "scp-bundle-laptop-to-box" do_b4

step B5 "box-import-and-run" box_sh '
set -eu
MC=/usr/local/bin/minicontainer
echo "--- phan dien: bundle chi x86 tren box ARM ---"
$MC import /root/demo-mc/repoinfo-x86-only.tar.gz >/dev/null
set +e
WRONG=$($MC run --rm --image=repoinfo-x86-1 --ram=96M python /app/repoinfo.py 2>&1); RC=$?
set -e
echo "$WRONG"
[ $RC -ne 0 ] || { echo "that: x86 chay duoc tren ARM?"; exit 1; }
echo "=> FAIL DO: bundle chi x86 khong chay duoc tren box ARM"
echo "--- bundle da kien truc: tu chon aarch64 ---"
$MC import /root/demo-mc/repoinfo-multi.tar.gz | tee /dev/stderr | grep -q "chon bien the aarch64"
$MC run -d --name repoinfo-box --image=repoinfo-arm-1 --ram=96M --net=bridge python /app/repoinfo.py
sleep 12
OUT=$($MC logs repoinfo-box); echo "$OUT"
echo "$OUT" | grep -q "\"program\": \"repoinfo.py\""
echo "$OUT" | grep -q "\"arch\": \"aarch64\""
echo "$OUT" | grep -q "\"kernel\": \"6.18.38-meson64\""
echo "=> XÁC NHẬN: chạy trên TV Box (aarch64), cùng chương trình repoinfo.py"
'

step Z "summary" bash -c '
echo "Hoàn tất 2 chiều: Box -> Laptop và Laptop -> Box."
echo "Log từng bước nằm trong: '"$RESULTS"'"
ls -1 '"$RESULTS"'/*.log
'
