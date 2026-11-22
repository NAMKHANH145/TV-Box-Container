# Bộ demo minicontainer — 3 phiên bản (22/11/2026)

| Demo | Cái gì được chứng minh | entry point | Kết quả log |
|---|---|---|---|
| **v1** (folder này) | Ship **1 bundle đa kiến trúc**: weather.py (Open-Meteo) box→laptop, repoinfo.py (GitHub API) laptop→box; runtime tự chọn biến thể x86_64/aarch64; phản diện: bundle chỉ x86 trên box → `Exec format error` | `bash demo-cross-ship.sh` | `results/` |
| **v2** (`demo-ver2/`) | `pip install` bình thường: pip tự chọn wheel theo CPU. Gửi nhầm → laptop giả lập chậm ~6×, box `Exec format error`; build tại chỗ → chạy | `bash demo-ver2/demo-ver2.sh` | `demo-ver2/results-normal/` |
| **v3** (`demo-v3/`) | HAL dịch phần cứng: cùng 1 Python, tự chọn backend `amlogic` (`ttyAML0`) / `pc_x86` (`ttyS*`); ép sai → `WRONG_BACKEND`. Dùng rootfs Buildroot `python-edge` | `bash demo-v3/demo-v3.sh` | `demo-v3/results-v3/` |

Chạy demo nào cũng cần:
```bash
cd demo-cross-ship && ./build-local.sh    # 1 lần: build binary aarch64 + x86_64 từ source
BOX=<IP_BOX> SSHPASS_FILE=<file chứa mật khẩu SSH> bash <script>
```
Không IP/mật khẩu nào nằm trong file. Số liệu cả 3 demo đã ghi vào `progress/VERIFICATION_LOG.md`.
Không chạy `build-local.sh` thì mỗi script tự tải binary từ release GitHub `v2.0.0`.

Mỗi bước in 4 dòng trước khi chạy: `MÁY` (đang thao tác ở máy nào), `VIỆC` (làm gì),
`GÕ` (đúng lệnh người dùng gõ), `THẤY` (kết quả mong đợi) — sau đó mới là output thật.

---

# Demo v1 — ship container 2 chiều: TV Box ⇄ Laptop bằng minicontainer

Folder này chứa **toàn bộ** tài liệu, mã nguồn, script và log thật của phiên demo chạy ngày
**22/11/2026** trên TV Box S905W (aarch64) và laptop x86_64.

```
demo-cross-ship/
├── demo-cross-ship.sh          # script duy nhất chạy lại TOÀN BỘ 2 chiều
├── build-local.sh              # build binary aarch64 + x86_64 từ ../build/src
├── minicontainer-linux-*       # binary do build-local.sh sinh ra (không commit)
├── Dockerfile.laptop           # ảnh harness ubuntu:24.04 cho phía laptop
├── app-box/                    # chương trình lấy thời tiết (chạy trên box)
│   ├── Dockerfile
│   ├── weather.py
│   └── resolv.conf
├── app-laptop/                 # chương trình lấy thông tin repo GitHub (chạy trên laptop)
│   ├── Dockerfile
│   ├── repoinfo.py
│   └── resolv.conf
└── results/                    # log thật từng bước + 2 bundle .tar.gz
```

---

## 1. Cách chạy demo

Không có IP hay mật khẩu nào nằm trong file. Trước khi chạy:

```bash
export BOX=<IP_BOX>
export SSHPASS_FILE=<đường_dẫn_file_chứa_mật_khẩu_SSH>   # chmod 600
cd demo-cross-ship
./build-local.sh          # 1 lần: binary 2.0.1-dev cho cả 2 arch
bash demo-cross-ship.sh
```

Biến tuỳ chọn: `SKIP_CLEANUP=1` (giữ lại image/container cũ), `RELEASE_VER=2.0.0`.

Script tự dọn rác đầu vào, dùng binary build từ source nếu có (thiếu thì tải release GitHub),
đóng gói source thành 1 file `.tar.gz` (có sha256) rồi mới gửi sang box, setup, build, chạy,
đóng gói, scp, import, xác nhận, rồi ghi log từng bước vào `results/`.

Laptop không cần sudo: script dùng vùng chứa `ubuntu:24.04 --privileged` làm "máy laptop"
(đúng cách CI của dự án chạy `local-test.sh`).

---

## 2. Tiền xử lý — dọn rác trên TV Box

Đã xoá trên box (giữ lại `test.sh`, `soak.sh`, `bench.sh` và các script hệ thống `aml-*.sh`):

- Log/script/tarball demo cũ: `mc-test*.log`, `box-*.sh`, `dod*.sh`, `ship-*.sh`,
  `final-*.log`, `task-svc-*.tar.gz`, `tasksvc/`, `recipes/`, `tests/`, binary v1.5.x.
