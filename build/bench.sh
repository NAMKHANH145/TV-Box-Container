#!/bin/bash
# Tầng 2: Performance benchmark
MC=minicontainer
BR=/opt/rootfs

echo "=== B1: Start latency (1000 iterations) ==="
T0=$(date +%s%N)
for i in $(seq 1 1000); do
    minicontainer run --rootfs $BR /bin/true >/dev/null 2>&1
done
T1=$(date +%s%N)
TOTAL_US=$(( (T1 - T0) / 1000 ))
echo "Total: ${TOTAL_US}us for 1000 runs"
echo "Average: $(( TOTAL_US / 1000 ))us per run"

echo "=== B2: p50/p99 Percentile Analysis ==="
for i in $(seq 1 1000); do
    T0=$(date +%s%N)
    minicontainer run --rootfs $BR /bin/true >/dev/null 2>&1
    T1=$(date +%s%N)
    echo $(( (T1 - T0) / 1000000 ))
done > /tmp/latency.txt
sort -n /tmp/latency.txt > /tmp/sorted.txt
N=$(wc -l < /tmp/latency.txt)
P50=$(sed -n "500p" /tmp/sorted.txt)
P95=$(sed -n "950p" /tmp/sorted.txt)
P99=$(sed -n "990p" /tmp/sorted.txt)
MIN=$(sed -n "1p" /tmp/sorted.txt)
MAX=$(sed -n "${N}p" /tmp/sorted.txt)
AVG=$(awk '{sum+=$1} END {printf("%d", sum/NR)}' /tmp/latency.txt)
echo "N=$N MIN=${MIN}ms AVG=${AVG}ms P50=${P50}ms P95=${P95}ms P99=${P99}ms MAX=${MAX}ms"

echo "=== B3: Idle memory footprint ==="
sync; sleep 2
RSS=$(ps -o rss= -C minicontainer 2>/dev/null | sort -rn | head -n 1)
echo "minicontainer RSS when idle: ${RSS:-0}KB"

rm -f /tmp/latency.txt /tmp/sorted.txt
echo "=== B4: IO throttle (5MB @ 1MB/s) ==="
time minicontainer run --io 1M --rootfs $BR /bin/dd if=/dev/zero of=/tmp/t bs=1M count=5 oflag=direct >/dev/null 2>&1
rm -f /opt/rootfs/tmp/t /tmp/t

echo "=== B5: Churn 100 cycles ==="
FAIL_COUNT=0
T0=$(date +%s%N)
for i in $(seq 1 100); do
    ID="churn_$i"
    $MC run -d --name "$ID" --rootfs $BR /bin/sleep 1 >/dev/null 2>&1 || FAIL_COUNT=$((FAIL_COUNT+1))
    $MC stop "$ID" >/dev/null 2>&1
    $MC rm "$ID" >/dev/null 2>&1
done
T1=$(date +%s%N)
TOTAL_US=$(( (T1 - T0) / 1000 ))
echo "Churn total: ${TOTAL_US}us for 100 cycles = $(( TOTAL_US / 100 ))us/cycle"
echo "Failed spawns: $FAIL_COUNT"
echo "Remaining cgroups: $(ls -d /sys/fs/cgroup/minicontainer-* 2>/dev/null | wc -l)"

echo "=== B6: OCI bundle ==="
mkdir -p /tmp/oci-test/rootfs
cp -a $BR/* /tmp/oci-test/rootfs 2>/dev/null
chmod 1777 /tmp/oci-test/rootfs/tmp 2>/dev/null
echo '{"ociVersion":"1.0.0","hostname":"perf-test","process":{"args":["/bin/echo","OCI_OK"]},"root":{"path":"rootfs"},"linux":{"resources":{"memory":{"limit":31457280}}}}' > /tmp/oci-test/config.json
$MC oci-run /tmp/oci-test
rm -rf /tmp/oci-test
echo "=== DONE ==="