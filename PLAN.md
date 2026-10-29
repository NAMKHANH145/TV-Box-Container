# PLAN.md — Kế hoạch phát triển minicontainer

> Tài liệu này là **lịch sử kế hoạch**: vì sao làm, khảo sát phần cứng, và 4 giai đoạn phát triển
> (PLANNING → IMPLEMENT → TESTING → RELEASE) cùng trạng thái thực tế của từng mục.
> Tài liệu liên quan: `GUIDE.md` (cách dùng), `FINAL.md` (kiến trúc chi tiết), `TASK.md` (chia việc 4 người),
> `progress/` (nhật ký từng phase + log kiểm chứng theo phiên bản).

**Trạng thái hiện tại: v1.4.0** — 14 module C (2.209 dòng), binary musl static ~125 KB (aarch64),
`test.sh` **74/74 PASS** trên TV Box thật. Toàn bộ mục P/I/T/R bên dưới đã đóng trừ những mục được
ghi rõ trong §9.

Môi trường mục tiêu (đo thực tế, xem §3): Amlogic S905W, 4× Cortex-A53 @1.2 GHz, 1.8 GB RAM không swap,
eMMC 14.7 GB, Ubuntu 26.04 LTS, kernel `6.18.38-meson64`, cgroup **v2 only**, không có gcc/make trên box.

---

## 1. Lý do chọn đề tài

### 1.1. Áp dụng lý thuyết môn Hệ điều hành vào hệ thống thực tế

Sinh viên thường dùng Docker như blackbox. Đề tài tự gọi trực tiếp các system call
`clone()`, `chroot()`, `mount()`, `setns()`, `prctl(seccomp)` để hiểu kernel cấp phát, kiểm soát
và thu hồi tài nguyên của một tiến trình.

### 1.2. Bài toán tối ưu tài nguyên trên phần cứng nhúng

TV Box 1–2 GB RAM làm gateway: Docker/containerd chạy nền ăn 50–100 MB ngay cả khi rảnh
(đo trên chính box này: ~300 MB AnonPages cho portainer + samba). Một CLI zero-daemon giải phóng
hoàn toàn phần RAM đó.

### 1.3. Nghiên cứu cơ chế bảo mật và giới hạn tài nguyên của kernel

Hai cơ chế trọng tâm: **namespaces** (đánh lừa tiến trình về hệ điều hành riêng) và **cgroups**
(siết trần RAM/CPU/PID/IO). Bổ sung seccomp và user namespace để tăng bề mặt bảo mật.

### 1.4. Tính khả thi

Một công cụ CLI bằng C, không client-server, không chạy ngầm: gõ lệnh → kernel tạo môi trường cô lập →
tiến trình chạy → thoát → kernel dọn sạch. Không cần runtime trung gian (runc/crun).

### 1.5. Điểm mới: tự build rootfs bằng Buildroot

Không dùng Alpine tải sẵn làm sản phẩm. Rootfs được **tự build bằng Buildroot trên laptop**
(target aarch64 Cortex-A53, BusyBox static + sh/ls/ps), `scp` sang box tại `/opt/rootfs`.
Alpine mini chỉ giữ làm baseline đối chứng. Như vậy nhóm làm cả "khách" (rootfs) lẫn "chủ" (runtime),
và Ubuntu trên box nguyên vẹn (mọi thứ nặng diễn ra trên laptop).

---

## 2. Các giải pháp tương tự và khoảng trống

### 2.1. Docker / containerd

Kiến trúc daemon-based: `dockerd` + `containerd` phải chạy liên tục, tốn 50–100 MB RAM và
sinh chu kỳ đánh thức CPU. Trên thiết bị 1 GB, mất 10% RAM chỉ để giữ công cụ quản lý là lãng phí.

### 2.2. Podman

Không daemon, nhưng phụ thuộc hệ sinh thái nặng: runc/crun, slirp4netns, dbus, systemd.
Kernel TV Box thường bị lược module → cài Podman hay thất bại.

### 2.3. LXC / LXD

