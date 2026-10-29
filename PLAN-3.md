# PLAN-3.md — Đánh giá sản phẩm, điểm yếu toàn bộ & kế hoạch testing đầy đủ

Sản phẩm hiện tại: `minicontainer` 93KB static + rootfs Buildroot 1.3MB — 6 namespaces, 5 controllers cgroup v2,
seccomp 21 syscall, CLI `run/ps/stats/stop/rm/oci-run`, test.sh 21/21. Chạy trên TV Box S905W (4×A53, 1.8GB RAM).

## 1. Điểm yếu toàn bộ — phân loại CỐ HỮU vs KHẮC PHỤC ĐƯỢC

### 1.1. Cố hữu (by design — chấp nhận, có lập luận bảo vệ)

| # | Điểm yếu | Lập luận giữ nguyên |
|---|---|---|
| I1 | Không image/registry/layers như Docker | Triết lý rootfs-là-thư-mục; pull image cần HTTPS/JSON/tar = +MB binary. Đã bù 1 phần bằng `oci-run` đọc bundle chuẩn |
| I2 | Không mạng ảo (chỉ host/isolated) | NAT bridge tốn CPU A53, tăng latency gói tin; gateway nhúng cần host-net trực tiếp |
| I3 | Không daemon → không restart-policy, logs tập trung, `exec` vào container đang chạy | Việc của systemd unit bên ngoài; daemon thường trực vi phạm luận điểm 0MB nền |
| I4 | Seccomp deny-list (không phải allow-list kiểu gVisor) | Allow-list gãy app lạ; deny-list là cân bằng đúng cho tool dạy học |
| I5 | Single-arch (aarch64), rootfs chuẩn bị tay | Đồ án target 1 box cụ thể, không phải sản phẩm đa nền |
| I6 | Không `/sys` trong container (kernel cấm mount sysfs trong userns) | Hóa ra cách ly mạnh hơn Docker; app cần /sys thì không chạy — ghi rõ giới hạn |
| I7 | Không volumes/snapshots | Ngoài scope data management; bind-mount thủ công nếu cần (chưa có flag) |

### 1.2. Khắc phục được — xếp đợt (chi tiết §2)

| # | Điểm yếu | Mức | Cách khắc phục |
|---|---|---|---|
| F1 | **Ctrl-C foreground để lại con mồ côi + cgroup rác** (launcher chết, con về init, không ai dọn) | Cao | Handler SIGINT/SIGTERM: forward signal cho con, `waitpid`, dọn cgroup rồi mới thoát |
| F2 | **Không quota dung lượng đĩa** (`io.max` chỉ bóp tốc độ — container vẫn ghi đầy eMMC được) | Cao | Project quota ext4 (`quotactl`) hoặc kiểm tra `du` định kỳ; tối thiểu: flag `--disk` + enforce khi vượt |
| F3 | Thiếu validate đầu vào (`--cpu 9999`, rootfs không tồn tại → lỗi khó hiểu từ syscall sâu) | Cao | Validate sau parse: cpu 1–100, ram/pids/io > 0, rootfs có `/bin/sh`, báo lỗi 1 dòng rõ |
| F4 | `test.sh` chưa phủ `--cpu`/`--io` tự động, T5/reboot manual | Trung bình | Thêm T5-auto (đọc `cpu.stat` sau 3s, assert 10–30%), T-io-auto (dd 5MB@1M assert 4–8s), reboot semi-auto (poll + checklist) |
| F5 | `ps/stats` output chỉ cho người đọc | Trung bình | Thêm `--json` cho `ps/stats` để script/tool khác dùng |
| F6 | `stop` TERM-timeout cứng 3s | Thấp | Flag `--timeout`, mặc định giữ 3s |
| F7 | Không tài liệu phát hành (man/README/CHANGELOG, version `--version`) | Thấp | `VERSION` hardcode + `--version`, README build/deploy/test 1 trang, CHANGELOG theo phase |
| F8 | OCI subset im lặng bỏ qua mounts/hooks (bundle có mà không báo) | Trung bình | Warn ra stderr khi gặp key ngoài subset + document danh sách hỗ trợ |
| F9 | Tên cgroup/state không chống PID-reuse tuyệt đối sau reboot dài | Thấp | `/run` là tmpfs (mất khi reboot) nên thực tế an toàn; ghi nhận, không cần code |

