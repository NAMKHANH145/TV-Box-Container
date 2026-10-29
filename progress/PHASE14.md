# PHASE14.md — P2-10: `test.sh` tự động, 21/21 xanh (xong)

## 1. `build/test.sh` (đã deploy `/root/test.sh` trên box)

1 lệnh ra toàn bộ số liệu: T1(buildroot+alpine), T2(jail), T3(hostname), T4(OOM), T7(latency<500ms assert),
T8(không cgroup rác), T9(dockerd RSS info), userns/pids/net/seccomp/breakout, vòng đời detach→ps→stats→stop→rm,
mount-leak=0. Kết thúc in `PASS=x FAIL=y`, exit != 0 khi có FAIL. Có prologue dọn state cũ + tên container
unique (`t$$`) để lần chạy không nhiễm nhau, prerequisite check rõ thiếu gì.

## 2. Ba lỗi do chính test/uSpec lộ ra khi chạy (không phải code sai)

1. **Check `-e` symlink tuyệt đối từ host**: `/opt/alpine/bin/sh -> /bin/busybox` resolve theo root hiện tại —
   host không có `/bin/busybox` nên `-e` false dù image nguyên vẹn. Fix: check file đích (`bin/busybox`).
2. **T2 phải chuyển sang `/tmp`**: userns-nobody không viết được `/etc` root-owned (đúng bảo mật) → jail test
   dùng `/tmp` 1777. Bonus: `/dev/null` image phải `666` (chuẩn) chứ không phải `644`.
3. **Detach treo SSH**: con thừa hưởng pipe stdout của session → sshd đợi đến khi con chết. Fix trong code:
   con detach `dup2(/dev/null)` 0,1,2 trước `exec` (chuẩn daemon). Nhờ vậy mới phát hiện và đóng.

## 3. Kết quả cuối: `PASS=21 FAIL=0` (~2 phút, T7 latency 64ms)

## 4. Đánh giá

- Từ giờ mọi sửa code đều verify bằng 1 lệnh — hết thời "test tay sót".
- T5 (CPU cap) và reboot vẫn manual (cần sampling theo thời gian / ngắt session) — ghi nhận, không giấu.
