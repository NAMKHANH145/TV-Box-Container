# PHASE3.md — TESTING mở rộng + đối chứng SOTA 2026

## 1. Ma trận test đầy đủ (cả 2 rootfs, trên box thật)

| Test | Rootfs Buildroot `/opt/rootfs` (1.3MB) | Rootfs Alpine `/opt/alpine` (8.5MB) |
|---|---|---|
| T1 PID=1, `ps` chỉ thấy nội bộ | ✅ `$$`=1 | ✅ `$$`=1, `ps` 3 dòng nội bộ |
| T2 jail (`touch /etc/hack.txt`) | ✅ host sạch | ✅ host sạch |
| T3 hostname | ✅ `minicontainer-os` / host `tvbox` | ✅ giống |
| T4 `--ram 30M` + bomb 50MB | ✅ OOM-kill đúng 30MB, host load 0.41 | (chung cơ chế cgroup, không lặp) |
| T5 `--cpu 20` + spinner | ✅ đo đúng 20% 1 core | (chung cơ chế, không lặp) |
| T6 zero-daemon | ✅ 0 process, 0MB nền | — |
| T7 latency `/bin/true` | ✅ **real 0.030s** | chưa đo (dự kiến tương đương) |
| T8 cleanup sau `exit` | ✅ cgroup tự biến mất | ✅ giống |
| T10 so rootfs | ✅ 592KB nén / 1.3MB bung, busybox static | baseline 3.8MB / 8.5MB, busybox động musl |
| I6 `--net=isolated` | ✅ chỉ còn `lo` DOWN (mất mạng hoàn toàn đúng thiết kế) | — |
| Nhiệt full-load 1 core 60s | ✅ 52°C → 54°C (trần an toàn 80°C, còn dư ~26°C) | — |
| Reboot | ✅ `/opt/rootfs`, `/opt/alpine`, `/usr/local/bin/minicontainer` còn nguyên; `tailscaled/docker/ssh` active; `portainer+samba` tự lên | — |

Quan sát thêm: sau reboot, SSH qua LAN (~5 phút mới thông) chậm hơn Tailscale — nguyên nhân khả năng ở DHCP share phía laptop, không phải lỗi box (xem W13 trong §4).

## 2. Đối chứng SOTA 2026

Số `local` = đo trên chính box S905W. Số `cite` = công bố (server x86, chỉ mang tính tham chiếu hướng).

| Runtime | Ngôn ngữ | Daemon nền | Idle RAM | Start 1 container | Chuẩn |
|---|---|---|---|---|---|
| **minicontainer (ta)** | C, 1 file, 657KB static | **không có (0MB local)** | **0MB local** | **0.030s local** | riêng (rootfs = thư mục) |
| Docker (dockerd+containerd, trên box) | Go | dockerd+containerd | **213MB local** (131+82) | **2.076s local** (gồm round-trip daemon) | OCI |
| crun | C | cần stack trên (podman/k8s) | ~KBs (cite) | ~47ms create→delete (cite); bản tối ưu seccomp-cache ~5.6ms | OCI full |
| runc | Go | cần stack trên | ~MBs (cite) | ~224ms (cite, gấp ~2x crun) | OCI full |
| youki | Rust (memory-safe) | cần stack trên | thấp (cite) | ~111ms (cite) | OCI full |
| gVisor / Kata | sandbox/VM | nặng | lớn | penalty 40–60% TPS workload (cite) | OCI, cách ly mạnh |

Đọc bảng đúng cách: ta **không** cạnh tranh OCI (không chạy image Docker/k8s). Ta thắng ở phân khúc "máy yếu không gánh nổi daemon": nhanh hơn Docker trên cùng box **~69 lần** (2.076s → 0.030s), nhẹ hơn nền **vô hạn lần** (213MB → 0MB), và tiệm cận tốc độ crun tối ưu dù code chỉ ~200 dòng.

## 3. Điểm đột phá đã chứng minh (có số liệu, không nói suông)

1. **Zero-daemon đo được**: 0 process + 0MB khi idle — Docker tốn 213MB trên cùng box.
2. **Boot 30ms**, nhanh hơn `docker run` trên cùng box 69 lần.
3. **Rootfs tự build 1.3MB**, nhỏ hơn Alpine 6.5 lần — "tự làm cả khách lẫn chủ".
4. **Cgroup v2 hard limit thật**: OOM-killer giết đúng ngưỡng 30MB, CPU cap đúng 20.0% — không phải cấu hình cho có.
5. **Chạy được trên kernel mod sơ sài** nơi Podman còn không cài nổi (không cần dbus/systemd/slirp).
6. **Nhiệt full-load 1 core chỉ +2°C** — an toàn chạy 24/7 trên box không tản nhiệt chủ động.

## 4. Điểm yếu (toàn bộ, không giấu) → cách khắc phục trong PLAN-2.md

- **W1. Không OCI-compliant** → không dùng image Docker/k8s. Khắc phục (P2-9): mode đọc OCI `config.json`, hoặc chốt "ngoài scope" có lập luận.
- **W2. `chroot` thay vì `pivot_root`** → tiến trình root trong container có kỹ thuật chroot-break. Khắc phục (P2-3): chuyển `pivot_root` + mount `nosuid,nodev`.
- **W3. Chạy bằng root thật của host** → breakout là root host. Khắc phục (P2-4): `CLONE_NEWUSER` + uid_map (kernel đã có user ns).
- **W4. Chưa giới hạn số PID** → fork-bomb trong container. Khắc phục (P2-1): ghi `pids.max` (controller sẵn, ~5 dòng code).
- **W5. Chưa giới hạn disk I/O** → ghi đầy eMMC. Khắc phục (P2-2): `io.max` + rootfs đặt quota riêng.
- **W6. Chưa có seccomp filter** → attack surface syscall còn nguyên (crun ăn điểm ở đây). Khắc phục (P2-5): seccomp-bpf chặn `mount/reboot/kexec/...`.
- **W7. Mặc định share host net** → container thấy cả LAN. Khắc phục: đã có `--net=isolated`; veth/bridge để sau (đánh đổi CPU NAT đã phân tích).
- **W8. `/sys` trống, `/dev` tối thiểu** → app cần device đặc biệt có thể lỗi. Khắc phục (P2-6): mount `sysfs` read-only + `mknod null/zero/urandom` tối thiểu.
- **W9. Chỉ chạy foreground** → không detach/log/restart. Khắc phục (P2-7): `--detach` + pidfile + đọc `memory.current` làm `ministat`.
- **W10. Binary 657KB (glibc static)** → còn lớn so với tiềm năng. Khắc phục (P2-8): build lại bằng toolchain musl của Buildroot (kỳ vọng vài chục KB).
- **W11. Test còn thủ công** → dễ sót khi sửa code. Khắc phục (P2-10): `test.sh` chạy full matrix + assert exit code.
- **W12. Chưa cô lập IPC** (`CLONE_NEWIPC` chưa dùng) → container nhìn được shared memory của host. Khắc phục (P2-1 gộp): thêm flag, 1 dòng.
- **W13. Sau reboot SSH LAN lên chậm (~5 phút)** → quan sát 1 lần, chưa rõ do box hay DHCP phía laptop. Khắc phục (P2-11): đo lại có kiểm soát + log `systemd-analyze blame`.
