# Demo v3 — HAL dịch phần cứng: cùng 1 app, tự nhận diện board

Chạy: `../build-local.sh` (1 lần) rồi `BOX=<IP_BOX> SSHPASS_FILE=<file> bash demo-v3.sh`
Log thật: `results-v3/*.log` · lần cuối 23:40:59 → 23:41:22 (~23 giây, binary `2.0.1-dev`).
Mỗi bước in `MÁY / VIỆC / GÕ / THẤY` trước khi chạy lệnh thật.

---

## 1. Ý tưởng (khác hẳn v1/v2)

v1 ship **image**, v2 ship **image + pip**. v3 ship **một file Python duy nhất**, chạy trên:

- **Box**: rootfs **Buildroot `python-edge`** có sẵn (`/opt/rootfs-python-edge`, Python 3.14) — đồ cũ của dự án.
- **Laptop**: image OCI `python:3.12-alpine` (stdlib, không cần cài gì).

Hai môi trường này **khác nhau ở tên thiết bị gốc**:

| Khả năng logic | Box (Amlogic S905W) | Laptop (x86_64 PC) |
|---|---|---|
| UART | `ttyAML0` (meson_uart) | `ttyS0…ttyS31` (8250/16550) |
| GPIO chip | 2 | 1 |
| I2C bus | 1 (`i2c-0`) | 17 |
| Nhiệt độ | ~59 °C | ~66 °C |
| Kernel | `6.18.38-meson64` | `6.8.0-124-generic` |

Code không được hard-code tên nào trong đó. Nó có **lớp HAL** dịch tên riêng của từng board
thành **khái niệm chung** `uart / gpio / i2c / thermal`.

---

## 2. Cơ chế chung của cả 2 máy

minicontainer dùng cùng một lệnh trên cả hai đầu để đưa phần cứng thật vào sandbox:

```bash
--mount bind:/dev:/hostdev:ro,rbind
--mount bind:/sys:/hostsys:ro,rbind
```

> Phải dùng `rbind`. `bind` thường bị `EINVAL` vì runtime tạo mount point bằng `mkdir_p`
> (thành thư mục); `rbind` mới bind được cây thiết bị. Sysfs mount thuần thì bị `EPERM` trong userns.

HAL đọc `/hostdev/...` và `/hostsys/class/thermal/...`, rồi tự chọn backend:

```python
if glob.glob("/hostdev/ttyAML*"): backend = "amlogic"
elif glob.glob("/hostdev/ttyS*"): backend = "pc_x86"
```

Có thể ép sai bằng `--env HW_BACKEND=...` để làm phản diện.

---

## 3. Kết quả thật

| Case | Máy / rootfs | backend | UART | GPIO | I2C | Nhiệt | status |
|---|---|---|---|---|---|---|---|
| A1 auto | Box / Buildroot `python-edge` | `amlogic` (auto:ttyAML) | 1 × ttyAML0 | 2 | 1 | ≈55–65 °C | **OK** |
| A2 ép sai | Box / Buildroot | `pc_x86` (forced) | **0** | 2 | 1 | ≈64 °C | **WRONG_BACKEND**, `missing=['uart']` |
| B1 auto | Laptop / OCI alpine | `pc_x86` (auto:ttyS) | 32 × ttyS* | 1 | 17 | ≈55–76 °C | **OK** |
| B1 ép sai | Laptop / OCI alpine | `amlogic` (forced) | **0** | 1 | 17 | ≈55–76 °C | **WRONG_BACKEND**, `missing=['uart']` |

Cùng `program=hwprobe.py (HAL)`, cùng `pid=1`, nhưng `soc_hint` đổi theo backend:
`Amlogic S905W (meson)` ↔ `x86_64 PC`.

---

## 4. Câu nói khi demo

> "Em ship đúng một file Python. Trên box nó tự thấy `ttyAML0` và chọn backend Amlogic; trên
> laptop nó thấy 32 cổng `ttyS` và chọn backend PC. Ứng dụng chỉ hỏi 'có UART không, có bao
> nhiêu GPIO, nhiệt độ bao nhiêu' — không biết tên thiết bị của từng máy. Em thử ép sai backend
> thì UART biến mất, báo `WRONG_BACKEND` ngay. Đó là lớp dịch mà một dự án cần có khi chạy trên
> phần cứng khác nhau."

So sánh với v2: v2 dạy **pip chọn wheel theo CPU**; v3 dạy **app chọn driver/device theo board**.
Hai tầng khác nhau của cùng một vấn đề "chạy được trên máy khác".

---

## 5. File

```
demo-v3/
├── hwprobe.py          # HAL + bang dich backend (stdout JSON)
├── app/Dockerfile      # OCI cho laptop: FROM python:3.12-alpine, COPY hwprobe.py
├── demo-v3.sh          # 6 buoc: P1 P2 A1 A2 B1(+ep sai) Z
├── README.md
└── results-v3/*.log    # JSON that tu 4 lan chay (2 dung + 2 ep sai)
```

Chạy lại: `cd demo-cross-ship && ./build-local.sh`, rồi `BOX=<IP_BOX> SSHPASS_FILE=<file> bash demo-v3/demo-v3.sh`.
Script cài luôn binary aarch64 `2.0.1-dev` vào `/usr/local/bin/minicontainer`, đóng gói
`hwprobe.py` thành `.tar.gz`, giải nén và đặt vào rootfs Buildroot ở bước P2.

Skipped: bind trực tiếp từng device node (`-v /dev/ttyAML0:/dev/ttyAML0`) — lỗi `Not a directory`
do runtime tạo target bằng `mkdir_p`. Add khi: runtime hỗ trợ tạo file target cho bind device.
