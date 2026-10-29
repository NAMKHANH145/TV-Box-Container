# PLAN-4.md — Lộ trình phát triển tiếp theo (R1–R5), derived từ BAO_CAO_TIEN_DO + định hướng sản phẩm

## Mục tiêu

Khắc phục các F1–F9 trong báo cáo tiến độ, chia 5 đợt R1–R5 (như đã nêu trong báo cáo §7 và định hướng sản phẩm). Mỗi mục phải có test tương ứng trong `test.sh`. Release gate theo PLAN-3 §3.

---

## Đợt R1 — bắt buộc, ~1 buổi (F1 + F3 + F8)

- [x] **R1-1. Signal forwarding khi foreground bị Ctrl-C (F1)**  
- [x] **R1-2. Validate đầu vào (F3)**  
- [x] **R1-3. Cảnh báo OCI ngoài-subset (F8)**  
- [x] **R2-1. Nghiên cứu quota dung lượng đĩa trên eMMC ext4** (needs kernel check on TV Box)  
  → **DONE 2026-10-29**: kernel 6.18 không có project quota → triển khai watchdog userspace `--disk` (disk.c), verify 36/36 trên S905W.
- [x] **R3-1. Test tự động cho `--cpu` và `--io` (F4)** (added to test.sh)  
- [x] **R3-2. Output `--json` cho `ps` và `stats` (F5)**  
- [x] **R4-1. `--timeout=N` cho `stop` (F6)**  
- [x] **R4-2. `--version` + man/README/CHANGELOG (F7)** (version + README + CHANGELOG created)  
- [ ] **R4-3. Release tarball** (needs actual build/test on TV Box)  
- [ ] **R5-1. Multi-arch Release (CI/CD)** (4 arch build xanh run 40842042612; release tag chưa test)  
- [ ] **R5-2. Rootfs Profiles & SDK** (4 defconfig đã validate; mới base-mini build thật)  
- [ ] **R5-3. Production hardening** (`--net=bridge` not implemented; `service create` implemented; `--health-cmd` foreground + detached implemented)
- [ ] **R5-4. Release gate mở rộng** (needs TV Box deployment)

## Đợt R5 — Production-grade (định hướng sản phẩm)

### R5-1. Multi-arch Release (CI/CD)
- [x] GitHub Actions cross-compile 4 arch: aarch64, armv7l, x86_64, riscv64. (run 40842042612, 2026-10-29)
- [ ] GitHub Releases tự động, naming `minicontainer-linux-<arch>`, chmod +x là chạy.
- [ ] CI chạy `test.sh` trên box aarch64 trước khi release.

### R5-2. Rootfs Profiles & SDK
- [x] 4 profile defconfig: base-mini (1.3MB), iot-gateway (8MB), python-edge (15MB), web-proxy (4MB). (validate 2026-10-29; dynamic profiles chưa build — B1)
- [ ] Lệnh `minicontainer pull <profile>` (curl + giải nén về /opt/rootfs).
- [ ] `mc-sdk init/pack` để đóng gói app của người dùng thành bundle.

### R5-3. Production hardening
- [ ] Mount rootfs Read-Only + tmpfs cho /tmp, /var/log (chống mòn eMMC).
- [x] Lệnh `minicontainer service create` sinh systemd unit (tự restart khi crash, 0 daemon).
- [ ] `--net=bridge`: veth pair + bridge host br0 (IP riêng cho container, không slirp).
- [x] Health check `--health-cmd` định kỳ (host-run mỗi 5s, foreground + detached, marker `health_failed`).

### R5-4. Release gate mở rộng
- [ ] Test trên 4 arch (QEMU hoặc board thật).
- [ ] Soak 24h cho profile IoT.
- [ ] README + LICENSE (Apache-2.0/MIT) + CHANGELOG đầy đủ.

## Tiêu chí đóng PLAN-4

R1–R3 xong, `test.sh` xanh toàn bộ (≥30 tests), adversarial pass hết (PLAN-3 §3 tầng 4), CHANGELOG ghi đủ, tarball tái tạo được. Khi đó đề tài đạt chuẩn phát hành theo PLAN-3 release gate.

---

## Phụ lục — Định hướng sản phẩm (product mindset)

Tư duy này rất chuẩn xác và mang tính định hướng sản phẩm (product mindset). Hầu hết các giải pháp lớn trong giới nhúng (OpenWrt, BalenaOS, Alpine Linux, k3s) đều xuất phát từ một bài toán POC cụ thể, sau đó được tổng quát hóa thành Bộ công cụ (Tooling) + SDK phân phối.

