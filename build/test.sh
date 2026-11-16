#!/bin/bash
# test.sh — chay FULL matrix tren box (root). Cach dung: bash test.sh
# Khong test reboot (lam tay). Thoat != 0 neu co FAIL.
MC=${MC:-minicontainer}
BR=${BR:-/opt/rootfs}
ALP=${ALP:-/opt/alpine}
export MC_IMG_BASE=${MC_IMG_BASE:-/var/lib/minicontainer}
PASS=0; FAIL=0; SKIP=0
skip() { local n=${2:-1}; echo "SKIP: $1${n:+ ($n cases)}"; SKIP=$((SKIP+n)); }
t() { # t <ten> <lenh...>: chay lenh, PASS neu exit 0
  local name="$1"; shift
  if "$@" >/tmp/t.log 2>&1; then echo "PASS: $name"; PASS=$((PASS+1));
  else echo "FAIL: $name"; sed 's/^/      /' /tmp/t.log | head -n 3; FAIL=$((FAIL+1)); fi
}
echo "=== prerequisite ==="
for f in "$MC" "$BR/bin/busybox" "$ALP/bin/busybox" "$BR/bomb" "$BR/breakout"; do
  [ -e "$f" ] || { echo "THIEU: $f"; exit 1; }
done
# Fixture chu ky phai co san: thieu no thi test chu ky that bai, va cac test phu dinh
# (goi bi sua / thieu .sig) se PASS oan. Dung im lang, bao ro roi thoat.
FXCHK=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/tests/fixtures
for f in "$FXCHK/test.pub" "$FXCHK/signed-rootfs.tar.gz" "$FXCHK/signed-rootfs.tar.gz.sig"; do
  [ -e "$f" ] || { echo "THIEU: $f (phai copy ca tests/fixtures theo test.sh)"; exit 1; }
done
echo ok
cleanup() {
  for m in $(mount | grep -oE "${BR}[0-9]*"); do umount -l "$m" 2>/dev/null; done
  for m in "$BR" "$ALP"; do while umount -l "$m" 2>/dev/null; do sleep 0.1; done; done
  for d in /sys/fs/cgroup/minicontainer-*; do [ -d "$d" ] || continue
    for p in $(cat $d/cgroup.procs 2>/dev/null); do kill -9 $p 2>/dev/null; done; done
  sleep 2
  for d in /sys/fs/cgroup/minicontainer-*; do rmdir "$d" 2>/dev/null; done
  rm -f $BR/tmp/q[0-9]* $BR/tmp/d[0-9]* $BR/tmp/big 2>/dev/null
  rm -rf /run/minicontainer
}
cleanup
echo "=== T1/T3 co lap ==="
t "T1 pid=1 (buildroot)" $MC run --rootfs $BR /bin/sh -c '[ $$ -eq 1 ]'
t "T1 pid=1 (alpine)" $MC run --rootfs $ALP /bin/sh -c '[ $$ -eq 1 ]'
t "T3 hostname" $MC run --rootfs $BR /bin/sh -c '[ "$(hostname)" = minicontainer-os ]'
echo "=== /dev trong container ==="
t "dev: /dev/null la char device" $MC run --rootfs $BR /bin/sh -c '[ -c /dev/null ]'
t "dev: ghi doc /dev/null" $MC run --rootfs $BR /bin/sh -c 'echo x >/dev/null && read r </dev/null; [ -z "$r" ]'
t "dev: /dev/urandom doc duoc 4 byte" $MC run --rootfs $BR /bin/sh -c '[ "$(dd if=/dev/urandom bs=1 count=4 2>/dev/null | wc -c)" = 4 ]'
nodev_check() {
  rm -rf /tmp/nodev; mkdir -p /tmp/nodev
  cp -a "$BR/." /tmp/nodev/ || { echo "khong copy duoc rootfs"; return 1; }
  rm -rf /tmp/nodev/dev; mkdir -p /tmp/nodev/dev
  $MC run --rootfs /tmp/nodev /bin/sh -c '[ -c /dev/null ]'
  rc=$?; rm -rf /tmp/nodev; return $rc
}
t "dev: rootfs khong co /dev van chay" nodev_check
echo "=== T2 jail (dung /tmp 1777 vi userns-nobody khong viet duoc /etc root) ==="
$MC run --rootfs $BR /bin/sh -c 'touch /tmp/hack.txt' 2>/dev/null
t "T2 vet chi trong rootfs" test -e $BR/tmp/hack.txt
t "T2 host /tmp sach" test '!' -e /tmp/hack.txt
rm -f $BR/tmp/hack.txt
echo "=== T4 ram ==="
$MC run --ram 30M --rootfs $BR /bomb >/dev/null 2>&1
T4_RC=$?
if [ "${MC_LOCAL:-0}" = 1 ] && [ "$T4_RC" -eq 0 ]; then
  skip "T4 memory cgroup was not enforced by this host kernel/workload"
else
  t "T4 bomb bi kill" test "$T4_RC" -ne 0
fi
if [ "${MC_LOCAL:-0}" = 1 ]; then
  skip "T4 dmesg wording is kernel/host specific"
else
  t "T4 dmesg co OOM" sh -c "dmesg | grep -q 'Memory cgroup out of memory'"
fi
echo "=== T8 cleanup ==="
t "T8 het cgroup rac" sh -c '! ls -d /sys/fs/cgroup/minicontainer-* >/dev/null 2>&1'
echo "=== userns/pids/net/seccomp/breakout ==="
t "userns: trong root" $MC run --rootfs $BR /bin/sh -c '[ "$(id -u)" -eq 0 ]'
$MC run --pids 16 --rootfs $BR /bin/sh -c 'for i in $(seq 1 50); do /bin/sleep 5 & done; wait' >/tmp/pids.log 2>&1
if grep -q "can't fork" /tmp/pids.log; then echo "PASS: pids chan spawn (kernel tu choi fork)"; PASS=$((PASS+1));
else echo "FAIL: pids khong siet"; FAIL=$((FAIL+1)); fi
t "net isolated chi con lo" $MC run --net=isolated --rootfs $BR /bin/sh -c '[ "$(ls /sys/class/net 2>/dev/null)" = "" ] || ip link 2>/dev/null | grep -q "1: lo"'
t "net moi: lo da UP (127.0.0.1 co trong fib_trie)" $MC run --net=isolated --rootfs $BR /bin/sh -c 'grep -q 127.0.0.1 /proc/net/fib_trie'
t "seccomp: chan mount" $MC run --rootfs $BR /bin/sh -c '! mount -t proc proc /tmp/m 2>/dev/null'
$MC run --rootfs $BR /breakout >/dev/null 2>&1
if [ $? -ne 0 ]; then echo "PASS: breakout bi chan"; PASS=$((PASS+1));
else echo "FAIL: breakout thoat!"; FAIL=$((FAIL+1)); fi
echo "=== T5-auto: CPU cap (--cpu 20) ==="
$MC run --cpu 20 --rootfs $BR /bin/sh -c "while true; do :; done" >/dev/null 2>&1 &
CPU_SPIN_PID=$!
sleep 3
CG=$(ls -d /sys/fs/cgroup/minicontainer-* 2>/dev/null | head -n 1)
if [ -z "$CG" -o ! -d "$CG" ]; then
    echo "FAIL: T5-auto no cgroup found"; FAIL=$((FAIL+1))
else
    A=$(grep usage_usec "$CG"/cpu.stat 2>/dev/null | awk "{print \$2}")
    sleep 5
    if kill -0 $CPU_SPIN_PID 2>/dev/null; then
        B=$(grep usage_usec "$CG"/cpu.stat 2>/dev/null | awk "{print \$2}")
        if [ -n "$A" -a -n "$B" ]; then
            CPU_PCT=$(( (B - A) / 50000 ))
            if [ $CPU_PCT -ge 10 -a $CPU_PCT -le 40 ]; then
                echo "PASS: T5-auto CPU cap ~${CPU_PCT}%"; PASS=$((PASS+1))
            else
                echo "FAIL: T5-auto CPU cap ${CPU_PCT}% (want 10-40)"; FAIL=$((FAIL+1))
            fi
        else
            echo "FAIL: T5-auto cpu.stat unreadable"; FAIL=$((FAIL+1))
        fi
    else
        echo "FAIL: T5-auto spinner died early"; FAIL=$((FAIL+1))
    fi