Khởi chạy cả một hệ điều hành thu nhỏ (cron, syslog, udev) → độ trễ lớn trên chip ARM yếu.

### 2.4. Research gap

Đã có dự án giáo dục (bocker bằng bash, demo C của kỹ sư kernel) nhưng chỉ dừng ở minh họa API
hoặc là công cụ nội bộ của hãng. Khoảng trống: một **CLI micro-engine hoàn chỉnh**, tối ưu cho
TV Box + kernel mod, kèm tài liệu hệ thống hóa.

#### 2.4.1. Ưu điểm của hướng tiếp cận

- Chạy như một lệnh thường; không service nền, không RAM thường trú.
- Không runtime trung gian; gọi thẳng syscall → binary vài chục KB, chạy trên kernel cũ không
  cần thư viện phụ thuộc (musl static).
- Bề mặt tính năng nhỏ, dễ đọc, dễ kiểm chứng bằng chính kernel (`/proc`, `/sys/fs/cgroup`).

#### 2.4.2. Nhược điểm và phản biện

**2.4.2.1. Không pull image kiểu Docker Hub.**
Phản biện: HTTPS + JSON + tar/gzip sẽ biến binary thành hàng chục MB. Người dùng mục tiêu tự build
rootfs bằng Buildroot hoặc nạp qua USB. Thực tế v1.4.0 bổ sung `pull` **nhưng theo triết lý UNIX**:
gọi `curl`/`wget` bên ngoài, chỉ làm phần xác thực (`--sha256`, chặn path tuyệt đối/`..`) và giải nén
an toàn — binary vẫn ~125 KB, không nhúng TLS stack.

**2.4.2.2. Không có mạng ảo phức tạp (NAT/DNS).**
Phản biện: MQTT broker, script cảm biến cần mạng vật lý hoặc IPC cục bộ. NAT bằng iptables tốn CPU
trên S905W. Mặc định `--net=host` (zero-cost), `--net=isolated` để ngắt mạng, và `--net=bridge`
(v1.3.0) chỉ làm phần tối thiểu cần cho container-to-container: bridge `mcbr0` + veth + IP tĩnh
10.88/16, **không NAT, không DNS** — bù lại độ trễ gói tin ~0.5 ms.

### 2.5. So sánh định lượng (đo trên box)

| Tiêu chí | Docker 29.7.2 | minicontainer v1.4.0 |
|---|---|---|
| RAM khi rảnh | ~300 MB (portainer + samba) / 50–100 MB daemon trần | **0 MB** (không process nền) |
| Kích thước công cụ | ~207 MB (engine) | **125 KB** (musl static aarch64) |
| Độ trễ `run` → shell | ~2.076 s | **10–65 ms** |
| Cgroup | v1/v2 qua containerd | ghi trực tiếp cgroup v2 |
| Runtime phụ thuộc | runc, containerd, systemd | không |

---

## 3. Khảo sát phần cứng (2026-10-06)

Đo trực tiếp qua SSH trên box (`root@<IP_BOX>` — IP lấy từ `BOX=` env, không lưu trong repo).

| Hạng mục | Thông số thực đo |
|---|---|
| Board | Amlogic Meson GXL (S905W) P281 Development Board |
| SoC / CPU | S905W, 4× Cortex-A53 64-bit, 100/250/500/667/1000/1200 MHz; có `aes pmull sha1 sha2 crc32` |
| Cache | L1 128 KB ×2 loại, L2 512 KB chung (working set của tool nằm gọn → khởi động ms) |
| RAM | MemTotal 1.848.204 kB, **không swap** → vượt RAM là OOM-kill ngay (cơ sở an toàn cho test T4) |
| Lưu trữ | eMMC 16 GB Toshiba 016GE2; `/boot` 512 MB vfat + `/` 11.3 GB ext4, trống ~7.9 GB |
| `/tmp` | tmpfs 903 MB (nằm trên RAM) → rootfs lâu dài đặt ở `/opt/rootfs` (eMMC) |
| Mạng | `eth0` meson8b-dwmac 100 Mbps full; **không wifi**; Tailscale |
| GPU | Mali-450, driver `lima`, `/dev/dri` có card0/card1/renderD128 (không dùng trong đề tài) |
| OS / kernel | Ubuntu 26.04 LTS, `6.18.38-meson64` aarch64 |
| Container nền | cgroup **v2 only**, đủ 10 namespace, Docker 29.7.2 + portainer + samba |
| Nhiệt độ idle | 48 °C, load ~0.45 |

