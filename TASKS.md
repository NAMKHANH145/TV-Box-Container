# TASKS — Chia việc nhóm minicontainer

> Cập nhật: 2026-10-29 · Phiên bản hiện tại: **v1.2.0** · Test trên box: **39/39 PASS**
> Repo: <https://github.com/NAMKHANH145/TV-Box-Container>
> Tài liệu nền: `README.md` (cách dùng), `PLAN-4.md` (lộ trình R1–R5), `VERIFICATION_LOG.md` (bằng chứng đo trên S905W).

Tài liệu này trả lời 3 câu hỏi: **dự án có những module nào**, **mỗi người đọc gì / làm gì**, và **khi nào coi là xong**.

---

## 1. Trạng thái hiện tại (đọc trước khi nhận việc)

| Hạng mục | Trạng thái |
|---|---|
| Runtime (namespace, cgroup, seccomp, disk quota) | ✅ Chạy thật trên TV Box S905W |
| 39 test case (`build/test.sh`) | ✅ 39/39 PASS (2026-10-29) |
| Disk quota userspace `--disk` | ✅ Foreground + detached + RLIMIT_FSIZE |
| OCI bundle (subset) | ✅ `oci-run`, có cảnh báo key ngoài subset |
| CLI (`run/ps/stats/stop/rm/exec?`) | ✅ Trừ `exec` (chưa có) |
| `--net=bridge` | ❌ Chưa làm (hiện chỉ `host` / `isolated`) |
| Health check chế độ `-d` | ✅ Watchdog dùng chung với `--disk`, marker `health_failed` |
| Rootfs profiles (4 bản) | ✅ Defconfig đã validate với Buildroot; mới build base-mini trên phần cứng |
| CI GitHub Actions | ✅ 4/4 arch xanh (run `40842042612`); release theo tag chưa test |
| Soak test 24h | ❌ Chưa làm |

**Nguyên tắc:** mọi task phải có (1) code, (2) test trong `build/test.sh` hoặc test riêng, (3) cập nhật `README/CHANGELOG` nếu thay đổi hành vi. Không nhận task nào chỉ "chạy được trên laptop".

---

## 2. "10 module là tất cả rồi à?" — Có 11 module

Đúng là ít, vì đây là runtime tối giản chứ không phải Docker. Mỗi module một việc, tổng ~1.385 dòng C:

| # | File | Dòng | Nhiệm vụ | Độ khó đọc |
|---|---|---:|---|---|
| 1 | `src/main.c` | 85 | `main()`, dispatch command, `--version`, in arch/libc | Dễ |
| 2 | `src/cli.c` | 46 | Parse argv → `struct config`, validate số (`--ram`, `--cpu`…) | Dễ |
| 3 | `src/common.c/.h` | 98 + 102 | Helper dùng chung: log, `die_msg`, `parse_bytes`, validate tên/đường dẫn | Dễ |
| 4 | `src/ns.c` | 61 | `clone()` 6 namespace, map userns (root trong → nobody ngoài), chroot | **Khó** |
| 5 | `src/run.c` | 222 | Vòng đời container: setup cgroup → clone → supervisor → tín hiệu → health/disk | **Khó** |
| 6 | `src/cgroup.c` | 104 | Ghi `memory.max`, `cpu.max`, `pids.max`, `io.max`; kill/cleanup cgroup v2 | Trung bình |
| 7 | `src/disk.c` | 119 | Đo dung lượng rootfs (nftw), watchdog quota, marker `disk_exceeded` | Trung bình |
| 8 | `src/seccomp.c` | 102 | BPF deny-list 21 syscall nguy hiểm, trả `EPERM` | **Khó** |
| 9 | `src/oci.c` | 129 | Đọc `config.json` (subset OCI), cảnh báo key lạ | Trung bình |
| 10 | `src/state.c` | 264 | `ps/stats/stop/rm`, state trong `/run/minicontainer/<id>/` | Trung bình |
| 11 | `src/systemd.c` | 53 | Sinh unit systemd để tự restart container, vẫn zero-daemon | Dễ |

Thứ tự đọc đề xuất cho người mới: **main → cli → common → state → run → cgroup → ns → seccomp → disk → oci → systemd**. Đọc `run.c` cuối cùng vì nó gọi gần như mọi thứ.

