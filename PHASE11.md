# PHASE11.md — P2-8: build musl, binary 658KB → 57KB (xong)

## 1. Cách làm

Dùng chính toolchain Buildroot đã build ở phase 1 (`buildroot/output/host/bin/aarch64-buildroot-linux-musl-gcc`) biên dịch `minicontainer.c` static, rồi `strip`. Không cài thêm gì, không đụng box.

## 2. Kết quả

| Bản | Size | Chạy trên box? |
|---|---|---|
| glibc static (Ubuntu cross) | 658,080 B | ✅ (bản đang chạy) |
| musl static | 137,976 B | — |
| musl static + strip | **58,648 B (~57KB)** | ✅ đã deploy, regression pass |

Nhỏ hơn **11.5 lần**. Vượt mục tiêu <100KB trong PLAN-2.

## 3. Regression sau đổi toolchain

`id`=root trong/userns, `Seccomp: 2`, PID=1, latency **10ms** (còn nhanh hơn 21ms trước — nhiễu đo, thực chất tương đương).

## 4. Đánh giá

- Toàn bộ "hệ thống": tool **57KB** + rootfs **1.3MB** = ~1.4MB cho 1 container đầy đủ cô lập 6 namespaces + 5 controllers + seccomp. So với dockerd+containerd 213MB nền: nhẹ hơn **~150 lần**.
- Từ giờ build chuẩn là musl (`minicontainer` trong `build/` đã thay bằng bản musl strip). Quy trình build ghi vào test.sh (P2-10) để ai cũng lặp lại được.