### 3.1. Kernel hỗ trợ gì (điểm quyết định)

| Cơ chế | Trạng thái trên box | Kết luận |
|---|---|---|
| Namespaces | Đủ 10 loại; `unshare` hỗ trợ `-m -u -i -n -p -U -C -T` | `clone()` với NEWPID/NEWNS/NEWUTS/NEWNET/NEWIPC/NEWUSER chạy được |
| Cgroups | Chỉ v2 (`cgroup2 on /sys/fs/cgroup`), controller `cpu memory io pids hugetlb cpuset rdma` | Code **phải** dùng API v2 |
| Seccomp / userns | Có | Đã dùng trong v1.2.0 |
| Toolchain | Box **không có** gcc/make | Cross-compile trên laptop |
| Module mạng | `bridge veth xt_MASQUERADE nf_nat` đã load (nhờ Docker) | Đủ cho `--net=bridge`, chưa cần NAT |
| ext4 project quota | **Không có** | `--disk` phải làm bằng userspace watchdog |

### 3.2. Ba điều chỉnh bắt buộc so với đề xuất ban đầu

1. **Cgroup v1 → v2.** Bảng ánh xạ API (dùng xuyên suốt trong code):

   | Đề xuất gốc (v1) | Thực tế trên box (v2) |
   |---|---|
   | `mkdir /sys/fs/cgroup/memory/minicontainer` | `mkdir /sys/fs/cgroup/minicontainer-<pid>` |
   | `memory.limit_in_bytes` = 31457280 | `memory.max` = `31457280`, thêm `memory.swap.max` = 0 |
   | `cpu.shares` (trọng số tương đối) | `cpu.max` = `"$QUOTA $PERIOD"` (vd `20000 100000` = 20% × 1 CPU) |
   | `tasks` | `cgroup.procs` |
   | — | `pids.max` (giới hạn số tiến trình) |
   | — | `io.max` (`MAJ:MIN rbps=… wbps=…`) |
   | bật controller qua mount option | ghi `+cpu +memory +pids +io` vào `cgroup.subtree_control` của cha |
   | OOM qua `dmesg` | `dmesg` **hoặc** đếm `memory.events:max` |
   | — | `cgroup.kill` (giết sạch cây, v1 không có) |

2. **`CLONE_NEWNET` phải tách thành flag.** Đề xuất gốc vừa NEWNET vừa nói "dùng chung mạng host" —
   mâu thuẫn. Chốt: mặc định **không** NEWNET (`--net=host`); `--net=isolated` bật NEWNET không cấu hình
   gì (container mất mạng, đúng nghĩa "ngắt"); `--net=bridge` (v1.3.0) bật NEWNET + veth/bridge.
   Mọi netns mới phải `netns_lo_up()` — `lo` mặc định DOWN khiến mọi bind 127.0.0.1 thất bại.

3. **`chroot` thay cho `pivot_root`.** Đã thử `pivot_root` (P2-5): dưới user namespace trên kernel này
   luôn `EPERM`. Chốt dùng `chroot` + `chdir("/")` trong mount ns riêng tư (`MS_REC|MS_PRIVATE` trên `/`).
   Xem `FINAL.md` §5.

Các bẫy kernel khác đã gặp và cách xử lý (chi tiết ở `FINAL.md` §6): `uid_map 0 65534 1` cho userns,
`setgroups=deny` trước khi ghi gid_map, `memfd_create` cần `MFD_CLOEXEC`, mount `/dev` phải bind node
từ host (mknod bị từ chối trong userns), `/var/log -> ../tmp` trong Buildroot (mount đè lên symlink sẽ
che `/tmp`), remount read-only trên tmpfs trả `EPERM` (phải fail loudly).