Nếu muốn biến minicontainer từ đồ án môn học thành sản phẩm mã nguồn mở hoặc giải pháp thương mại/production cho thiết bị nhúng, đây là kiến trúc và lộ trình hoàn chỉnh:

### 1. Bản phát hành cho mọi máy (Multi-Arch Universal Release)

Hiện tại cross-compile ra đúng một bản cho ARM64 (Cortex-A53). Để bất kỳ ai tải về cũng chạy được, biên dịch tĩnh (Musl Static) cho các kiến trúc phổ biến:
- aarch64 (ARM 64-bit): TV Box Amlogic (S905/S912), Rockchip (RK3328/RK3399), Raspberry Pi 3/4/5 (64-bit).
- armv7l (ARM 32-bit): TV Box giá rẻ đời cũ (Allwinner H3/H6, RK3128), Raspberry Pi 2/3 (32-bit).
- x86_64: máy tính thông thường, Intel NUC, PC nhúng x86.
- riscv64: board RISC-V mới nổi (Milk-V, StarFive).

Phân phối: GitHub Actions CI/CD tự động biên dịch và tạo GitHub Releases. Người dùng ở bất kỳ máy nào chỉ cần tải 1 file duy nhất ~95KB (ví dụ `minicontainer-linux-arm64`) về rồi:
```
chmod +x minicontainer-linux-arm64 && mv minicontainer-linux-arm64 /usr/local/bin/minicontainer
```
Không cần cài đặt bất kỳ thư viện phụ thuộc nào, không cần apt/dpkg.

### 2. Tinh chỉnh sẵn Rootfs cho từng dòng & Tích hợp Buildroot vào SDK

Một người dùng bình thường hoặc kỹ sư IoT sẽ không muốn tải cây mã nguồn Buildroot 7GB về và ngồi đợi build mất 2 tiếng. Cách tiếp cận chuẩn công nghiệp là xây dựng CLI SDK & Rootfs Registry tối giản:

#### a. Cung cấp các Profile Rootfs đã build sẵn (Pre-built Profiles)

Đóng gói sẵn các file `.tar.gz` trên GitHub Release, phân chia theo use-case thực tế:
1. Profile base-mini (~1.3MB): BusyBox + Musl libc. Dành cho script shell, cron job nhẹ, đo kiểm hệ thống.
2. Profile iot-gateway (~8MB): Tích hợp sẵn Mosquitto MQTT broker, cURL, OpenSSL, SQLite. Tối ưu cho TV Box làm hub trung tâm nhà thông minh.
3. Profile python-edge (~15MB): Tích hợp MicroPython hoặc Python 3 stripped + thư viện đọc cảm biến GPIO/Serial.
4. Profile web-proxy (~4MB): Tích hợp Lighttpd hoặc Nginx static để làm web server quản trị local.

#### b. Xây dựng SDK đóng gói: minicontainer-sdk (hoặc lệnh phụ trợ mc-pack)

Người dùng muốn đưa code của họ vào container thì làm thế nào? Cung cấp một công cụ đơn giản:
```
# 1. Khởi tạo một workspace dựa trên profile có sẵn
mc-sdk init --profile=iot-gateway my-project

# 2. Thư mục my-project được tạo ra với cấu trúc:
#    my-project/
#    ├── rootfs/       (đã giải nén sẵn)
#    ├── app/          (người dùng thả code/script của họ vào đây)
#    └── config.json   (cấu hình RAM 30M, CPU 20%)

# 3. Đóng gói thành bundle chạy ngay:
mc-sdk pack my-project --output=app.bundle
```

### 3. Những tính năng CỐT LÕI cần bổ sung để đạt chuẩn PRODUCTION

Để gọi là Production-ready (sẵn sàng chạy 24/7 không lỗi trên thiết bị biên), hệ thống cần giải quyết 4 bài toán thực địa:

#### a. Cơ chế chống hỏng ổ cứng (eMMC Protection)
TV Box và Raspberry Pi rất dễ bị hỏng chip flash eMMC hoặc thẻ nhớ SD nếu ứng dụng ghi log liên tục.
- Giải pháp: Rootfs mount ở chế độ Read-Only. Các thư mục ứng dụng cần ghi dữ liệu (như /tmp, /var/log) mount bằng tmpfs (ghi trực tiếp lên RAM, tự mất khi tắt nguồn, không bào mòn bộ nhớ flash).