## 2. Kế hoạch chi tiết (R-series, sau PLAN-2)

- **R1 (bắt buộc, ~1 buổi): F1 + F3 + F8** — signal forwarding, validate inputs, warn OCI ngoài-subset. Toàn bộ có test trong test.sh.
- **R2 (~1 buổi): F2 disk quota** — nghiên cứu `quotactl`/project quota trên ext4 eMMC; nếu kernel mod thiếu quota → fallback: watchdog `du` + kill (ghi rõ trong báo cáo là fallback).
- **R3 (~nửa buổi): F4 test mở rộng + F5 `--json`** — test.sh lên ~30 tests.
- **R4 (đóng gói phát hành): F7 + release tarball** (`minicontainer` + rootfs + test.sh + README), GUIDE.md cập nhật lệnh mới.

## 3. Quy trình testing đầy đủ (chuẩn sản phẩm)

### Tầng 1 — Regression (`test.sh`, mỗi lần sửa code)
- Chạy full suite, yêu cầu 21+/21+ (sau R3: ~30). Fail 1 = không deploy. Đã tự động.

### Tầng 2 — Performance benchmark (so sánh công bằng, cùng box)
| Metric | Cách đo | Mục tiêu |
|---|---|---|
| Start latency p50/p99 (100 lần) | `date +%s%N` quanh `run /bin/true` | p99 < 100ms (hiện ~60ms) |
| Idle footprint | `ps` + `/proc` sau reboot, không chạy gì | 0 process / 0MB (hiện đạt) |
| Boot vs Docker | `time docker run` vs `time minicontainer run` cùng việc | giữ tỉ lệ ~50–70x (hiện 69x) |
| Binary + rootfs size | `ls -la`, `du -sh` mỗi release | không tăng quá 10%/release không lý do |

### Tầng 3 — Stress & soak (độ bền)
- **Churn 500 vòng** `run -d → stop → rm` liên tiếp, assert không rác (state/cgroup/mount) và latency không trôi.
- **Fork-bomb sustained** 60s trong `--pids 16`: host load < 1.0, không OOM host.
- **Đĩa đầy**: container `--disk` (sau R2) ghi vượt → bị chặn, host còn ≥500MB, eMMC không lỗi.
- **Soak 24h**: 2 container detach (1 idle, 1 cron echo) — memory.current phẳng, không zombie, `ps` đúng.

### Tầng 4 — Security adversarial (mỗi release, checklist)
1. chroot-break classic → phải chặn (đã có test).
2. Đọc file host (`/etc/shadow`, `/HOST_MARKER`) từ trong → phải ENOENT.
3. `CapEff` trong container đầy nhưng `id` ngoài = 65534 + file tạo ra owned nobody.
4. Probe từng syscall trong deny-list → EPERM hết (viết vòng lặp C 21 cái, assert).
5. Mount/namespace escape: `nsenter --target <host-pid> -m` từ trong → phải từ chối.
6. Seccomp bypass qua `exec` binary suid trong rootfs → NO_NEW_PRIVS chặn (tạo file suid test).

### Tầng 5 — Compatibility
- Cả 2 rootfs (Buildroot + Alpine) pass regression.
- Bundle OCI mẫu của tool khác (chỉ dùng subset) chạy được.
- Sau mỗi reboot: test.sh pass + dich vụ box (ssh/docker/tailscale) lên.

### Release gate (đủ mới gọi là bản phát hành)
Regression xanh + benchmark không thụt lùi + adversarial pass hết + CHANGELOG ghi đủ + tarball tái tạo được từ `make` sạch.
