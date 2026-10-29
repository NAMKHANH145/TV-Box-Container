#!/bin/bash
# soak.sh — chay lap lai Nhieu vong container, kiem leak sau moi vong.
# Cach dung (tren box, root):  bash soak.sh            # 100 vong
#                             ITER=1000 bash soak.sh   # dai hon
#                             HOUR=24 bash soak.sh     # chay theo gio (chan ITER)
MC=${MC:-minicontainer}
BR=${BR:-/opt/rootfs}
ITER=${ITER:-100}
if [ -n "${HOUR:-}" ]; then ITER=$(( HOUR * 3000 )); fi  # ~3 vong/giay -> ~3000/gio

count_cgroups() { ls -d /sys/fs/cgroup/minicontainer-* 2>/dev/null | wc -l; }
count_mounts()  { mount 2>/dev/null | grep -cE "$BR/"; }
count_state()   { ls -d /run/minicontainer/* 2>/dev/null | wc -l; }
mem_avail()     { awk '/MemAvailable/{print $2}' /proc/meminfo; }

LEAK=0; ERR=0
MEM0=$(mem_avail)
# bridge can iproute2 + module veth; khong co thi bo qua vong bridge
NB=$(cat /proc/sys/kernel/random/uuid 2>/dev/null | cut -c1-8)
if $MC run -d --name probe$NB --net=bridge --rootfs $BR /bin/sh -c 'sleep 5' >/dev/null 2>&1; then
  $MC stop probe$NB >/dev/null 2>&1; $MC rm probe$NB >/dev/null 2>&1; HAVE_BRIDGE=1
else
  HAVE_BRIDGE=0; echo "soak: khong co bridge -> vong bridge se bi bo qua"
fi
echo "soak: ITER=$ITER  start cgroups=$(count_cgroups) mounts=$(count_mounts) state=$(count_state) MemAvailable=${MEM0}kB"

start=$(date +%s)
i=0
while [ $i -lt $ITER ]; do
  i=$((i+1))
  case $(( i % 5 )) in
    0) $MC run --rootfs $BR /bin/true || ERR=$((ERR+1)) ;;
    1) $MC run --ram=64M --cpu=20 --rootfs $BR /bin/sh -c 'echo hi >/dev/null' || ERR=$((ERR+1)) ;;
    2) $MC run --net=isolated --rootfs $BR /bin/sh -c 'cat /proc/net/route >/dev/null' || ERR=$((ERR+1)) ;;
    3) $MC run --pids=64 --disk=50M --rootfs $BR /bin/sh -c 'true' || ERR=$((ERR+1)) ;;
    4)
      [ $HAVE_BRIDGE = 1 ] || continue
      N=s$$
      $MC run -d --name $N --net=bridge --rootfs $BR /bin/sh -c 'sleep 30' >/dev/null 2>&1 || { ERR=$((ERR+1)); continue; }
      $MC exec $N /bin/sh -c 'test "$(id -u)" -eq 0' >/dev/null 2>&1 || ERR=$((ERR+1))
      [ -f /run/minicontainer/$N/ip ] || ERR=$((ERR+1))
      $MC stop $N >/dev/null 2>&1 || ERR=$((ERR+1))
      $MC rm $N >/dev/null 2>&1 || ERR=$((ERR+1))
      ;;
  esac
  if [ $(( i % 20 )) -eq 0 ]; then
    sleep 1   # de watchdog/cgroup cua vong truoc tu don het
    C=$(count_cgroups); M=$(count_mounts); S=$(count_state)
    if [ "$C" != 0 ] || [ "$M" != 0 ] || [ "$S" != 0 ]; then
      echo "LEAK tai vong $i: cgroups=$C mounts=$M state=$S"; LEAK=$((LEAK+1))
    fi
    [ $(( i % 100 )) -eq 0 ] && echo "  $i/$ITER  elapsed=$(( $(date +%s) - start ))s  MemAvailable=$(mem_avail)kB  err=$ERR leak=$LEAK"
  fi
done

MEM1=$(mem_avail)
DROP=$(( (MEM0 - MEM1) / 1024 ))
echo "soak xong: $i vong, elapsed=$(( $(date +%s) - start ))s, loi=$ERR, lan-leak=$LEAK"
echo "  cuoi: cgroups=$(count_cgroups) mounts=$(count_mounts) state=$(count_state) MemAvailable=${MEM1}kB (giam ${DROP}kB)"
# MemAvailable bien dong theo chuong trinh khac -> bao dong 64MB rieng cho leak
[ $DROP -gt 65536 ] && { echo " NGHI: memory giam qua 64MB sau $i vong"; LEAK=$((LEAK+1)); }
[ $ERR -eq 0 ] && [ $LEAK -eq 0 ] || exit 1
echo "SOAK OK"
