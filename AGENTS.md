# AGENTS.md — Bàn giao dự án minicontainer

> Đọc file này trước khi làm bất cứ gì. Đây là dự án container runtime zero-daemon bằng C cho
> TV Box Amlogic S905W (aarch64, 1.8 GB RAM, Ubuntu 26.04, kernel `6.18.38-meson64`).
> Trạng thái: **v1.5.0** (tag `v1.4.0` đã phát hành) · `test.sh` **84 case** (74/74 PASS trên phần
> cứng thật ở v1.4.0) · nhánh `main` đã push.

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
build/src/*.c        15 module (main, cli, common, ns, run, exec, bridge, cgroup,
                     disk, seccomp, oci, state, systemd, pull, sdk) — ~2.412 dòng
build/Makefile       make / make tools / make deploy BOX=… SSHPASS_FILE=… / make clean
build/test.sh        84 case, phải xanh trước mọi PR (cần box + root)
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
- Thao tác cần xác nhận trước: tag/release, đăng ký self-hosted runner, chọn LICENSE (shared state).

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
  `make` → gọi `gcc -static -O2 -Wall` trực tiếp; gate fail nếu binary > 200 KB.
- Asset release tải về **mất bit `+x`** → phải `chmod +x`.
- Signal handler của `run.c` chỉ được làm 2 việc async-signal-safe: ghi `sig_atomic_t` và `kill(child)`.
  Không `fopen`/`rmdir`/`usleep`/`cgroup_cleanup` trong handler (deadlock nếu signal đến giữa
  `malloc`/stdio); dọn dẹp làm sau `waitpid` trong luồng bình thường.
- `sdk pack` kiểm tra symlink **lexically** (không `realpath`): target tuyệt đối luôn hợp lệ, target
  tương đối chỉ bị từ chối khi đi lên quá gốc rootfs. Đừng đổi sang "chặn mọi `..`" — sẽ chặn cả
  `usr/bin/x -> ../../bin/y` của Buildroot/Alpine.

## 6. Việc tiếp theo (theo thứ tự)

1. Soak 24 h: `ITER=117000 bash build/soak.sh` trên box, ghi kết quả vào `progress/VERIFICATION_LOG.md`.
2. `bash test.sh` trên box (kỳ vọng `TONG=84`), rồi Ctrl-C 20 lần liền để kiểm đường dọn dẹp mới.
3. Tag `v1.5.0` sau soak: `bash build/release.sh` rồi tag (hỏi trước — shared state).
4. Verify 4 asset musl trên phần cứng thật (hiện chỉ aarch64 build local); asset mới có kèm
   `smoke-sdk.sh` nên mỗi arch tự kiểm được `sdk` mà không cần root.
5. Self-hosted runner aarch64 chạy `test.sh` trước release.
6. Bridge NAT/DNS/port mapping — chỉ làm khi có nhu cầu thật.

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