# So sánh MIT vs Apache-2.0 cho minicontainer (Track B: Huy)

Repo hiện dùng **MIT** (`LICENSE`, `Copyright (c) 2026 NAMKHANH145`). Đổi sang Apache-2.0 chỉ cần thay 1 file
và cập nhật README/GUIDE (dòng "License"). Bảng dưới là đủ để quyết định.

| Tiêu chí | MIT | Apache-2.0 |
|---|---|---|
| Độ dài / độ phức tạp | ~170 từ, 1 file | ~1.700 từ, 1 file + NOTICE (tuỳ chọn) |
| Cho phép thương mại, sửa, phân phối, private | ✅ | ✅ |
| Yêu cầu giữ copyright notice | ✅ | ✅ |
| Yêu cầu ghi nhận thay đổi (change notice) | ❌ (không bắt) | ✅ (phải nêu trong file thay đổi) |
| Bảo vệ bằng sáng chế (patent grant) | ❌ không có | ✅ có (người đóng góp cấp patent cho người dùng) |
| Bảo vệ trademark | ❌ | ✅ (không được dùng tên/logo người đóng góp) |
| Phù hợp với code nhúng C / toolchain musl | ✅ rộng rãi (Linux kernel, busybox dùng GPL; musl dùng MIT) | ✅ (Android, Kubernetes dùng Apache-2.0) |
| Tương thích với dependency | musl (MIT), Buildroot (GPLv2) — MIT không "lây" GPL | Apache-2.0 không tương thích GPL v2 (chỉ GPL v3+), nhưng minicontainer không link GPL library nên không vấn đề |
| Phù hợp mục tiêu đồ án / CV | ✅ ngắn gọn, dễ đọc | ✅ "chuyên nghiệp" hơn, có patent grant |

## Khuyến nghị

- **Giữ MIT** nếu: ưu tiên đơn giản, repo là đồ án/cá nhân, không có bằng sáng chế liên quan, muốn người khác
  fork/dùng mà không phải lo change notice. Phù hợp hiện tại.
- **Đổi sang Apache-2.0** nếu: dự định công bố rộng rãi, lo ngại patent trolling, hoặc muốn "chuẩn doanh nghiệp"
  (đặc biệt nếu sau này thêm code từ các dự án Apache-2.0).

## Chi phí đổi (nếu quyết định Apache-2.0)

1. Thay `LICENSE` bằng văn bản Apache-2.0 (giữ `Copyright 2026 NAMKHANH145`).
2. Thêm header `// SPDX-License-Identifier: Apache-2.0` vào đầu mỗi `.c`/`.h` (tuỳ chọn nhưng khuyến nghị).
3. Cập nhật README/GUIDE/TASK dòng "License".
4. Commit + push; GitHub tự detect Apache-2.0.

**Kết luận đề xuất: giữ MIT** cho giai đoạn hiện tại; xem xét Apache-2.0 khi dự án ra khỏi phạm vi đồ án.
