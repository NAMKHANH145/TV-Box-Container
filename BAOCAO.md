# BAOCAO.md — Báo cáo tổng hợp: Hệ thống quản lý container siêu nhẹ trên TV Box

Đề tài môn Hệ điều hành — Amlogic S905W (4×Cortex-A53 @1.2GHz, 1.8GB RAM, eMMC 12GB, kernel 6.18.38-meson64).
Sản phẩm (v1.4.0): `minicontainer` **136,712 B** musl static, ~2.200 dòng C / 14 module + 4 rootfs profile (**1.3 MB → 22.2 MB**).
Zero-daemon, 6 namespaces, 5 controllers cgroup v2, seccomp 21 syscall,
CLI `run/ps/stats/stop/rm/exec/oci-run/service/pull`, `--net=bridge`, `--read-only`, CI 4 kiến trúc + release theo tag.
Test: **74/74 PASS** trên box thật (2026-10-29).

---

## 1. Kiến trúc hệ thống

### 1.1. Triết lý thiết kế

- **Zero-daemon**: không có `minicontainerd`. Mỗi lần `run` là 1 process cha → clone → exec; kết thúc lệnh, mọi tài nguyên dọn sạch. Khi idle: 0 process, 0MB.
- **Một syscall cho mỗi cơ chế**: clone (namespaces), chroot/pivot_root (filesystem), mount (proc), write cgroupfs (limits), execvp (chuyển quyền).
- **Không qua runc/crun/containerd**: gọi thẳng kernel qua libc. Binary musl static → không phụ thuộc loader host.
- **Rootfs là thư mục, không phải image**: người dùng tự chuẩn bị (Buildroot tự build hoặc Alpine baseline). Khớp triết lý UNIX: 1 tool làm 1 việc.

### 1.2. Sơ đồ tổng thể

```
[laptop build] --cross compile--> minicontainer (137KB, aarch64 musl static)
       | scp
       v
[TV Box /opt/rootfs]  <-- pivot_root --  container process (UID 0 trong, nobody ngoài)
       |
       +-- /proc  (PID ns riêng, PID=1)
       +-- /dev   (null/zero/urandom node trong image)
       +-- /sys   (trống — kernel cấm sysfs trong userns)
       |
       +-- cgroup v2: /sys/fs/cgroup/minicontainer-<pid>-<n>
             memory.max / cpu.max / pids.max / io.max  + cgroup.procs
       +-- netns bridge: mcbr0 10.88.0.1/16 + veth mcv<pid>/mcc<pid>
```

### 1.3. Các thành phần (14 module, 1 binary)

| Module | Trách nhiệm |
|---|---|
| `common` | die/parse_bytes/file I/O, struct cfg |
| `seccomp` | BPF deny-list 21 syscall |
| `ns` | clone child, chroot, userns map, mount make-private, tạo `/dev`, `--read-only` |
| `cgroup` | setup/attach/cleanup v2 |
| `run` | điều phối clone → map → go → detach/wait |
| `state` | ps/stats/stop/rm (không daemon) |
| `exec` | `setns()` vào container đang chạy (user→net/mnt→pid), seccomp + cgroup kế thừa |
| `bridge` | `mcbr0` + veth, đặt tên/IP/route bằng ioctl trong netns, bring-up `lo` |
| `disk` | quota userspace (nftw) + watchdog health `--health-cmd` |
| `pull` | tải profile (curl/wget/local), check dung lượng, verify sha256, chặn tar độc hại |
| `systemd` | sinh unit `service create` (restart khi crash, vẫn zero-daemon) |
| `cli` | parse flags (2 dạng) |
| `oci` | bundle mode subset |
| `main` | dispatch |
| `Makefile` | make/tools/deploy/clean |

---

## 2. Use case cụ thể hệ thống hướng đến

**Bối cảnh**: TV Box làm gateway biên cho IoT — thu thập cảm biến, chạy MQTT broker/script Python nhỏ, RAM 1–2GB, không có daemon container runtime.

**Use case 1 — Chạy service nhẹ cô lập**:
```bash
minicontainer run --ram 64M --cpu 50 --pids 128 --rootfs /opt/rootfs /usr/bin/mosquitto
```
Service chạy PID=1 trong ns riêng, RAM/CPU bị siết, host không bị ảnh hưởng.

**Use case 2 — Test script không làm bẩn host**:
```bash
minicontainer run --rootfs /opt/alpine /bin/sh -c "rm -rf /bin"   # host /bin nguyên vẹn
```

