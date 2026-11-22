# Bố cục slide thuyết trình — minicontainer

**Hình thức:** 16 slide, 16:9 (13,33 × 7,5 in), ~20 phút trình bày + 5 phút Q&A.
**File trình chiếu:** [`SLIDE_MINICONTAINER.pptx`](SLIDE_MINICONTAINER.pptx) — mỗi slide có speaker notes.
**Báo cáo kèm theo:** [`BAO_CAO_TIEN_DO_MINICONTAINER.docx`](BAO_CAO_TIEN_DO_MINICONTAINER.docx).

## Nguyên tắc thiết kế

- Một ý lớn trên mỗi slide; tiêu đề là một câu kết luận, không phải nhãn chung.
- Ba màu: navy `#12324F` (nền/tiêu đề), teal `#1F7A8C` (nhấn), xám `#5A6B7B` (phụ).
- Mọi con số đều lấy từ log trên box thật, không dùng số ước lượng.
- Slide 6 (sơ đồ khối) và slide 9 (luồng run) là trọng tâm — dành nhiều thời gian nhất.

## Tổng quan

| # | Slide | Nội dung | Thời gian |
|---|---|---|---|
| 1 | Tiêu đề | Tên đề tài, phiên bản, phần cứng | 0:30 |
| 2 | Nội dung | 6 phần của bài nói | 0:30 |
| 3 | Vấn đề | Docker chiếm 207 MB, box chỉ 1,8 GB | 1:30 |
| 4 | Mục tiêu | 4 mục tiêu, 4 thước đo | 1:00 |
| 5 | Giải pháp | Bỏ tầng trung gian: 4 tầng → 1 syscall | 1:30 |
| 6 | **Sơ đồ khối** | Laptop → CI → box → container | 2:00 |
| 7 | Công nghệ | 8 công nghệ cốt lõi | 1:00 |
| 8 | Kiến trúc | 15 module, 4 tầng | 1:30 |
| 9 | **Luồng run** | 8 bước bắt buộc | 2:00 |
| 10 | Bảo mật | 4 lớp: mount ns → userns → seccomp → cap | 1:30 |
| 11 | Tài nguyên | cgroup v2 + watchdog đĩa | 1:00 |
| 12 | Mạng | host / isolated / bridge | 1:00 |
| 13 | Hiệu năng | So sánh trực tiếp với Docker | 1:00 |
| 14 | Kiểm chứng | 84/84, soak 6.000, CI 4 arch, release | 1:00 |
| 15 | Bài học | Đã sửa gì, còn hạn chế gì | 1:00 |
| 16 | Kết luận | 3 điểm nhớ + hướng phát triển + Q&A | 1:00 |

---

## Chi tiết từng slide

### Slide 1 — Tiêu đề

- **Trên slide:** `minicontainer`; phụ đề "Container runtime zero-daemon cho thiết bị nhúng TV Box";
  dòng số liệu `1 binary 169 KB · 0 MB khi rảnh · 84/84 test trên phần cứng thật`.
- **Nói:** Chào thầy và các bạn. Đề tài là một container runtime viết bằng C, không daemon, chạy trên
  TV Box 1,8 GB RAM. Điểm nhấn nằm ở ba con số dưới tiêu đề.
- **Mẹo:** không đọc toàn bộ slide; nói 3 câu rồi chuyển.

### Slide 2 — Nội dung

- **Trên slide:** 6 phần được đánh số 01–06.
- **Nói:** Bài đi từ bài toán → sơ đồ khối → công nghệ → kiến trúc → kết quả → hạn chế.
- **Mẹo:** nói rõ "phần 4 là trọng tâm, em dành khoảng 8 phút".

### Slide 3 — Vấn đề

- **Trên slide:** ba thẻ số liệu `207 MB` (RAM Docker khi rảnh), `~2,1 s` (một lần `docker run`),
  `1,8 GB` (RAM box, không swap); 4 ràng buộc; câu hỏi nghiên cứu trong khối navy.
- **Nói:** Docker/containerd chiếm 207 MB ngay cả khi không chạy container nào. Trên box 1,8 GB,
  đó là 11% RAM cho một công cụ quản lý. Kernel lại chỉ có cgroup v2 và không có ext4 project quota.
- **Câu chốt:** "Làm sao chạy container mà không phải nuôi daemon thường trú?"

### Slide 4 — Mục tiêu và thước đo

- **Trên slide:** 4 mục tiêu (syscall trực tiếp, cô lập, bảo mật, rootfs tự chủ) và 4 thước đo
  (`0 MB`, `10–65 ms`, `84/84`, `169 KB`).
- **Nói:** mỗi mục tiêu gắn với một tầng kỹ thuật; mỗi thước đo đều kiểm chứng được trên box.

### Slide 5 — Giải pháp

- **Trên slide:** hai chuỗi so sánh. Docker: CLI → dockerd → containerd → runc → kernel (~207 MB).
  minicontainer: CLI → kernel (169 KB). Bốn nguyên tắc thiết kế và một dòng đánh đổi.
