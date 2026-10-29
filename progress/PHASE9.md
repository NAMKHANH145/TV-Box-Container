# PHASE9.md — P2-4: user namespace, root trong → nobody ngoài (xong, đã test)

## 1. Thiết kế

`CLONE_NEWUSER` + parent ghi map `0 65534 1` vào `uid_map`/`gid_map` (sau `deny` vào `setgroups`). Thoát container = nobody host, không phải root.

Đồng bộ 2 chiều qua 2 pipe (mở rộng handshake cũ): con báo `ready` ngay sau clone → cha ghi maps + gắn cgroup → con nhận `go` mới chạy. Bắt buộc, vì con trong ns mới `getpid()` luôn =1 nên không tự gắn gì được.

## 2. Ba cái bẫy kernel đã vấp (mỗi cái đều là kiến thức môn học)

1. **Thư mục host root-owned**: con trong userns là nobody ngoài host → `mkdir .oldroot` bị từ chối. Fix: parent (host-root) tạo trước, dọn sau `waitpid`.
2. **Mount sau pivot bị EPERM + sysfs bị cấm tuyệt đối trong userns** (mount mới/bind/remount đều chết): dồn mọi mount (proc/dev) về trước pivot; bỏ `/sys` hẳn — container không thấy gì về thiết bị host (cách ly mạnh hơn cả Docker).
3. **`mknod` trong userns chết** (EPERM/EOVERFLOW): `/dev` phải có sẵn trong IMAGE (đúng chuẩn Docker: node tạo lúc build image). Đã chốt 3 node `null/zero/urandom` vào cả 2 image + `chmod 1777` cho `/tmp` hai image.

## 3. Cái bẫy tinh vi nhất: quên `setuid(0)` = nobody vô dụng

Sau map, con vẫn mang host-uid 0 của parent — nằm NGOÀI map → hiển thị nobody, không quyền gì kể cả trong container. Phải `setgid(0); setuid(0)` sau go-signal để kuid rơi về host-65534 (= root trong, vô quyền ngoài). Triệu chứng khi thiếu: `id` → nobody, mọi thứ Permission denied.

## 4. Kết quả test

| Test | Kết quả | Đạt? |
|---|---|---|
| `id` trong container | `uid=0(root)`, map `0 65534 1` | ✅ |
| `sleep 30` nhìn từ host | UID **65534** | ✅ |
| File tạo trong container, nhìn từ host | owned **nobody:nogroup** | ✅ |
| Breakout chroot-break (đòn đã thủng chroot ở phase 8) | vẫn bị chặn (pivot + userns, phòng thủ 2 lớp) | ✅ |
| Regression T1/hostname | PID=1, `minicontainer-os` | ✅ |

## 5. Đánh giá

- Đây là lớp bảo mật giá trị nhất từ đầu dự án: kể cả có bug cho kẻ tấn công thoát namespace, hắn cũng chỉ là nobody trên host.
- Giá phải trả (ghi nhận trung thực): `/sys` biến mất, `/dev` đóng băng theo image, mỗi lần chạy thêm 2 pipe handshake (latency đo lại ở phase tổng hợp).