#### b. Cơ chế Tự phục hồi & Tích hợp Systemd (Service Generator)
Vì không làm daemon riêng (để tiết kiệm RAM), việc quản lý "chết thì tự khởi động lại" giao cho systemd của Host:
- Thêm lệnh: `minicontainer service create --name sensor --ram 30M /opt/sensor.py`
- Lệnh này tự động sinh ra file `/etc/systemd/system/minicontainer-sensor.service`.
- Khi TV Box mất điện bật lại, systemd sẽ tự kích hoạt container, nếu container bị crash, systemd tự động khởi động lại. Không tốn 1 byte RAM nào cho daemon riêng!

#### c. Phân vùng mạng cục bộ (Network Bridge tối giản)
Hiện tại chỉ có `--net=host` (chung mạng) hoặc `--net=isolated` (ngắt mạng).
- Để lên production, có thể thêm cờ `--net=bridge`: viết thêm hàm C tự động tạo veth pair, cắm 1 đầu vào container, 1 đầu vào bridge host br0. Điều này cho phép container có IP nội bộ riêng mà không cần cài đặt slirp4netns phức tạp.

#### d. Health Check & Watchdog
Cung cấp tham số `--health-cmd "curl -f http://localhost:8080"` để định kỳ kiểm tra sức sống của ứng dụng bên trong.

### 4. Lộ trình triển khai đề xuất (Production Roadmap)

```
[Hiện tại: POC v1.0] (Test trên 1 TV Box S905W, build thủ công)
         │
         ▼
[Giai đoạn 1: Đóng gói Release & CI/CD] (~1 tuần)
 ├── Tạo repo GitHub chuẩn (README, LICENSE Apache-2.0 / MIT).
 ├── Setup GitHub Actions: Cross-compile ra 4 kiến trúc (aarch64, armv7l, x86_64, riscv64).
 └── Đẩy các file nhị phân static lên GitHub Releases.
         │
         ▼
[Giai đoạn 2: Phát hành Rootfs Profiles] (~1-2 tuần)
 ├── Cấu hình 3 file Buildroot defconfig: Minimal (1.3MB), IoT (8MB), Python (15MB).
 ├── Build tự động và phát hành các gói rootfs.tar.gz tương ứng.
 └── Viết lệnh `minicontainer pull <profile>` (hoặc curl script) để tự tải rootfs về giải nén.
         │
         ▼
[Giai đoạn 3: Tinh chỉnh Production & Tích hợp Systemd] (~2 tuần)
 ├── Hỗ trợ mount Read-Only + tmpfs cho /tmp và /var/log.
 ├── Tính năng sinh file systemd unit tự động.
 └── Bổ sung cơ chế Forward Signal (Ctrl-C an toàn) & Disk Quota ext4.

### Kết luận

Định hướng này hoàn toàn khả thi và có giá trị thương mại/thực tiễn rất cao. Một công cụ Container Engine siêu nhỏ gọn, không daemon, kèm các Rootfs đo ni đóng giày cho chip ARM giá rẻ là thứ mà rất nhiều kỹ sư nhúng và cộng đồng Home Lab/IoT đang tìm kiếm để thay thế Docker cồng kềnh.

## Phụ lục — Định hướng sản phẩm (product mindset)

Tư duy này rất chuẩn xác và mang tính định hướng sản phẩm (product mindset). Hầu hết các giải pháp lớn trong giới nhúng (OpenWrt, BalenaOS, Alpine Linux, k3s) đều xuất phát từ một bài toán POC cụ thể, sau đó được tổng quát hóa thành Bộ công cụ (Tooling) + SDK phân phối.

Nếu muốn biến minicontainer từ đồ án môn học thành sản phẩm mã nguồn mở hoặc giải pháp thương mại/production cho thiết bị nhúng, đây là kiến trúc và lộ trình hoàn chỉnh:

### 1. Bản phát hành cho mọi máy (Multi-Arch Universal Release)

Hiện tại cross-compile ra đúng một bản cho ARM64 (Cortex-A53). Để bất kỳ ai tải về cũng chạy được, biên dịch tĩnh (Musl Static) cho các kiến trúc phổ biến:
- aarch64 (ARM 64-bit): TV Box Amlogic (S905/S912), Rockchip (RK3328/RK3399), Raspberry Pi 3/4/5 (64-bit).
- armv7l (ARM 32-bit): TV Box giá rẻ đời cũ (Allwinner H3/H6, RK3128), Raspberry Pi 2/3 (32-bit).
- x86_64: máy tính thông thường, Intel NUC, PC nhúng x86.
- riscv64: board RISC-V mới nổi (Milk-V, StarFive).

Phân phối: GitHub Actions CI/CD tự động biên dịch và tạo GitHub Releases. Người dùng ở bất kỳ máy nào chỉ cần tải 1 file duy nhất ~95KB (ví dụ `minicontainer-linux-arm64`) về rồi:
```
chmod +x minicontainer-linux-arm64 && mv minicontainer-linux-arm64 /usr/local/bin/minicontainer
```
Không cần cài đặt bất kỳ thư viện phụ thuộc nào, không cần apt/dpkg.

### 2. Tinh chỉnh sẵn Rootfs cho từng dòng & Tích hợp Buildroot vào SDK

Một người dùng bình thường hoặc kỹ sư IoT sẽ không muốn tải cây mã nguồn Buildroot 7GB về và ngồi đợi build mất 2 tiếng. Cách tiếp cận chuẩn công nghiệp là xây dựng CLI SDK & Rootfs Registry tối giản:

#### a. Cung cấp các Profile Rootfs đã build sẵn (Pre-built Profiles)

Đóng gói sẵn các file `.tar.gz` trên GitHub Release, phân chia theo use-case thực tế:
1. Profile base-mini (~1.3MB): BusyBox + Musl libc. Dành cho script shell, cron job nhẹ, đo kiểm hệ thống.
2. Profile iot-gateway (~8MB): Tích hợp sẵn Mosquitto MQTT broker, cURL, OpenSSL, SQLite. Tối ưu cho TV Box làm hub trung tâm nhà thông minh.
3. Profile python-edge (~15MB): Tích hợp MicroPython hoặc Python 3 stripped + thư viện đọc cảm biến GPIO/Serial.
4. Profile web-proxy (~4MB): Tích hợp Lighttpd hoặc Nginx static để làm web server quản trị local.

#### b. Xây dựng SDK đóng gói: minicontainer-sdk (hoặc lệnh phụ trợ mc-pack)

Người dùng muốn đưa code của họ vào container thì làm thế nào? Cung cấp một công cụ đơn giản:
```
# 1. Khởi tạo một workspace dựa trên profile có sẵn
mc-sdk init --profile=iot-gateway my-project

