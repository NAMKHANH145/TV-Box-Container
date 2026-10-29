# TASK.md — Phân chia công việc nhóm 4 người

> Cập nhật: 2026-10-30 · Phiên bản: **v1.4.0** · Test trên box: **74/74 PASS**
> Repo: <https://github.com/NAMKHANH145/TV-Box-Container>
> Tài liệu nền: `PLAN.md` (lộ trình), `GUIDE.md` (cách dùng), `FINAL.md` (kiến trúc),
> `progress/` (nhật ký phase + bằng chứng đo).

Tài liệu trả lời 3 câu: **dự án gồm module nào**, **mỗi người làm gì**, **khi nào coi là xong**.
Dự án đã hoàn thành phần runtime; §7 là phần việc còn lại đã chia theo track.

---

## 1. Trạng thái hiện tại (đọc trước khi nhận việc)

| Hạng mục | Trạng thái |
|---|---|
| Runtime (namespace, cgroup, seccomp, disk quota) | ✅ chạy thật trên TV Box S905W |
| 74 test case (`build/test.sh`) | ✅ **74/74 PASS** (2026-10-29, v1.4.0) |
| CLI `run/ps/stats/stop/rm/exec/oci-run/service/pull` | ✅ đủ |
| `--net=host\|isolated\|bridge` | ✅ bridge: 2 container ping nhau, HTTP 200, MQTT 3 container |
| `--read-only`, `pull`, `/dev` tự tạo, health detached | ✅ v1.3.0–v1.4.0 |
| 4 rootfs profile | ✅ base-mini 1.3 MB, web-proxy 2.6 MB, iot-gateway 14.1 MB, python-edge 22.2 MB |
| CI GitHub Actions | ✅ 4/4 arch musl (100.704–125.392 B), gate >200 KB fail (run `40857971212`) |
| Release `v1.3.0` | ✅ phát hành thật (run `40851398981`), asset aarch64 pass 55/55 trên box |
| Soak | 🟡 800 vòng/588 s: 0 lỗi 0 leak; **24 h chưa chạy** |

**Nguyên tắc:** mọi task phải có (1) code, (2) test trong `build/test.sh` hoặc test riêng,
(3) cập nhật tài liệu nếu thay đổi hành vi. Không nhận task chỉ "chạy được trên laptop".

---

## 2. Bản đồ module (14 module, ~2.209 dòng C)

| # | File | Dòng | Nhiệm vụ | Độ khó |
|---|---|---:|---|---|
| 1 | `src/main.c` | 91 | dispatch lệnh + `--version` | Dễ |
| 2 | `src/cli.c` | 46 | parse argv → `struct cfg`, validate | Dễ |
| 3 | `src/common.c/.h` | 96+116 | helper: `die`, `parse_bytes`, file I/O, `struct cfg` | Dễ |
| 4 | `src/ns.c` | 218 | clone child: userns map, chroot, `/dev`, `--read-only`, bridge config | **Khó** |
| 5 | `src/run.c` | 243 | supervisor: cgroup → clone → wait/detach → watchdog → dọn | **Khó** |
| 6 | `src/exec.c` | 124 | `setns` user→net/uts/ipc→mnt→pid, `O_PATH` root, fork, seccomp | **Khó** |
| 7 | `src/bridge.c` | 159 | `mcbr0` + veth, ioctl trong netns, `netns_lo_up()` | Trung bình |
| 8 | `src/cgroup.c` | 104 | v2: `memory.max/cpu.max/pids.max/io.max`, attach, cleanup | Trung bình |
| 9 | `src/disk.c` | 119 | quota userspace (nftw) + watchdog health | Trung bình |
| 10 | `src/seccomp.c` | 102 | BPF deny-list 21 syscall → `EPERM` | **Khó** |
| 11 | `src/oci.c` | 129 | đọc `config.json` (subset OCI) | Trung bình |
| 12 | `src/state.c` | 265 | `ps/stats/stop/rm` từ `/run/minicontainer/<id>` | Trung bình |
| 13 | `src/systemd.c` | 53 | sinh unit `service create` | Dễ |
| 14 | `src/pull.c` | 332 | tải/verify/giải nén profile an toàn | Trung bình |

Thứ tự đọc cho người mới: **main → cli → common → state → pull → run → cgroup → ns → bridge →
exec → seccomp → disk → oci → systemd**. `run.c` sớm vì nó gọi gần như mọi thứ; `ns.c`/`exec.c`
cuối vì chứa ràng buộc khó nhất (userns, setns, mount).

Ngoài `src/`:

```
build/            Makefile, test.sh (74 case), soak.sh, bench.sh, release.sh, bomb.c, breakout.c
profiles/         4 defconfig Buildroot + build.sh + README
.github/workflows/build.yml   CI 4 kiến trúc + release + SHA256SUMS theo tag
progress/         PHASE1..16.md + VERIFICATION_LOG.md
```