**Use case 3 — Chạy nền + giám sát**:
```bash
minicontainer run -d --name sensor --ram 30M /opt/sensor.py
minicontainer ps; minicontainer stats sensor; minicontainer stop sensor
```

**Use case 4 — Chạy bundle OCI có sẵn**:
```bash
minicontainer oci-run /opt/oci-test
```

**Người dùng mục tiêu**: sinh viên/nhà phát triển nhúng, không phải hệ thống production đa tenant.

---

## 3. Đi sâu về thiết kế hệ thống

### 3.1. Namespaces (6 loại, CLONE_*)

- `CLONE_NEWPID` → con có PID=1, không thấy tiến trình host (`ps` chỉ thấy nội bộ).
- `CLONE_NEWNS` → mount tree riêng (make-private `MS_REC|MS_PRIVATE` đầu tiên để không lan ra host).
- `CLONE_NEWUTS` → hostname riêng (`minicontainer-os` vs `tvbox`).
- `CLONE_NEWIPC` → shared memory isolated.
- `CLONE_NEWNET` → chỉ bật khi `--net=isolated` (mất mạng hoàn toàn, đúng thiết kế).
- `CLONE_NEWUSER` → map `0 65534 1`: root trong, nobody ngoài.

**Bẫy kernel quan trọng**: thứ tự bắt buộc `clone(NEWPID)` → con mount lại `/proc` (proc cũ trỏ cây PID cũ). Nếu mount trước clone thì `/proc` vẫn thấy host.

### 3.2. pivot_root vs chroot

`chroot` chỉ đổi cách giải quyết path → root có kỹ thuật chroot-break (`chroot` vào subdir → `chdir ../../..` → `chroot .`). `pivot_root` đổi hẳn root mount của namespace + detach root cũ. Test A/B trên box: chroot cũ **thủng**, pivot **chặn**.

Điều kiện kernel: new_root phải là mount point riêng → `mount --bind rootfs rootfs` trước. Mount `/proc` phải làm **trước** pivot (sau pivot bị EPERM trên kernel này). `/sys` bị cấm hoàn toàn trong userns → bỏ, cách ly mạnh hơn cả Docker.

### 3.3. Cgroups v2 (5 controllers)

Box chỉ có v2 (v1 toàn 0 hierarchy). API mapping từ đề cương gốc:

| Đề cương (v1) | Code thực tế (v2) |
|---|---|
| `memory.limit_in_bytes` | `memory.max` |
| `cpu.shares` | `cpu.max` dạng `$MAX $PERIOD` |
| `tasks` | `cgroup.procs` |
| `/sys/fs/cgroup/memory/minicontainer` | `/sys/fs/cgroup/minicontainer-<pid>-<n>` |

Controllers: **memory** (hard limit → OOM-killer), **cpu** (quota %), **pids** (chống fork-bomb), **io** (bóp tốc độ whole-disk, fallback partition→minor 0), **cpuset** (available, chưa dùng).

Code tự ghi `+cpu +memory +pids +io` vào `cgroup.subtree_control` khi start (ghi lại no-op, an toàn qua reboot).

### 3.4. Seccomp-BPF deny-list (21 syscall)

Viết tay, không libseccomp. Trả `EPERM` (không kill) để app còn chạy được. Danh sách: mount/umount2/pivot_root, reboot/kexec, init/finit/delete_module, ptrace, clock_settime/adjtime/settimeofday, bpf/perf_event_open/fanotify_init, name_to_handle_at/open_by_handle_at, keyctl/add_key/request_key. Kèm `NO_NEW_PRIVS` (chặn suid leo thang).

**Bug tự bắn vào chân**: đặt filter trước setup → filter chặn chính `mount()` của setup → `make-private /: EPERM`. Fix: đặt filter ngay trước `execvp`.

### 3.5. User namespace (3 bẫy kernel)

1. Con trong userns là nobody ngoài host → `mkdir .oldroot` bị từ chối → parent (host-root) tạo trước, dọn sau.
2. Mount sau pivot bị EPERM → dồn mount trước pivot; `/sys` bị cấm → bỏ.
3. `mknod` trong userns bị EPERM/EOVERFLOW → `/dev` phải có sẵn trong image (giống Docker: node tạo lúc build image).
4. Bẫy tinh vi: quên `setuid(0)` → con mang host-uid 0 (ngoài map) → nobody vô dụng. Phải `setgid(0); setuid(0)` sau go-signal.

Kết quả: trong là root, ngoài host là nobody (UID 65534). File tạo ra owned nobody. Thoát namespace cũng chỉ là nobody.