---

## 4. Giai đoạn 1 — POC (PLANNING + IMPLEMENT + TESTING)

Mục tiêu: chứng minh 1 lệnh `run` cô lập + giới hạn được, trên rootfs tự build.

### 4.1. PLANNING

| Mã | Nội dung | Trạng thái |
|---|---|---|
| P0 | Chốt scope: `run --ram 30M [--cpu 20] [--net=host\|isolated] --rootfs <dir> <cmd>`; không pull, không mạng ảo | [x] |
| P1 | Buildroot target aarch64 Cortex-A53 (BusyBox static + sh/ls/ps) → tar vài MB → `/opt/rootfs`; Alpine mini làm baseline | [x] |
| P2 | Chuyển bảng v1→v2 (§3.2) thành hằng số/thư viện trong code để tài liệu và code khớp nhau | [x] |
| P3 | Chia 4 giai đoạn (cô lập → filesystem → cgroup → CLI), mỗi bước test thủ công bằng `unshare` trước khi viết C | [x] |

### 4.2. IMPLEMENT

| Mã | Nội dung | Trạng thái |
|---|---|---|
| I0 | Buildroot ra `rootfs.tar`, giải nén `/opt/rootfs`, verify `chroot … /bin/sh` | [x] |
| I1 | Khung CLI + `getopt`: `--ram --cpu --rootfs --net` (chưa namespace) | [x] |
| I2 | `clone()` + stack `malloc()` + `sethostname()` + `execvp()` → shell PID=1 trong PID/UTS ns | [x] |
| I3 | `chroot` + `chdir` + `mount proc` + `umount` khi thoát | [x] |
| I4 | Cgroup v2: `memory.max`, `cpu.max`, `cgroup.procs`, bật controller ở cha; lỗi rõ ràng nếu kernel thiếu | [x] |
| I5 | `waitpid()` + dọn dẹp: umount `/proc`, rmdir cgroup | [x] |
| I6 | `--net=isolated` (NEWNET) và user ns + uid_map | [x] — `isolated` đạt; userns chuyển sang P2-4 |

### 4.3. TESTING

| Test | Kịch bản | Tiêu chí | Code |
|---|---|---|---|
| T1 | `run … /bin/sh` → `ps aux`, `echo $$` | `$$`=1, 2 dòng, không thấy systemd/sshd | I0+I2+I3 |
| T2 | `touch /etc/hack.txt`, `rm -rf /bin` trong container | Host nguyên vẹn 100% | I3 |
| T3 | `hostname` trong/ngoài | trong `minicontainer-os`, ngoài `tvbox` | I2 |
| T4 | `--ram 30M` + memory bomb 50 MB | bomb bị kill, `dmesg` có cgroup OOM, host không lag | I4 |
| T5 | `--cpu 20` + `while(1)` | không vượt ~20% 1 CPU | I4 |
| T6 | Reboot, không chạy gì | 0 process, 0 MB nền | thiết kế |
| T7 | `time run … /bin/true` vs `docker run` | ms, nhanh hơn Docker rõ rệt | pipeline |
| T8 | `exit` → kiểm tra cgroup/mount/ps | không cgroup dir, không mount thừa, không zombie | I5 |
| T9 | `docker stats` vs T6 | số liệu đối chứng cho báo cáo | §2.1 |
| T10 | `du -sh` + chạy lại T1–T3 với Alpine | rootfs ≤ ~10 MB, cô lập đạt trên **cả hai** rootfs | I0 |

Thứ tự: T1→T2→T3 → T10 → T4→T5 → T6→T7→T8→T9. Fail thì quay về đúng bước I, không sửa lan man.
**Kết quả thực tế:** 21/21 test thủ công đạt ở v1.0.0 (lịch sử trong `progress/PHASE*.md`).

---

## 5. Giai đoạn 2 — Hardening cho sản phẩm (P2)

Mục tiêu: từ "POC chạy được" thành engine dùng được hằng ngày trên box.

