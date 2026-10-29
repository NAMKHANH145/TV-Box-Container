# PHASE5.md — P2-2: giới hạn disk I/O `io.max` (xong, đã test)

## 1. Khảo sát trước code

- Controller `io` có sẵn; `io.stat` cho thấy device eMMC là `179:0` (whole-disk), rootfs nằm trên partition `179:2`.
- Rootfs có `dd` (BusyBox 1.38) nhưng **không có `/dev/zero`, và `/dev/null` là file thường rỗng** (Buildroot để lại) — mọi redirect `/dev/null` trước giờ thực chất ghi vào file rác trong rootfs. Vừa là bug vừa là bằng chứng cho W8.

## 2. Hai phát hiện khi implement (đều phải sửa mới chạy)

1. **`io.max` báo `ENODEV` với partition**: blk-throttle (kernel này) chỉ nhận whole-disk `179:0`, không nhận `179:2`. → Code thử device của rootfs trước, gặp `ENODEV` thì rơi về `minor=0`. Thêm helper `try_write_file()` (không-chet, giữ errno) để làm fallback; lỗi khác vẫn `die()` to (fail to còn hơn giới hạn ảo).
2. **`/dev/null` file thường** → đã `rm + mknod` lại `null` (c 1 3) và `zero` (c 1 5) thật. Việc dựng `/dev` tử tế để cho P2-6, đây chỉ là prerequisite tối thiểu để test.

Code thêm: flag `--io=N[M]` (áp cho cả `rbps`+`wbps`), tự enable `+io` trong `subtree_control`.

## 3. Kết quả test trên box (`dd ... oflag=direct`, bypass page cache)

| Kịch bản | Kết quả | Đạt? |
|---|---|---|
| Baseline không giới hạn, ghi 20MB | 0.979s ≈ **20MB/s** (tốc độ thật eMMC) | (mốc đối chứng) |
| `--io 1M`, ghi 10MB | **10.062s ≈ 1MB/s** — bóp đúng 20 lần | ✅ |
| Host trong lúc bóp | load 0.38, SSH mượt | ✅ |

## 4. Đánh giá

- Container giờ không còn đường nào làm nghẽn eMMC chung (kiểu tấn công `dd` đầy đĩa / cày I/O) — khép nốt mặt trận tài nguyên sau RAM (T4), CPU (T5), PID (P2-1).
- Giới hạn đặt theo whole-disk nên mọi container trên cùng đĩa đều bị siết công bằng — đúng ngữ nghĩa "trần" của cgroup.
- Tồn đọng chuyển sang P2-6: dựng `/dev`+`/sys` bài bản thay vì vá từng node.