---

## 3. Chia việc — 4 track

Mỗi track có 1 owner chính. Bảng dưới là **phân công đã thực hiện** (dùng làm mẫu khi chia lại
hoặc khi bảo vệ: ai làm gì). Cột "Trạng thái" phản ánh kết quả thực tế.

### Track A — Core runtime (C) · owner: Khánh

| ID | Việc | Ưu tiên | Trạng thái |
|---|---|---|---|
| A1 | Health check cho `-d`: watchdog `--health-cmd` mỗi 5 s, marker `health_failed`, kill cgroup | P0 | ✅ |
| A2 | `exec <id> <cmd>` bằng `setns` thuần C (không `nsenter`) | P1 | ✅ 5 test |
| A3 | `--net=bridge`: `mcbr0` + veth, cấu hình bằng ioctl | P2 | ✅ 6 test, HTTP 200 |
| A4 | Rà seccomp cho arch mới, thêm syscall có test | P2 | ✅ filter dùng `__NR_*` portable |
| A5 | Bridge + NAT/DNS/port mapping ra LAN | P3 | ⬜ chưa làm (§7) |
| A6 | `--read-only` + tmpfs `/tmp` `/run` `/dev/shm` | P1 | ✅ 8 test |
| A7 | `pull <profile>` an toàn (sha256, chặn tar xấu, atomic) | P1 | ✅ 11 test |

### Track B — Rootfs, build & release · owner: Huy

| ID | Việc | Ưu tiên | Trạng thái |
|---|---|---|---|
| B1 | Build 3 profile động (web-proxy, python-edge, iot-gateway) | P0 | ✅ cả 3 chạy container thật |
| B2 | CI 4 arch xanh + release theo tag | P0 | ✅ `v1.3.0` phát hành; asset musl ở `v1.4.0` (chưa tag) |
| B3 | `release.sh` tarball + `SHA256SUMS.txt` | P1 | ✅ |
| B4 | `profiles/build.sh`: `--clean`, kiểm dung lượng trước build | P2 | ⬜ chưa làm |
| B5 | Cache toolchain CI / self-hosted runner | P2 | ⬜ chưa làm (§7) |

### Track C — Test, an ninh & độ bền · owner: Dev C

| ID | Việc | Ưu tiên | Trạng thái |
|---|---|---|---|
| C1 | Mở rộng `test.sh` lên 74 case | P0 | ✅ 74/74, chạy 3 lần liền xanh |
| C2 | Soak 24 h | P1 | 🟡 script xong; 800 vòng/588 s đã có; 24 h chưa chạy |
| C3 | Security regression: bomb, breakout, mount, ptrace, `open_by_handle_at` | P1 | ✅ mọi vector bị chặn |
| C4 | Test đa kiến trúc | P2 | ✅ aarch64 thật; x86_64 native; armv7l/riscv64 QEMU |

### Track D — Tài liệu, demo & báo cáo · owner: Dev D

| ID | Việc | Ưu tiên | Trạng thái |
|---|---|---|---|
| D1 | Hướng dẫn cho người mới (nay là `GUIDE.md`) | P0 | ✅ |
| D2 | `profiles/README.md`: 4 profile, static vs dynamic, cách build | P0 | ✅ |
| D3 | Demo 5 phút (asciinema/video) | P1 | ⬜ chưa làm |
| D4 | Báo cáo/số liệu khớp bằng chứng (nay là `FINAL.md`) | P1 | ✅ |
| D5 | Gộp 26 tài liệu → 4 file + `progress/` | P1 | ✅ 2026-10-30 |

---

## 4. Mốc thời gian đã đi qua

| Tuần | Mục tiêu | Người |
|---|---|---|
| 1 | B1 (1 profile) + C1 (test mới) + D1/D2 + bắt đầu A2 (`exec`) | Khánh, Huy, C, D |
| 2 | B2 (CI xanh) + B3 (release) + C2 (soak) + xong A2 | Huy, Khánh, C |
| 3 | A3 (bridge) + C4 (đa kiến trúc) + D3 (demo) | Khánh, C, D |
| 4 | Đóng băng tính năng, full test 3 lần, D4 (báo cáo), tag `v1.3.0` | Cả nhóm |
| 5 | A6/A7 (`--read-only`, `pull`), CI musl, 74 test, gộp tài liệu | Khánh, Huy |

Quy tắc: task P0 trễ → cắt P2 trước, **không** cắt test.

---

## 5. Quy trình làm việc

### Bắt đầu

```bash
git clone https://github.com/NAMKHANH145/TV-Box-Container.git
cd TV-Box-Container/build
make            # cần toolchain musl aarch64 (xem profiles/README.md)
make tools      # bomb + breakout cho test an ninh
```

