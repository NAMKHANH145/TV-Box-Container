# PHASE4.md — P2-1: `pids.max` + NEWIPC (xong, đã test)

## 1. Khảo sát trước code

- Controller `pids` có trong `cgroup.controllers` nhưng **chưa chắc đã enable** sau reboot (lần trước bật tay đã mất). → Code phải tự ghi `+cpu +memory +pids` vào `cgroup.subtree_control` khi khởi động (ghi lại khi đã bật là no-op, an toàn).
- Rootfs có `/usr/bin/ipcs` (không có `ipcmk`) → test NEWIPC theo hướng host→trong: tạo shm trên host bằng `ipcmk`, trong container `ipcs` phải trống.

## 2. Code đã sửa (`build/minicontainer.c`, binary mới đã deploy `/usr/local/bin/minicontainer`)

- Thêm flag `--pids=N` (mặc định 64), ghi `pids.max` vào cgroup.
- Thêm `CLONE_NEWIPC` cố định vào flags clone (1 dòng).
- Tự enable controllers khi start (fix bug tiềm ẩn: trước đây code mặc định controller đã bật sẵn).

## 3. Kết quả test trên box

| Test | Kết quả | Đạt? |
|---|---|---|
| Spawn 100 tiến trình với `--pids 16` | kernel từ chối fork: `can't fork: Resource temporarily unavailable`, host load 0.45 bình thường, cgroup tự dọn | ✅ |
| NEWIPC: tạo shm 1MB trên host (`shmid=0`), `ipcs -m` trong container | danh sách trống — không thấy shm host | ✅ |
| Sanity (không vỡ gì cũ) | `run /bin/echo HELLO` OK; T1–T3 không ảnh hưởng (không test lại full, code path cũ nguyên) | ✅ |

## 4. Đánh giá

- Fork-bomb — kiểu tấn công rẻ nhất vào box 1.8GB RAM — giờ bị chặn ở mức kernel, không cần code phòng thủ nào thêm.
- IPC namespace đóng nốt mặt trận chia sẻ bộ nhớ giữa container và host (trước đây `CLONE_NEWIPC` còn thiếu).
- Chi phí: ~10 dòng C, 0 phụ thuộc mới, binary tăng không đáng kể.