| Mã | Nội dung | Trạng thái |
|---|---|---|
| P2-1 | `pids.max` + `CLONE_NEWIPC` (+ `/dev/shm`) | [x] |
| P2-2 | `io.max` giới hạn throughput đĩa cho container | [x] |
| P2-3 | Mount `/dev` (bind node từ host) và tuỳ chọn `--sys` mount sysfs read-only | [x] |
| P2-4 | User namespace + uid_map (`0 65534 1`) để chạy container không cần root thật | [x] |
| P2-5 | Thử `pivot_root` → **bị chặn** dưới userns (`EPERM`), chốt `chroot` | [x] (kết luận) |
| P2-6 | Seccomp BPF deny-list 21 syscall trả `EPERM` + `NO_NEW_PRIVS` | [x] |
| P2-7 | Giảm binary 658 KB (glibc) → 57 KB (musl static) | [x] |
| P2-8 | `run -d` (detach) + lưu state + `ps`, `stop`, `rm`, `logs`, `stats` | [x] |
| P2-9 | `test.sh` tự động (suite tăng dần lên 54 test) | [x] |
| P2-10 | Đọc manifest OCI (`config.json`) → chạy image OCI layout | [x] |
| P2-11 | Điều tra SSH chậm ~5 phút sau reboot | [x] (tài liệu hoá, xem GUIDE) |

Kèm theo: `service create` sinh unit systemd để chạy container như service; `--disk` watchdog nftw
(kernel không có ext4 project quota); health check cho container detached.

---

## 6. Giai đoạn 3 — Đánh giá giới hạn và hoàn thiện (P3)

Mục tiêu: biết rõ **cái gì cố tình không làm được** và **cái gì còn thiếu**, có test cho từng tầng.

### 6.1. Điểm yếu theo thiết kế (chấp nhận)

| Mã | Điểm yếu | Lý do chấp nhận |
|---|---|---|
| I1 | Không pull từ registry, không layer/overlay | Giữ binary nhỏ; `pull` chỉ tải tar + verify |
| I2 | Không NAT/DNS, IP tĩnh 10.88/16 | Đủ cho demo container-to-container, không tốn CPU NAT |
| I3 | Không port < 1024 trong container | mapped root thiếu `CAP_NET_BIND_SERVICE` — dùng port ≥ 1024 |
| I4 | Không có SELinux/AppArmor profile | seccomp + userns + cgroup là đủ cho mục tiêu đồ án |
| I5 | Không checkpoint/restore, không live-migrate | ngoài scope |
| I6 | `--disk` là watchdog userspace (1 s) | kernel box không có project quota |
| I7 | Không hỗ trợ multi-arch trong 1 rootfs | mỗi board một rootfs |

### 6.2. Việc cần làm thêm (F) — đã đóng

| Mã | Nội dung | Trạng thái |
|---|---|---|
| F1 | `--read-only` (remount bind + tmpfs `/tmp`, `/run`, `/dev/shm`) | [x] v1.4.0 |
| F2 | `pull <profile>` (curl/wget, sha256, chặn tar path xấu, atomic rename) | [x] v1.4.0 |
| F3 | `exec <id>` (setns user→net/uts/ipc→mnt→pid, seccomp + cgroup giữ nguyên) | [x] v1.3.0 |
| F4 | `--net=bridge` + `netns_lo_up()` | [x] v1.3.0 |
| F5 | Tự tạo `/dev` trong container khi rootfs thiếu | [x] v1.3.0 |
| F6 | CI musl (Docker Hub `muslcc/x86_64:*`) + gate >200 KB fail | [x] v1.4.0 |
| F7 | `soak.sh` (vòng lặp kiểm tra rác mỗi 20 vòng) | [x] |
| F8 | `bench.sh` (đo đối chứng Docker) | [x] |
| F9 | 4 profile Buildroot (base-mini, web-proxy, iot-gateway, python-edge) | [x] |

### 6.3. Năm tầng kiểm thử