### 3.6. Mount propagation

Systemd để mount propagation mặc định `shared` → mount trong container lan ngược ra host (mỗi lần chạy để lại 1 `/proc` trên `/opt/rootfs/proc`). Fix: `MS_REC|MS_PRIVATE` ngay sau clone. Regression: chạy 3 lần, mount leak = 0.

### 3.7. Zero-daemon detach

`run -d`: launcher tạo cgroup, clone, ghi state vào `/run/minicontainer/<id>/`, in id rồi **thoát ngay** (không `waitpid`). Con mồ côi về init. State đọc trực tiếp từ filesystem + cgroupfs — `ps/stats/stop/rm` không cần daemon. `stop` TERM cả cgroup, 3s sau KILL sót.

**Bug**: con thừa hưởng pipe stdout của session → SSH treo đến khi con chết. Fix: con detach `dup2(/dev/null)` 0,1,2 trước exec.

---

## 4. Quá trình thực hiện (16 phase)

| Phase | Nội dung | Kết quả chính |
|---|---|---|
| 1 | Planning + Buildroot rootfs (P0–P3) | Toolchain native, Alpine baseline, rootfs 1.3MB, chroot OK |
| 2 | Implement I1–I5 (clone/chroot/cgroup/CLI) | T1–T5/T7/T9 pass, bug parser + truncation đã sửa |
| 3 | Testing mở rộng + SOTA 2026 | 2 rootfs pass, nhiệt 52→54°C, reboot OK, Docker 213MB/2.076s |
| 4 | P2-1: pids.max + NEWIPC | Fork-bomb bị chặn, IPC isolated |
| 5 | P2-2: io.max | 20MB/s → 1MB/s đúng |
| 6 | P2-6: /dev + /sys + fix mount leak | tmpfs fresh, read-only sys, 0 leak |
| 7 | P2-11: điều tra reboot chậm | Box vô tội, Tailscale NAT 4.5 phút |
| 8 | P2-3: pivot_root | Test A/B: chroot thủng, pivot chặn |
| 9 | P2-4: userns | Trong root/ngoài nobody, 3 bẫy + setuid |
| 10 | P2-5: seccomp | 21 syscall EPERM, latency không tăng |
| 11 | P2-8: musl | 658KB → 57KB |
| 12 | P2-7: detach + stats | Zero-daemon quan sát |
| 13 | CLI ps/stop/rm | Vòng đời đầy đủ |
| 14 | P2-10: test.sh | 21/21 xanh |
| 15 | P2-9: OCI mode | Bundle đọc được, limit từ JSON |
| 16 | Dọn rác + tách module | 10 module, Makefile, 21/21 |
| 17 | Đợt R1–R2 (PLAN-4) | `--json` cho ps/stats, `--timeout`, disk quota userspace `--disk`, Ctrl-C foreground |
| 18 | Đợt R3–R4 (PLAN-4) | seccomp theo kiến trúc, `stop/rm` dùng `cgroup.kill`, `--version`, README/CHANGELOG, release tarball |
| 19 | Đợt R5 a (v1.2–v1.3) | health check detached, `/dev` trong container, `exec` (setns), `--net=bridge`, 4 profile build thật, CI 4 arch + release tag |
| 20 | Đợt R5 b (v1.4) | `--read-only` chống mòn eMMC + tmpfs, `pull <profile>`, 74/74 test, verify asset release trên box thật |

---

## 5. Số liệu so sánh với SOTA 2026

| Runtime | Ngôn ngữ | Daemon nền | Idle RAM | Start 1 container | Chuẩn |
|---|---|---|---|---|---|
| **minicontainer v1.4.0** | C (musl), 137KB | không có (0MB) | **0MB** | **0.010–0.065s** | rootfs là thư mục + tar profile |
| Docker (trên box) | Go | dockerd+containerd | **207MB** | **2.076s** | OCI |
| crun | C | cần stack trên | ~KBs | ~47ms create→delete | OCI full |
| runc | Go | cần stack trên | ~MBs | ~224ms | OCI full |
| youki | Rust | cần stack trên | thấp | ~111ms | OCI full |
| gVisor/Kata | sandbox/VM | nặng | lớn | penalty 40–60% TPS | OCI, cách ly mạnh |

**Kết luận**: nhanh hơn Docker trên cùng box **~33–207 lần**, nhẹ hơn nền **vô hạn lần** (207MB → 0MB), tiệm cận crun dù code chỉ ~2.200 dòng C. Không cạnh tranh OCI (không chạy image Docker/k8s) — đó là đánh đổi chủ đích.

