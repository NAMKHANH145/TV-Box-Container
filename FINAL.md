# Final Checklist — v1.2.0 (2026-10-29)

## Đã implement + verify (TV Box S905W, kernel 6.18.38-meson64)
- Detached health check: `run -d --health-cmd` dùng chung watchdog với `--disk`
  (1 process/container, tự thoát), chạy lệnh mỗi 5s, kill cgroup khi fail,
  ghi marker `health_failed`; container khỏe không bị giết oan
- R2-1 disk quota watchdog: foreground (0 extra process) + detached
- `stop`/`rm`: cgroup.kill, rmdir retry, `.oldroot` + watchdog + marker cleanup
- `ps --json` state rỗng = `[]`; parse số liệu chặt; `--net=bridge` báo lỗi rõ
- `--version` theo arch/libc; seccomp dùng `__NR_*` + AUDIT_ARCH theo kiến trúc
- `test.sh`: **39/39 PASS**; latency 65ms; CPU 20%; IO 3.09s; mount leak 0
- 4 rootfs profile (`base-mini`, `iot-gateway`, `python-edge`, `web-proxy`):
  defconfig validate với Buildroot 2026.11-git, giữ đúng package
- Binary: 116,232 B aarch64 musl static; bản trước giữ tại
  `/usr/local/bin/minicontainer.prev.bak`

## Remaining (không chặn release)
- R5-1: GitHub Actions workflow 4 arch có, **chưa chạy thật** trên CI
- R5-2: 4 defconfig đã validate; mới `base-mini` build + chạy thật, 3 profile
  dynamic cần build (B1 trong `TASKS.md`)
- R5-3: `--net=bridge` (netlink/veth) và `exec` chưa làm; health check vẫn chạy
  trên host, không phải trong container
- T3: 24h soak chưa chạy
- `--disk` là watchdog 1s, không phải quota kernel: burst ~1s vẫn lọt; single
  file bị RLIMIT_FSIZE chặn

## Bottom line
Core + disk quota + detached health production-ready trên hardware thật. Các
mục còn lại là hạ tầng/engineering (CI, build profile, bridge, soak).
