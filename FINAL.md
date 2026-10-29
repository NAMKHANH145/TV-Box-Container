# Final Checklist — v1.4.0 (2026-10-29)

## Đã implement + verify (TV Box S905W, kernel 6.18.38-meson64)
- `--read-only` (`src/ns.c`): bind rootfs lên chính nó trong mount ns riêng
  (`MS_REC|MS_PRIVATE` trên `/` trước để không lan propagation ra host), tmpfs
  cho `tmp`/`run`/`var/log`/`dev/shm`, rồi `MS_REMOUNT|MS_BIND|MS_RDONLY` trên
  `/` sau chroot + `/proc`. Verify: ghi `/etc` bị từ chối và file không tồn tại
  sau lệnh, `/tmp`+`/var/log` vẫn ghi được, host rootfs còn writable,
  `[ grep -c " /tmp tmpfs " ] = 1` (bắt bug mount đè symlink `/var/log -> ../tmp`)
- Behavior kernel 6.18: rootfs đặt trên tmpfs thì flip read-only trả **EPERM**
  (cả ext4 thì OK; đã bisect, host util-linux cũng vậy) → runtime in hướng dẫn
  đặt rootfs ở `/opt/rootfs`, `pull` cảnh báo bằng `statfs` TMPFS_MAGIC
- `pull <profile>` (`src/pull.c`): `execvp` argv literal (không có shell → không
  injection từ `MC_PULL_URL`), validate member tar (chặn path tuyệt đối + `..`),
  `statvfs` chặn thiếu dung lượng, staging + rename atomic, `--force` swap `.old`,
  `--sha256`, cảnh báo tmpfs. Verify: pull `web-proxy` thật 1.4 MB → 2.9 MB giải
  nén, `run` được, chạy được `--read-only`, giữ quyền +x, đủ 11 test case
- `exec <id> <cmd>` (`src/exec.c`): `setns` user→net/mnt→pid, `O_PATH`
  `/proc/<pid>/root`, `fork`, seccomp, attach cgroup. Verify: uid 0, PID≠1,
  `mount` bị từ chối, `/proc/self/cgroup` trùng với init, id sai bị chặn
- `--net=bridge` (`src/bridge.c`): `mcbr0` 10.88.0.1/16 + veth `mcv<pid>`/`mcc<pid>`;
  host dùng `ip`, container tự đặt `eth0`/IP/route bằng ioctl. Verify: host ping
  container 0% loss, **2 container ping nhau**, **3 container mosquitto
  publish/subscribe**, lighttpd bind 8080 trả **HTTP 200** cho client
  `python-edge` ở container khác, IP lưu `/run/minicontainer/<id>/ip`, veth tự
  mất khi container chết, `--net=host|isolated` không đổi hành vi
- `/dev` trong container (`ns.c`): tmpfs + bind device node từ host (mknod bị từ
  chối trong userns). Đây là lý do 3 profile động (rootfs `/dev` rỗng) trước đây
  chết ngay khi chạy `-d`
- Detached health check + R2-1 disk quota watchdog (giữ nguyên từ v1.2.0)
- `stop`/`rm`: cgroup.kill, rmdir retry, `.oldroot` + watchdog + marker + `ip` cleanup
- `test.sh`: **74/74 PASS** (gồm 8 case `--read-only`, 11 case `pull`)
- Release `v1.3.0` đã chạy thật: CI build 4 arch, tải asset, verify SHA256SUMS,
  cả 4 binary chạy được (QEMU), asset aarch64 pass toàn bộ suite trên box thật
- Binary: 136.712 B aarch64 musl static, 0 warning ở cả musl-cross và
  glibc `-static -O2 -Wall`
- Soak: 800 vòng/588s, 0 leak (cgroups/mounts/state), MemAvailable trong ngưỡng;
  smoke 12 vòng sau refactor `--read-only`. **24h chưa chạy** — xem dưới

## Remaining (không chặn release)
- **Soak 24h** — Khánh chạy riêng lúc khác: `ITER=117000 bash soak.sh` trên box
  (`HOUR=24` chỉ ra 72000 vòng ≈ 15h vì tốc độ đo được 1.36 vòng/giây)
- CI: thêm toolchain musl.cc + `actions/cache` để asset release nhẹ ~137 KB
  (hiện glibc static 623–932 KB); self-hosted runner để chạy `test.sh` trong CI
- mc-sdk: chưa implement `init`/`pack`
- C4: armv7l/riscv64/x86_64 mới verify bằng QEMU, chưa có phần cứng thật
- LICENSE: README còn ghi "MIT or Apache-2.0 (to be decided)" → chưa có file
- `--net=bridge` không NAT/DNS; port < 1024 không bind được trong container
  (mapped root thiếu `CAP_NET_BIND_SERVICE`)
- `--disk` là watchdog 1s, không phải quota kernel: burst ~1s vẫn lọt; single
  file bị RLIMIT_FSIZE chặn

## Bottom line
14 module, 2.209 dòng C, zero-daemon. Toàn bộ backlog R5 (health, exec, bridge,
`--read-only`, `pull`) đã xong và verify trên phần cứng; 4/4 profile chạy thật.
Phần còn lại là việc hạ tầng (soak 24h, cache toolchain CI, runner tự host,
mc-sdk) chứ không phải tính năng runtime.
