# AGENTS.md — Bàn giao dự án minicontainer

> Đọc file này trước khi làm bất cứ gì. Đây là dự án container runtime zero-daemon bằng C cho
> TV Box Amlogic S905W (aarch64, 1.8 GB RAM, Ubuntu 26.04, kernel `6.18.38-meson64`).
> Trạng thái: **v2.0.0** (P0–P4 + P6 đã commit trên `main`, **chưa tag**) · `test.sh` **183 case**:
> box S905W **182 PASS / 0 FAIL / 1 SKIP**, laptop x86_64 **176 PASS / 0 FAIL / 7 SKIP** (đo
> 2026-11-17) · gate P6.4 **10/10** (5 recipe × 2 máy; box aarch64 chạy gateway compose thật, laptop
> `curl` sang được) · binary aarch64 musl **284.208 B** · v1.5.4 (2026-11-12) là mốc đã phát hành ·
> phần **hardware-only** của v2.0 (watchdog thật, 5 reboot, p50, soak 24 h, corpus 6 image) **chưa
> verify** — box đã có, dùng `BOX=<IP_BOX>`.

## 1. Thứ tự đọc (khoảng 25 phút)

1. `README.md` — một trang tổng quan.
2. `PLAN.md` — vì sao làm, khảo sát phần cứng, 4 giai đoạn (POC → hardening → đánh giá → release),
   lịch sử phiên bản, §9 việc còn lại.
3. `FINAL.md` — kiến trúc đầu-cuối: 14 module, luồng syscall của `run`, bẫy kernel (§7), bằng chứng (§8).
4. `GUIDE.md` — build/deploy/dùng từng lệnh CLI, profile, test, troubleshooting.
5. `TASK.md` — phân chia 4 track, Definition of Done, handoff.
6. `progress/` — chỉ mở khi cần truy nguyên: `PHASE1..16.md`, `VERIFICATION_LOG.md`, `CHANGELOG.md`,
   `PLAN-2/3/4.md` (kế hoạch cũ đã đóng).

Đừng đọc hết `progress/` trước; nó là kho lịch sử, không phải trạng thái hiện tại.

## 2. Cây làm việc

```
build/src/*.c        24 module (main, cli, common, ns, namespace_setup, run, exec,
                     bridge, cgroup_v2, disk, seccomp, oci, state, systemd, pull, sdk,
                     log, preflight, update, img, layer, json, build, ship) — ~6.000 dòng
                     (+ 5 file seccomp_strict_*.c sinh tự động, 638 dòng mỗi file)
build/Makefile       make / make tools / make deploy BOX=… SSHPASS_FILE=… / make clean
build/test.sh        183 case, phải xanh trước mọi PR (cần box + root)
build/local-test.sh  chạy test.sh trên host Linux thường (cần root + /bin/busybox)
build/smoke-sdk.sh   11 check sdk init/pack, không cần root/box — chạy cả trên CI
build/soak.sh        ITER=… (24 h ≈ ITER=117000), kiểm rò cgroup/mount/state
build/bench.sh       độ trễ + so sánh Docker
build/release.sh     tarball + SHA256SUMS, version đọc từ src/main.c
profiles/            4 defconfig Buildroot + build.sh [--clean] (kiểm dung lượng trước build)
.github/workflows/   CI 4 arch musl + smoke-sdk (push main/PR) + release theo tag
progress/            lịch sử + bằng chứng
```

Entry point code: `build/src/main.c` (dispatch) → `run.c` (supervisor) → `ns.c` (thân container).
Phụ thuộc hai chiều duy nhất là qua `common.h`.

## 3. Quy ước bắt buộc (không vi phạm)

- **Không hard-code IP hay mật khẩu** trong code, script, tài liệu hay commit. Dùng biến môi trường
  `BOX=`, `SSHPASS_FILE=`, `MC_PULL_URL=`. Trong tài liệu dùng placeholder `<IP_BOX>`.