### Cấu trúc ngoài `src/`

```
build/            Makefile, test.sh (39 case), bomb.c/breakout.c (test an ninh)
profiles/         4 defconfig Buildroot + build.sh
.github/workflows/build.yml   CI 4 kiến trúc
deploy/           script triển khai lên box
docs gốc: README.md, CHANGELOG.md, PLAN-4.md, VERIFICATION_LOG.md, FINAL.md
```

---

## 3. Chia việc (đề xuất 4 track)

Mỗi track có 1 owner chính. Cột "Owner" đang để trống — điền tên khi họp nhóm. Ước lượng theo buổi làm việc thực tế (1 buổi ≈ 3–4h).

### Track A — Core runtime (C) · owner: Khánh

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **A1 / R5-3a** | ~~Health check cho chế độ `-d`~~ ✅ **DONE 2026-10-29**: mở rộng watchdog chạy `--health-cmd` mỗi 5s, marker `health_failed`, kill cgroup | P0 | 0.5–1 buổi | `src/disk.c`, `src/run.c` | Test `-d --health-cmd false` → STOPPED ≤15s, marker tồn tại, cgroup sạch; `true` không bị giết oan |
| **A2 / R5-3b** | Lệnh `exec <id> <cmd>` qua `nsenter`/`setns` (PID, mnt, net) | P1 | 2–3 buổi | A1 | `exec` chạy được trong container đang chạy; ghi rõ giới hạn userns trong README |
| **A3 / R5-3c** | `--net=bridge`: tạo veth pair + `br0` bằng netlink hoặc `ip` (không thêm daemon) | P2 | 3–5 buổi | Kernel box có `veth`, `bridge` | 2 container ping nhau qua bridge; test trong `test.sh`; fallback báo lỗi rõ nếu kernel thiếu module |
| **A4** | Rà lại seccomp cho arch mới, thêm syscall vào deny-list có test | P2 | 1 buổi | — | `breakout` fail trên cả 4 arch; không ảnh hưởng 39 test cũ |

### Track B — Rootfs, build & release · owner: Huy

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **B1 / R5-2** | Build thật 3 profile còn lại (`iot-gateway`, `python-edge`, `web-proxy`) trên Buildroot | P0 | 2–4 buổi (máy yếu lâu hơn) | `profiles/*.defconfig` đã validate | `rootfs.tar.gz` chạy được `minicontainer run`; đo kích thước/RAM thực tế, ghi vào `profiles/README.md` |
| **B2 / R5-1** | CI GitHub Actions 4 arch đã xanh (run `40842042612`); còn: cache toolchain + thử release theo tag | P0 | 0.5 buổi | Repo đã push | 4 artifact build xanh (**xong**); tag `v*` tạo release có checksum (B3) |
| **B3 / R4-3** | Đóng gói release tarball (binary + README + test.sh + checksum) | P1 | 0.5 buổi | B2 | `release.sh` chạy end-to-end; tarball cài trên box không lỗi |
| **B4** | `profiles/build.sh`: thêm `--clean`, kiểm tra dung lượng đĩa trước build, log rõ | P2 | 0.5 buổi | B1 | Build lại từ đầu bằng 1 lệnh; lỗi thiếu dung lượng báo trước khi build |

### Track C — Test, an ninh & độ bền · owner: Dev C

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **C1 / R5-4** | Mở rộng `test.sh`: health detached, quota detached, `exec`, bridge (khi có) | P0 | 1–2 buổi | A1 | ≥ 40 case, chạy 3 lần liên tiếp đều PASS, không rác cgroup/mount |
| **C2** | Soak test 24h: 1 container sleep + 1 container ăn RAM, đo RSS host, số cgroup, mount leak | P1 | 1 buổi code + 24h chạy | C1 | Báo cáo số liệu trong `VERIFICATION_LOG.md`; RSS host không tăng theo thời gian |
| **C3** | Security regression: `bomb`, `breakout`, mount, ptrace, `open_by_handle_at`; thử escape bằng userns | P1 | 1–2 buổi | — | Mọi vector bị chặn; ghi lại syscall + errno thực tế |
| **C4** | Test đa kiến trúc: chạy binary x86_64/armv7l trên máy thật/VM/QEMU | P2 | 1–2 buổi | B2 | Ghi rõ cái nào chạy thật, cái nào chỉ QEMU trong log |

