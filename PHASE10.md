# PHASE10.md — P2-5: seccomp-bpf deny-list viết tay (xong, đã test)

## 1. Thiết kế (không libseccomp — đúng chất môn học)

Filter BPF viết tay, 21 syscall bị chặn trả `EPERM` (số NR aarch64 tra từ headers cross, không đoán):
`mount/umount2/pivot_root` (chống tái cấu trúc), `reboot/kexec_load/kexec_file_load` (chống reboot/host-hijack),
`init_module/finit_module/delete_module` (chống nạp module), `ptrace` (chống bám tiến trình),
`clock_settime/clock_adjtime/settimeofday` (chống đổi giờ — đổi giờ host làm TLS/log sai),
`bpf/perf_event_open/fanotify_init` (chống quan sát kernel), `name_to_handle_at/open_by_handle_at` (chống mở file bằng handle, đường vòng qua jail),
`keyctl/add_key/request_key` (chống keyring).
Mặc định ALLOW. Sai arch → KILL. Kèm `NO_NEW_PRIVS` (điều kiện cài filter + tiện tay vô hiệu suid leo thang).

## 2. Bug tự bắn vào chân (bài học đắt giá)

Đặt `install_filter()` ngay sau `setuid` (trước setup) → chính filter chặn `mount()` của setup: `make-private /: EPERM`, 100% lệnh chết.
Fix: chuyển xuống **ngay trước `execvp`** (sau setup, trước trao quyền cho app) — đúng như comment trong code đã viết mà tay đặt sai chỗ.

## 3. Kết quả test

| Test | Kết quả | Đạt? |
|---|---|---|
| `Seccomp` trong container | `2` (filter mode), 1 filter | ✅ |
| `mount` / `umount /proc` | `Operation not permitted` | ✅ |
| `date -s 2030` | từ chối, giờ host nguyên vẹn | ✅ |
| Regression: `id`=root, app/ps/urandom | bình thường | ✅ |
| Latency sau full hardening (userns+pivot+seccomp) | **real 0.021s** (không tăng so với 30ms ban đầu) | ✅ |

## 4. Đánh giá

- Ba lớp phòng thủ giờ xếp chồng: **pivot_root** (không đường về host) + **userns** (thoát ra cũng chỉ là nobody) + **seccomp** (kể cả root trong container cũng mất 21 syscall nguy hiểm).
- Chi phí đo được: ~0ms latency thêm, ~40 dòng BPF/C, 0 phụ thuộc.
- Giới hạn trung thực: deny-list (không phải allow-list như gVisor/Kata) — syscall mới trong kernel tương lai mặc định cho qua. Với đồ án môn học đây là điểm cân bằng đúng; allow-list để dành nếu muốn đi tiếp.