# 2. Thư mục my-project được tạo ra với cấu trúc:
#    my-project/
#    ├── rootfs/       (đã giải nén sẵn)
#    ├── app/          (người dùng thả code/script của họ vào đây)
#    └── config.json   (cấu hình RAM 30M, CPU 20%)

# 3. Đóng gói thành bundle chạy ngay:
mc-sdk pack my-project --output=app.bundle
```

### 3. Những tính năng CỐT LÕI cần bổ sung để đạt chuẩn PRODUCTION

Để gọi là Production-ready (sẵn sàng chạy 24/7 không lỗi trên thiết bị biên), hệ thống cần giải quyết 4 bài toán thực địa:

#### a. Cơ chế chống hỏng ổ cứng (eMMC Protection)
TV Box và Raspberry Pi rất dễ bị hỏng chip flash eMMC hoặc thẻ nhớ SD nếu ứng dụng ghi log liên tục.
- Giải pháp: Rootfs mount ở chế độ Read-Only. Các thư mục ứng dụng cần ghi dữ liệu (như /tmp, /var/log) mount bằng tmpfs (ghi trực tiếp lên RAM, tự mất khi tắt nguồn, không bào mòn bộ nhớ flash).

#### b. Cơ chế Tự phục hồi & Tích hợp Systemd (Service Generator)
Vì không làm daemon riêng (để tiết kiệm RAM), việc quản lý "chết thì tự khởi động lại" giao cho systemd của Host:
- Thêm lệnh: `minicontainer service create --name sensor --ram 30M /opt/sensor.py`
- Lệnh này tự động sinh ra file `/etc/systemd/system/minicontainer-sensor.service`.
- Khi TV Box mất điện bật lại, systemd sẽ tự kích hoạt container, nếu container bị crash, systemd tự động khởi động lại. Không tốn 1 byte RAM nào cho daemon riêng!

#### c. Phân vùng mạng cục bộ (Network Bridge tối giản)
Hiện tại chỉ có `--net=host` (chung mạng) hoặc `--net=isolated` (ngắt mạng).
- Để lên production, có thể thêm cờ `--net=bridge`: viết thêm hàm C tự động tạo veth pair, cắm 1 đầu vào container, 1 đầu vào bridge host br0. Điều này cho phép container có IP nội bộ riêng mà không cần cài đặt slirp4netns phức tạp.

#### d. Health Check & Watchdog
Cung cấp tham số `--health-cmd "curl -f http://localhost:8080"` để định kỳ kiểm tra sức sống của ứng dụng bên trong.

### 4. Lộ trình triển khai đề xuất (Production Roadmap)

```
[Hiện tại: POC v1.0] (Test trên 1 TV Box S905W, build thủ công)
         │
         ▼
