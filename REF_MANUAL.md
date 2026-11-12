# minicontainer — REF MANUAL trên laptop

Tài liệu này dành cho người muốn build, chạy, kiểm chứng và đo hiệu năng
`minicontainer` trên Linux x86_64.

## 1. Kết luận kiểm chứng hiện tại

Kết quả đã chạy trên laptop Linux có quyền root:

```text
Local integration matrix: PASS=78 FAIL=0 SKIP=6 TONG=84
SDK smoke test:            PASS=11 FAIL=0 TONG=11
Native x86_64 build:       0 compiler warnings
Root integration:           normal, isolated, bridge đều chạy được
Cleanup:                    không còn mount/veth/cgroup rác sau test
```

Kết luận chính: sản phẩm đã được chứng minh chạy ổn trên laptop trong phạm vi
78 test có thể kiểm chứng trực tiếp. Không gọi đây là 84/84 PASS trên laptop:
6 test được skip vì phụ thuộc kernel, storage hoặc image của môi trường.

Các skip local gồm OOM/dmesg, IO throttle theo thiết bị lưu trữ và profile
`web-proxy` không có trong fixture local. Trên TV Box, các bài đó vẫn phải
chạy theo chế độ bắt buộc của `test.sh`.

## 2. Điều kiện hệ thống

- Linux x86_64.
- `gcc`, `make`, `strip`, `bash`, `tar`, `sha256sum`.
- `/bin/busybox` để tạo rootfs fixture.
- `sudo` hoặc shell root.
- User namespace, mount namespace và cgroup v2.
- Quyền root để ghi `/sys/fs/cgroup`, mount filesystem và tạo veth/bridge.

```bash
uname -m
command -v gcc make strip bash tar sha256sum busybox
findmnt /sys/fs/cgroup
```

## 3. Build native x86_64

```bash
cd ~/TV-Box/build
make clean
make CC=gcc STRIP=strip
make tools CC=gcc STRIP=strip
file ./minicontainer
./minicontainer --version
./minicontainer --help
```

Bản native thường in:

```text
minicontainer 1.5.2 (x86_64, glibc, static)
```

Đừng dùng kích thước native static glibc để kết luận kích thước asset musl
cho TV Box.

## 4. Smoke test không cần container root

```bash
cd ~/TV-Box/build
bash smoke-sdk.sh
```

Kỳ vọng:

```text
PASS=11 FAIL=0 TONG=11
```

Smoke test kiểm tra `sdk init`, `sdk pack`, checksum, symlink an toàn, tar
round-trip và `pull` local.

## 5. Local integration matrix — cách khuyên dùng

Script portable tự tạo hai rootfs BusyBox tại `/var/tmp`, copy `bomb` và
`breakout`, rồi chạy ma trận test:

```bash
cd ~/TV-Box
sudo -S env MC_LOCAL_ROOT=/var/tmp/minicontainer-local \
  ./build/local-test.sh
```

Script sẽ:

1. Build binary native x86_64 và tools.
2. Tạo hai rootfs BusyBox local.
3. Chạy `build/test.sh` với `MC`, `BR`, `ALP` trỏ vào fixture.
4. Xóa fixture sau khi test xong.

Có thể chọn thư mục khác:

```bash
sudo -S env MC_LOCAL_ROOT=/tmp/mc-local ./build/local-test.sh
```

Có thể chạy với rootfs/binary riêng:

```bash
sudo -S env \
  MC=/absolute/path/minicontainer \
  BR=/absolute/path/rootfs-a \
  ALP=/absolute/path/rootfs-b \
  IMG=/absolute/path/images \
  WPDEST=/absolute/path/pulled \
  bash ./build/test.sh
```

Kết quả local hợp lệ hiện tại:

```text
=== KET QUA: PASS=78 FAIL=0 SKIP=6 TONG=84 ===
```

`FAIL` là lỗi thật. `SKIP` chỉ hợp lệ khi có lý do môi trường rõ ràng.

## 6. Chạy thủ công một container

```bash
cd ~/TV-Box/build
sudo -S ./minicontainer run \
  --rootfs=/var/tmp/minicontainer-local/rootfs \
  -- /bin/sh
```

Trong container:

```sh
echo "PID=$$"
hostname
ps
cat /proc/1/cmdline
exit
```

Dấu `--` phân tách option của runtime với command bên trong.

Không tương tác:

```bash
sudo -S ./minicontainer run \
  --rootfs=/var/tmp/minicontainer-local/rootfs \
  -- /bin/sh -c 'test "$$" -eq 1 && test -c /dev/null && echo RUN_OK'
```

Mạng:

```bash
BR=/var/tmp/minicontainer-local/rootfs
sudo -S ./minicontainer run --net=host --rootfs="$BR" -- /bin/sh
sudo -S ./minicontainer run --net=isolated --rootfs="$BR" -- /bin/sh
sudo -S ./minicontainer run --net=bridge --rootfs="$BR" -- /bin/sh
```

`bridge` cần root, iproute2, veth, bridge và backend iptables/nft phù hợp.

## 7. Resource limits và security smoke

```bash
sudo -S ./minicontainer run --cpu=20 --rootfs="$BR" -- /bin/sh -c \
  'while :; do :; done'

sudo -S ./minicontainer run --pids=16 --rootfs="$BR" -- /bin/sh -c \
  'for i in $(seq 1 50); do sleep 5 & done; wait'

sudo -S ./minicontainer run --disk=1M --rootfs="$BR" -- /bin/sh -c \
  'dd if=/dev/zero of=/tmp/file bs=1M count=4'

sudo -S ./minicontainer run --rootfs="$BR" -- /bin/sh -c \
  '! mount -t proc proc /tmp/m'
```

Theo dõi cgroup trong terminal khác:

```bash
sudo find /sys/fs/cgroup -maxdepth 1 -type d -name 'minicontainer-*'
sudo cat /sys/fs/cgroup/minicontainer-*/memory.current
sudo cat /sys/fs/cgroup/minicontainer-*/cpu.stat
sudo cat /sys/fs/cgroup/minicontainer-*/pids.current
```

## 8. Detached lifecycle

```bash
sudo -S ./minicontainer run -d --name demo \
  --rootfs="$BR" -- /bin/sleep 60
sudo ./minicontainer ps
sudo ./minicontainer stats demo
sudo ./minicontainer exec demo /bin/sh -c 'echo EXEC_OK'
sudo ./minicontainer stop demo
sudo ./minicontainer rm demo
sudo ./minicontainer ps
```

Sau đó kiểm tra không còn state/cgroup rác:

```bash
sudo find /run/minicontainer -maxdepth 2 -type f 2>/dev/null
sudo find /sys/fs/cgroup -maxdepth 1 -type d -name 'minicontainer-*'
```

## 9. Đo hiệu năng

`bench.sh` đã nhận biến `MC` và `BR`, nên chạy được cả rootfs TV Box lẫn
fixture laptop:

```bash
cd ~/TV-Box
sudo -S env \
  KEEP_ROOT=1 MC_LOCAL_ROOT=/var/tmp/minicontainer-local \
  ./build/local-test.sh
sudo -S env \
  MC="$PWD/build/minicontainer" \
  BR=/var/tmp/minicontainer-local/rootfs \
  bash ./build/bench.sh
```

Benchmark gồm:

- 1.000 lần startup `/bin/true`.
- min/avg/p50/p95/p99/max latency.
- RSS idle.
- IO throttle tham khảo.
- 100 chu kỳ detached → stop → rm.
- OCI bundle smoke test.

Khi báo cáo, ghi cùng lúc:

```text
commit/hash
CPU, RAM, kernel, filesystem, storage
compiler/toolchain, rootfs, số vòng, ngày đo
N, min, avg, p50, p95, p99, max
```

Không so sánh trực tiếp số IO laptop NVMe với TV Box eMMC. Không dùng benchmark
laptop để tuyên bố hiệu năng S905W.

## 10. Các test không thể suy ra từ laptop

- Kernel vendor và module riêng của S905W.
- OOM log/behavior đúng theo kernel box.
- IO throttle theo eMMC.
- Kích thước/hiệu năng asset aarch64 musl trên phần cứng thật.
- Độ ổn định nhiệt và soak dài hạn trên box.

Các phần này phải chạy thêm trên TV Box. Laptop kiểm chứng được logic runtime,
namespace, cgroup CPU/PIDs, filesystem, seccomp, bridge, lifecycle, SDK và
cleanup — nhưng không thay thế hardware validation.

## 11. Quy trình ghi kết quả

```bash
cd ~/TV-Box
uname -a
findmnt /sys/fs/cgroup
file build/minicontainer
git rev-parse --short HEAD
sudo -S env MC_LOCAL_ROOT=/var/tmp/minicontainer-local \
  ./build/local-test.sh 2>&1 | tee /tmp/minicontainer-local-test.log
```

Trong báo cáo cần ghi rõ:

1. Commit/hash đã test.
2. Kiến trúc và kernel.
3. Lệnh build.
4. `PASS/FAIL/SKIP`.
5. Danh sách skip và lý do.
6. Benchmark percentile và số vòng.
7. Có hay không có quyền root.

Không ghi `84/84 PASS` nếu kết quả thực tế là `78 PASS, 6 SKIP`.
