# PHASE15.md — P2-9: OCI-bundle mode tối thiểu (xong, đã test)

## 1. Quyết định: làm subset, không làm full OCI, không bỏ

Full OCI (mounts/hooks/seccomp-OCI/devices/namespaces đầy đủ) = viết lại runc, trật luận điểm siêu nhẹ.
Subset đã làm: `minicontainer oci-run <bundle>` đọc `config.json` → đổi sang flags nội bộ → chạy pipeline cũ:
`process.args[]`, `hostname`, `root.path` (tương đối theo bundle), `linux.resources` = `memory.limit` (byte),
`cpu.quota/cpu.period` (đổi ra %), `pids.limit`. Parser JSON viết tay ~60 dòng (không lib): tìm key theo scope
(`root.path` scope trong `"root"` để khỏi nhầm `path` của hooks), không hỗ trợ escape — có tài liệu trong code.

## 2. Kết quả test

| Test | Kết quả | Đạt? |
|---|---|---|
| `oci-run` bundle mẫu (args+hostname+rootfs) | `OCI_OK`, hostname `oci-demo` | ✅ |
| Limit từ JSON (bomb 50MB, `memory.limit` 30M) | OOM-kill, exit 1 | ✅ |
| Regression: foreground + full `test.sh` | 21/21 | ✅ |

## 3. Đánh giá

- Tool giờ đọc được bundle theo đúng schema OCI (bundle của tool khác sinh ra mà chỉ dùng subset này là chạy) —
  câu "tương thích OCI một phần" có bằng chứng, không nói suông.
- Giới hạn trung thực: mounts/hooks/seccomp-OCI/devices không hỗ trợ; bundle Docker Hub cần chuyển tay
  (export rootfs + viết config tối thiểu). Với đồ án môn học đây là điểm dừng đúng.
- Binary 89KB → 93KB cho cả OCI mode. PLAN-2 hết mục.