- Tất cả container còn tồn tại (STOPPED) và **toàn bộ image cũ** (18 image, gồm cả 2 image
  x86_64 để lại từ demo trước) → kho image về `0`.
- Không có cgroup `minicontainer-*`, không có `mcbr0`/veth, không có rule NAT `10.88` sót lại.

---

## 3. Chiều A — TV Box → Laptop (chương trình lấy thời tiết)

Nguồn dữ liệu: **Open-Meteo API** (không cần API key), Hà Nội
`21.03°N, 105.85°E`. Chương trình `weather.py` dùng `urllib` của Python chuẩn,
in ra JSON gồm nhiệt độ, độ ẩm, mã thời tiết, thời điểm quan sát và thông tin
`arch / kernel / hostname / pid` đọc từ bên trong container.

| Bước | Lệnh chính | Kết quả đo được |
|---|---|---|
| **A1** | `build-local.sh` → scp `minicontainer-linux-aarch64` lên box (hoặc `curl` release) + `sha256sum` | sha256 `ef0016ff…f9972` (aarch64 musl tĩnh, 288.304 B, `2.0.1-dev`) |
| **A2** | `install -m755 … /usr/local/bin/minicontainer` + `minicontainer check` | `minicontainer 2.0.1-dev (aarch64, musl, static)`; 11/11 dòng `ok` (kernel `6.18.38-meson64`, cgroup v2, userns, xattr, iptables, curl, RAM 1804 MB, disk 7198 MB); 1 cảnh báo overlayfs (không dùng) |
| **A3** | `pull --platform=linux/arm64 python:3.12-alpine` → `build --platform=linux/arm64 -t weather-arm:1` → `run -d --net=bridge` | Build xanh; log: `{"program":"weather.py","temperature_2m_C":20.0,"relative_humidity_2m_pct":93,"observed_at":"2026-11-23T00:15","ran_on":{"arch":"aarch64","kernel":"6.18.38-meson64","pid":1}}` |
| **A4** | `pull --platform=linux/amd64 python:3.12-alpine` → `build --platform=linux/amd64 -t weather-x86:1` → `export --arch=x86_64 weather-arm-1 weather-x86-1 -o weather-multi.tar.gz` | **1 bundle 34 MB** chứa `aarch64.tar` + `x86_64.tar` + `multiarch.json`, sha256 `bf3bb9b0…4416` |
| **A5** | `scp` bundle từ box về laptop | sha256 trên laptop **khớp** `bf3bb9b0…4416` |
| **A6** | laptop `import weather-multi.tar.gz` → `run -d ... --image=weather-arm-1 ...` → `logs` | Runtime in `chon bien the x86_64 (may nay x86_64)`, chạy native **x86_64 / kernel 6.8.0-124-generic**. Log: cùng `temperature_2m_C:20.0`, humidity 93, `program:weather.py`, `pid:1` |

→ **Cùng một Dockerfile**, build 2 arch trên box; chương trình chạy native ở đích, dữ liệu thời
tiết trả về nhất quán giữa hai máy.

---

## 4. Chiều B — Laptop → TV Box (chương trình lấy dữ liệu GitHub API)

Nguồn dữ liệu: **GitHub REST API**
`https://api.github.com/repos/NAMKHANH145/TV-Box-Container`. `repoinfo.py` lấy tên repo,
số sao, branch mặc định và mô tả, rồi in kèm `arch/kernel/hostname/pid`.

| Bước | Lệnh chính | Kết quả đo được |
|---|---|---|
| **B1** | dùng `minicontainer-linux-x86_64` build từ source + verify sha256 | sha256 `7e23a80e…08364`, `minicontainer 2.0.1-dev (x86_64, glibc, static)` |
| **B2** | `minicontainer check` trong harness | 12/12 `ok`, kernel `6.8.0-124-generic`, cgroup v2, overlayfs `yes`, RAM 15694 MB |
| **B3** | laptop `pull amd64` → `build x86` → `run` thử → `pull arm64` → `build arm` → `export` | Log local: `{"program":"repoinfo.py","repo":"NAMKHANH145/TV-Box-Container","stars":0,"default_branch":"main","arch":"x86_64","kernel":"6.8.0-124-generic"}`; bundle đa kiến trúc **34 MB** sha `b0f787e2…604b` + bundle chỉ x86 **17 MB** sha `cec131db…51f6` |
| **B4** | `scp` 2 bundle lên box | sha256 trên box **khớp** cả hai |
| **B5** | box thử bundle chỉ x86 → `Exec format error` (FAIL ĐỘ); import bundle đa arch → `chon bien the aarch64`, chạy native | Log: cùng repo/stars/branch, `arch:aarch64`, `kernel:6.18.38-meson64`, `pid:1` |

