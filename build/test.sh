#!/bin/bash
# test.sh — chay FULL matrix tren box (root). Cach dung: bash test.sh
# Khong test reboot (lam tay). Thoat != 0 neu co FAIL.
MC=minicontainer
BR=/opt/rootfs
ALP=/opt/alpine
PASS=0; FAIL=0
t() { # t <ten> <lenh...>: chay lenh, PASS neu exit 0
  local name="$1"; shift
  if "$@" >/tmp/t.log 2>&1; then echo "PASS: $name"; PASS=$((PASS+1));
  else echo "FAIL: $name"; sed 's/^/      /' /tmp/t.log | head -n 3; FAIL=$((FAIL+1)); fi
}
echo "=== prerequisite ==="
for f in /usr/local/bin/minicontainer $BR/bin/busybox $ALP/bin/busybox $BR/bomb $BR/breakout; do
  [ -e "$f" ] || { echo "THIEU: $f"; exit 1; }
done
echo ok
cleanup() {
  for m in $(mount | grep -oE "/opt/rootfs[0-9]*"); do umount -l "$m" 2>/dev/null; done
  for m in /opt/rootfs /opt/alpine; do while umount -l "$m" 2>/dev/null; do sleep 0.1; done; done
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
t "T4 bomb bi kill" test $? -ne 0
t "T4 dmesg co OOM" sh -c "dmesg | grep -q 'Memory cgroup out of memory'"
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
if [ $TIO_MS -ge 2400 -a $TIO_MS -le 4500 ]; then
    echo "PASS: Tio-auto ${TIO_MS}ms (2.4-4.5s for 3MB @1MB/s)"; PASS=$((PASS+1))
else
    echo "FAIL: Tio-auto ${TIO_MS}ms (not 2.4-4.5s)"; FAIL=$((FAIL+1))
fi
rm -f /opt/rootfs/tmp/t /tmp/t 2>/dev/null
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
  echo "SKIP: bridge ping tests (kernel/thieu iproute2)"
else
  $MC run -d --name $B2 --net=bridge --rootfs $BR /bin/sh -c 'sleep 60' >/dev/null 2>&1
  IP1=$(cat /run/minicontainer/$B1/ip 2>/dev/null)
  IP2=$(cat /run/minicontainer/$B2/ip 2>/dev/null)
  t "bridge: ghi state/ip trong dai 10.88" sh -c "case '$IP1' in 10.88.*) case '$IP2' in 10.88.*) exit 0;; esac;; esac; exit 1"
  t "bridge: host ping duoc container" ping -c2 -W2 "$IP1"
  t "bridge: trong container co eth0 + route mac dinh" $MC exec $B1 /bin/sh -c 'grep -q "^eth0" /proc/net/route'
  t "bridge: 2 container ping nhau" $MC exec $B1 /bin/sh -c "ping -c2 -W2 $IP2"
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
t "ro: rootfs tren host khong bi nhiem ro" sh -c 'echo x > /opt/rootfs/tmp/hostprobe; rc=$?; c=$(cat /opt/rootfs/tmp/hostprobe); rm -f /opt/rootfs/tmp/hostprobe; [ $rc -eq 0 ] && [ "$c" = x ]'
echo "=== pull profile ==="
PD=/tmp/mcpull; ZEROS=0000000000000000000000000000000000000000000000000000000000000000
rm -rf $PD; mkdir -p $PD/src/bin $PD/out
printf '#!/bin/sh\necho ok\n' > $PD/src/bin/hello; chmod +x $PD/src/bin/hello
tar -czf $PD/base-mini-rootfs.tar.gz -C $PD/src .
sha256sum $PD/base-mini-rootfs.tar.gz | cut -d' ' -f1 > $PD/sha
t "pull: giai nen tu nguon dia phuong" sh -c "$MC pull base-mini --url=$PD --dest=$PD/out/hello | grep -q 'pull: xong'"
t "pull: giu duoc quyen thuc thi" test -x $PD/out/hello/bin/hello
t "pull: tu choi dest da ton tai" sh -c "! $MC pull base-mini --url=$PD --dest=$PD/out/hello >/dev/null 2>&1"
t "pull: sha256 dung duoc chap nhan" sh -c "$MC pull base-mini --url=$PD --dest=$PD/out/hello --sha256=$(cat $PD/sha) --force | grep -q 'sha256 khop'"
t "pull: sha256 sai bi tu choi" sh -c "! $MC pull base-mini --url=$PD --dest=$PD/out/bad --sha256=$ZEROS >/dev/null 2>&1"
mkdir -p $PD/evilsrc && echo bad > $PD/evilsrc/f
tar -czPf $PD/evil-abs-rootfs.tar.gz $PD/evilsrc >/dev/null 2>&1
t "pull: tu choi tar co duong dan tuyet doi" sh -c "! $MC pull evil-abs --url=$PD --dest=$PD/out/evil >/dev/null 2>&1"
t "pull: dest cua file bi tu choi khong bi tao" sh -c '[ ! -e '"$PD"'/out/evil ]'
t "pull: khong de lai file staging" sh -c '! ls -a '"$PD"'/out/ 2>/dev/null | grep -q mc-pull'
IMG=/root/mcpull
WPDEST=/opt/mc-pull-wp   # read-only can ext4, khong dung /tmp (tmpfs)
if [ -f $IMG/web-proxy-rootfs.tar.gz ]; then
  S=$(sha256sum $IMG/web-proxy-rootfs.tar.gz | cut -d' ' -f1)
  rm -rf $WPDEST
  t "pull: profile that (web-proxy)" sh -c "$MC pull web-proxy --url=$IMG --dest=$WPDEST --sha256=$S | grep -q 'sha256 khop'"
  t "pull: chay container tu profile vua pull" $MC run --rootfs $WPDEST /bin/sh -c '[ -c /dev/null ] && echo ok >/tmp/x'
  t "pull: profile chay duoc --read-only" $MC run --read-only --rootfs $WPDEST /bin/sh -c 'echo x >/tmp/y; ! echo z >/etc/wp 2>/dev/null; [ ! -e /etc/wp ]'
else
  echo "SKIP: pull profile that (thieu $IMG/web-proxy-rootfs.tar.gz)"
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
rm -rf $SD
echo "=== leak ==="
t "mount leak = 0" sh -c '! mount | grep -qE "/opt/(rootfs|alpine)/"'
echo "=== T7 latency (assert <500ms) ==="
S=$(date +%s%N); $MC run --rootfs $BR /bin/true; E=$(date +%s%N); MS=$(( (E - S) / 1000000 ))
if [ "$MS" -lt 500 ]; then echo "PASS: T7 latency ${MS}ms"; PASS=$((PASS+1));
else echo "FAIL: T7 latency ${MS}ms"; FAIL=$((FAIL+1)); fi
echo "=== T9 docker nen (info) ==="
ps -o comm,rss= -C dockerd -C containerd 2>/dev/null
echo "=== KET QUA: PASS=$PASS FAIL=$FAIL TONG=$((PASS+FAIL)) ==="
[ "$FAIL" -eq 0 ]