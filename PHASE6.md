# PHASE6.md — P2-6: `/dev` + `/sys` bài bản (xong, đã test)

## 1. Code thêm (`child_fn`, sau chroot)

- `mount("sysfs", "/sys", ..., MS_RDONLY)` — đọc được, ghi bị từ chối.
- `mount("tmpfs", "/dev", ..., "size=1m,mode=755")` + `mknod null (1,3) / zero (1,5) / urandom (1,9)` — fresh mỗi lần chạy, không ghi eMMC.
- **`mount(NULL, "/", NULL, MS_REC|MS_PRIVATE)` đầu tiên** — ngắt lan truyền mount (xem §2).

## 2. Bug nghiêm trọng lòi ra khi test: mount leak ra host

Triệu chứng: `mount /sys` báo `EBUSY` từ lần chạy thứ 2.
Root cause (xác minh bằng `mount | grep rootfs`): systemd để mount propagation mặc định `shared` → mọi mount trong container (`/proc`, `/sys`, `/dev`) lan ngược ra host, mỗi lần chạy để lại 1 `/proc` chồng trên `/opt/rootfs/proc` của host (đếm được 12 cái tồn).
Cách thấy: mount trong child ns nhưng hiện trên host = propagation chưa cắt.
Fix: `MS_REC|MS_PRIVATE` ngay sau `clone()` (chuẩn runc cũng làm vậy) + dọn tay 14 mount tồn trên host. Verify sau fix: chạy 3 lần liên tiếp, `mount | grep /opt` = 0.

## 3. Kết quả test

| Test | Kết quả | Đạt? |
|---|---|---|
| `/dev` trong container | tmpfs fresh, đúng 3 node `null/zero/urandom` | ✅ |
| `/dev/urandom` dùng được | đọc ra 8 byte ngẫu nhiên | ✅ |
| Ghi `/sys` | `Read-only file system` | ✅ |
| Đọc `/sys` | thấy `block/bus/class` | ✅ |
| Mount leak sau nhiều lần chạy | 0 | ✅ |

## 4. Đánh giá

- Container giờ có thiết bị tối thiểu để app thật chạy (random, null, zero) mà không động gì đến `/dev` host.
- Vụ mount leak là lỗi đúng chất "môn Hệ điều hành": hiểu sai propagation mặc định của systemd. Đã fix tận gốc + có regression check (đếm mount host sau mỗi lần test).