→ Cùng `repoinfo.py`, chạy native trên cả hai máy; box nhận bundle do **laptop** đóng gói.

---

## 5. Các bẫy đã gặp và cách script xử lý

0. **Bundle đa kiến trúc**: `export --arch=x86_64 weather-arm-1 weather-x86-1 -o weather-multi.tar.gz`
   tạo 1 file chứa `multiarch.json` + 1 inner bundle mỗi arch. Khi `import`, runtime so `MC_ARCH`
   và tự chọn biến thể — laptop chọn x86_64, box chọn aarch64. Không cần qemu, không cần gõ
   `pull --platform` ở máy đích. Dòng `load: canh bao: image la aarch64, may nay la x86_64` khi
   pull base arm64 trên laptop **không phải giả lập**: kho ảnh lưu theo tên nên pull đè bản cũ;
   build COPY-only chỉ ghi file, không có mã ARM nào thực thi trên laptop.
1. **Bridge không có DNS**: runtime đọc `/etc/resolv.conf` *trong rootfs* và từ chối `127.0.0.*`
   (host dùng `127.0.0.53`). → `COPY resolv.conf /etc/resolv.conf` với `1.1.1.1`/`8.8.8.8`.
2. **Không dùng `RUN` trong Dockerfile cross-arch**: `RUN` phải thực thi mã máy đích → cần
   qemu/binfmt. `COPY` chỉ là ghi file → build amd64 trên aarch64 (và ngược lại) chạy được.
3. **Pull base ngay trước mỗi build**: hai arch cùng tên ref `python-3.12-alpine`; nếu pull cả
   hai rồi mới build, build có thể chọn nhầm bản cuối cùng. Script pull → build từng cặp.
4. **Import cần `/usr/bin/{mkdir,rm,tar,sha256sum}`**: Alpine chỉ có `/bin/*` → dùng harness
   `ubuntu:24.04` như CI.
5. **`pull` cần `curl` trên máy host**; privileged container cần delegate cgroup
   (`mc-host` + `cgroup.subtree_control`) giống `.github/workflows/build.yml`.
6. Không có quyền `sudo` trên laptop → harness chạy bằng Docker privileged, không sửa hệ thống.

---

## 6. Tổng kết

| | Chiều A (Box → Laptop) | Chiều B (Laptop → Box) |
|---|---|---|
| Chương trình | `weather.py` (Open-Meteo) | `repoinfo.py` (GitHub API) |
| Build trên | TV Box | Laptop |
| Chạy thử native | aarch64 / `6.18.38-meson64` | x86_64 / `6.8.0-124-generic` |
| Bundle | `weather-multi.tar.gz` (34 MB) | `repoinfo-multi.tar.gz` (34 MB) |
| Xác nhận ở đích | `arch:x86_64`, cùng nhiệt độ 20.0°C | `arch:aarch64`, cùng repo/stars |

Kết luận: minicontainer `2.0.1-dev` **build – đóng gói – ship – import – chạy native** được cả hai
chiều giữa TV Box và laptop mà không cần Docker daemon, không dùng qemu, chỉ với 1 binary tĩnh.

Thời gian chạy toàn script lần này: **00:27:11 → 00:30:33 (≈ 3 phút 22 giây)**, đã có binary local
nên chỉ còn tải base image từ Docker Hub.

---

## 7. So sánh tài nguyên với Docker (cùng bài test, 22/11/2026)

Cùng workload `/bin/true`, N=20 lần mỗi runtime, đo liên tiếp trên cùng 1 máy:

| Chỉ số | TV Box S905W | Laptop |
|---|---|---|
| minicontainer p50 (start container) | **64 ms** | **2 ms** |
| Docker p50 (`alpine:3.19`) | 1641 ms | 568 ms |
| Nhanh hơn | **~26×** | **~284×** |
| Tiến trình nền minicontainer | **0 / 0 KB** | **0 / 0 KB** |
| Tiến trình nền Docker | 250 MB | 120 MB |
| RAM lúc chạy (minicontainer / tăng của Docker) | 512 KB / +15 MB | 924 KB / +11 MB |
| Đĩa: binary minicontainer / image Docker | 284 KB / 320 MB | 1,2 MB / 212 MB |

Bảng đầy đủ (p95, CPU time, disk state, điều kiện đo) nằm trong [README gốc](../README.md#performance-measured-22-nov-2026)
và số liệu thô trong `progress/VERIFICATION_LOG.md`.
