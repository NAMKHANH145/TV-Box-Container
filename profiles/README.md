# Rootfs profiles cho minicontainer

Mỗi profile là một defconfig Buildroot cho TV Box **Amlogic S905W (aarch64, Cortex-A53)**.
Defconfig chỉ chứa gói tối thiểu; kết quả là `rootfs.tar.gz` giải nén thẳng vào `/opt/rootfs-<profile>` rồi chạy bằng `minicontainer`.

| Profile | Nội dung | libc/link | Dung lượng (giải nén / tar.gz) | Dùng cho |
|---|---|---|---|---|
| `base-mini` | BusyBox (sh, ls, ps, dd…) | musl **static** | ~1.3 MB / 0.6 MB | shell script, cron, sandbox |
| `web-proxy` | BusyBox + lighttpd | musl dynamic | 2.6 MB / 1.4 MB | admin UI, proxy tĩnh |
| `iot-gateway` | BusyBox + Mosquitto broker/clients + OpenSSL + SQLite + cURL | musl dynamic | 14.1 MB / 6.9 MB | MQTT gateway, thu thập dữ liệu |
| `python-edge` | BusyBox + Python 3 + pyserial + spidev | musl dynamic | 22.2 MB / 9.7 MB | đọc cảm biến UART/SPI |

> **Vì sao có static và dynamic?** `python3`, `lighttpd`, `mosquitto` trong Buildroot đều
> `depends on !BR2_STATIC_LIBS`, nên 3 profile này buộc phải dynamic. Chỉ `base-mini`
> giữ được static. Container chạy bằng `chroot` vào rootfs nên loader của musl nằm
> trong chính rootfs — không phụ thuộc libc của host.

## Yêu cầu

- Linux x86_64, ~15 GB trống (Buildroot tree + toolchain + output mỗi profile).
- Buildroot tree đặt tại `build/buildroot` (không commit vào repo — đã có trong `.gitignore`).
  ```bash
  cd build
  wget https://buildroot.org/downloads/buildroot-2026.11.tar.gz   # hoặc bản 2026.11-git đang dùng
  tar xf buildroot-2026.11.tar.gz && mv buildroot-2026.11 buildroot
  ```
- Lần build đầu tự build toolchain musl aarch64 (30–90 phút tuỳ máy). Các lần sau dùng ccache.

## Build

```bash
# từ thư mục gốc repo
./profiles/build.sh base-mini       # nhanh nhất, nên chạy đầu tiên
./profiles/build.sh web-proxy
./profiles/build.sh iot-gateway
./profiles/build.sh python-edge
```

Script chạy `make O=build/rootfs-<profile> BR2_DEFCONFIG=profiles/<profile>.defconfig defconfig`
rồi `make -j$(nproc)`. Output:

```
build/rootfs-<profile>/images/rootfs.tar.gz
```

Build lại một profile: xoá `build/rootfs-<profile>/` rồi chạy lại (hoặc `make -C build/buildroot O=... clean`).
Không cần xoá `build/buildroot` — toolchain và ccache dùng chung.

## Deploy lên box

```bash
BOX=<IP_BOX>          # đổi theo mạng của bạn
P=base-mini
ssh root@$BOX "mkdir -p /opt/rootfs-$P"
scp build/rootfs-$P/images/rootfs.tar.gz root@$BOX:/tmp/
ssh root@$BOX "tar xzf /tmp/rootfs.tar.gz -C /opt/rootfs-$P"
ssh root@$BOX "minicontainer run --rootfs /opt/rootfs-$P /bin/sh"
```

Kiểm tra nhanh sau khi giải nén:

```bash
ssh root@$BOX 'ls /opt/rootfs-'$P'/bin/busybox && minicontainer run --rootfs /opt/rootfs-'$P' /bin/sh -c "hostname"'
```

## Thêm profile mới

1. Copy một defconfig gần nhất, đổi `BR2_TARGET_GENERIC_HOSTNAME`.
2. Chỉ thêm gói cần thiết; chạy kiểm tra cấu hình trước khi build thật:
   ```bash
   make -C build/buildroot O=/tmp/cfg-new BR2_DEFCONFIG=$PWD/profiles/new.defconfig defconfig
   grep -E '^BR2_PACKAGE_' /tmp/cfg-new/.config
   ```
   Symbol gõ sai sẽ bị Buildroot bỏ qua im lặng — luôn grep lại `.config`.
3. Nếu gói `depends on !BR2_STATIC_LIBS` thì **không** đặt `BR2_STATIC_LIBS=y`.
4. Build, chạy `minicontainer run`, đo kích thước rồi cập nhật bảng trên.

## Đã kiểm tra

- 2026-10-29: cả 4 defconfig parse thành công với Buildroot `2026.11-git`. Các symbol
  `MOSQUITTO(_BROKER/_CLIENTS)`, `OPENSSL`, `SQLITE`, `LIBCURL(_CURL)`, `PYTHON3`,
  `PYTHON_SERIAL`, `PYTHON_SPIDEV`, `LIGHTTPD`, `STATIC_LIBS` đều giữ nguyên trong `.config`.
- `base-mini` đã build và chạy trên box (rootfs ~1.3 MB, toàn bộ `test.sh` PASS với rootfs này).
- `web-proxy` đã build: 2.6 MB giải nén (tar.gz 1.4 MB).
- `python-edge` đã build: 22.2 MB giải nén (tar.gz 9.7 MB).
- `iot-gateway` đã build: 14.1 MB giải nén (tar.gz 6.9 MB).
- Lưu ý disk: mỗi profile động cần ~7.5 GB output. Sau khi build xong chỉ giữ
  `images/rootfs.tar.gz` (đã lưu ở `build/rootfs-images/`, gitignore) rồi xoá
  `build/rootfs-<profile>/` — build lại được từ defconfig.
- Chạy thật trên box: `web-proxy` (lighttpd 1.4.85, HTTP 200 qua `--net=bridge`),
  `python-edge` (Python 3.14.8, `import serial, spidev`) và `iot-gateway`
  (Mosquitto 2.1.2: 1 broker + 1 publisher + 1 subscriber ở 3 container khác nhau,
  tin đi qua `mcbr0`; `curl`/`sqlite3` cũng có sẵn). Xem `../progress/VERIFICATION_LOG.md`.
  Server chỉ bind được port >= 1024 (mapped root không có `CAP_NET_BIND_SERVICE`),
  và `lo` được runtime bật tự động nên 127.0.0.1 dùng được trong container.