### Trước khi mở PR

```bash
cd build && make clean && make && make tools
make deploy BOX=<IP_BOX> SSHPASS_FILE=<file>   # hoặc scp thủ công
ssh root@<IP_BOX> 'cd /root && bash test.sh'
scp bomb breakout root@<IP_BOX>:/opt/rootfs/   # test.sh cần 2 file này trong rootfs
```

- **Không hard-code IP/password** trong code, script hay commit — dùng `BOX=`, `SSHPASS_FILE=`.
- PR nhỏ, mỗi PR một task ID (A1, B2…), mô tả: **làm gì – test thế nào – số đo trước/sau**.
- Thay đổi hành vi ⇒ cập nhật `GUIDE.md` (và `FINAL.md` nếu đổi kiến trúc) trong cùng PR.
- Không commit `build/buildroot/`, rootfs đã build, binary, log (đã có `.gitignore`).

### Definition of Done (áp dụng mọi task)

1. Build sạch `-Wall`, không warning mới.
2. Có test tự động hoặc bằng chứng đo trên box (ghi vào PR).
3. Chạy lại full suite: không giảm số PASS.
4. Không rác: cgroup, mount, process, file trong `/run/minicontainer`.
5. Tài liệu cập nhật nếu người dùng thấy thay đổi.

---

## 6. Backlog đã xong (để không nhận trùng)

R1-1 signal forwarding · R1-2 validate input · R1-3 cảnh báo OCI · R2-1 disk quota ·
R3-1 test CPU/IO · R3-2 JSON output · R4-1 `stop --timeout` · R4-2 `--version` ·
F9 PID reuse · dọn cgroup/mount/state (v1.1.0) · health detached (v1.2.0) · 4 defconfig profile ·
`exec` + bridge + `/dev` tự tạo + soak + release checksum (v1.3.0) · `--read-only` + `pull` +
CI musl + 74 test (v1.4.0).

## 7. Việc còn lại (đã chia track)

| # | Việc | Track | Ghi chú |
|---|---|---|---|
| 1 | Tag + phát hành `v1.4.0` | B | `bash release.sh` rồi tag; CI dựng asset musl; sau đó tải asset aarch64 chạy `test.sh` đóng R5-1. **Cần Khánh quyết** (shared state) |
| 2 | Soak 24 h | C | `ITER=117000 bash build/soak.sh` (1.36 vòng/s) — ghi số vào `progress/VERIFICATION_LOG.md` |
| 3 | Verify asset musl 4 arch trên phần cứng thật | C | mới aarch64 build local |
| 4 | Self-hosted runner aarch64 chạy `test.sh` trước release | B | đăng ký runner là thao tác shared state |
| 5 | `mc-sdk init/pack` | A | `pack` tạo `<profile>-rootfs.tar.gz` đúng định dạng `pull` nhận (member tương đối, giữ `+x`); `init` sinh skeleton |
| 6 | LICENSE | D | chọn MIT hoặc Apache-2.0; README đang ghi "to be decided" |
| 7 | Bridge NAT + DNS + port mapping | A | cân nhắc chi phí CPU trên S905W |
| 8 | Demo 5 phút | D | asciinema/video, chạy lại được, không cắt ghép |

## 8. Việc chưa nên làm (tránh phân tâm)

- Viết lại bằng Rust/Go — mất lợi thế binary ~125 KB static.
- Thêm daemon/agent — phá vỡ zero-daemon.
- Hỗ trợ đầy đủ OCI runtime spec — chỉ giữ subset đang dùng.
- Bridge bản đầy đủ (NAT/port mapping) trước khi có nhu cầu thật.

---

## 9. Bàn giao (handoff)

Không còn process nền (soak/bench/test) chạy trên laptop hay box; `gh run watch` đã kết thúc.
Local working tree sạch, `main` đã push.

### Bối cảnh máy

- Laptop `/home/namkhanh/TV-Box`: ~808 MB (đã dọn từ 7.4 GB). `build/buildroot/output/host/bin`
  là toolchain musl mà `build/Makefile` mặc định dùng — **không được xoá**.
- Box S905W: `/tmp` 32 KB, `/var` 333 MB, đĩa 23 %.
- Kết nối: `BOX=<IP> SSHPASS_FILE=<file>` (không hard-code — xem `GUIDE.md` §6).
- Ảnh `muslcc/x86_64:*` 4 arch đã cache trong Docker local (~10 s/arch khi rebuild).

### Đọc gì trước

1. `PLAN.md` — vì sao làm, 4 giai đoạn.
2. `FINAL.md` — kiến trúc, bẫy kernel, bằng chứng.
3. `GUIDE.md` — build/deploy/dùng CLI.
4. `TASK.md` (file này) — ai làm gì, còn gì.
5. `progress/` — chi tiết từng phase khi cần truy nguyên.