# Kế hoạch 22/11/2026 — biến "đóng gói source" thành tính năng của minicontainer

## 1. Bối cảnh

3 demo hiện gửi **build context** (thư mục `app-box/`, `app/`, `hwprobe.py`) bằng lệnh shell:

```bash
tar czf app.tar.gz -C . app && scp app.tar.gz root@<IP_BOX>:/root/demo-mc/ \
  && ssh root@<IP_BOX> 'tar xzf app.tar.gz'
```

Đây là giải pháp tạm (hướng 1) — đúng quy trình "đóng gói → chuyển → giải nén", nhưng nằm rải rác
trong script demo, mỗi chỗ viết một kiểu, không có checksum, không phải tính năng của sản phẩm.

Phân biệt rõ hai loại đóng gói:

| | Đối tượng | Lệnh hiện có | Trạng thái |
|---|---|---|---|
| `export` / `import` | **image** sau khi build | có sẵn, dùng trong demo v1 A4/A5, B3/B4 | ổn, không đụng |
| `pack` / `unpack` (đề xuất) | **build context trước khi build** | chưa có, demo đang dùng `tar` shell | cần trở thành tính năng |

## 2. Mục tiêu

```bash
minicontainer pack <dir> -o FILE [--exclude=PATTERN]...
# -> FILE.tar.gz + FILE.sha256

minicontainer unpack FILE [-C DIR]
# -> giải nén, kiểm sha256 nếu có file sidecar
```

- Người dùng không phải nhớ cờ `tar`, không tự `sha256sum`.
- Giữ nguyên quyền và symlink; chạy được trên box (GNU tar) lẫn harness CI.
- Scp vẫn do người dùng/runtime làm (minicontainer không quản lý SSH) nhưng chỉ chuyển **1 file**.

## 3. Thiết kế tối giản

- File mới `build/src/pack.c`: `do_pack(int argc, char **argv)`, `do_unpack(int argc, char **argv)`.
- Gọi `/usr/bin/tar` qua `run_wait()` — cùng dependency mà `import`/`export` đã dùng;
  **không dùng `system()`**, truyền `--exclude` như argument riêng để tránh injection.
- Dùng lại `fmt_path()`, `write_file()` trong `common.c`; sidecar `<FILE>.sha256` do `sha256sum`
  tạo hoặc tự băm bằng helper hiện có (ưu tiên helper có sẵn, không thêm thư viện).
- Đăng ký trong `common.h` (prototype), `main.c` (dispatch), `cli.c` (usage).
  `Makefile` đã dùng `wildcard src/*.c` → **không cần sửa Makefile**.
- Lỗi rõ ràng: thiếu thư mục nguồn, `-o` trỏ vào thư mục, archive hỏng, sha256 không khớp.

Không làm (YAGNI): nén song song, `--exclude` kiểu `.gitignore` đầy đủ, upload SSH tích hợp,
máy chủ archive, đọc/ghi tar bằng C thuần.

## 4. Việc cần làm

| Việc | File |
|---|---|
| Implement `pack` / `unpack` | `build/src/pack.c` (mới), `src/common.h`, `src/main.c`, `src/cli.c` |
| Test 5 case mới | `build/test.sh` (+ `local-test.sh` nếu cần fixture) |
| Docs lệnh mới | `README.md`, `GUIDE.md` |
| Chuyển 3 demo sang lệnh mới, bỏ `tar` shell | `demo-cross-ship/demo-cross-ship.sh`, `demo-ver2/demo-ver2.sh`, `demo-v3/demo-v3.sh` + 3 README |
| Ghi số liệu | `progress/VERIFICATION_LOG.md` |

## 5. Kiểm chứng

- Case mới: pack/unpack round-trip giữ symlink + quyền; sidecar sha256 đúng; `--exclude` bỏ file;
  path có dấu cách; từ chối thư mục nguồn không tồn tại; archive bị sửa → unpack từ chối.
- Suite hiện tại: box `test.sh` **184 PASS / 0 FAIL / 1 SKIP**, CI `local-test.sh` **178 / 0 / 7**
  (sau khi thêm case thì tổng tăng tương ứng, vẫn 0 FAIL).
- Binary aarch64 musl vẫn < 300 KB; `bench.sh` P50 ~61 ms, idle 0 process / 0 KB — không đổi.
- Chạy lại 3 demo bằng binary mới, log refresh, 2 chiều ship + 2 pha "cố tình sai" vẫn xanh.

## 6. Trình tự

1. **Đã xong (tạm)**: dùng `tar czf` trong 3 script demo — chạy ngay được, log có sha256.
2. Implement `pack`/`unpack` + test trên host, rồi trên box.
3. Đổi 3 demo sang `minicontainer pack/unpack`; xoá lệnh `tar` khỏi NOTE và script.
4. Cập nhật docs + log; quyết định version (`2.0.1` hay `2.1.0`) và **chỉ tag/release khi được xác nhận riêng**.

## 7. Rủi ro

- `tar` trên box là GNU, trên một số rootfs tối giản có thể là BusyBox → thử nghiệm cờ trước khi dùng;
  nếu không có cờ deterministic thì bỏ qua, không bắt buộc.
- Không đụng luồng `build --platform`, `pip install`, HAL — 3 demo phải giữ nguyên thông điệp.
- Không commit archive hay binary demo (`.gitignore` đã chặn `demo-cross-ship/**/*.tar.gz`).

## 8. Định nghĩa xong

Một người dùng mới gõ đúng 3 lệnh mà không cần đọc script:

```bash
minicontainer pack app -o app.tar.gz
scp app.tar.gz root@<IP_BOX>:/root/demo-mc/
minicontainer unpack /root/demo-mc/app.tar.gz -C /root/demo-mc
```

và thấy sha256 khớp, cây thư mục nguyên vẹn, build chạy native ở đầu kia.

---

## Da lam them 23/11: bundle da kien truc (ngoai ke hoach pack/unpack ban dau)

V1 duoc nang cap theo yeu cau "1 bundle, tu nhan dien arch dich":

- `export [--arch=ARCH] <image> <image>... -o out.tar.gz` — file `src/multiarch.c` moi: gom nhieu
  rootfs thanh 1 file + `multiarch.json` (co truong `preferred` ghi y kien truc dich luc export).
- `import` — `src/ship.c` doc index, chon bien the khop `MC_ARCH`, roi chay lai luong import cu
  (van kiem sha256 cua inner bundle). Khong co qemu; khong the "dich" ma may.
- Gioi han: de co bien the x86 trong bundle, may export van phai build/pull ban x86 truoc (A4 tren
  box). Runtime chi **chon** bien the, khong tu sinh ra ma may khac.
- `namespace_setup.c`: bo warning `/dev/console unavailable` (thiet bi optional, thieu tren
  harness → noise moi lan `run`).
- 2 test moi trong `build/test.sh`: `multi export: bundle co 2 bien the`,
  `multi import: tu chon arch may nay`. Tong case 185 → 187.