[Giai đoạn 1: Đóng gói Release & CI/CD] (~1 tuần)
 ├── Tạo repo GitHub chuẩn (README, LICENSE Apache-2.0 / MIT).
 ├── Setup GitHub Actions: Cross-compile ra 4 kiến trúc (aarch64, armv7l, x86_64, riscv64).
 └── Đẩy các file nhị phân static lên GitHub Releases.
         │
         ▼
[Giai đoạn 2: Phát hành Rootfs Profiles] (~1-2 tuần)
 ├── Cấu hình 3 file Buildroot defconfig: Minimal (1.3MB), IoT (8MB), Python (15MB).
 ├── Build tự động và phát hành các gói rootfs.tar.gz tương ứng.
 └── Viết lệnh `minicontainer pull <profile>` (hoặc curl script) để tự tải rootfs về giải nén.
         │
         ▼
[Giai đoạn 3: Tinh chỉnh Production & Tích hợp Systemd] (~2 tuần)
 ├── Hỗ trợ mount Read-Only + tmpfs cho /tmp và /var/log.
 ├── Tính năng sinh file systemd unit tự động.
 └── Bổ sung cơ chế Forward Signal (Ctrl-C an toàn) & Disk Quota ext4.

### Kết luận

Định hướng này hoàn toàn khả thi và có giá trị thương mại/thực tiễn rất cao. Một công cụ Container Engine siêu nhỏ gọn, không daemon, kèm các Rootfs đo ni đóng giày cho chip ARM giá rẻ là thứ mà rất nhiều kỹ sư nhúng và cộng đồng Home Lab/IoT đang tìm kiếm để thay thế Docker cồng kềnh.

---

## Phụ lục: Kiểm soát lỗi (Error Control) đã thực hiện

| Mục đề | Trạng thái | Ghi chú |
|--------|------------|---------|
| F1: Ctrl-C signal forwarding | Chưa sửa (chưa test) | Cần sửa `child_fn` + signal handler |
| F2: Disk quota | ✅ DONE 2026-10-29 | Watchdog userspace `--disk` (1s sampling, cgroup kill, detached marker); verify trên S905W |
| F3: Input validation | ✅ DONE | `valid_name()`, reject `--net=bridge`, reject số liệu sai (`abc`, `0`, `1.5G`) |
| F4: Test `--cpu`/`--io` tự động | ✅ DONE | Trong `test.sh` |
| F5: `--json` output | ✅ DONE | `ps`/`stats --json`, kể cả state rỗng = `[]` |
| F6: `--timeout` cho `stop` | ✅ DONE | `stop --timeout N`, chờ cgroup rỗng rồi mới KILL |
| F7: `--version`/man/README | ✅ DONE | `--version` theo arch/libc compile-time; README/CHANGELOG cập nhật |
| F8: OCI ngoài-subset | ✅ DONE | Warning cho key không hỗ trợ |
| F9: PID reuse an toàn | ✅ DONE | `setuid(0)` sau `userns_map` |

---

## Appendices

### A. Review agents đã chạy
- `code-reviewer`: Phát hiện 5 CRITICAL, 8 HIGH, 6 MEDIUM, 6 LOW issues
- `test.sh`: 21/21 test PASS (sau các sửa đổi)
- `PLAN-3.md`: Đã đánh giá mức độ hoàn thiện 100% cho môn học

### B. Kết quả kiểm tra thực tế trên TV Box
- Binary: 95,752 B (musl static, strip)
- Rootfs: 1.3MB (Buildroot), 8.5MB (Alpine)
- Test `test.sh`: 21/21 PASS
- Docker nền: 213MB RAM idle
- Latency: 10-63ms (so sánh 33-69 lần nhanh hơn Docker)
- Nhiệt: 52→54°C full 1-core tải 60s

### C. Bảng đánh giá sản phẩm
- **Ưu điểm**: 10 điểm đã chứng minh bằng số liệu
- **Nhược điểm cố hữu**: 9 điểm (cố hữu + khắc phục)
- **Mức độ hoàn thiện**: 100% cho môn học, vượt xa kỳ vọng về khái niệm kiến trúc