- **Nói:** điểm khác biệt là độ dài chuỗi trước khi tới kernel. Đổi lại, không chạy được image Docker.
- **Mẹo:** chỉ tay vào chuỗi Docker khi nói "4 tầng", rồi chuỗi dưới "1 tầng".

### Slide 6 — Sơ đồ khối (trọng tâm)

- **Trên slide:** laptop + GitHub Actions ở trên; khung TV Box gồm 5 bước
  `CLI → Supervisor → clone() → Container init → execvp()`; hàng cơ chế kernel (namespaces,
  filesystem, cgroup v2, seccomp, network); dải state/observability ở dưới.
- **Nói:** đi từ trái sang phải theo 5 hộp, sau đó xuống hàng kernel và nói mỗi hộp là một syscall.
  Kết thúc bằng dòng dưới cùng: khi container thoát, supervisor dọn sạch rồi thoát → idle 0 MB.
- **Mẹo:** đây là slide nên nói chậm; nếu bị hỏi "không có daemon thì ai quản lý?", chỉ vào dải state
  và giải thích `ps/stats/stop/rm` đọc trực tiếp `/run` + cgroupfs.

### Slide 7 — Công nghệ sử dụng

- **Trên slide:** 8 thẻ: C11 + musl static, namespaces, cgroup v2, seccomp-BPF, chroot + mount ns,
  veth + bridge + iptables, Buildroot, GitHub Actions.
- **Nói:** tất cả là cơ chế có sẵn của kernel; không thư viện nặng, không runtime ngoài. Musl static
  để chạy trên box không cần cài gì; Buildroot để kiểm soát rootfs; CI để build 4 kiến trúc.
- **Mẹo:** không đọc hết 8 thẻ; chọn 3 thẻ liên quan câu hỏi của giảng viên nhất.

### Slide 8 — Kiến trúc phần mềm

- **Trên slide:** 4 tầng (vào, điều phối, cơ chế, nền) với tên module; badge `15 module · ~2.400 dòng C · 169 KB`.
- **Nói:** chỉ có một phụ thuộc hai chiều là `common.h`; các module còn lại gọi xuống tầng cơ chế.
  Kích thước này nhỏ hơn dockerd khoảng 1.600 lần.
- **Mẹo:** nếu bị hỏi "sao không tách thư viện?", trả lời: mục tiêu là 1 binary, không API ổn định.

### Slide 9 — Luồng một lần run (trọng tâm)

- **Trên slide:** 8 bước, mỗi bước một dòng; dải cảnh báo cam ở cuối:
  "mount /proc sau clone · seccomp ngay trước execvp".
- **Nói:** đây là phần dễ sai nhất. Kể 2 bẫy: mount `/proc` trước clone thì `ps` thấy tiến trình host;
  cài seccomp trước setup thì chính lệnh `mount` bị chặn.
- **Mẹo:** nhấn mạnh "thứ tự quan trọng hơn từng lệnh" — đây là bài học trung tâm của đồ án.

### Slide 10 — Cô lập và bảo mật

- **Trên slide:** 4 lớp L1–L4; bảng 6 namespace; ghi chú `--seccomp=strict` allow-list 201 syscall.
- **Nói:** lớp 1 mount ns + chroot; lớp 2 userns biến root trong container thành nobody ngoài;
  lớp 3 seccomp; lớp 4 drop capability, chỉ giữ `NET_RAW` và `NET_BIND_SERVICE`.
- **Mẹo:** nói rõ đây **không** tương đương AppArmor/SELinux; đó là hạn chế đã ghi trong báo cáo.

### Slide 11 — Giới hạn tài nguyên

- **Trên slide:** bảng 5 dòng (RAM, CPU, PID, IO, đĩa) + thẻ "1 s" giải thích watchdog.
- **Nói:** bốn giới hạn đầu ghi trực tiếp cgroupfs. Riêng đĩa: kernel không có project quota nên
  dùng watchdog 1 giây + `RLIMIT_FSIZE`; đây là watchdog, không phải quota.
- **Mẹo:** chủ động nói "không phải quota" trước khi bị hỏi.

### Slide 12 — Mạng

- **Trên slide:** 3 thẻ chế độ; sơ đồ bridge `container A — mcbr0 — container B`; ghi chú `lo` phải UP.
- **Nói:** mặc định host (0 chi phí), isolated (ngắt mạng), bridge (NAT, DNS, publish TCP).
  Container-end cấu hình bằng ioctl nên rootfs không cần lệnh `ip`.
- **Mẹo:** câu chuyện mosquitto/Python bind 127.0.0.1 thất bại vì `lo` DOWN rất dễ gây ấn tượng.

### Slide 13 — Hiệu năng

- **Trên slide:** bảng so sánh 4 tiêu chí với Docker; hai số lớn `33–207×` và `~1.600×`.
- **Nói:** đo trên cùng chiếc box, không phải máy ảo. Nhanh hơn 33–207 lần tùy phép đo;
  nhẹ hơn khoảng 1.600 lần về kích thước công cụ.