- **Không force-push**, không sửa git config, không `git add -A` (stage theo tên file).
- Không commit binary, rootfs đã build, `build/buildroot/`, log (`.gitignore` đã có).
- Không xoá `build/buildroot/output/host/bin` — đó là toolchain musl mà `build/Makefile` dùng mặc định.
- Thao tác cần xác nhận trước: tag/release (shared state). Self-hosted runner và LICENSE đã xong.

## 4. Vòng kiểm chứng (làm y như cũ, đừng đoán)

```bash
cd build && make clean && make && make tools          # phải 0 warning

# deploy (cần BOX và SSHPASS_FILE trong env, không ghi ra repo)
make deploy BOX=<IP_BOX> SSHPASS_FILE=<file>
scp bomb breakout root@<IP_BOX>:/opt/rootfs/          # test.sh cần $BR/bomb, $BR/breakout
scp test.sh soak.sh root@<IP_BOX>:/root/

ssh root@<IP_BOX> 'cd /root && bash test.sh'          # 74/74 PASS
```

- Không có quyền truy cập box? Vẫn build được và chạy `test.sh` một phần bằng QEMU cho các lệnh
  `--version`/`ps`/validate, nhưng **không** được tuyên bố container chạy được: namespace + cgroup
  cần root thật.
- Sau reboot, SSH có thể im ~5 phút (Tailscale double-NAT). Chờ, đừng rút điện.
- Mọi số liệu mới phải ghi vào `progress/VERIFICATION_LOG.md` (hoặc bổ sung file mới trong `progress/`),
  không bịa số.

## 5. Sự thật kỹ thuật dễ sai (đọc kỹ)

- Kernel box chỉ có **cgroup v2**: `memory.max`, `cpu.max` (`"$QUOTA $PERIOD"`), `pids.max`,
  `io.max`, `cgroup.procs`, `cgroup.kill`. Không có API v1.
- Root switch dùng **`chroot`**, không phải `pivot_root`. `pivot_root` đã thử và bị `EPERM` dưới
  userns trên kernel này (FINAL.md §6.2). Đừng "sửa" sang pivot_root mà không test lại.
- Thứ tự setup trong `ns.c` là bắt buộc: `setgid/setuid(0)` → `MS_REC|MS_PRIVATE /` → bind ro (nếu
  `--read-only`) → `chroot/chdir` → `mount /proc` → `/dev` → cấu hình netns → remount ro → seccomp → exec.
  Seccomp **phải** đặt ngay trước `execvp`, nếu không nó chặn chính `mount` của setup.
- `--net=isolated|bridge`: `lo` phải được UP (`netns_lo_up()`), nếu không mọi bind 127.0.0.1 fail.
- `--read-only`: bỏ qua path là symlink (`/var/log -> ../tmp`); rootfs trên tmpfs → remount ro `EPERM`
  → phải fail loudly.
- Port < 1024 không bind được trong container (mapped root không có `CAP_NET_BIND_SERVICE`).
- CI dùng image Docker Hub `muslcc/x86_64:<target>` (musl.cc chặn IP runner GitHub); image không có
  `make` → gọi `gcc -static -O2 -Wall` trực tiếp; gate fail nếu binary > 300 KB
  (mốc này chỉ để bắt nhầm glibc static 620 KB+; musl hiện ~237 KB).
- Asset release tải về **mất bit `+x`** → phải `chmod +x`.
- Signal handler của `run.c` chỉ được làm 2 việc async-signal-safe: ghi `sig_atomic_t` và `kill(child)`.
  Không `fopen`/`rmdir`/`usleep`/`cgroup_cleanup` trong handler (deadlock nếu signal đến giữa
  `malloc`/stdio); dọn dẹp làm sau `waitpid` trong luồng bình thường.
- `sdk pack` kiểm tra symlink **lexically** (không `realpath`): target tuyệt đối luôn hợp lệ, target
  tương đối chỉ bị từ chối khi đi lên quá gốc rootfs. Đừng đổi sang "chặn mọi `..`" — sẽ chặn cả
  `usr/bin/x -> ../../bin/y` của Buildroot/Alpine.
