# PHASE8.md — P2-3: `chroot` → `pivot_root` (xong, đã test tấn công)

## 1. Vì sao phải đổi

`chroot` chỉ đổi cách giải quyết đường dẫn — tiến trình root có kỹ thuật chroot-break kinh điển (`chroot` vào thư mục con → `chdir ../../..` nhiều lần → `chroot .`) là ra lại host. `pivot_root` đổi hẳn root mount của namespace rồi detach root cũ → không còn đường về.

## 2. Code (`child_fn`, thay khối chroot cũ)

`mount --bind rootfs rootfs` (biến thành mount point riêng — điều kiện kernel của `pivot_root`) → `mkdir .oldroot` → `syscall(SYS_pivot_root)` (glibc không có wrapper) → `chdir(/)` → `umount2(MNT_DETACH)` → `rmdir` (best-effort, sạch eMMC). Các mount `/proc` `/sys` `/dev` sau đó không đổi (đường dẫn mới đã đúng).

## 3. Test tấn công A/B trên box (binary `breakout` static, marker `/HOST_MARKER`)

| Lồng | Kết quả | Đạt? |
|---|---|---|
| `chroot` thủ công (`unshare+chroot`, mô phỏng bản cũ) | **THOAT DUOC RA HOST**, đọc đúng marker `secret` (exit 0) | (chứng minh test có giá trị — không phải tool test hỏng) |
| `minicontainer` bản `pivot_root` | **KHÔNG thoát được** (`No such file or directory`, exit 1) | ✅ |
| Sanity T1/T2 sau đổi | PID=1, jail nguyên vẹn | ✅ |
| Mount leak | 0 | ✅ |

## 4. Đánh giá

- Đóng lỗ hổng nghiêm trọng nhất của W2 với ~10 dòng C. Cặp A/B trên là slide bảo vệ đắt giá: cùng 1 đòn tấn công, cũ thủng — mới chặn.
- Chi phí: mỗi lần chạy thêm 1 bind-mount + 1 pivot (đo latency lại ở phase tổng hợp; dự kiến tăng vài ms).
