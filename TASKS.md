# TASKS — Chia việc nhóm minicontainer

> Cập nhật: 2026-10-29 · Phiên bản hiện tại: **v1.4.0** · Test trên box: **74/74 PASS**
> Repo: <https://github.com/NAMKHANH145/TV-Box-Container>
> Tài liệu nền: `README.md` (cách dùng), `PLAN-4.md` (lộ trình R1–R5), `VERIFICATION_LOG.md` (bằng chứng đo trên S905W).

Tài liệu này trả lời 3 câu hỏi: **dự án có những module nào**, **mỗi người đọc gì / làm gì**, và **khi nào coi là xong**.

---

## 1. Trạng thái hiện tại (đọc trước khi nhận việc)

| Hạng mục | Trạng thái |
|---|---|
| Runtime (namespace, cgroup, seccomp, disk quota) | ✅ Chạy thật trên TV Box S905W |
| 74 test case (`build/test.sh`) | ✅ **74/74 PASS** (2026-10-29, v1.4.0) — thêm `--read-only` (8) và `pull` (11) |
| Disk quota userspace `--disk` | ✅ Foreground + detached + RLIMIT_FSIZE |
| OCI bundle (subset) | ✅ `oci-run`, có cảnh báo key ngoài subset |
| CLI (`run/ps/stats/stop/rm/exec`) | ✅ Đủ, `exec` dùng `setns` (`src/exec.c`) |
| `--net=bridge` | ✅ `mcbr0` + veth, 2 container ping nhau, test trong `test.sh` |
| `/dev` trong container | ✅ tmpfs + bind node (rootfs động không có `/dev/null`) |
| Health check chế độ `-d` | ✅ Watchdog dùng chung với `--disk`, marker `health_failed` |
| Rootfs profiles (4 bản) | ✅ Cả 4 build + chạy thật trên box: base-mini 1.3 MB, web-proxy 2.6 MB, iot-gateway 14.1 MB, python-edge 22.2 MB |
| CI GitHub Actions | ✅ 4/4 arch xanh; release theo tag `v1.3.0` (run `40851398981`) kèm `SHA256SUMS.txt` — tải asset về verify, bản aarch64 chạy `test.sh` **55/55** trên box |
| `--read-only` rootfs | ✅ Bind ro + tmpfs `/tmp`,`/run`,`/dev/shm`; bỏ qua symlink `/var/log`; fail rõ nếu rootfs ở tmpfs |
| `pull <profile>` | ✅ curl/wget/local path, check dung lượng, `--sha256`, chặn tar độc hại, rename atomic |
| Soak test 24h | 🟡 Dữ liệu dài nhất 800 vòng/588s: 0 lỗi, 0 leak; smoke 12 vòng có cycle `--read-only` OK. **24h để Khánh chạy riêng**: `ITER=117000 bash soak.sh` |

**Nguyên tắc:** mọi task phải có (1) code, (2) test trong `build/test.sh` hoặc test riêng, (3) cập nhật `README/CHANGELOG` nếu thay đổi hành vi. Không nhận task nào chỉ "chạy được trên laptop".

---

## 2. "10 module là tất cả rồi à?" — Có 14 module

Đúng là ít, vì đây là runtime tối giản chứ không phải Docker. Mỗi module một việc, tổng ~2.209 dòng C (`.c` + `common.h`):

| # | File | Dòng | Nhiệm vụ | Độ khó đọc |
|---|---|---:|---|---|
| 1 | `src/main.c` | 91 | `main()`, dispatch command (`run/ps/stats/stop/rm/exec/oci-run/service`), `--version` | Dễ |
| 2 | `src/cli.c` | 46 | Parse argv → `struct config`, validate số (`--ram`, `--cpu`…) | Dễ |
| 3 | `src/common.c/.h` | 96 + 116 | Helper dùng chung: log, `die_msg`, `parse_bytes`, validate tên/đường dẫn, `struct bridge_cfg` | Dễ |
| 4 | `src/ns.c` | 218 | `clone()` 6 namespace, map userns, chroot, **tạo `/dev`**, **`--read-only` (bind ro + tmpfs)**, bridge trong netns | **Khó** |
| 5 | `src/run.c` | 243 | Vòng đời container: setup cgroup → clone → bridge → supervisor → tín hiệu → health/disk | **Khó** |
| 6 | `src/exec.c` | 124 | `exec <id>`: `setns` user→net/mnt→pid, `O_PATH` root, fork, seccomp, attach cgroup | **Khó** |
| 7 | `src/bridge.c` | 159 | `mcbr0` + veth pair (`ip` ở host), đặt tên/IP/route bằng ioctl trong netns | Trung bình |
| 8 | `src/cgroup.c` | 104 | Ghi `memory.max`, `cpu.max`, `pids.max`, `io.max`; kill/cleanup cgroup v2 | Trung bình |
| 9 | `src/disk.c` | 119 | Đo dung lượng rootfs (nftw), watchdog quota/health, marker | Trung bình |
| 10 | `src/seccomp.c` | 102 | BPF deny-list 21 syscall nguy hiểm, trả `EPERM` | **Khó** |
| 11 | `src/oci.c` | 129 | Đọc `config.json` (subset OCI), cảnh báo key lạ | Trung bình |
| 12 | `src/state.c` | 265 | `ps/stats/stop/rm`, state trong `/run/minicontainer/<id>/` (kèm `ip`) | Trung bình |
| 13 | `src/systemd.c` | 53 | Sinh unit systemd để tự restart container, vẫn zero-daemon | Dễ |
| 14 | `src/pull.c` | 332 | `pull <profile>`: tải (curl/wget/local), check dung lượng, verify `--sha256`, chặn tar độc hại, stage + rename | Trung bình |