---

## 6. Điểm mạnh

1. **Zero-daemon đo được**: 0 process + 0MB khi idle; Docker tốn 207MB trên cùng box.
2. **Boot 30–65ms**, nhanh hơn `docker run` cùng box 33–207 lần.
3. **4 rootfs profile tự build** (base-mini 1.3 MB → python-edge 22.2 MB), phân phối bằng `minicontainer pull` (sha256 + kiểm tra dung lượng + chặn tar độc hại).
4. **Cgroup v2 hard limit thật**: OOM-killer giết đúng ngưỡng 30MB, CPU cap đúng 20%, fork-bomb bị chặn ở 16.
5. **Chạy được trên kernel mod sơ sài** nơi Podman còn không cài nổi (không dbus/systemd/slirp).
6. **Nhiệt full-load 1 core chỉ +2°C** (52→54°C) — an toàn 24/7 không tản nhiệt chủ động.
7. **3 lớp phòng thủ xếp chồng**: pivot_root + userns (nobody) + seccomp 21 syscall.
8. **Binary 137KB** — nhẹ hơn dockerd ~950 lần; CI cross-compile 4 kiến trúc, release theo tag kèm `SHA256SUMS.txt`.
9. **Test tự động 74/74** (trước đó 21→39→55), mount leak = 0, cgroup/state/veth = 0 sau mỗi lần chạy; soak 800 vòng liên tục 0 lỗi.
10. **OCI-bundle mode** đọc được bundle chuẩn (args/hostname/rootfs/limits verified).
11. **Cách ly thật sự giữa container**: `exec` vào container đang chạy (PID≠1, uid 0, seccomp + cgroup kế thừa), `--net=bridge` để 2 container tự thấy nhau (HTTP 200 lighttpd↔python, MQTT pub/sub 3 container qua `mcbr0`).
12. **Chống mòn thẻ nhớ**: `--read-only` đóng băng rootfs, mọi ghi rơi vào tmpfs (`/tmp`, `/run`, `/dev/shm`).

---

## 7. Điểm yếu (trung thực)

### Cố hữu (by design, có lập luận)
- Không registry/image format (tarball profile tự build là thay thế gần nhất); không NAT/DNS cho `--net=bridge` (container ra LAN cần host route 10.88/16); không log driver (stdout vào `/run/minicontainer/<id>/log`); deny-list không phải allow-list; không `/sys`; không volumes/bind-mount; chưa có `mc-sdk` đóng gói app; board thật mới có aarch64 (armv7l/riscv64/x86_64 mới verify bằng QEMU).

### Đã khắc phục trong PLAN-3/PLAN-4 (đều có test)
- F1 Ctrl-C để lại con mồ côi → forward tín hiệu + `cgroup.kill`.
- F2 chưa quota dung lượng → `--disk` watchdog userspace (1s) + `RLIMIT_FSIZE`.
- F3 validate đầu vào → `--ram=abc`, `--net=routable`, tên lạ bị từ chối.
- F4 phủ `--cpu`/`--io` tự động → có trong `test.sh` (T5-auto, Tio-auto).
- F5 → `ps --json`, `stats --json` (state rỗng là `[]`).
- F6 → `stop --timeout N`.
- F7 → `--version` theo arch/libc + README + CHANGELOG.
- F8 → `oci-run` cảnh báo key ngoài subset.
- F9 → tên cgroup/state kèm PID, `stop`/`rm` retry rmdir + marker `.oldroot`.
- Không có `exec`/mạng → thêm `exec` (setns), `--net=isolated|bridge`, `lo` tự UP, `/dev` tự tạo, `--read-only`, `pull`.

---

## 8. Kết luận

Hệ thống **đạt mục tiêu đề tài**: tự gọi syscall Linux (clone/chroot/pivot/mount/sethostname/execvp/waitpid) để xây container siêu nhẹ, áp dụng đúng lý thuyết môn Hệ điều hành (Process Management, Memory Management, File System, Namespaces, Cgroups), chạy được trên phần cứng hạn chế, zero-daemon, có số liệu đo đạc thật và test tự động. 16 phase thực hiện, mỗi phase có khảo sát → can thiệp → đánh giá → ghi lại. Tổng artifact: ~20 file markdown + 14 module C (2.209 dòng) + Makefile + `test.sh` (74 case) + `soak.sh` + `bench.sh` + `release.sh` + 4 defconfig profile + CI 4 kiến trúc.