- **Mẹo:** nói ngay "đánh đổi: không chạy image Docker/Kubernetes" để tránh bị coi là quảng cáo quá.

### Slide 14 — Kiểm chứng

- **Trên slide:** 4 ô `84/84`, `6.000`, `4`, `v1.5.4`; hai cột bullet kiểm chứng bổ sung và phát hành.
- **Nói:** 84/84 test trên box; soak 6.000 vòng không rò RAM; CI build 4 kiến trúc; release v1.5.4
  có 4 asset musl + SHA256SUMS. Sau test, state/cgroup/veth/mount đều bằng 0.
- **Mẹo:** nhấn "0 rác sau mỗi lần chạy" — đây là điểm kiểm chứng chất lượng dọn dẹp.

### Slide 15 — Bài học và hạn chế

- **Trên slide:** hai cột đối xứng: 5 lỗi đã xử lý (dấu ✓) và 5 hạn chế còn lại (dấu !).
- **Nói:** 5 lỗi đều liên quan thứ tự syscall hoặc race khi dọn dẹp. 5 hạn chế đều nằm ngoài phạm vi
  hoặc do kernel, đã ghi rõ trong báo cáo.
- **Mẹo:** sự trung thực ở slide này thường được đánh giá cao hơn việc che bớt.

### Slide 16 — Kết luận

- **Trên slide:** "Một binary. Không daemon. 84/84 test trên phần cứng thật."; 3 điểm nhớ;
  thẻ hướng tiếp theo; `Q&A`.
- **Nói:** mục tiêu đã đạt; giá trị lớn nhất là hiện thực hóa kiến thức môn Hệ điều hành bằng syscall
  thật trên phần cứng thật. Cảm ơn và mời câu hỏi.
- **Mẹo:** đứng yên ở slide này, không quay lại slide trước trừ khi được hỏi.

---

## Phụ lục A — Câu hỏi khó và trả lời ngắn

| Câu hỏi | Trả lời có bằng chứng |
|---|---|
| Sao không dùng `pivot_root`? | Đã thử ở phase 8 khi chạy root thật; khi thêm userns (v1.1.0) `pivot_root` trả `EPERM` trên kernel 6.18.38-meson64 ở mọi cấu hình. Chốt `chroot` + `chdir("/")` trong mount ns private; tiến trình ngoài chỉ là nobody 65534, không có `CAP_SYS_CHROOT`. |
| Sao không nhúng `runc`/`crun`? | Mục tiêu là 1 binary, zero-daemon. `runc` ~224 ms và ~MB; `crun` ~47 ms nhưng vẫn cần stack. Ở đây `clone()` + cgroupfs + `execvp()`: 10–65 ms, 169 KB, 0 MB idle. Đổi lại không chạy image Docker/k8s. |
| `--disk` có phải quota thật? | Không. Kernel không có ext4 project quota. Có 2 lớp: `RLIMIT_FSIZE` chặn một file tức thời, watchdog `nftw` 1 giây chặn tổng dung lượng; burst xấu nhất xấp xỉ một chu kỳ lấy mẫu. |
| Bảo mật có tương đương Docker? | Ba lớp: mount ns private → userns map 0↔65534 → seccomp 21 syscall + `NO_NEW_PRIVS`. Test `bomb`, `breakout`, `mount`, `ptrace` đều bị chặn. **Không** tương đương: thiếu AppArmor/SELinux, deny-list chứ không allow-list mặc định, không `/sys`. |
| 74 hay 84 test? | 84/84 trên box ở v1.5.4 (12/11/2026). `sdk` 10 case không cần root nên cũng chạy trên laptop/CI. Phần cứng thật mới verify aarch64; 3 arch còn lại ở mức QEMU-user + smoke-sdk. |
| Nếu kernel thiếu userns? | Box này có userns (map `0 65534 1` thành công). Thiếu userns thì "root trong container" = root trên host, mất một lớp phòng thủ; khi đó chỉ còn mount ns + seccomp và phải hạ tuyên bố bảo mật. |

## Phụ lục B — Checklist trước khi bảo vệ

- [ ] Mở `SLIDE_MINICONTAINER.pptx` trên đúng máy trình chiếu, kiểm tra font và tỉ lệ 16:9.
- [ ] Xuất thêm PDF dự phòng (`soffice --headless --convert-to pdf`).
- [ ] Chạy thử toàn bộ 20 phút, bấm giờ; cắt bớt nếu quá 22 phút.
- [ ] Chuẩn bị terminal demo (nếu có): `minicontainer run --rootfs /opt/rootfs /bin/sh` và `ps`.
- [ ] Kiểm tra box đã bật, binary `1.5.4`, không còn cgroup/veth rác trước khi demo.
- [ ] Đọc lại Phụ lục A; chuẩn bị sẵn số liệu `84/84`, `6.000`, `10–65 ms`, `207 MB`.
- [ ] Mang theo bản DOCX in sẵn để đối chiếu khi bị hỏi chi tiết.