- `-p HOST:CONTAINER` cần **3 thứ**, thiếu một là host tự `curl 127.0.0.1:HOST` timeout:
  DNAT ở `PREROUTING` (client ngoài), DNAT ở `OUTPUT` (client là chính host), và
  `net.ipv4.conf.mcbr0.route_localnet=1` (chỉ trên `mcbr0`, không đụng `all`) + MASQUERADE cho
  source `127/8` sang bridge. Đo được: `route_localnet=0` → `curl=000`, bật trên mcbr0 → `200`.
- User namespace 1-uid: `setgroups` bị `deny` nên mọi `initgroups()`/`setgid()` để "drop privileges"
  đều `EPERM`. Image đa tiến trình phải chạy trên mapped root (nginx: `user root;` +
  `master_process off;`; mosquitto: `user root`). Chown sang uid khác cũng `EINVAL`. Vì chỉ map
  đúng 1 uid (`0 -> 65534`), `--user <uid khác 0>` **không dùng được** (trả lỗi rõ ràng); đừng
  "sửa" bằng cách nới `uid_map` khi chưa có subuid delegation.
- Vài image không có thư mục `/proc` (vd `busybox:latest`) → mountpoint `/proc` **phải** được tạo ở
  phía host (`namespace_prepare_filesystem`) trước khi vào userns; trong userns thì `mkdir` trên
  rootfs thuộc host-root trả `EACCES`.
- Image thiếu `/etc/resolv.conf` làm `socket.getfqdn()` của Python chặn ~5 s trước khi `listen`
  (`HTTPServer.server_bind`), dễ bị đọc nhầm là "container không chạy".

## 6. Việc tiếp theo (theo thứ tự)

1. Soak 24 h: `ITER=117000 bash build/soak.sh` trên box, ghi kết quả vào `progress/VERIFICATION_LOG.md`.
2. `bash test.sh` trên box đã chạy 2026-11-17: **182 PASS / 0 FAIL / 1 SKIP / 183** (SKIP là
   `P3.8 --emulate`). Còn lại: Ctrl-C 20 lần liền để kiểm đường dọn dẹp.
   **Không chạy test.sh đồng thời với soak.sh** (assertion cleanup là toàn cục, sẽ FAIL oan).
3. v2.0.0 đã xong P0–P4 + P6, suite xanh trên cả 2 máy, nhưng **chưa tag**: còn phần hardware-only
   (`/dev/watchdog` thật, reboot 5 lần, p50, 6-image corpus), rồi `bash build/release.sh` + tag.
   Bản `v1.5.4` tag 2026-11-12 (84/84, soak 6000) là mốc cuối đã phát hành.
   Lưu ý: `v1.5.3` có CI build đỏ (trùng `install_filter_strict`) — dùng `v1.5.4` trở đi.
4. Verify 4 asset musl trên phần cứng thật (hiện chỉ aarch64 build local); asset mới có kèm
   `smoke-sdk.sh` nên mỗi arch tự kiểm được `sdk` mà không cần root.
5. Self-hosted runner aarch64 chạy `test.sh` trước release.

Đã xong 2026-10-30 (không cần box): tag `v1.4.0`, `sdk init/pack` + 10 test, `smoke-sdk.sh` + job CI,
`profiles/build.sh --clean` + kiểm dung lượng, LICENSE MIT, signal handler `run.c` async-signal-safe.

## 7. Bàn giao nhanh

```bash
git status               # phải sạch trước khi bắt đầu
git log --oneline -5
cd build && make         # xác nhận toolchain còn dùng được
```

Nếu `make` lỗi vì thiếu toolchain: `make CC=aarch64-linux-gnu-gcc STRIP=aarch64-linux-gnu-strip`
hoặc dùng Docker `muslcc/x86_64:aarch64-linux-musl` như trong `GUIDE.md` §2.2.