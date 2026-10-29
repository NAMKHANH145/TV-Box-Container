# Final Checklist — v1.3.0 (2026-10-29)

## Đã implement + verify (TV Box S905W, kernel 6.18.38-meson64)
- `exec <id> <cmd>` (`src/exec.c`): `setns` user→net/mnt→pid, `O_PATH`
  `/proc/<pid>/root`, `fork`, seccomp, attach cgroup. Verify: uid 0, PID≠1,
  `mount` bị từ chối, `/proc/self/cgroup` trùng với init, id sai bị chặn
- `--net=bridge` (`src/bridge.c`): `mcbr0` 10.88.0.1/16 + veth `mcv<pid>`/`mcc<pid>`;
  host dùng `ip`, container tự đặt `eth0`/IP/route bằng ioctl. Verify: host ping
  container 0% loss, **2 container ping nhau**, IP lưu `/run/minicontainer/<id>/ip`,
  veth tự mất khi container chết, `--net=host|isolated` không đổi hành vi
- `/dev` trong container (`ns.c`): tmpfs + bind device node từ host (mknod bị từ
  chối trong userns). Đây là lý do 3 profile động (rootfs `/dev` rỗng) trước đây
  chết ngay khi chạy `-d`
- End-to-end thật: lighttpd (`web-proxy`) bind 8080 trên bridge trả **HTTP 200**
  cho client `python-edge` chạy ở container khác (528 KB RSS, 1 pid)
- Detached health check + R2-1 disk quota watchdog (giữ nguyên từ v1.2.0)
- `stop`/`rm`: cgroup.kill, rmdir retry, `.oldroot` + watchdog + marker + `ip` cleanup
- Soak smoke `ITER=200`: 146s, 0 lỗi, 0 lần báo leak, MemAvailable +3 MB
- 4 rootfs profile đã build: `base-mini` 1.3 MB, `web-proxy` 2.6 MB,
  `iot-gateway` 14.1 MB, `python-edge` 22.2 MB (giải nén)
- `release.sh`: version đọc từ `src/main.c`, kèm `SHA256SUMS.txt`; CI gắn checksum
  vào release theo tag
- `test.sh`: **54/54 PASS**; latency <500ms assert; CPU 20%; IO 3.09s; mount leak 0
- Binary: 124,424 B aarch64 musl static, build 0 warning

## Remaining (không chặn release)
- R5-1: release job theo tag `v1.3.0` cần chạy thật một lần để xác nhận artifact
  + SHA256SUMS; cache toolchain CI chưa thêm
- R5-2: `iot-gateway` mới build xong, chưa chạy mosquitto publish/subscribe trên box
- T3: mới soak 200 vòng; 24h (`HOUR=24 bash soak.sh`) chưa chạy
- C4: armv7l/riscv64 mới build trong CI, chưa chạy trên phần cứng/QEMU
- `--net=bridge` không NAT/DNS; port < 1024 không bind được trong container
  (mapped root thiếu `CAP_NET_BIND_SERVICE`)
- `--disk` là watchdog 1s, không phải quota kernel: burst ~1s vẫn lọt; single
  file bị RLIMIT_FSIZE chặn

## Bottom line
13 module, 1.779 dòng C, zero-daemon. Toàn bộ backlog R5-3 (health, exec,
bridge) đã xong và verify trên phần cứng; 3/4 profile chạy thật. Phần còn lại
là việc hạ tầng (tag release, soak 24h, xác nhận arch khác).