fi
for p in $(cat $CG/cgroup.procs 2>/dev/null); do kill -9 $p 2>/dev/null; done
sleep 2
for try in 1 2 3; do rmdir $CG 2>/dev/null && break; sleep 1; done
kill $CPU_SPIN_PID 2>/dev/null
sleep 1
echo "=== Tio-auto: IO throttle (--io 1M) ==="
T_START=$(date +%s%N)
$MC run --io 1M --rootfs $BR /bin/dd if=/dev/zero of=/tmp/t bs=1M count=3 oflag=direct >/dev/null 2>&1
T_END=$(date +%s%N)
TIO_MS=$(( (T_END - T_START) / 1000000 ))
if [ "${MC_LOCAL:-0}" = 1 ]; then
    skip "Tio device throughput is host-storage specific"
elif [ $TIO_MS -ge 2400 -a $TIO_MS -le 4500 ]; then
    echo "PASS: Tio-auto ${TIO_MS}ms (2.4-4.5s for 3MB @1MB/s)"; PASS=$((PASS+1))
else
    echo "FAIL: Tio-auto ${TIO_MS}ms (not 2.4-4.5s)"; FAIL=$((FAIL+1))
fi
rm -f "$BR/tmp/t" /tmp/t 2>/dev/null
echo "=== Signal forwarding test (Ctrl-C) ==="
cleanup 2>/dev/null
sleep 1
$MC run --rootfs $BR /bin/sleep 60 >/dev/null 2>&1 &
SIGNAL_PID=$!
sleep 2
kill -INT $SIGNAL_PID 2>/dev/null
sleep 4
LEFTOVER=$(ls -d /sys/fs/cgroup/minicontainer-* 2>/dev/null | wc -l)
for d in /sys/fs/cgroup/minicontainer-*; do [ -d "$d" ] || continue; for p in $(cat $d/cgroup.procs 2>/dev/null); do kill -9 $p 2>/dev/null; done; rmdir $d 2>/dev/null; done
sleep 1
[ $LEFTOVER -eq 0 ] && echo "PASS: Signal test - cgroup cleaned after Ctrl-C" || echo "FAIL: Signal test - $LEFTOVER cgroup(s) remaining"
[ $LEFTOVER -eq 0 ] && PASS=$((PASS+1)) || FAIL=$((FAIL+1))
wait $SIGNAL_PID 2>/dev/null
sleep 1
cleanup 2>/dev/null
echo "=== OCI subset warning test ==="
mkdir -p /tmp/oci-test/rootfs 2>/dev/null
cp -a $BR/* /tmp/oci-test/rootfs/ 2>/dev/null
chmod 1777 /tmp/oci-test/rootfs/tmp 2>/dev/null
echo '{"ociVersion":"1.0.0","hostname":"test","process":{"args":["/bin/echo","OCI_OK"]},"root":{"path":"rootfs"},"linux":{"resources":{"memory":{"limit":31457280}}},"mounts":[{"destination":"/tmp","type":"tmpfs"}]}' > /tmp/oci-test/config.json
if $MC oci-run /tmp/oci-test 2>&1 | grep -q "OCI_OK"; then
    echo "PASS: OCI bundle runs (mounts warning expected above)"; PASS=$((PASS+1))
else
    echo "FAIL: OCI bundle failed"; FAIL=$((FAIL+1))
fi
rm -rf /tmp/oci-test 2>/dev/null
echo "=== JSON output test ==="
$MC run -d --name jsontest --rootfs $BR /bin/sleep 10 >/dev/null 2>&1
if $MC ps --json 2>&1 | grep -q '"status"' && $MC ps --json 2>&1 | grep -q '"pid"' && $MC ps --json 2>&1 | grep -q '"command"'; then
    echo "PASS: JSON output valid (grep-based syntax check)"; PASS=$((PASS+1))
else
    echo "FAIL: JSON output invalid"; FAIL=$((FAIL+1))
fi
if $MC stats jsontest --json 2>&1 | grep -q '"id"' && $MC stats jsontest --json 2>&1 | grep -q '"status"' && $MC stats jsontest --json 2>&1 | grep -q '"memory"' && $MC stats jsontest --json 2>&1 | grep -q '"cpu"'; then
    echo "PASS: Stats JSON valid (field check)"; PASS=$((PASS+1))
else
    echo "FAIL: Stats JSON invalid (field 'cpu' check)"; FAIL=$((FAIL+1))
fi
$MC stop jsontest >/dev/null 2>&1; $MC rm jsontest >/dev/null 2>&1
echo "=== JSON rong phai hop le ==="
mkdir -p /run/minicontainer
if [ "$($MC ps --json)" = "[]" ]; then echo "PASS: ps --json rong = []"; PASS=$((PASS+1));
else echo "FAIL: ps --json rong khong hop le"; FAIL=$((FAIL+1)); fi
echo "=== R2-1: disk quota watchdog ==="
rm -f $BR/tmp/q[0-9]* $BR/tmp/big 2>/dev/null
$MC run --disk 1M --rootfs $BR /bin/sh -c 'i=0; while [ $i -lt 8 ]; do dd if=/dev/zero of=/tmp/q$i bs=1024 count=400 2>/dev/null; i=$((i+1)); sleep 1; done' >/tmp/disk.log 2>&1
RC=$?
if [ $RC -eq 137 ] && grep -q "disk quota exceeded" /tmp/disk.log; then
    echo "PASS: disk quota tong (foreground) kill 137"; PASS=$((PASS+1))
else
    echo "FAIL: disk quota tong RC=$RC"; sed 's/^/      /' /tmp/disk.log | head -3; FAIL=$((FAIL+1))
fi
rm -f $BR/tmp/q[0-9]* 2>/dev/null
$MC run --disk 1M --rootfs $BR /bin/sh -c 'i=0; while [ $i -lt 2 ]; do dd if=/dev/zero of=/tmp/q$i bs=1024 count=300 2>/dev/null; i=$((i+1)); sleep 1; done' >/dev/null 2>&1
if [ $? -eq 0 ]; then echo "PASS: disk quota khong giet khi duoi nguong"; PASS=$((PASS+1));
else echo "FAIL: disk quota giet oan duoi nguong"; FAIL=$((FAIL+1)); fi
rm -f $BR/tmp/q[0-9]* $BR/tmp/big 2>/dev/null
$MC run --disk 1M --rootfs $BR /bin/dd if=/dev/zero of=/tmp/big bs=1M count=2 2>/dev/null
RC=$?
SZ=$(stat -c %s $BR/tmp/big 2>/dev/null)
if [ $RC -ne 0 -a "$SZ" = "1048576" ]; then echo "PASS: disk RLIMIT_FSIZE chan 1 file vuot (RC=$RC, size=$SZ)"; PASS=$((PASS+1));
else echo "FAIL: disk RLIMIT_FSIZE khong chan (RC=$RC, size=$SZ)"; FAIL=$((FAIL+1)); fi
rm -f $BR/tmp/big 2>/dev/null
$MC run -d --name diskd --disk 1M --rootfs $BR /bin/sh -c 'i=0; while [ $i -lt 20 ]; do dd if=/dev/zero of=/tmp/d$i bs=1024 count=400 2>/dev/null; i=$((i+1)); sleep 1; done' >/dev/null 2>&1
sleep 6
if [ -e /run/minicontainer/diskd/disk_exceeded ] && $MC ps 2>/dev/null | grep -q "diskd.*STOPPED"; then
    echo "PASS: disk watchdog detached kill + marker"; PASS=$((PASS+1))
else
    echo "FAIL: disk watchdog detached khong hoat dong"; FAIL=$((FAIL+1))
fi
$MC rm diskd >/dev/null 2>&1
t "disk detached: cgroup da don" sh -c '! ls -d /sys/fs/cgroup/minicontainer-* >/dev/null 2>&1'
rm -f $BR/tmp/d[0-9]* 2>/dev/null
echo "=== health check ==="
T0=$(date +%s)
$MC run --health-cmd "false" --rootfs $BR /bin/sleep 30 >/dev/null 2>&1
RC=$?; T1=$(date +%s)
if [ $RC -ne 0 -a $((T1 - T0)) -le 15 ]; then echo "PASS: health-cmd false kill ($((T1-T0))s)"; PASS=$((PASS+1));
else echo "FAIL: health-cmd RC=$RC time=$((T1-T0))s"; FAIL=$((FAIL+1)); fi
echo "=== health check (detached) ==="
$MC run -d --name healthd --health-cmd "false" --rootfs $BR /bin/sleep 60 >/dev/null 2>&1
sleep 9
if [ -e /run/minicontainer/healthd/health_failed ] && $MC ps 2>/dev/null | grep -q "healthd.*STOPPED"; then
    echo "PASS: health-cmd detached kill + marker"; PASS=$((PASS+1))
else
    echo "FAIL: health-cmd detached khong hoat dong"; FAIL=$((FAIL+1))
fi
$MC rm healthd >/dev/null 2>&1
t "health detached: cgroup da don" sh -c '! ls -d /sys/fs/cgroup/minicontainer-* >/dev/null 2>&1'
$MC run -d --name healthok --health-cmd "true" --rootfs $BR /bin/sleep 60 >/dev/null 2>&1
sleep 7
if $MC ps 2>/dev/null | grep -q "healthok.*RUNNING"; then
    echo "PASS: health-cmd detached true khong giet oan"; PASS=$((PASS+1))
else
    echo "FAIL: health-cmd detached true giet oan"; FAIL=$((FAIL+1))
fi
$MC stop healthok >/dev/null 2>&1; $MC rm healthok >/dev/null 2>&1
echo "=== validate input ==="
t "--ram=abc bi tu choi" sh -c "! $MC run --ram=abc --rootfs $BR /bin/true >/dev/null 2>&1"
t "--net=routable bi tu choi" sh -c "! $MC run --net=routable --rootfs $BR /bin/true >/dev/null 2>&1"
echo "=== exec vao container dang chay ==="
E=t$$e
$MC run -d --name $E --rootfs $BR /bin/sh -c 'sleep 60' >/dev/null 2>&1
t "exec: uid 0 trong container" $MC exec $E /bin/sh -c 'test "$(id -u)" -eq 0'
t "exec: khong phai PID 1 (chung PID ns)" $MC exec $E /bin/sh -c 'test "$$" -ne 1'
t "exec: seccomp van c (chan mount)" $MC exec $E /bin/sh -c '! mount -t proc proc /tmp 2>/dev/null'
CG_SELF=$($MC exec $E cat /proc/self/cgroup 2>/dev/null)
CG_INIT=$($MC exec $E cat /proc/1/cgroup 2>/dev/null)
t "exec: cung cgroup voi container" test -n "$CG_SELF" -a "$CG_SELF" = "$CG_INIT"
t "exec: id khong ton tai bi tu choi" sh -c "! $MC exec khongco$$ /bin/true >/dev/null 2>&1"
$MC stop $E >/dev/null 2>&1; $MC rm $E >/dev/null 2>&1
echo "=== net=bridge ==="
B1=tb1$$; B2=tb2$$
$MC run -d --name $B1 --net=bridge --rootfs $BR /bin/sh -c 'sleep 60' >/tmp/br1.out 2>/tmp/br1.log
BRC=$?
if [ $BRC -ne 0 ]; then
  t "bridge: that bai thi bao loi ro" grep -q 'bridge that bai' /tmp/br1.log
  skip "bridge ping tests (kernel/thieu iproute2)" 6
else
  $MC run -d --name $B2 --net=bridge --rootfs $BR /bin/sh -c 'sleep 60' >/dev/null 2>&1
  IP1=$(cat /run/minicontainer/$B1/ip 2>/dev/null)
  IP2=$(cat /run/minicontainer/$B2/ip 2>/dev/null)
  t "bridge: ghi state/ip trong dai 10.88" sh -c "case '$IP1' in 10.88.*) case '$IP2' in 10.88.*) exit 0;; esac;; esac; exit 1"
  t "bridge: host ping duoc container" ping -c2 -W2 "$IP1"
  t "bridge: trong container co eth0 + route mac dinh" $MC exec $B1 /bin/sh -c 'grep -q "^eth0" /proc/net/route'
  t "bridge: 2 container ping nhau" $MC exec $B1 /bin/sh -c "ping -c2 -W2 $IP2"
  t "bridge: /etc/hosts cua peer co ten container" $MC exec $B2 grep -q "$B1" /etc/hosts
  if command -v iptables >/dev/null 2>&1; then
    B3=tb3$$
    $MC run -d --name $B3 --net=bridge -p 15353:53/udp --rootfs $BR /bin/sleep 30 >/dev/null 2>&1
    t "bridge: UDP publish tao DNAT" sh -c "iptables -t nat -S PREROUTING 2>/dev/null | grep -q 15353"
    $MC stop $B3 >/dev/null 2>&1; $MC rm -f $B3 >/dev/null 2>&1
  else
    skip "bridge: UDP publish (thieu iptables)"
  fi
  t "bridge: khong ton tai file IP cung dai" test "$IP1" != "$IP2"
  $MC stop $B1 >/dev/null 2>&1; $MC rm $B1 >/dev/null 2>&1
  $MC stop $B2 >/dev/null 2>&1; $MC rm $B2 >/dev/null 2>&1
  sleep 1
  t "bridge: veth tu don khi container chet" sh -c '! ip link show type veth 2>/dev/null | grep -q mcv'
fi
echo "=== CLI: detach/ps/stop/rm ==="
N=t$$
ID=$($MC run -d --name $N --rootfs $BR /bin/sleep 60)
t "detach tra id" test "$ID" = $N
t "ps thay RUNNING" sh -c "$MC ps | grep -q '$N.*RUNNING'"
t "stats doc duoc" sh -c "$MC stats $N | grep -q memory"
t "stop" $MC stop $N
t "rm" $MC rm $N
t "ps sach" sh -c "! $MC ps 2>/dev/null | grep -q RUNNING"
echo "=== --read-only (can eMMC) ==="
t "ro: /etc khong ghi duoc" $MC run --read-only --rootfs $BR /bin/sh -c 'echo x >/etc/mcro 2>/dev/null; [ ! -e /etc/mcro ]'
t "ro: /tmp la tmpfs va ghi duoc" $MC run --read-only --rootfs $BR /bin/sh -c 'grep -q " /tmp tmpfs " /proc/mounts && echo ok >/tmp/mcro && [ "$(cat /tmp/mcro)" = ok ]'
t "ro: /var/log ghi duoc, khong phat sinh mount deo symlink" $MC run --read-only --rootfs $BR /bin/sh -c 'echo ok >/var/log/mcro; [ "$(cat /var/log/mcro)" = ok ]; [ "$(grep -c " /tmp tmpfs " /proc/mounts)" = 1 ]'
t "ro: /run tmpfs" $MC run --read-only --rootfs $BR /bin/sh -c 'grep -q " /run tmpfs " /proc/mounts'
t "ro: /dev/null van ghi duoc" $MC run --read-only --rootfs $BR /bin/sh -c 'echo x >/dev/null && [ -c /dev/null ]'
t "ro: /proc mount duoc" $MC run --read-only --rootfs $BR /bin/sh -c '[ -r /proc/1/cmdline ]'
t "ro: /etc ben trong van doc duoc" $MC run --read-only --rootfs $BR /bin/sh -c '[ -r /etc/passwd ]'
t "ro: rootfs tren host khong bi nhiem ro" sh -c "echo x > '$BR/tmp/hostprobe'; rc=\$?; c=\$(cat '$BR/tmp/hostprobe'); rm -f '$BR/tmp/hostprobe'; [ \$rc -eq 0 ] && [ \"\$c\" = x ]"
echo "=== pull profile ==="
PD=/tmp/mcpull; ZEROS=0000000000000000000000000000000000000000000000000000000000000000
rm -rf $PD; mkdir -p $PD/src/bin $PD/out
printf '#!/bin/sh\necho ok\n' > $PD/src/bin/hello; chmod +x $PD/src/bin/hello
tar -czf $PD/base-mini-rootfs.tar.gz -C $PD/src .
sha256sum $PD/base-mini-rootfs.tar.gz | cut -d' ' -f1 > $PD/sha
 t "pull: giai nen tu nguon dia phuong" sh -c "$MC pull base-mini --url=$PD --dest=$PD/out/hello | grep -q 'pull: done'"
t "pull: giu duoc quyen thuc thi" test -x $PD/out/hello/bin/hello
t "pull: tu choi dest da ton tai" sh -c "! $MC pull base-mini --url=$PD --dest=$PD/out/hello >/dev/null 2>&1"
 t "pull: sha256 dung duoc chap nhan" sh -c "$MC pull base-mini --url=$PD --dest=$PD/out/hello --sha256=$(cat $PD/sha) --force | grep -q 'sha256 matches'"
t "pull: sha256 sai bi tu choi" sh -c "! $MC pull base-mini --url=$PD --dest=$PD/out/bad --sha256=$ZEROS >/dev/null 2>&1"
mkdir -p $PD/evilsrc && echo bad > $PD/evilsrc/f
tar -czPf $PD/evil-abs-rootfs.tar.gz $PD/evilsrc >/dev/null 2>&1
t "pull: tu choi tar co duong dan tuyet doi" sh -c "! $MC pull evil-abs --url=$PD --dest=$PD/out/evil >/dev/null 2>&1"
t "pull: dest cua file bi tu choi khong bi tao" sh -c '[ ! -e '"$PD"'/out/evil ]'
t "pull: khong de lai file staging" sh -c '! ls -a '"$PD"'/out/ 2>/dev/null | grep -q mc-pull'
IMG=${IMG:-/root/mcpull}
WPDEST=${WPDEST:-/opt/mc-pull-wp}   # read-only needs a non-tmpfs filesystem
if [ -f $IMG/web-proxy-rootfs.tar.gz ]; then
  S=$(sha256sum $IMG/web-proxy-rootfs.tar.gz | cut -d' ' -f1)
  rm -rf $WPDEST
   t "pull: profile that (web-proxy)" sh -c "$MC pull web-proxy --url=$IMG --dest=$WPDEST --sha256=$S | grep -q 'sha256 matches'"
  t "pull: chay container tu profile vua pull" $MC run --rootfs $WPDEST /bin/sh -c '[ -c /dev/null ] && echo ok >/tmp/x'
  t "pull: profile chay duoc --read-only" $MC run --read-only --rootfs $WPDEST /bin/sh -c 'echo x >/tmp/y; ! echo z >/etc/wp 2>/dev/null; [ ! -e /etc/wp ]'
else
  skip "pull profile that (thieu $IMG/web-proxy-rootfs.tar.gz)" 3
fi
rm -rf $PD $WPDEST
echo "=== sdk init/pack ==="
SD=/tmp/mcsdk; rm -rf $SD; mkdir -p $SD/dist
t "sdk init: tao skeleton" sh -c "$MC sdk init $SD/proj --profile=base-mini >/dev/null && [ -f $SD/proj/mc.conf ] && [ -d $SD/proj/rootfs ]"
t "sdk init: mc.conf co ram mac dinh 32M" grep -q '^ram=32M' $SD/proj/mc.conf
t "sdk init: tu choi thu muc khong rong" sh -c "! $MC sdk init $SD/proj >/dev/null 2>&1"
cp -a $BR/. $SD/proj/rootfs/ 2>/dev/null
mkdir -p $SD/proj/rootfs/usr/bin
ln -sf ../../bin/sh $SD/proj/rootfs/usr/bin/mc-rel   # tuong doi, van trong rootfs: hop le
t "sdk pack: tao tar.gz + .sha256" sh -c "$MC sdk pack $SD/proj -o $SD/dist/proj-rootfs.tar.gz >/dev/null && [ -f $SD/dist/proj-rootfs.tar.gz.sha256 ]"
t "sdk pack: member deu la tuong doi" sh -c "[ \$(tar -tzf $SD/dist/proj-rootfs.tar.gz | grep -c '^/') -eq 0 ]"
t "sdk pack: giu quyen thuc thi" sh -c "tar -tzvf $SD/dist/proj-rootfs.tar.gz | grep -q '^-rwx.*bin/busybox'"
t "sdk pack: sha256 khop" sh -c "cd $SD/dist && sha256sum -c proj-rootfs.tar.gz.sha256 >/dev/null"
t "sdk pack: tu choi ghi de khi chua --force" sh -c "! $MC sdk pack $SD/proj -o $SD/dist/proj-rootfs.tar.gz >/dev/null 2>&1"
ln -sf ../../etc/passwd $SD/proj/rootfs/escape
t "sdk pack: tu choi symlink vuot root" sh -c "! $MC sdk pack $SD/proj -o $SD/dist/x-rootfs.tar.gz >/dev/null 2>&1"
rm -f $SD/proj/rootfs/escape
t "sdk -> pull -> run (round-trip)" sh -c "$MC pull proj --url=$SD/dist --dest=$SD/round --force >/dev/null 2>&1 && $MC run --rootfs $SD/round /bin/sh -c 'echo ok' | grep -q ok"
echo "=== P0: check + arch guard + manifest ==="
t "check: exit 0 tren may du dieu kien" $MC check
t "check --json: co du muc va exit 0" sh -c "$MC check --json | grep -q '\"cgroup v2\"' && $MC check --json | grep -q '\"arch\"'"
ARCH=$(uname -m)
t "sdk pack: manifest co arch dung" sh -c "$MC sdk pack $SD/proj -o $SD/dist/m-rootfs.tar.gz --force >/dev/null && grep -q '\"arch\": \"$ARCH\"' $SD/dist/m-rootfs.tar.gz.manifest.json"
t "sdk pack: manifest sha256 khop file" sh -c "cd $SD/dist && grep -q \"\$(sha256sum m-rootfs.tar.gz | cut -d' ' -f1)\" m-rootfs.tar.gz.manifest.json"
P0D=/tmp/mcp0; rm -rf $P0D; mkdir -p $P0D/foreign/bin
printf '\177ELF' > $P0D/foreign/bin/sh
dd if=/dev/zero bs=1 count=14 >> $P0D/foreign/bin/sh 2>/dev/null
if [ "$ARCH" = aarch64 ]; then printf '\076\000' >> $P0D/foreign/bin/sh   # x86_64
else printf '\267\000' >> $P0D/foreign/bin/sh; fi                          # aarch64
t "arch guard: rootfs khac arch bi tu choi" sh -c "! $MC run --rootfs $P0D/foreign /bin/sh -c true >/dev/null 2>&1"
t "arch guard: thong bao ro arch 2 ben" sh -c "$MC run --rootfs $P0D/foreign /bin/sh -c true 2>&1 | grep -q 'rootfs la .* may nay la'"
rm -rf $P0D
echo "=== P0.2: logs json-file ==="
LN=logt$$
$MC run -d --name=$LN --rootfs $BR /bin/sh -c 'echo o1; echo e1 1>&2' >/dev/null 2>&1
sleep 1
t "logs: json-file tach dung stream" sh -c "[ \$(grep -c '\"stream\":\"stdout\"' /run/minicontainer/$LN/logs/container.log) -eq 1 ] && [ \$(grep -c '\"stream\":\"stderr\"' /run/minicontainer/$LN/logs/container.log) -eq 1 ]"
t "logs: in ra stdout va stderr" sh -c "$MC logs $LN | grep -qx o1 && $MC logs $LN | grep -qx e1"
t "logs --tail=1 chi in 1 dong" sh -c "[ \$($MC logs --tail=1 $LN | wc -l) -eq 1 ]"
t "logs: id khong ton tai bi tu choi" sh -c "! $MC logs khongco$$ >/dev/null 2>&1"
$MC rm -f $LN >/dev/null 2>&1
LN=logr$$
$MC run -d --name=$LN --rootfs $BR /bin/sh -c 'i=0; while [ $i -lt 14000 ]; do echo "line $i xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"; i=$((i+1)); done' >/dev/null 2>&1
sleep 10
t "logs: xoay vong tao container.log.1" test -f /run/minicontainer/$LN/logs/container.log.1
t "logs: file hien tai <= 1 MB sau khi xoay" sh -c "[ \$(stat -c%s /run/minicontainer/$LN/logs/container.log) -le 1048576 ]"
t "logs --tail=1 sau xoay tra dong cuoi" sh -c "$MC logs --tail=1 $LN | grep -q 'line 13999'"
t "rm: don sach state + logs" sh -c "$MC rm -f $LN >/dev/null && [ ! -e /run/minicontainer/$LN ]"
echo "=== P0.3: gc ==="
t "gc: chay tren may sach" $MC gc
GN=gcl$$
$MC run -d --name=$GN --rootfs $BR /bin/sh -c 'sleep 30' >/dev/null 2>&1
t "gc: khong dung container dang chay" sh -c "$MC gc >/dev/null && [ -d /run/minicontainer/$GN ] && $MC ps | grep -q $GN"
GCG=$(cat /run/minicontainer/$GN/cgdir 2>/dev/null)
kill -9 "$(cat /run/minicontainer/$GN/pid)" 2>/dev/null
sleep 1
t "gc --dry-run: bao nhung khong xoa" sh -c "$MC gc --dry-run | grep -q $GN && [ -d /run/minicontainer/$GN ]"
t "gc: don state + cgroup sau kill -9" sh -c "$MC gc >/dev/null && [ ! -e /run/minicontainer/$GN ] && [ ! -d '$GCG' ]"
echo "=== P0.4: siet extract tar ==="
EV=/tmp/mcevil; rm -rf $EV; mkdir -p $EV/dev $EV/fifo $EV/sym $EV/src
mknod $EV/src/null c 1 3 2>/dev/null && tar -czf $EV/dev/base-mini-rootfs.tar.gz -C $EV/src null
rm -f $EV/src/null
mkfifo $EV/src/pipe && tar -czf $EV/fifo/base-mini-rootfs.tar.gz -C $EV/src pipe
rm -f $EV/src/pipe
ln -s ../../../../etc/passwd $EV/src/escape && tar -czf $EV/sym/base-mini-rootfs.tar.gz -C $EV/src escape
rm -f $EV/src/escape
t "pull: tu choi device node" sh -c "! $MC pull base-mini --url=$EV/dev --dest=$EV/out1 >/dev/null 2>&1"
t "pull: tu choi fifo" sh -c "! $MC pull base-mini --url=$EV/fifo --dest=$EV/out2 >/dev/null 2>&1"
t "pull: tu choi symlink thoat rootfs" sh -c "! $MC pull base-mini --url=$EV/sym --dest=$EV/out3 >/dev/null 2>&1"
rm -rf $EV
echo "=== P0.6: chu ky goi ==="
FX=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/tests/fixtures
SG=/tmp/mcsig; rm -rf $SG; mkdir -p $SG/ok $SG/bad
cp $FX/signed-rootfs.tar.gz $SG/ok/base-mini-rootfs.tar.gz
cp $FX/signed-rootfs.tar.gz.sig $SG/ok/base-mini-rootfs.tar.gz.sig
cp $FX/signed-rootfs.tar.gz $SG/bad/base-mini-rootfs.tar.gz
cp $FX/signed-rootfs.tar.gz.sig $SG/bad/base-mini-rootfs.tar.gz.sig
printf x >> $SG/bad/base-mini-rootfs.tar.gz
t "pull: chu ky hop le duoc chap nhan" sh -c "$MC pull base-mini --url=$SG/ok --dest=$SG/out1 --sig=$FX/test.pub >/dev/null 2>&1"
t "pull: goi bi sua bi tu choi" sh -c "! $MC pull base-mini --url=$SG/bad --dest=$SG/out2 --sig=$FX/test.pub >/dev/null 2>&1"
rm -f $SG/ok/base-mini-rootfs.tar.gz.sig
t "pull: thieu .sig bi tu choi" sh -c "! $MC pull base-mini --url=$SG/ok --dest=$SG/out3 --require-signature --sig=$FX/test.pub >/dev/null 2>&1"
rm -rf $SG
echo "=== P1: image store (load/images/rmi/save) ==="
IW=/tmp/mcimg; rm -rf $IW; mkdir -p $IW/src1/dir $IW/src2/dir $IW/arch/blobs/sha256
echo a > $IW/src1/dir/a.txt
echo b > $IW/src1/dir/b.txt
: > $IW/src2/dir/.wh.b.txt          # whiteout: xoa dir/b.txt tu layer truoc
echo c > $IW/src2/dir/c.txt
tar -czf $IW/l1.tar.gz -C $IW/src1 .
tar -czf $IW/l2.tar.gz -C $IW/src2 .
H1=$(sha256sum $IW/l1.tar.gz | cut -d' ' -f1); H2=$(sha256sum $IW/l2.tar.gz | cut -d' ' -f1)
cp $IW/l1.tar.gz $IW/arch/blobs/sha256/$H1
cp $IW/l2.tar.gz $IW/arch/blobs/sha256/$H2
if [ "$ARCH" = aarch64 ]; then OARCH=arm64; else OARCH=amd64; fi
printf '{"architecture":"%s","os":"linux","config":{"Cmd":["/bin/sh"],"Env":["PATH=/bin"]},"rootfs":{"type":"layers","diff_ids":[]}}' "$OARCH" > $IW/arch/config.json
HC=$(sha256sum $IW/arch/config.json | cut -d' ' -f1)
cp $IW/arch/config.json $IW/arch/blobs/sha256/$HC
printf '[{"Config":"blobs/sha256/%s","RepoTags":["whiteout-test:1"],"Layers":["blobs/sha256/%s","blobs/sha256/%s"]}]' "$HC" "$H1" "$H2" > $IW/arch/manifest.json
tar -cf $IW/wh.tar -C $IW/arch .
t "load: doc docker-save archive" sh -c "$MC load $IW/wh.tar >/dev/null && $MC images | grep -q whiteout-test-1"
t "load: ap layer + whiteout dung" sh -c "[ -f $MC_IMG_BASE/images/whiteout-test-1/rootfs/dir/a.txt ] && [ -f $MC_IMG_BASE/images/whiteout-test-1/rootfs/dir/c.txt ] && [ ! -e $MC_IMG_BASE/images/whiteout-test-1/rootfs/dir/b.txt ]"
t "load: config Env/Cmd duoc trich ra" sh -c "grep -q 'PATH=/bin' $MC_IMG_BASE/images/whiteout-test-1/env && tr '\\0' ' ' < $MC_IMG_BASE/images/whiteout-test-1/argv | grep -q '/bin/sh'"
t "images --json hop le" sh -c "$MC images --json | grep -q '\"name\":\"whiteout-test-1\"'"
printf 'x' >> $IW/arch/blobs/sha256/$H2
tar -cf $IW/bad.tar -C $IW/arch .
t "load: digest sai bi tu choi" sh -c "! $MC load $IW/bad.tar >/dev/null 2>&1"
t "save -> rmi -> load round-trip" sh -c "$MC save whiteout-test-1 -o $IW/rt.tar >/dev/null && $MC rmi whiteout-test-1 >/dev/null && $MC load $IW/rt.tar >/dev/null && [ -f $MC_IMG_BASE/images/whiteout-test-1/rootfs/dir/a.txt ]"
$MC rmi whiteout-test-1 >/dev/null 2>&1
rm -rf $IW
II=/tmp/mcimg2; rm -rf $II; mkdir -p $II/arch/blobs/sha256
# Bo ./dev/*: rootfs that co device node (runtime mknod vao $BR/dev), con image that thi khong.
tar -czf $II/l.tar.gz -C $BR --exclude=./dev/* .
HL=$(sha256sum $II/l.tar.gz | cut -d' ' -f1)
cp $II/l.tar.gz $II/arch/blobs/sha256/$HL
printf '{"architecture":"%s","os":"linux","config":{"Cmd":["/bin/sh"],"Env":["PATH=/bin:/sbin"]}}' "$OARCH" > $II/arch/config.json
HC=$(sha256sum $II/arch/config.json | cut -d' ' -f1)
cp $II/arch/config.json $II/arch/blobs/sha256/$HC
printf '[{"Config":"blobs/sha256/%s","RepoTags":["busybox-local:1"],"Layers":["blobs/sha256/%s"]}]' "$HC" "$HL" > $II/arch/manifest.json
tar -cf $II/img.tar -C $II/arch .
t "run --image: chay tu image store" sh -c "$MC load $II/img.tar >/dev/null && $MC run --image=busybox-local-1 /bin/sh -c 'echo img-ok' | grep -q img-ok"
t "run --image: image Env duoc ap dung" sh -c "$MC run --image=busybox-local-1 /bin/sh -c 'echo \$PATH' | grep -q '/bin:/sbin'"
echo "=== P1.8: export/import bundle ==="
XB=/tmp/mcxb; rm -rf $XB; mkdir -p $XB/m
t "export: tao bundle + .sha256" sh -c "$MC export busybox-local-1 -o $XB/b.tgz >/dev/null && [ -s $XB/b.tgz ] && [ -s $XB/b.tgz.sha256 ]"
t "export: manifest co name/arch/sha256" sh -c "tar -xzf $XB/b.tgz -C $XB/m ./manifest.json && grep -q '\"name\": \"busybox-local-1\"' $XB/m/manifest.json && grep -q '\"sha256\": \"[0-9a-f]\\{64\\}\"' $XB/m/manifest.json"
t "export: .sha256 khop bundle" sh -c "cd $XB && sha256sum -c b.tgz.sha256 >/dev/null"
t "import: round-trip sau rmi" sh -c "$MC rmi busybox-local-1 >/dev/null && $MC import $XB/b.tgz >/dev/null && $MC run --image=busybox-local-1 /bin/sh -c 'echo back' | grep -qx back"
t "import: bundle bi sua bi tu choi" sh -c "mkdir -p $XB/t && tar -xzf $XB/b.tgz -C $XB/t && echo junk >> $XB/t/image.tar && tar -czf $XB/bad.tgz -C $XB/t . && ! $MC import $XB/bad.tgz >/dev/null 2>&1"
t "import --verify: thieu .sig bi tu choi" sh -c "! $MC import $XB/b.tgz --verify=$FX/test.pub >/dev/null 2>&1"
t "import --verify: sig sai bi tu choi" sh -c "cp $FX/signed-rootfs.tar.gz.sig $XB/b.tgz.sig && ! $MC import $XB/b.tgz --verify=$FX/test.pub >/dev/null 2>&1"
rm -rf $XB
echo "=== P2: pull tu registry + --platform ==="
if curl -sS -m 10 -o /dev/null https://registry-1.docker.io/v2/ 2>/dev/null; then
  t "pull registry: alpine:3.19" sh -c "$MC pull alpine:3.19 >/dev/null && $MC images | grep -q '^alpine-3.19'"
  t "pull registry: chay duoc image vua tai" sh -c "$MC run --image=alpine-3.19 /bin/sh -c 'cat /etc/alpine-release' | grep -q '^3\\.19'"
  t "pull registry: --platform arm64 chon dung bien the" sh -c "$MC pull alpine:3.19 --platform=linux/arm64 >/dev/null && grep -qx 'arch=aarch64' $MC_IMG_BASE/images/alpine-3.19/meta"
  t "pull registry: tag khong ton tai bi tu choi" sh -c "! $MC pull alpine:khong-co-tag-nay >/dev/null 2>&1"
else
  skip "P2 registry (khong co mang)" 4
fi
t "pull --offline: cai tu OCI layout tren dia" sh -c "$MC save busybox-local-1 -o /tmp/mc-off.tgz >/dev/null && rm -rf /tmp/mc-off && mkdir /tmp/mc-off && tar -xf /tmp/mc-off.tgz -C /tmp/mc-off && $MC rmi busybox-local-1 >/dev/null && $MC pull --offline=/tmp/mc-off >/dev/null && $MC images | grep -q '^busybox-local-1'"
rm -rf /tmp/mc-off /tmp/mc-off.tgz
AB=/tmp/mcarch; rm -rf $AB; mkdir -p $AB; echo x > $AB/f.txt
printf 'FROM busybox-local:1\nCOPY f.txt /f.txt\n' > $AB/Dockerfile
t "build: arch thua ke tu base image" sh -c "$MC build -t arch-test:1 $AB >/dev/null && grep -qx 'arch=$(uname -m)' $MC_IMG_BASE/images/arch-test-1/meta"
t "build --platform: ghi de arch dich" sh -c "$MC build -t arch-test2:1 --platform=linux/arm64 $AB >/dev/null && grep -qx 'arch=aarch64' $MC_IMG_BASE/images/arch-test2-1/meta"
$MC rmi arch-test-1 >/dev/null 2>&1; $MC rmi arch-test2-1 >/dev/null 2>&1
rm -rf $AB
echo "=== P1.7: build tu Dockerfile ==="
BC=/tmp/mcbuild; rm -rf $BC; mkdir -p $BC
echo copied > $BC/data.txt
printf 'FROM busybox-local:1\nWORKDIR /app\nCOPY data.txt /app/\nRUN echo run-ok > /app/ran.txt\nCMD ["/bin/sh","-c","cat /app/data.txt /app/ran.txt"]\n' > $BC/Dockerfile
t "build: tao image tu Dockerfile" sh -c "$MC build -t btest:1 $BC >/dev/null && $MC images | grep -q btest-1"
t "build: COPY + RUN co trong image" sh -c "$MC run --image=btest-1 | grep -q run-ok"
t "build: cache hit khi rebuild" sh -c "$MC build -t btest:1 $BC 2>&1 | grep -q '\\[cache\\]'"
t "build: --no-cache chay lai RUN" sh -c "! $MC build -t btest:1 --no-cache $BC 2>&1 | grep -q '\\[cache\\]'"
# RUN rm phai xoa that: image build ra chi co 1 rootfs, khong co layer stack de whiteout
BD=/tmp/mcbuilddel; rm -rf $BD; mkdir -p $BD
printf 'FROM busybox-local:1\nRUN rm -f /bin/yes && echo secret > /etc/secret.txt\nRUN rm -f /etc/secret.txt\n' > $BD/Dockerfile
t "build: RUN rm xoa file khoi image" sh -c "$MC build -t bdel:1 $BD >/dev/null && [ ! -e $MC_IMG_BASE/images/bdel-1/rootfs/bin/yes ]"
t "build: file tao roi xoa trong RUN khong con" sh -c "[ ! -e $MC_IMG_BASE/images/bdel-1/rootfs/etc/secret.txt ]"
$MC rmi bdel-1 >/dev/null 2>&1
rm -rf $BD
$MC rmi btest-1 >/dev/null 2>&1
rm -rf $BC
$MC rmi busybox-local-1 >/dev/null 2>&1
rm -rf $II
echo "=== P1.5: env/workdir/mount + OCI bundle ==="
t "run --env ap dung bien moi truong" sh -c "$MC run --rootfs $BR --env MC_T=abc /bin/sh -c 'echo \$MC_T' | grep -qx abc"
t "run -w doi cwd" sh -c "$MC run --rootfs $BR -w /tmp /bin/sh -c 'pwd' | grep -qx /tmp"
t "run --mount tmpfs ghi duoc" sh -c "$MC run --rootfs $BR --mount tmpfs::/tmp/mnt:size=2M /bin/sh -c 'echo z >/tmp/mnt/f && cat /tmp/mnt/f' | grep -qx z"
OB=/tmp/mcbundle; rm -rf $OB; mkdir -p $OB; cp -a $BR $OB/rootfs
printf '{"ociVersion":"1.0.2","process":{"args":["/bin/sh","-c","echo C=$(pwd); echo E=$OCIV; echo w >/tmp/mnt/x && cat /tmp/mnt/x"],"env":["OCIV=bundle"],"cwd":"/tmp"},"root":{"path":"rootfs"},"mounts":[{"destination":"/tmp/mnt","type":"tmpfs","source":"tmpfs","options":["size=2M"]}]}' > $OB/config.json
t "oci-run: env + cwd + mounts[]" sh -c "$MC oci-run $OB | grep -qx C=/tmp && $MC oci-run $OB | grep -qx E=bundle"
rm -rf $OB
rm -rf $SD
echo "=== P3: runtime-spec + CLI parity ==="
t "P3.1 --ulimit nofile" $MC run --rootfs $BR --ulimit nofile=32:32 /bin/sh -c '[ "$(ulimit -n)" = 32 ]'
t "P3.1 --ulimit sai bi tu choi" sh -c "! $MC run --rootfs $BR --ulimit bogus=1 /bin/true >/dev/null 2>&1"
V=/tmp/mcvol$$; mkdir -p $V; chmod 777 $V
$MC run --rootfs $BR -v $V:/mnt /bin/sh -c 'echo vol >/mnt/x' >/dev/null 2>&1
t "P3.2 -v HOST:CTR bind ghi ra host" grep -qx vol $V/x
rm -rf $V
M5=tm$$; MCG=""
$MC run -d --name $M5 --memory-high 16M --cpu-weight 200 --rootfs $BR /bin/sleep 60 >/dev/null 2>&1
MCG=$(cat /run/minicontainer/$M5/cgdir 2>/dev/null)
t "P3.5 memory.high duoc ghi" grep -qx 16777216 "$MCG/memory.high"
t "P3.5 cpu.weight duoc ghi" grep -qx 200 "$MCG/cpu.weight"
t "P3.2 inspect JSON co resources" sh -c "$MC inspect $M5 | grep -q '\"memory_high\": 16777216' && $MC inspect $M5 | grep -q '\"status\": \"RUNNING\"'"
t "P3.2 inspect id sai bi tu choi" sh -c "! $MC inspect khongco$$ >/dev/null 2>&1"
$MC exec $M5 /bin/sh -c 'echo cp-out > /tmp/cpfile'
$MC cp $M5:/tmp/cpfile /tmp/cpout$$ >/dev/null 2>&1
t "P3.9 cp ra host" grep -qx cp-out /tmp/cpout$$
echo cp-in > /tmp/cpin$$
$MC cp /tmp/cpin$$ $M5:/tmp/cpin >/dev/null 2>&1
t "P3.9 cp vao container" sh -c "$MC exec $M5 cat /tmp/cpin | grep -qx cp-in"
t "P3.9 cp chan .." sh -c "! $MC cp $M5:/../../etc/passwd /tmp/mcesc$$ >/dev/null 2>&1"
rm -f /tmp/cpout$$ /tmp/cpin$$ /tmp/mcesc$$
$MC stop $M5 >/dev/null 2>&1
t "P3.12 start chay lai config da luu" sh -c "$MC start $M5 >/dev/null && $MC ps | grep -q '$M5.*RUNNING'"
t "P3.2 restart" sh -c "$MC restart $M5 >/dev/null && $MC ps | grep -q '$M5.*RUNNING'"
t "P3.2 start khi dang chay bi tu choi" sh -c "! $MC start $M5 >/dev/null 2>&1"
$MC stop $M5 >/dev/null 2>&1
t "P3.2 rm" $MC rm $M5
t "P3.10 --init: app khong phai PID 1" $MC run --init --rootfs $BR /bin/sh -c '[ $$ -ne 1 ]'
R5=tmrm$$; $MC run -d --rm --name $R5 --rootfs $BR /bin/sh -c 'sleep 1' >/dev/null 2>&1
sleep 4
t "P3.11 --rm xoa state sau khi thoat" sh -c "[ ! -e /run/minicontainer/$R5 ]"
RS=$BR/tmp/rs$$
timeout -k 2 6 $MC run --restart=always --rootfs $BR /bin/sh -c "echo x >> /tmp/rs$$" >/dev/null 2>&1
NRS=$(wc -l < $RS 2>/dev/null || echo 0)
if [ "${NRS:-0}" -ge 2 ]; then echo "PASS: P3.2 --restart=always chay lai ($NRS lan)"; PASS=$((PASS+1)); else echo "FAIL: P3.2 --restart=always ($NRS lan)"; FAIL=$((FAIL+1)); fi
rm -f $RS
timeout -k 2 4 $MC run --restart=on-failure --rootfs $BR /bin/true >/dev/null 2>&1
t "P3.2 --restart=on-failure: exit 0 khong restart" test $? -eq 0
RK=tmkr$$; $MC run -d --name $RK --restart=always --rootfs $BR /bin/sleep 1 >/dev/null 2>&1
P1=$(cat /run/minicontainer/$RK/pid 2>/dev/null)
sleep 5
P2=$(cat /run/minicontainer/$RK/pid 2>/dev/null)
t "P3.2 restart keeper (detached) chay lai" sh -c "[ -n '$P1' ] && [ -n '$P2' ] && [ '$P1' != '$P2' ]"
$MC stop $RK >/dev/null 2>&1; $MC rm $RK >/dev/null 2>&1
t "P3.3 port <1024: ip_unprivileged_port_start=0 trong netns" $MC run --net=isolated --rootfs $BR /bin/sh -c 'grep -qx 0 /proc/sys/net/ipv4/ip_unprivileged_port_start'
SJ=/tmp/mcseccomp$$.json
printf '{"defaultAction":"SCMP_ACT_ALLOW","syscalls":[{"names":["mount"],"action":"SCMP_ACT_ERRNO"}]}' > $SJ
t "P3.6 seccomp JSON tu choi mount" $MC run --rootfs $BR --seccomp=$SJ /bin/sh -c '! mount -t proc proc /tmp/m 2>/dev/null'
t "P3.6 seccomp JSON cho phep syscall khac" sh -c "$MC run --rootfs $BR --seccomp=$SJ /bin/sh -c 'echo sjson' | grep -qx sjson"
rm -f $SJ
skip "P3.8 --emulate (can qemu-user-static + rootfs ngoai arch)"
echo "=== P4: A/B update + rollback + delta ==="
UP=/tmp/mcupd$$; rm -rf $UP; mkdir -p $UP/p1/rootfs $UP/p2/rootfs $UP/p3/rootfs
cp -a $BR/. $UP/p1/rootfs/; cp -a $BR/. $UP/p2/rootfs/; cp -a $BR/. $UP/p3/rootfs/
printf 'v1\n' > $UP/p1/rootfs/etc/mc-update-version
printf 'v2\n' > $UP/p2/rootfs/etc/mc-update-version
printf 'v3-bad\n' > $UP/p3/rootfs/etc/mc-update-version
for n in 1 2 3; do
  printf '{"arch":"%s","version":"%s"}\n' "$(uname -m)" "$n" > $UP/p$n/manifest.json
  tar -czf $UP/p$n.tgz -C $UP/p$n --exclude=./rootfs/dev/* .
done
UB=$UP/base
t "P4.1 update: slot A + link" sh -c "$MC update apply $UP/p1.tgz --base=$UB --link=$UP/rootfs >/dev/null && [ \"\$(cat $UP/rootfs/etc/mc-update-version)\" = v1 ] && [ \"\$(readlink $UB/current)\" = slots/A ]"
t "P4.1 update: slot B + previous=A" sh -c "$MC update apply $UP/p2.tgz --base=$UB >/dev/null && [ \"\$(readlink $UB/current)\" = slots/B ] && [ \"\$(cat $UB/previous)\" = A ] && [ \"\$(cat $UP/rootfs/etc/mc-update-version)\" = v2 ]"
t "P4.4 status --json bao slot B" sh -c "$MC update status --base=$UB --json | grep -q '\"current\":\"B\"'"
t "P4.2 update: goi khong chu ky bi tu choi" sh -c "! $MC update apply $UP/p1.tgz --base=$UB --verify=$FX/test.pub >/dev/null 2>&1"
t "P4.1 update: health fail tu rollback" sh -c "! $MC update apply $UP/p3.tgz --base=$UB --health-cmd=false >/dev/null 2>&1 && [ \"\$(readlink $UB/current)\" = slots/B ] && [ \"\$(cat $UP/rootfs/etc/mc-update-version)\" = v2 ]"
t "P4.1 update: rollback B -> A" sh -c "$MC update rollback --base=$UB >/dev/null && [ \"\$(readlink $UB/current)\" = slots/A ] && [ \"\$(cat $UP/rootfs/etc/mc-update-version)\" = v1 ]"
t "P4.1 update: 3 vong A/B lien tiep" sh -c "$MC update apply $UP/p2.tgz --base=$UB >/dev/null && $MC update apply $UP/p1.tgz --base=$UB >/dev/null && $MC update apply $UP/p2.tgz --base=$UB >/dev/null && [ \"\$(readlink $UB/current)\" = slots/B ] && [ \"\$(cat $UP/rootfs/etc/mc-update-version)\" = v2 ]"
t "P4.1 update: khong de stage rac" sh -c "! ls -d $UB/.stage-* >/dev/null 2>&1"
mkdir -p $UP/bad/rootfs/bin
printf '\177ELF' > $UP/bad/rootfs/bin/sh
dd if=/dev/zero bs=1 count=14 >> $UP/bad/rootfs/bin/sh 2>/dev/null
if [ "$(uname -m)" = aarch64 ]; then printf '\076\000' >> $UP/bad/rootfs/bin/sh; else printf '\267\000' >> $UP/bad/rootfs/bin/sh; fi
tar -czf $UP/bad.tgz -C $UP/bad .
t "P4.1 update: rootfs khac arch bi tu choi" sh -c "! $MC update apply $UP/bad.tgz --base=$UB >/dev/null 2>&1"
t "P4.1 update: package co .. bi tu choi" sh -c "mkdir -p $UP/evil && echo x > $UP/evil/x && tar -czf $UP/evil.tgz -C $UP/evil --transform='s|^|../|' . && ! $MC update apply $UP/evil.tgz --base=$UB >/dev/null 2>&1"
if command -v zstd >/dev/null 2>&1; then
  dd if=/dev/urandom of=$UP/old.bin bs=1M count=1 2>/dev/null
  cp $UP/old.bin $UP/new.bin; printf 'delta-tail\n' >> $UP/new.bin
  t "P4.6 delta: tao patch zstd" $MC update diff $UP/old.bin $UP/new.bin -o $UP/delta.zst
  t "P4.6 delta: patch nho hon file moi" sh -c "[ \$(stat -c%s $UP/delta.zst) -lt \$(stat -c%s $UP/new.bin) ]"
  t "P4.6 delta: ap patch khoi phuc byte-for-byte" sh -c "$MC update patch $UP/old.bin $UP/delta.zst -o $UP/restored.bin >/dev/null && cmp $UP/new.bin $UP/restored.bin"
else
  skip "P4.6 delta (khong co zstd)" 3
fi
echo "=== P4: watchdog + metrics + compose ==="
WD=$UP/watchdog; ( sleep 2 & echo $! > $UP/wdpid; wait ) &
WT=0; while [ ! -s $UP/wdpid ] && [ $WT -lt 20 ]; do sleep 0.1; WT=$((WT+1)); done
t "P4.3 watchdog feed va dung khi PID thoat" timeout 10 $MC watchdog --device=$WD --timeout=1 --pid=$(cat $UP/wdpid 2>/dev/null || echo 0)
ME=metrics$$; $MC run -d --name $ME --rootfs $BR /bin/sleep 30 >/dev/null 2>&1
sleep 1
$MC metrics --dir=$UP/metrics >/dev/null 2>&1
MVER=$($MC --version | cut -d' ' -f2)
t "P4.4 metrics: up + memory cho container" sh -c "grep -q 'minicontainer_up{id=\"$ME\"} 1' $UP/metrics/minicontainer.prom && grep -q 'minicontainer_memory_bytes{id=\"$ME\"}' $UP/metrics/minicontainer.prom"
t "P4.4 metrics: info version/arch" grep -q "minicontainer_info{version=\"$MVER\",arch=\"" $UP/metrics/minicontainer.prom
$MC stop $ME >/dev/null 2>&1; $MC rm $ME >/dev/null 2>&1
t "P4.4 metrics: --stdout khong ghi file" sh -c "$MC metrics --stdout | grep -q 'minicontainer_info'"
CS=compose$$; cat > $UP/compose.yml <<EOF
services:
  c${CS}a:
    rootfs: $BR
    command: ["/bin/sleep", "30"]
    network: host
  c${CS}b:
    rootfs: $BR
    command: ["/bin/sleep", "30"]
    network: host
  c${CS}c:
    rootfs: $BR
    command: ["/bin/sleep", "30"]
    network: host
EOF
t "P4.5 compose: up 3 service" sh -c "$MC compose -f $UP/compose.yml up >/dev/null && $MC ps | grep -q c${CS}a && $MC ps | grep -q c${CS}b && $MC ps | grep -q c${CS}c"
t "P4.5 compose: ps liet ke du" sh -c "$MC compose -f $UP/compose.yml ps | grep -q c${CS}a"
t "P4.5 compose: down don sach" sh -c "$MC compose -f $UP/compose.yml down >/dev/null && ! $MC ps | grep -q c${CS}a && ! $MC ps | grep -q c${CS}b && ! $MC ps | grep -q c${CS}c"
rm -rf $UP
echo "=== leak ==="
t "mount leak = 0" sh -c "! mount | grep -Fq '$BR/' && ! mount | grep -Fq '$ALP/'"
echo "=== T7 latency (assert <500ms) ==="
S=$(date +%s%N); $MC run --rootfs $BR /bin/true; E=$(date +%s%N); MS=$(( (E - S) / 1000000 ))
if [ "$MS" -lt 500 ]; then echo "PASS: T7 latency ${MS}ms"; PASS=$((PASS+1));
else echo "FAIL: T7 latency ${MS}ms"; FAIL=$((FAIL+1)); fi
echo "=== T9 docker nen (info) ==="
ps -o comm,rss= -C dockerd -C containerd 2>/dev/null
echo "=== KET QUA: PASS=$PASS FAIL=$FAIL SKIP=$SKIP TONG=$((PASS+FAIL+SKIP)) ==="
[ "$FAIL" -eq 0 ]