Thứ tự đọc đề xuất cho người mới: **main → cli → common → state → pull → run → cgroup → ns → bridge → exec → seccomp → disk → oci → systemd**. Đọc `run.c` sớm vì nó gọi gần như mọi thứ; đọc `ns.c`/`exec.c` sau cùng vì hai file này chứa các ràng buộc khó nhất (userns, setns, mount).

### Cấu trúc ngoài `src/`

```
build/            Makefile, test.sh (74 case), soak.sh, bench.sh, release.sh,
                  bomb.c/breakout.c (test an ninh)
profiles/         4 defconfig Buildroot + build.sh
.github/workflows/build.yml   CI 4 kiến trúc + release kèm SHA256SUMS theo tag
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
| **A2 / R5-3b** | ~~Lệnh `exec <id> <cmd>`~~ ✅ **DONE 2026-10-29** (`src/exec.c`, `setns` thuần C, không dùng `nsenter`): vào user→net/mnt→pid, `O_PATH` `/proc/<pid>/root`, fork, seccomp, attach cgroup. 5 test trong `test.sh`. | P1 | 2–3 buổi | A1 | `exec` chạy được trong container đang chạy; ghi rõ giới hạn userns trong README |
| **A3 / R5-3c** | ~~`--net=bridge`~~ ✅ **DONE 2026-10-29** (`src/bridge.c`): `mcbr0` 10.88.0.1/16 + veth `mcv<pid>`/`mcc<pid>`, host dùng `ip`, container tự đặt tên/IP/route bằng ioctl. 6 test trong `test.sh`, lighttpd↔python3 HTTP 200 qua bridge. | P2 | 3–5 buổi | Kernel box có `veth`, `bridge` | 2 container ping nhau qua bridge; test trong `test.sh`; fallback báo lỗi rõ nếu kernel thiếu module |
| **A6 / R5-3** | ~~Mount rootfs Read-Only + tmpfs~~ ✅ **DONE 2026-10-29** (`src/ns.c: ro_setup()`): bind rootfs ro trong mount ns private, tmpfs `/tmp` 32M / `/run` 8M / `/dev/shm` 64M, bỏ qua path là symlink (`/var/log -> ../tmp`), fail rõ khi rootfs nằm trên tmpfs. 8 test trong `test.sh`. | P1 | 1 buổi | A1 | `--read-only`: ghi `/etc` → EROFS, ghi `/tmp` OK, host rootfs không bị ảnh hưởng, 0 mount leak |
| **A7 / R5-2** | ~~Lệnh `pull <profile>`~~ ✅ **DONE 2026-10-29** (`src/pull.c`): tải curl/wget/local path, check dung lượng đích, `--sha256`, từ chối tar có thành viên tuyệt đối/`..`, stage + rename atomic, `--force`. 11 test. | P1 | 1 buổi | B1 | `pull web-proxy --url=… --dest=/opt/rootfs-web-proxy` rồi `run --rootfs` chạy được | P3 | 1–2 buổi | A3 | `--net=bridge` có `--publish`/forwarding, test ping 8.8.8.8 từ container |
| **A5** | `--net=bridge` + NAT/DNS để container ra ngoài LAN (chỉ khi có nhu cầu thật) | P3 | 1–2 buổi | A3 | `--net=bridge` có `--publish`/forwarding, test ping 8.8.8.8 từ container |
| **A4** | Rà lại seccomp cho arch mới, thêm syscall vào deny-list có test | P2 | 1 buổi | — | `breakout` fail trên cả 4 arch; không ảnh hưởng 74 test hiện có |

### Track B — Rootfs, build & release · owner: Huy

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **B1 / R5-2** | ~~Build thật 3 profile còn lại~~ ✅ **DONE 2026-10-29**: `web-proxy` 3 MB (lighttpd 1.4.85), `python-edge` 24 MB (Python 3.14.8 + pyserial/spidev), `iot-gateway` 15 MB (Mosquitto 2.1.2) — cả 3 chạy container thật trên box, broker pub/sub đi qua `mcbr0` giữa 3 container | P0 | 2–4 buổi (máy yếu lâu hơn) | `profiles/*.defconfig` đã validate | `rootfs.tar.gz` chạy được `minicontainer run`; đo kích thước/RAM thực tế, ghi vào `profiles/README.md` |
| **B2 / R5-1** | CI 4 arch xanh + release theo tag `v1.3.0` (run `40842042612`, `40851398981`) ✅. Còn: cache toolchain (tải musl-cross từ `musl.cc` + `actions/cache` để asset release nhỏ như bản musl 137 KB thay vì 729 KB) | P0 | 0.5 buổi | Repo đã push | Release job theo tag chạy xanh (**xong**); toolchain cache bật, thời gian build giảm |
| **B3 / R4-3** | ~~Đóng gói release tarball~~ ✅ (asset release đã chạy thật: 4 binary + `test.sh` + `SHA256SUMS.txt`, verify trên box) **DONE 2026-10-29**: `release.sh` đọc version từ `src/main.c`, đóng binary + test/soak/bench + profiles + rootfs + `SHA256SUMS.txt`; CI gắn `SHA256SUMS.txt` vào release theo tag | P1 | 0.5 buổi | B2 | `release.sh` chạy end-to-end; tarball cài trên box không lỗi |
| **B4** | `profiles/build.sh`: thêm `--clean`, kiểm tra dung lượng đĩa trước build, log rõ (đã sửa đường dẫn toolchain + `chmod +x`) | P2 | 0.5 buổi | B1 | Build lại từ đầu bằng 1 lệnh; lỗi thiếu dung lượng báo trước khi build |

### Track C — Test, an ninh & độ bền · owner: Dev C

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **C1 / R5-4** | ~~Mở rộng `test.sh`~~ ✅ **DONE**: 74 case (exec 5, bridge 6, `/dev` 4, `lo` 1, `--read-only` 8, `pull` 11); **74/74 PASS** trên box, trước đó 3 lần liên tiếp 54/54 | P0 | 1–2 buổi | A1 | ≥ 40 case, chạy 3 lần liên tiếp đều PASS, không rác cgroup/mount |
| **C2** | Soak test 24h. 🟡 Script **DONE** (6 cycle, có `--read-only`); số liệu dài nhất 800 vòng/588s: 0 lỗi, 0 leak. **24h Khánh chạy lúc khác**: `ITER=117000 bash soak.sh` (`HOUR=24` chỉ ra ~15h vì tốc độ thật 1.36 vòng/giây) | P1 | 24h chạy | C1 | Báo cáo số liệu trong `VERIFICATION_LOG.md`; RSS host không tăng theo thời gian |
| **C3** | Security regression: `bomb`, `breakout`, mount, ptrace, `open_by_handle_at`; thử escape bằng userns | P1 | 1–2 buổi | — | Mọi vector bị chặn; ghi lại syscall + errno thực tế |
| **C4** | ~~Test đa kiến trúc~~ ✅ **DONE 2026-10-29**: aarch64 trên box thật (55/55 với asset release), x86_64 native, armv7l + riscv64 dưới `qemu-user` (`--version`). Chưa có board thật cho 3 arch sau — ghi rõ trong `VERIFICATION_LOG.md` | P2 | 1–2 buổi | B2 | Ghi rõ cái nào chạy thật, cái nào chỉ QEMU trong log |

### Track D — Tài liệu, demo & báo cáo · owner: Dev D

| ID | Việc | Ưu tiên | Ước lượng | Phụ thuộc | Điều kiện xong |
|---|---|---|---|---|---|
| **D1** | `README.md` bản tiếng Việt cho người mới: cài đặt, 5 lệnh đầu tiên, troubleshooting | P0 | 1 buổi | — | Người chưa biết project làm theo được trong ≤15 phút |
| **D2** | `profiles/README.md`: mô tả 4 profile, static vs dynamic, cách build/deploy | P0 | 0.5 buổi | B1 | Có bảng kích thước thực tế + lệnh copy-paste |
| **D3** | Demo script 5 phút (asciinema/video): chạy container, giới hạn RAM, quota, kill | P1 | 1 buổi | C1 | Video/script chạy lại được, không cắt ghép |
| **D4** | ~~Cập nhật `BAOCAO.md`/slide~~ ✅ **DONE 2026-10-29**: số liệu v1.4.0 (74/74, exec/bridge/read-only/pull, 4 profile, release 4 arch). Chỉ còn soak 24h (C2) chưa có số | P1 | 1 buổi | C2, B1 | Số liệu khớp `VERIFICATION_LOG.md`, không còn số cũ |

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

R1-1 signal forwarding · R1-2 validate input · R1-3 cảnh báo OCI · R2-1 disk quota · R3-1 test CPU/IO · R3-2 JSON output · R4-1 `stop --timeout` · R4-2 `--version` + docs · F9 PID reuse · dọn cgroup/mount/state (v1.1.0) · health check detached (v1.2.0) · 4 defconfig profile (validate 2026-10-29) · `exec` qua setns + `--net=bridge` + `/dev` tự tạo + soak script + release checksum (v1.3.0).

## 7. Việc chưa nên làm (tránh phân tâm)

- Viết lại bằng Rust/Go — mất lợi thế 114KB static.
- Thêm daemon/agent — phá vỡ thiết kế zero-daemon.
- Hỗ trợ đầy đủ OCI runtime spec — chỉ cần subset đang dùng.
- `--net=bridge` bản đầy đủ (NAT, port mapping) — làm bản tối thiểu trước, đo nhu cầu sau.