1. **Unit thủ công** — từng syscall bằng `unshare`/`chroot` (giai đoạn 1).
2. **`test.sh` tự động** — 74 case trên box thật (mỗi PR phải xanh).
3. **Soak** — hàng trăm vòng run/stop/rm, kiểm tra rò cgroup/mount/state (`build/soak.sh`).
4. **Ứng dụng thật** — lighttpd HTTP 200, MQTT pub/sub 3 container, Python `serial/spidev` import.
5. **Đa kiến trúc** — aarch64 chạy thật trên box; x86_64 native; armv7l/riscv64 qua QEMU-user
   (chỉ chứng minh binary nạp + parse lệnh; container thật cần một board/VM của chính kiến trúc đó).

---

## 7. Giai đoạn 4 — Release (R)

| Mã | Nội dung | Trạng thái |
|---|---|---|
| R1 | `build/release.sh` đóng gói tarball + SHA256SUMS | [x] |
| R2 | Changelog, hướng dẫn, báo cáo | [x] → gộp vào 4 tài liệu chính |
| R3 | CI GitHub Actions build 4 arch + release theo tag | [x] — `v1.3.0` phát hành thật, run `40851398981` |
| R4 | Release tarball kèm checksum, test lại binary từ asset (`55/55` trên box) | [x] |
| R5 | `--read-only`, `pull`, `exec`, bridge, health, profile | [x] v1.3.0–v1.4.0 |
| R5-1 | Asset chuyển sang musl (100–125 KB thay vì 443–932 KB) | [x] run `40857971212` |

### 7.1. Định hướng sản phẩm (không chặn release)

- **mc-sdk**: thư viện C để nhúng minicontainer vào ứng dụng khác (chưa làm).
- **Self-hosted runner** cho CI: runner GitHub hiện bị musl.cc chặn (đã workaround bằng Docker Hub);
  self-hosted sẽ build nhanh và không phụ thuộc mạng ngoài.
- **Board khác** (armv7l/riscv64 thật): hiện chỉ QEMU-user.
- **Bridge nâng cao**: NAT ra Internet, DNS nội bộ, port mapping.
- **LICENSE**: chưa chốt (MIT vs Apache-2.0).
- **`--disk` chính xác hơn**: dùng project quota nếu kernel đích có.

---

## 8. Lịch sử phiên bản

| Version | Nội dung chính | Ngày |
|---|---|---|
| 1.0.0 | POC: namespaces + chroot + cgroup v2 + CLI + test thủ công 21/21 | 2026-10-06 |
| 1.1.0 | pids/io/NEWIPC, `/dev`+`--sys`, userns, seccomp, musl 57 KB, detach/state/stats, `test.sh` | 2026-10 |
| 1.2.0 | OCI manifest, `service create` (systemd unit), health check, `--disk` watchdog | 2026-10 |
| 1.3.0 | `exec`, `--net=bridge` + `netns_lo_up`, tự tạo `/dev`, 54→55 test, asset musl CI | 2026-10-29 |
| 1.4.0 | `--read-only`, `pull`, 74/74 test, CI asset musl 100–125 KB | 2026-10-29 |

(Chi tiết theo từng phase: `progress/PHASE1.md` … `progress/PHASE16.md`; bằng chứng đo: `progress/VERIFICATION_LOG.md`.)

---

## 9. Việc còn lại (không chặn release)

| # | Việc | Ghi chú |
|---|---|---|
| 1 | Tag `v1.4.0` và chạy release job | User quyết định thời điểm; CI đã xanh với `workflow_dispatch` |
| 2 | Soak 24 h | `ITER=117000 bash build/soak.sh` (~1.36 vòng/s). Đã có 800 vòng/588 s, 0 lỗi 0 leak |
| 3 | Verify asset musl trên phần cứng thật | Mới aarch64 build local (Buildroot toolchain) chạy `test.sh`; asset tải từ GitHub chưa chạy lại |
| 4 | `mc-sdk` | Thư viện nhúng — xem §7.1 |
| 5 | LICENSE | Chọn MIT hoặc Apache-2.0 |
| 6 | Bridge NAT + port < 1024 | Cần `CAP_NET_BIND_SERVICE`/iptables — cân nhắc chi phí CPU |