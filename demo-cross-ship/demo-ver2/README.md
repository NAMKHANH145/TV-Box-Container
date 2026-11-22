# Demo ver2 — flow làm việc bình thường: `pip install` y nguyên, hệ thống gánh phần còn lại

Chạy: `../build-local.sh` (1 lần) rồi `BOX=<IP_BOX> SSHPASS_FILE=<file> bash demo-ver2.sh`
Log thật: `results-normal/*.log` · lần chạy cuối 23:37:07 → 23:40:38 (≈ 3 phút 31 giây), binary `2.0.1-dev`.
Mỗi bước in `MÁY / VIỆC / GÕ / THẤY` trước khi chạy lệnh thật.

---

## 1. Nguyên tắc (đây là điểm sửa so với bản ver2 cũ)

Người dùng **không học lệnh mới**. Dockerfile duy nhất, dùng ở cả 2 máy — và nó do chính CLI
sinh ra bằng một lệnh (`minicontainer init`), không phải viết tay:

```bash
minicontainer init myapp --pip=Pillow
# -> myapp/{Dockerfile, app.py, README.md}; Dockerfile y hệt bên dưới
```

```dockerfile
FROM python:3.12-alpine
RUN pip install --no-cache-dir Pillow       # lệnh pip bình thường
COPY app.py /app/app.py
CMD ["python", "/app/app.py"]
```

Không có `pip download --platform`, không có `site-packages/` dựng tay, không có `--platform`
trong lệnh cài. Hệ thống (minicontainer + pip trong image đích) tự chọn wheel đúng CPU:
build trên box → wheel `aarch64`; build trên laptop → wheel `x86_64`.

`app.py`: tạo ảnh PNG 64×64, resize 16×16, in mã băm PNG + `elf_e_machine` (62 = x86_64,
183 = aarch64) + `cpu_ms` từ phép đếm số nguyên tố. Cùng ảnh → cùng băm `fed3f8e7…` ở mọi lần.

> Lưu ý nhỏ: log có dòng `bridge DNS not configured` vì lúc `RUN`, build copy `resolv.conf`
> của host (`127.0.0.53`) vào rootfs. App không gọi mạng lúc chạy nên không ảnh hưởng.
> Nếu cần DNS runtime, thêm `COPY resolv.conf /etc/resolv.conf` **sau** dòng `RUN`.

---

## 2. Quy trình và kết quả thật

| Bước | Hành động | Kết quả |
|---|---|---|
| **P1** | `build-local.sh` → binary x86_64 từ source; `init --pip=Pillow` sinh Dockerfile; `diff` với Dockerfile demo | `2.0.1-dev (x86_64, glibc, static)`; `init` tạo 3 file; **diff rỗng** → Dockerfile demo đúng y output của `init` |
| **P2** | Cài binary aarch64 vào box; đóng gói `app/` thành `app.tar.gz`, scp rồi giải nén trên box | `2.0.1-dev (aarch64, musl, static)`, 2 file, 116 B + 1478 B |
| **A1** | Trên box: `pull arm64` → `build` → `run` | pip tự tải `pillow-13.0.0-…aarch64.whl`; `e_machine=183`, `cpu_ms=11.9` → ✅ |
| **A2/A2b** | `export` bundle arm (**28 MB**, sha `08d98d49…0394`) → scp laptop | sha khớp |
| **A3 (SAI)** | Laptop import bundle từ box, chạy | `e_machine=183` trên kernel laptop, `cpu_ms=3.6` (~7× chậm) → ❌ phải giả lập |
| **A4 (ĐÚNG)** | Trên laptop: **cùng Dockerfile**, `pull amd64` → `build` → `run` | pip tự tải `…x86_64.whl`; `e_machine=62`, `cpu_ms=0.6` → ✅ |
| **B1/B1b** | Laptop export bundle x86 (**28 MB**, sha `9b93a6c9…332cc`) → scp box | sha khớp |
| **B2 (SAI)** | Box import bundle từ laptop, chạy | `execvp: Exec format error` → ❌ |
| **B3 (ĐÚNG)** | Trên box: **cùng Dockerfile**, build lại | pip tự tải wheel aarch64; `e_machine=183`, `cpu_ms=13.5` → ✅ |

### Bảng chốt

| Máy | Cách có image | e_machine | cpu_ms | Kết quả |
|---|---|---|---|---|
| Box | build tại box | 183 | 11,9 | ✅ |
| Laptop | ship ảnh box | 183 | 3,6 | ❌ giả lập |
| Laptop | build tại laptop | 62 | 0,6 | ✅ |
| Box | ship ảnh laptop | — | — | ❌ `Exec format error` |
| Box | build tại box (lần 2) | 183 | 13,5 | ✅ |

Cả 5 lần: cùng `png_sha256_16 = fed3f8e7384ce294`, cùng `prime_count = 17984`.

---

## 3. Câu nói khi demo

> "Người dùng chỉ gõ một lệnh `init` để sinh dự án có `pip install`, rồi chạy `build` trên máy
> mình định chạy. pip tự tải wheel của kiến trúc đó — không cần lệnh đặc biệt nào. Em thử mang ảnh
> của máy khác sang: laptop thì chạy được nhưng bị giả lập, chậm gấp 6 lần; box thì lỗi
> `Exec formaterror` luôn. Vậy hệ thống đã gánh phần còn lại: người dùng không phải biết mình đang dùng ARM hay x86."

Bản thử nghiệm đầu (tải wheel tay bằng `pip download --platform`, rồi `COPY site-packages/`)
đã bị **thay thế và xoá** — nó bắt người dùng học lệnh đặc biệt. Flow chính giờ chỉ cần
`app/` + `results-normal/` với đúng một lệnh `pip install`.

---

## 4. Chạy lại

```bash
cd demo-cross-ship           # thu muc cha cua demo-ver2/
./build-local.sh
BOX=<IP_BOX> SSHPASS_FILE=<file chứa mật khẩu SSH> bash demo-ver2/demo-ver2.sh
```

Script tự dọn container cùng tên, tự pull base ngay trước mỗi build (tránh nhầm ref giữa 2 arch),
tự export/scp/import và tự `grep` khẳng định — không thao tác tay.