### Track D — Tài liệu, demo & báo cáo · owner: Dev D

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **D1** | `README.md` bản tiếng Việt cho người mới: cài đặt, 5 lệnh đầu tiên, troubleshooting | P0 | 1 buổi | — | Người chưa biết project làm theo được trong ≤15 phút |
| **D2** | `profiles/README.md`: mô tả 4 profile, static vs dynamic, cách build/deploy | P0 | 0.5 buổi | B1 | Có bảng kích thước thực tế + lệnh copy-paste |
| **D3** | Demo script 5 phút (asciinema/video): chạy container, giới hạn RAM, quota, kill | P1 | 1 buổi | C1 | Video/script chạy lại được, không cắt ghép |
| **D4** | Cập nhật `BAOCAO.md`/slide theo số liệu v1.2.0 | P1 | 1 buổi | C2, B1 | Số liệu khớp `VERIFICATION_LOG.md`, không còn số cũ |

---

## 4. Mốc thời gian đề xuất

| Tuần | Mục tiêu | Người |
|---|---|---|
| **Tuần 1** | B1 (build 1 profile) + C1 (test mới) + D1/D2 + A2 (`exec`) bắt đầu | Khánh, Huy, C, D |
| **Tuần 2** | B2 (CI xanh) + B3 (release) + C2 (soak bắt đầu) + hoàn tất A2 | Huy, Khánh, C |
| **Tuần 3** | A3 (`--net=bridge`) + C4 (đa kiến trúc) + D3 (demo) | Khánh, C, D |
| **Tuần 4** | Đóng băng tính năng, chạy full test 3 lần, D4 (báo cáo), tag `v1.3.0` | Cả nhóm |

Quy tắc: task P0 trễ → cắt P2 trước, **không** cắt test.

---

## 5. Quy trình làm việc

### Bắt đầu
```bash
git clone https://github.com/NAMKHANH145/TV-Box-Container.git
cd TV-Box-Container/build
make                 # cần toolchain musl aarch64 (xem profiles/README.md)
make tools           # bomb + breakout cho test an ninh
```

### Trước khi mở PR
```bash
cd build && make clean && make && make tools
# deploy lên box rồi chạy toàn bộ test:
scp minicontainer bomb breakout root@<BOX_IP>:/usr/local/bin/   # hoặc /opt/rootfs
ssh root@<BOX_IP> 'bash /opt/test.sh'
```
- Không hard-code IP/password trong code, script hay commit. Dùng biến môi trường (`BOX=...`, `SSHPASS_FILE=...`).
- PR nhỏ, mỗi PR 1 task ID (A1, B2…), mô tả: **làm gì – test thế nào – số đo trước/sau**.
- Thay đổi hành vi ⇒ cập nhật `README.md` + `CHANGELOG.md` trong cùng PR.
- Không commit `build/buildroot/`, rootfs đã build, binary, log (đã có `.gitignore`).

### Definition of Done (áp dụng mọi task)
1. Code build sạch (`-Wall`, không warning mới).
2. Có test tự động hoặc bằng chứng đo trên box (ghi vào PR).
3. Chạy lại full suite: không giảm số PASS.
4. Không để rác: `cgroup`, mount, process, file trong `/run/minicontainer`.
5. Tài liệu/CHANGELOG cập nhật nếu người dùng thấy thay đổi.

---

## 6. Backlog đã xong (để không nhận trùng)

R1-1 signal forwarding · R1-2 validate input · R1-3 cảnh báo OCI · R2-1 disk quota · R3-1 test CPU/IO · R3-2 JSON output · R4-1 `stop --timeout` · R4-2 `--version` + docs · F9 PID reuse · dọn cgroup/mount/state (v1.1.0) · health check detached (v1.2.0) · 4 defconfig profile (validate 2026-10-29).

## 7. Việc chưa nên làm (tránh phân tâm)

- Viết lại bằng Rust/Go — mất lợi thế 114KB static.
- Thêm daemon/agent — phá vỡ thiết kế zero-daemon.
- Hỗ trợ đầy đủ OCI runtime spec — chỉ cần subset đang dùng.
- `--net=bridge` bản đầy đủ (NAT, port mapping) — làm bản tối thiểu trước, đo nhu cầu sau.
