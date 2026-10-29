# PHASE13.md — CLI quản lý `ps/stats/stop/rm` (xong, đã test vòng đời đầy đủ)

## 1. Lệnh hiện có (57KB → 89KB, vẫn 0 daemon)

```
minicontainer run [-d] [--name ID] [--ram/--cpu/--pids/--io/--net/--rootfs/--hostname] <cmd>
minicontainer ps | stats <id> | stop <id> | rm [-f] <id>
```

- `run -d --name web ...` → in id, thoát ngay. State `/run/minicontainer/<id>/` (`pid`, `cgdir`, `cmd`).
- `ps` → bảng ID/STATUS/PID/COMMAND (kiểu `docker ps`, đọc state + `/proc`, không daemon).
- `stop` → TERM cả cgroup, 3s sau KILL sót, `rmdir` cgroup (container ở lại dạng STOPPED).
- `rm` → từ chối khi RUNNING (trừ `-f` tự stop trước), xóa state.

## 2. Test vòng đời đầy đủ trên box

`run -d` ×2 (web 50M-Buildroot, worker cpu20-Alpine) → `ps` thấy cả 2 RUNNING → `stats web` (262KB) →
`stop web` → STOPPED → `rm web` OK → `rm worker` bị từ chối đúng → `rm -f worker` xong →
`ps` trống, `/run/minicontainer` trống, mount leak = 0.

## 3. So với Docker (cho slide bảo vệ)

| | Docker | minicontainer |
|---|---|---|
| Liệt kê/quản lý | cần dockerd 131MB + containerd 82MB | 0MB (đọc file) |
| Start 1 container | 2.076s | 0.010–0.030s |
| Tool + rootfs | images hàng trăm MB | 89KB + 1.3MB |
| Cái Docker có mà ta không | images, registry, networks, volumes, restart-policy, logs driver | (ghi rõ giới hạn — xem P2-9) |

## 4. Đánh giá

- Đủ dùng thật cho gateway nhúng: chạy nền + giám sát + dọn dẹp, không cần gì thêm.
- Thiếu cố ý (đúng triết lý UNIX): không logs tập trung (dùng stdout redirect), không restart-policy (việc của systemd unit ngoài), không exec vào container đang chạy (cần `nsenter`, để dành nếu cần).
