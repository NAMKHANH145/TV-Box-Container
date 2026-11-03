# Soát `GUIDE.md` theo góc nhìn người mới (Track D: Dev D)

Đọc `GUIDE.md` từ đầu và làm theo như người chưa biết dự án. 10 chỗ vấp tìm được
(đã sửa các mục 1–4 ở commit `32b8bbb` và các commit trước; các mục 5–10 là góp ý, chưa sửa).

1. **Version string cũ (§1)** — GUIDE ghi `minicontainer 1.4.0` và "bản chuẩn 100–137 KB"; bản hiện tại
   là `1.5.0` / ~145 KB. Người mới chạy `--version` sẽ tưởng tải sai binary. ✅ đã sửa.
2. **`ls -l build/minicontainer # ~125 KB` (§2.1)** — số cũ. ✅ đã sửa (~145 KB).
3. **`gh release download v1.4.0` (§5.8/§3 deploy)** — tag cũ; người mới tải bản v1.4.0 thiếu `sdk`. ✅ đã sửa (v1.5.0).
4. **`BR=/opt/rootfs-web bash test.sh` (§7)** — GUIDE cũ nói override được qua env, nhưng `test.sh`
   hardcode `BR=/opt/rootfs`, `ALP=/opt/alpine`, `MC=minicontainer`. ✅ đã sửa văn bản.
5. **`test.sh` cần `bomb`/`breakout` ở `/opt/rootfs/`** (§7) — người mới không biết phải `make tools`
   rồi `scp` trước; nếu thiếu, `test.sh` exit ngay ở prerequisite. Nên thêm 1 dòng "chạy `make tools`
   trên laptop trước" ngay đầu §7 (hiện nằm sau).
6. **`--read-only` báo `remount ... Operation not permitted`** (§8) — hướng dẫn chuyển rootfs ra ext4,
   nhưng không nói rõ cách kiểm tra rootfs đang nằm trên tmpfs (`df -T /opt/rootfs`). Người mới khó tự
   chẩn đoán.
7. **`make deploy`** (§3) — dùng `SSHPASS_FILE` nhưng không nhắc rằng binary tải từ release **mất bit `+x`**
   và cần `chmod +x` trước khi `scp`/`make deploy` (§8 có nhắc nhưng ở phần khác). Nên gom vào §3.
8. **`oci-run` (§5.9) và `service create` (§5.10)** — chỉ có 1 ví dụ, không nói subset OCI nào được hỗ
   trợ (process args/env/cwd, root.path, hostname, limits) và cái gì bị bỏ qua. Người mới sẽ thử `mounts`
   rồi thất bại không hiểu vì sao.
9. **`--disk` (§5.12)** — nói "watchdog 1 s" nhưng không nói rõ burst xấu nhất (~1 chu kỳ lấy mẫu) và
   `RLIMIT_FSIZE` chỉ chặn **một file đơn lẻ**. Dễ bị hiểu nhầm là quota thật.
10. **`soak.sh` (§7)** — `ITER=117000` (~24 h) nhưng không nói cách chạy nền (`nohup`/`disown`) và cách
    giám sát; người mới chạy foreground sẽ mất SSH giữa chừng. Nên thêm ví dụ `nohup ... &`.

**Kết luận:** GUIDE nhìn chung đầy đủ và chính xác sau khi sửa số version. Các góp ý 5–10 là cải thiện
trải nghiệm, không phải lỗi. Dev D có thể dùng danh sách này làm đầu vào cho slide/báo cáo.
