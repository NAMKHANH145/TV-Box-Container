# FINAL.md — Kiến trúc và kết quả minicontainer v1.4.0

Tài liệu này mô tả **toàn bộ hệ thống từ đầu đến cuối**: triết lý, bản đồ module, luồng syscall của
một lần `run`, từng cơ chế kernel được dùng, các bẫy đã gặp, bằng chứng kiểm chứng trên phần cứng thật,
so sánh với SOTA và giới hạn. Cách dùng CLI xem `GUIDE.md`; lịch sử kế hoạch xem `PLAN.md`;
nhật ký từng phase xem `progress/`.

**Tóm tắt một dòng:** `minicontainer` là engine container zero-daemon viết bằng C, musl static
(144.904 B bản Buildroot / 125.392 B asset CI v1.4.0, aarch64), 15 module / 2.430 dòng, chạy trên
Amlogic S905W (1.8 GB RAM, kernel 6.18.38-meson64), `test.sh` **74/74 PASS** trên box thật.

---

## 1. Sản phẩm bàn giao

| Artifact | Nội dung |
|---|---|
| `minicontainer` | 1 binary static, 0 daemon, ~145 KB (Buildroot) / 125 KB (asset CI v1.4.0) |
| `build/test.sh` | 74 case tự động (cô lập, cgroup, seccomp, exec, bridge, read-only, pull, cleanup) |
| `build/soak.sh` | chạy vòng lặp, kiểm rò cgroup/mount/state, theo dõi `MemAvailable` |
| `build/bench.sh` | đo p50/p95/p99 và so sánh Docker trên cùng box |
| `build/release.sh` | tarball + `SHA256SUMS.txt` (version đọc từ `src/main.c`) |
| `profiles/` | 4 defconfig Buildroot + `build.sh` + README |
| `.github/workflows/build.yml` | CI 4 kiến trúc (musl, Docker Hub) + release theo tag |
| `PLAN.md`, `GUIDE.md`, `FINAL.md`, `TASK.md`, `progress/` | tài liệu |
| Bằng chứng phát hành | `v1.3.0` release thật trên GitHub, run `40851398981`; CI musl run `40857971212` |

---

## 2. Triết lý thiết kế

1. **Zero-daemon.** Không có `minicontainerd`. Mỗi `run` là một tiến trình cha (supervisor) → `clone()` →
   `execvp()`. Khi container thoát, cha dọn cgroup/state/mount rồi thoát. Idle = 0 process, 0 MB.
2. **Một syscall cho một cơ chế.** `clone()` (namespaces), `chroot()` (filesystem), `mount()` (/proc, /dev,
   read-only), ghi cgroupfs (giới hạn), `setns()` (exec), `prctl(seccomp)` (bảo mật), `execvp()` (chuyển quyền).
3. **Không runtime trung gian.** Không runc/crun/containerd; gọi thẳng kernel qua libc. Static musl →
   không phụ thuộc loader của host, chạy trên box không cài gì thêm.
4. **Rootfs là thư mục, không phải image.** Tự build bằng Buildroot (1.3 MB → 22.2 MB tuỳ profile),
   phân phối bằng tar + `pull`. Một công cụ làm một việc (UNIX).
5. **Đo được mới ghi.** Mọi tuyên bố trong tài liệu này có lệnh kiểm chứng trong `progress/VERIFICATION_LOG.md`
   hoặc `build/test.sh`.

---

## 3. Sơ đồ tổng thể

```
[laptop]  cross-compile (musl static)         [GitHub Actions] 4 arch + release
      │ scp                                          │ gh release download
      ▼                                              ▼
┌──────────────────────── TV Box (aarch64, 1.8GB, eMMC, kernel 6.18.38-meson64) ────────────────────────┐
│ /usr/local/bin/minicontainer  (~125 KB, zero daemon)                                                  │
│                                                                                                       │
│   supervisor (process cha, giữ state)                                                                 │
│      │ clone(NEWPID|NEWNS|NEWUTS|NEWIPC|NEWUSER [|NEWNET])                                            │
│      ▼                                                                                                │
│   container init (PID 1 trong ns, uid 0 trong / nobody ngoài)                                         │
│      ├── /            ← chroot(rootfs) trong mount ns riêng tư (MS_REC|MS_PRIVATE)                     │
│      ├── /proc        ← mount lại sau clone (bắt buộc với NEWPID)                                      │
│      ├── /dev         ← tmpfs + bind node từ host (mknod bị chặn trong userns)                         │
│      ├── /tmp /run /dev/shm ← tmpfs khi --read-only                                                    │
│      └── seccomp 21 syscall (EPERM) + NO_NEW_PRIVS                                                     │
│                                                                                                       │
│   cgroup v2: /sys/fs/cgroup/minicontainer-<pid>-<n>                                                    │
│      memory.max   cpu.max   pids.max   io.max   cgroup.procs   (cgroup.kill khi stop/rm)                │
│                                                                                                       │
│   netns (tuỳ --net):  isolated → lo UP  |  bridge → mcbr0 10.88.0.1/16 + veth mcv<pid>/mcc<pid>         │
│   state (tuỳ -d):     /run/minicontainer/<id>/{pid,cgroup,ip,log,health_failed}                        │
└───────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Bản đồ module (14 module, 1 binary)

| Module | Dòng | Trách nhiệm |
|---|---:|---|
| `main.c` | 103 | dispatch `run/ps/stats/stop/rm/exec/oci-run/service/pull/sdk/--version` |
| `cli.c` | 51 | `getopt` tự viết, nhận cả `--ram 30M` và `--ram=30M`, validate đầu vào |
| `common.c/.h` | 141+122 | `die()`, `parse_bytes()`, `valid_name`, `run_wait`/`run_capture`, `struct cfg` |
| `ns.c` | 218 | thân tiến trình con: mount private, chroot/chdir, `/proc`, `/dev`, `--read-only`, uid_map |
| `run.c` | 243 | supervisor: clone → pipe “go” → attach cgroup → wait/detach, watchdog disk/health, forward tín hiệu |
| `exec.c` | 124 | `setns()` user→net/uts/ipc→mnt→pid, `O_PATH /proc/<pid>/root`, fork, seccomp, attach cgroup |
| `bridge.c` | 159 | tạo `mcbr0`, veth, `netns_lo_up()`, cấu hình `eth0`/route bằng ioctl trong netns |
| `cgroup.c` | 104 | tạo/bật controller v2, ghi trần, attach, đọc stat, dọn (`cgroup.kill` + retry rmdir) |
| `disk.c` | 119 | `nftw` đếm dung lượng + `RLIMIT_FSIZE`; watchdog `--health-cmd` |
| `seccomp.c` | 102 | BPF deny-list 21 syscall, `NO_NEW_PRIVS`, filter theo `__NR_*` (portable 4 arch) |
| `oci.c` | 129 | đọc `config.json` → argv/flags (subset: process, root, hostname, limits) |
| `state.c` | 265 | `ps/stats/stop/rm`, `--json`, `--timeout`, đọc state + cgroupfs, không daemon |
| `systemd.c` | 53 | sinh unit `/etc/systemd/system/minicontainer-<name>.service` |
| `pull.c` | 291 | tải profile, `statvfs`, sha256, validate tar, staging + rename atomic |
| `sdk.c` | 328 | `sdk init` (skeleton dự án) + `sdk pack` (nftw validate → tar → `.sha256`) |

`build/Makefile`: `make` (cross musl), `make tools` (bomb/breakout), `make deploy BOX=…`,
`make clean`.

---

## 5. Luồng thực thi một lần `run`

### 5.1. Supervisor (host, root)

1. Parse + validate (`cli.c`): `--ram/--cpu/--pids/--io/--disk`, tên container chỉ `[A-Za-z0-9_-]`.
2. Chuẩn bị:
   - tạo cgroup `minicontainer-<pid>-<n>` và bật controller cha (`cgroup.subtree_control`);
   - nếu `--net=bridge`: tạo bridge `mcbr0` (nếu chưa có), veth `mcv<pid>`/`mcc<pid>`, gắn host-end;
   - tạo pipe đồng bộ “go” và “ready”; `malloc(1 MB)` stack cho con.
3. `clone(child_fn, stack, flags)` với `NEWPID|NEWNS|NEWUTS|NEWIPC|NEWUSER` (+ `NEWNET` khi isolated/bridge).
4. Ghi `uid_map = "0 65534 1"`, `setgroups = "deny"`, `gid_map` tương ứng — **sau** clone, **trước** khi con
   được chạy tiếp (con chặn ở pipe).
5. `signal(SIGCHLD, SIG_DFL)`/xử lý tín hiệu; `-d` thì ghi state, in id, đóng stdio và thoát ngay.
6. Foreground: chờ con; mỗi giây sample disk/health nếu bật (dùng `sigtimedwait` với `SIGALRM`, không polling nóng).
7. Dọn: `cgroup.kill`, wait, `rmdir` cgroup (retry), xoá `ip`/veth, `rm -rf` state, dọn `.oldroot` sót.

### 5.2. Tiến trình con (`ns.c: child_fn`)

Thứ tự **bắt buộc** (sai thứ tự = bug đã từng gặp):

```
setgid(0); setuid(0)            // bắt buộc: nếu quên, con mang host-uid 0 ngoài map → nobody vô dụng
mount(NULL, "/", NULL, MS_REC|MS_PRIVATE)   // chặn lan propagation ra host (systemd để shared)
sethostname("minicontainer-os")
[--read-only] mount --bind rootfs→rootfs + tmpfs /tmp /run /dev/shm
chroot(rootfs); chdir("/")      // đổi root (xem §6.2)
mount("proc", "/proc", "proc")  // SAU clone, SAU chroot
dev_setup()                     // tmpfs /dev + bind node host + symlink fd/stdin/stdout/stderr
[--net] bridge_configure()      // eth0/IP/route bằng ioctl (không cần `ip` trong rootfs)
[--read-only] remount bind RO   // sau /proc (xem lý do §6.2)
write "ready"; read "go"        // đồng bộ với supervisor
seccomp_install()               // SAU tất cả setup, TRƯỚC execvp
execvp(cmd[0], cmd)             // lỗi → die với errno
```

Điểm mấu chốt: **seccomp phải đặt ngay trước `execvp`**, vì filter chặn `mount` sẽ chặn chính bước
setup (`make-private /: EPERM`) — bug đã gặp ở phase 10.

---

## 6. Cơ chế chi tiết

### 6.1. Namespaces và user namespace

| Namespace | Cờ | Tác dụng |
|---|---|---|
| PID | `CLONE_NEWPID` | con là PID 1, `ps` chỉ thấy nội bộ; `/proc` phải mount lại |
| Mount | `CLONE_NEWNS` | cây mount riêng; `MS_REC\|MS_PRIVATE` để không lan sang host |
| UTS | `CLONE_NEWUTS` | `hostname` riêng |
| IPC | `CLONE_NEWIPC` | SysV shm/sem/msg riêng + `/dev/shm` |
| User | `CLONE_NEWUSER` | root trong container ↔ host uid/gid **65534 (nobody)** |
| Net | `CLONE_NEWNET` | chỉ khi `--net=isolated\|bridge` |

Bẫy userns đã xử lý:

1. `uid_map` chỉ một dòng `0 65534 1` — mapping rộng bị kernel mod từ chối.
2. `setgroups` phải ghi `deny` **trước** `gid_map`, nếu không `write gid_map: EPERM`.
3. `mknod` trong userns bị `EPERM` → `/dev` phải tạo bằng bind node từ host (v1.3.0).
4. `mkdir` trong rootfs khi chạy dưới userns có thể bị từ chối → thư mục cần trước (`.oldroot`) do
   supervisor (host-root) tạo hộ.
5. Quên `setuid(0)` sau map → con giữ host-uid 0 nằm ngoài map, trở thành nobody **bên trong** và
   mất khả năng ghi rootfs.

### 6.2. Filesystem: `chroot` (đã chốt), `pivot_root` (đã thử, bị chặn)

Lịch sử: phase 8 chạy A/B **khi container chạy bằng root thật (chưa có userns)**: thủ công
`chroot` vào thư mục con rồi `chdir ../../..` + `chroot .` → **thoát được**; `pivot_root` → chặn.
Vì vậy bản đó dùng `pivot_root`.

Khi thêm user namespace (v1.1.0), `pivot_root` dưới userns trên kernel 6.18.38-meson64 trả `EPERM`
trong mọi cấu hình đã thử (kể cả khi đã bind-mount new_root lên chính nó và mount `/proc` trước).
→ **Chốt: `chroot` + `chdir("/")` bên trong userns.** Về bảo mật, kỹ thuật chroot-break mất giá trị vì
tiến trình chỉ là `nobody` (65534) ngoài namespace, không có `CAP_SYS_CHROOT`/`CAP_SYS_ADMIN` ở host,
và mount namespace là private. Đây cũng là điểm khác biệt đáng nói khi bảo vệ: **không dùng được
pivot_root vì kernel + userns, và đã đo được lý do**.

Mount propagation: systemd để `/` là `shared`; nếu không `MS_REC|MS_PRIVATE` ngay sau `clone()`, mỗi lần
chạy để lại một `/proc` trên rootfs host (đã quan sát leak). Fix + regression test: chạy 3 lần, `mount`
leak = 0.

`/dev`: rootfs build bằng user thường có `/dev` rỗng (Buildroot chỉ tạo node khi build bằng root).
v1.3.0 tạo tmpfs `/dev` rồi bind `null, zero, urandom, random, tty, console` từ host + symlink
`fd/stdin/stdout/stderr`. Trước fix này, container `-d` chết ngay ở `open /dev/null`.

`--read-only` (v1.4.0):

```
MS_REC|MS_PRIVATE "/" → bind rootfs lên chính nó → chroot → mount /proc
→ tmpfs /tmp 32M, /run 8M, /dev/shm 64M
→ MS_REMOUNT|MS_BIND|MS_RDONLY trên "/"
```

- Bỏ qua đường dẫn mà distro trỏ bằng symlink (`/var/log -> ../tmp` trong Buildroot): mount đè lên symlink
  sẽ **che mất `/tmp`** bằng một tmpfs thứ hai (bug đã bắt bằng `grep -c " /tmp tmpfs "` = 1).
- Kernel 6.18 từ chối remount read-only cho bind của **tmpfs** (`EPERM`) → rootfs đặt dưới `/tmp`
  **fail loudly** kèm hướng dẫn; `pull` cảnh báo trước bằng `statfs` `TMPFS_MAGIC`.

### 6.3. Cgroup v2

Box chỉ có v2 (`cgroup2 on /sys/fs/cgroup`; v1 toàn 0 hierarchy). Bảng ánh xạ từ đề cương:

| Đề cương (v1) | Code (v2) |
|---|---|
| `/sys/fs/cgroup/memory/minicontainer` | `/sys/fs/cgroup/minicontainer-<pid>-<n>` |
| `memory.limit_in_bytes` | `memory.max` (+ `memory.swap.max` = 0) |
| `cpu.shares` | `cpu.max` = `"$QUOTA $PERIOD"` (`20000 100000` = 20% × 1 CPU) |
| `tasks` | `cgroup.procs` |
| bật controller khi mount | ghi `+cpu +memory +pids +io` vào `cgroup.subtree_control` |

Controller dùng: **memory** (hard limit → OOM-killer), **cpu** (quota %), **pids** (chống fork-bomb),
**io** (`MAJ:MIN rbps/wbps`; fallback partition → minor 0). `cpuset` có sẵn nhưng chưa dùng.

Chi tiết triển khai:

- Ghi controller vào `subtree_control` là idempotent (ghi lại no-op, an toàn qua reboot).
- Controller không có → báo lỗi rõ ràng, không crash.
- Dọn: `cgroup.kill` (v1 không có) giết cả cây; `rmdir` retry vì kernel có thể còn task đang exit;
  marker `.oldroot` + watchdog dọn phần sót nếu supervisor bị kill cứng.
- OOM quan sát qua `dmesg` (`Memory cgroup out of memory: Killed process …`) hoặc `memory.events:max`.
- `--disk`: kernel **không có** ext4 project quota → watchdog userspace (nftw, 1 s, baseline delta) +
  `RLIMIT_FSIZE` chặn tức thời một file đơn lẻ. Burst xấu nhất ≈ 1 khoảng lấy mẫu.

### 6.4. Seccomp-BPF

Viết tay (không libseccomp), deny-list 21 syscall trả **`EPERM`** (không kill, để app còn chạy được):

`mount, umount2, pivot_root, reboot, kexec_load, init_module, finit_module, delete_module, ptrace,
clock_settime, adjtimex, settimeofday, bpf, perf_event_open, fanotify_init, name_to_handle_at,
open_by_handle_at, keyctl, add_key, request_key` (+ biến thể arch).

Kèm `PR_SET_NO_NEW_PRIVS` (chặn leo thang qua suid). Filter dùng `__NR_*` của kiến trúc đích nên
portable cho cả 4 arch CI. `exec <id>` cài lại filter cho tiến trình mới.

### 6.5. Mạng

| Mode | Cơ chế | Bằng chứng |
|---|---|---|
| `host` | không `NEWNET` | route của container = route host; độ trễ 0 |
| `isolated` | `NEWNET` rỗng + `lo` UP | loopback bind/accept/send/recv OK; không ra ngoài |
| `bridge` | `NEWNET` + `mcbr0` 10.88.0.1/16 + veth | host↔container 0% loss (0.47 ms); container↔container 0% loss (0.73 ms) |

Bridge: host-end tạo/gắn bằng `ip` (iproute2); container-end cấu hình **bằng ioctl** (rootfs không cần `ip`);
IP dẫn xuất `10.88.<pid/256>.<pid%256>` (tránh giá trị dành riêng), ghi `/run/minicontainer/<id>/ip`.
Không NAT/DNS. `lo` trong netns mới mặc định DOWN → `netns_lo_up()` (gọi cho cả isolated và bridge) là
fix quyết định để mosquitto/Python bind 127.0.0.1 hoạt động.

Ứng dụng thật đã chạy: lighttpd bind `0.0.0.0:8080` (port < 1024 bị từ chối vì mapped root thiếu
`CAP_NET_BIND_SERVICE`) → host `curl 10.88.x.x:8080` **HTTP 200**; mosquitto broker trong container A,
publisher B, subscriber C qua bridge → `sub_out=[tu-m2-qua-bridge]`; Python `urllib` từ `python-edge`
→ `GET -> 200`.

### 6.6. Detach, state, ps/stats/stop/rm

`run -d`: supervisor tạo cgroup, clone, ghi state, in id và **thoát ngay** (không `waitpid`). Con mồ côi về
init. Không có daemon: `ps/stats/stop/rm` đọc trực tiếp `/run/minicontainer/<id>/` + cgroupfs.

Bug đã sửa: con thừa hưởng pipe stdout của session SSH → SSH treo đến khi con chết. Fix: detach `dup2`
`/dev/null` vào 0/1/2 trước `exec`.

`stop`: gửi TERM tới cả cgroup, chờ `--timeout` (mặc định 3 s), sau đó `cgroup.kill`.
`rm`: kiểm tra đã dừng, dọn state + cgroup + veth; `-f` thì stop trước.
`stats`: đọc `memory.current`, `memory.max`, `cpu.stat`, `pids.current`, `io.stat`.
Health (`--health-cmd`, chạy trên host mỗi 5 s): fail → kill cgroup; detached ghi `health_failed`.

### 6.7. `exec` (v1.3.0)

`setns()` theo thứ tự **user → net/uts/ipc → mount → pid**. Lý do thứ tự: vào userns trước để có quyền
join các ns còn lại; join pid **cuối** vì nó chỉ ảnh hưởng tiến trình con sinh ra sau (`fork`). Root mới
lấy bằng `O_PATH` fd trên `/proc/<pid>/root` + `fchdir` (không dùng đường dẫn host, tránh TOCTOU).
Sau `fork`: cài seccomp, attach vào cgroup container, `execvp`.

Kết quả kiểm chứng: uid 0; PID của shell ≠ 1 nhưng cùng PID ns (thấy init là 1); `mount` bị seccomp chặn;
`/proc/self/cgroup` trùng `/proc/1/cgroup`; id sai bị từ chối; không cần `nsenter`.

### 6.8. `pull` (v1.4.0)

```
execvp("curl"/"wget", argv literal)  → file tạm
statvfs: trống ≥ 4× kích thước?      → không thì từ chối
--sha256: so khớp trước khi giải nén → sai thì exit 1, không extract
tar -tzf: thành viên tuyệt đối / ".." → từ chối
giải nén vào .mc-pull-* → rename atomic → (--force: swap .old)
```

Không shell → URL từ `MC_PULL_URL` không thể inject lệnh. Cảnh báo khi đích là tmpfs (vì `--read-only`
sẽ fail). Giữ quyền `+x` của file trong tar.

### 6.9. `service create`, OCI

`service create` sinh unit systemd `Type=simple`, `Restart=always`, `RestartSec=5`, mặc định
`--ram=32M --cpu=20 --pids=64 --io=1M --rootfs=/opt/rootfs`. Vẫn zero-daemon (systemd chỉ restart,
không quản lý container thường trú).

`oci-run <bundle>` đọc `config.json`: `process.args/env/cwd`, `root.path`, `hostname`, limits từ
annotations → dựng argv cho `run`. Không hỗ trợ `mounts`, `hooks`, seccomp profile OCI; key ngoài subset
bị cảnh báo chứ không im lặng.

### 6.10. `sdk init` / `sdk pack` (v1.5.0)

Chiều ngược lại của `pull`: đóng gói rootfs của người dùng thành đúng định dạng `pull` nhận.

```
sdk init <dir>   → <dir>/{rootfs/, mc.conf, README.md}; từ chối nếu <dir> đã có nội dung
sdk pack <dir>   → nftw(FTW_PHYS) validate symlink lexically
                 → tar -czf OUT -C rootfs --numeric-owner --owner=0 --group=0 .
                 → <OUT>.sha256  ("<hash>  <tên file>")
```

Quy tắc: member tương đối nên `check_tar()` của `pull` chấp nhận; `+x` do tar giữ; symlink tuyệt đối
(`/bin/busybox`) hợp lệ, symlink tương đối chỉ bị từ chối khi đi ra ngoài gốc rootfs. Không thêm daemon
hay process nào — vẫn là 1 binary. `run_wait`/`run_capture` (trước là static trong `pull.c`) nay ở
`common.c` để cả `pull.c` và `sdk.c` dùng chung.

---

## 7. Bảng bẫy kernel đã gặp và cách xử lý

| # | Triệu chứng | Nguyên nhân | Fix |
|---|---|---|---|
| 1 | `ps` trong container thấy tiến trình host | mount `/proc` trước `clone(NEWPID)` | mount `/proc` **sau** clone (trong thân con) |
| 2 | Mỗi lần chạy để lại `/proc` trên rootfs host | `/` là `shared` (systemd) | `MS_REC\|MS_PRIVATE` ngay sau clone |
| 3 | `make-private /: EPERM` sau khi thêm seccomp | filter chặn chính `mount` của setup | cài seccomp **ngay trước** `execvp` |
| 4 | `write gid_map: EPERM` | chưa tắt setgroups | ghi `setgroups=deny` trước `gid_map` |
| 5 | Trong container không phải root dù đã map | thiếu `setuid(0)` sau map | `setgid(0); setuid(0)` sau đồng bộ “go” |
| 6 | `mknod /dev/null: EPERM` | userns không cho mknod | bind node từ host vào tmpfs `/dev` |
| 7 | Container `-d` chết ngay `open /dev/null` | rootfs thiếu `/dev` | xem #6 (v1.3.0) |
| 8 | `pivot_root: EPERM` | userns trên kernel này | dùng `chroot`, ghi rõ lý do (§6.2) |
| 9 | `--read-only` che mất `/tmp` (2 tmpfs) | `/var/log -> ../tmp`, mount đè symlink | bỏ qua path là symlink, kiểm `grep -c` = 1 |
| 10 | `remount read-only: EPERM` | rootfs trên tmpfs | fail loudly + hướng dẫn đặt ở ext4 |
| 11 | Bind 127.0.0.1 fail trong netns mới | `lo` mặc định DOWN | `netns_lo_up()` |
| 12 | SSH treo sau `run -d` | con giữ pipe stdout của session | detach `dup2(/dev/null)` |
| 13 | `mosquitto` fail `setgroups` | config `user mosquitto` | `user root` trong config container |
| 14 | Không bind được port 80 | mapped root thiếu `CAP_NET_BIND_SERVICE` | dùng ≥ 1024 |
| 15 | `--disk` không hẳn là quota | kernel không có project quota | watchdog 1 s + `RLIMIT_FSIZE` |

---

## 8. Kiểm chứng trên phần cứng

### 8.1. Suite tự động

`bash build/test.sh` trên box: **74/74 PASS** (v1.4.0). Suite hiện có **84 case** (thêm 10 case `sdk`
ở v1.5.0; 10 case này đã chạy trên laptop vì `sdk` không cần root/quyền container):
cô lập PID/UTS/fs, cgroup memory/cpu/pids/io, seccomp, breakout (chroot escape), userns, `/dev`,
detach + state + stats, `exec` (uid/PID/seccomp/cgroup/id sai), bridge (IP, ping, veth cleanup),
`--read-only` (8 case), `pull` (11 case), cleanup (cgroup/mount/state = 0).
Chạy liên tiếp 3 lần đều xanh; một lần FAIL từng thấy là do chạy tay song song, không phải regression.

### 8.2. Phát hành và assets

Tag `v1.3.0` → run `40851398981` xanh: build 4 arch + release job. Tải asset bằng
`gh release download v1.3.0`, verify `SHA256SUMS.txt`, chạy aarch64 asset trên box thật: **55/55 PASS**
(suite tại thời điểm đó). Asset mất bit `+x` khi tải (đã ghi trong GUIDE).

CI musl (run `40857971212`, `workflow_dispatch`): thay asset glibc bằng musl static-pie:

| Arch | glibc `v1.3.0` | musl (CI mới) |
|---|---:|---:|
| aarch64 | 729.416 B | **125.392 B** |
| armv7l | 443.828 B | **112.360 B** |
| riscv64 | 622.848 B | **104.808 B** |
| x86_64 | 931.880 B | **100.704 B** |

Gate chống hồi quy: bước CI fail nếu binary > 200 KB (dấu hiệu lẫn glibc).
Nguyên nhân đổi: musl.cc chặn IP runner GitHub Actions → dùng image Docker Hub `muslcc/x86_64:<target>`
(image chỉ có `gcc`/`strip`, không `make` → gọi gcc trực tiếp với flag như `Makefile`).

### 8.3. Đa kiến trúc — cái nào chạy thật

| Arch | Cách build | Chạy ở đâu | Kết quả |
|---|---|---|---|
| aarch64 | Buildroot musl / `aarch64-linux-gnu-gcc` | **TV Box thật** | 74/74 PASS; exec/bridge/health/disk thật |
| x86_64 | `gcc -static` | **native** | `--version`, `ps`, validate; container thật cần root |
| armv7l | `arm-linux-gnueabihf-gcc` | qemu-user | binary nạp, parse lệnh đúng; không tạo container |
| riscv64 | CI `riscv64-linux-gnu-gcc` | qemu-user (từ asset) | binary nạp; chưa chạy container |

Kết luận trung thực: đường dẫn namespace/seccomp/cgroup chỉ được verify bằng **phần cứng thật với aarch64**.

### 8.4. Soak và hiệu năng

- `ITER=200` (smoke): 200 vòng/146 s, `loi=0 lan-leak=0`, `MemAvailable` 1.591.408 → 1.594.800 kB.
- `ITER=800`: 800 vòng/588 s, 0 lỗi, 0 leak.
- `ITER=12` sau khi thêm `--read-only`: 12 vòng/7 s, 0 lỗi, RAM giảm 4 kB.
- 24 h **chưa chạy** (`ITER=117000` ≈ 24 h ở 1.36 vòng/s đo trên box).
- Nhiệt full-load 1 core: 52 → 54 °C.
- Latency `run … /bin/true`: 10–65 ms (p50/p95/p99 trong `bench.sh`), Docker cùng box ~2.076 s.

### 8.5. Profile chạy thật

| Profile | Giải nén | Chạy thật |
|---|---:|---|
| `base-mini` | 1.3 MB | toàn bộ suite PASS |
| `web-proxy` | 2.6 MB (tar.gz 1.4 MB) | lighttpd 1.4.85, HTTP 200 |
| `iot-gateway` | 14.1 MB | mosquitto, MQTT pub/sub 3 container |
| `python-edge` | 22.2 MB | Python 3.14.8, `import serial, spidev` OK |

Ba profile sau là **dynamic** (Buildroot package phụ thuộc `!BR2_STATIC_LIBS`) nên rootfs phải chứa
loader/lib của chính nó — đây chính là lý do `--read-only` và `/dev` tự tạo quan trọng.

---

## 9. So sánh với SOTA (2026)

| Runtime | Ngôn ngữ | Daemon nền | Idle RAM | Start 1 container | Chuẩn |
|---|---|---|---|---|---|
| **minicontainer v1.4.0** | C (musl), 125–137 KB | không (0 MB) | **0 MB** | **0.010–0.065 s** | rootfs dir + tar profile |
| Docker (trên box này) | Go | dockerd + containerd | **207 MB** | **2.076 s** | OCI |
| crun | C | cần stack trên | ~KB | ~47 ms create→delete | OCI full |
| runc | Go | cần stack trên | ~MB | ~224 ms | OCI full |
| youki | Rust | cần stack trên | thấp | ~111 ms | OCI full |
| gVisor/Kata | sandbox/VM | nặng | lớn | −40–60% TPS | OCI, cách ly mạnh |

Nhanh hơn Docker trên cùng box **~33–207 lần**, nền nhẹ hơn vô hạn (207 MB → 0 MB), tiệm cận crun
dù chỉ ~2.200 dòng C. Không cạnh tranh OCI đầy đủ (không chạy image Docker/k8s) — đánh đổi chủ đích.

---

## 10. Điểm mạnh

1. Zero-daemon **đo được**: 0 process, 0 MB khi idle; Docker 207 MB trên cùng box.
2. Boot 10–65 ms, nhanh hơn `docker run` 33–207 lần.
3. 4 profile tự build (1.3 → 22.2 MB), phân phối bằng `pull` có sha256 + kiểm dung lượng + chặn tar xấu.
4. Cgroup v2 hard limit thật: OOM đúng ngưỡng, CPU 20%, fork-bomb chặn ở `pids.max`.
5. Chạy trên kernel mod sơ sài nơi Podman không cài nổi (không dbus/slirp/systemd stack).
6. Nhiệt full-load +2 °C → an toàn 24/7 không tản nhiệt chủ động.
7. 3 lớp phòng thủ: chroot (mount ns private) → userns (nobody ngoài) → seccomp 21 syscall + `NO_NEW_PRIVS`.
8. Binary ~125 KB (nhẹ hơn dockerd ~1.600 lần), CI 4 arch, release theo tag + `SHA256SUMS.txt`.
9. 74/74 test, 0 leak sau mỗi lần chạy; soak 800 vòng liên tục 0 lỗi.
10. OCI-bundle subset đọc được bundle chuẩn.
11. Cách ly thật: `exec` vào container đang chạy; `--net=bridge` cho container nói chuyện với nhau
    (HTTP 200, MQTT 3 container).
12. Chống mòn eMMC: `--read-only` đóng băng rootfs, ghi vào tmpfs.

---

## 11. Hạn chế (trung thực)

**Theo thiết kế:**

- Không registry/layer/overlay; không image format chuẩn (profile tar là thay thế gần nhất).
- `--net=bridge` không NAT/DNS/port-mapping → container chỉ tới subnet 10.88/16.
- Không bind port < 1024 trong container.
- Deny-list seccomp (không phải allow-list); không `/sys` (kernel cấm sysfs trong userns).
- Không volumes/bind-mount tuỳ ý, không log driver (stdout → state dir khi `-d`).
- Không checkpoint/restore, không live-migrate, không SELinux/AppArmor.
- `--disk` là watchdog 1 s, không phải quota kernel.
- `sdk` là công cụ đóng gói, **không** phải thư viện nhúng (không có API C cho app khác).
- Board thật mới có aarch64; 3 arch còn lại mới verify bằng QEMU-user.

**Đã khắc phục (đều có test):** Ctrl-C để lại con mồ côi → forward tín hiệu + `cgroup.kill`;
thiếu quota → `--disk` + `RLIMIT_FSIZE`; validate đầu vào; phủ `--cpu`/`--io`; `--json`;
`--timeout`; `--version`; `oci-run` cảnh báo key lạ; tên cgroup/state kèm PID + retry rmdir.

---

## 12. Việc còn lại

| # | Việc | Trạng thái |
|---|---|---|
| 1 | Tag + phát hành `v1.4.0` | CI đã xanh (`workflow_dispatch`), chờ quyết định |
| 2 | Soak 24 h | `ITER=117000 bash build/soak.sh` |
| 3 | Verify 4 asset musl trên phần cứng thật | mới aarch64 build local |
| 4 | Self-hosted runner aarch64 chạy `test.sh` trước release | chưa có |
| 5 | `sdk init/pack` (tên cũ `mc-sdk`) | ✅ v1.5.0, 10 test trong `test.sh` |
| 6 | LICENSE | chưa chọn (MIT / Apache-2.0) |
| 7 | Bridge NAT + DNS + port mapping | cân nhắc chi phí CPU |

---

## 13. Kết luận

Hệ thống đạt mục tiêu đề tài: tự gọi syscall Linux (`clone`, `chroot`, `mount`, `sethostname`, `setns`,
`prctl`, `execvp`, `waitpid`) để xây engine container siêu nhẹ, áp dụng đúng lý thuyết môn Hệ điều hành
(Process Management, Memory Management, File System, Namespaces, Cgroups), chạy được trên phần cứng
hạn chế, zero-daemon, có số liệu đo thật và test tự động trên box.

**Bottom line:** 15 module, 2.430 dòng C, zero-daemon, 1 binary static. Toàn bộ backlog R5 (health,
`exec`, bridge, `--read-only`, `pull`, `sdk`) đã xong; phần runtime verify trên phần cứng thật,
`sdk` verify bằng test tự động (không cần root). Phần còn lại là việc hạ tầng (soak 24 h, runner tự
host, LICENSE), không phải tính năng runtime.
---

## 14. Chuẩn bị bảo vệ — câu hỏi khó và câu trả lời có bằng chứng

| Câu hỏi | Trả lời ngắn (kèm nơi có bằng chứng) |
|---|---|
| Sao không dùng `pivot_root` như tài liệu container chuẩn? | Đã dùng ở phase 8 khi container chạy root thật; khi thêm userns (v1.1.0) `pivot_root` trả `EPERM` trên `6.18.38-meson64` ở mọi cấu hình đã thử → chốt `chroot` + `chdir("/")`. Về bảo mật, kỹ thuật chroot-break cần quyền host, trong khi tiến trình chỉ là `nobody` (65534) ngoài namespace và không có `CAP_SYS_CHROOT`/`CAP_SYS_ADMIN` (§6.2). |
| Sao không nhúng `runc`/`crun` rồi gọi? | Mục tiêu là **zero-daemon, 1 binary, không stack**: `runc`/`crun` vẫn cần nơi tạo namespace, image/layer, config; `runc` ~224 ms và ~MB, `crun` ~47 ms. Ở đây `clone()` + `chroot` + cgroupfs + `execvp()` trực tiếp: 10–65 ms, 145 KB, 0 MB idle. Đánh đổi chấp nhận: **không chạy được image Docker/k8s** (§9, §11). |
| `--disk` có phải quota thật không? | **Không.** Kernel box không có ext4 project quota (`quotactl` không hỗ trợ). Có 2 lớp: `RLIMIT_FSIZE` chặn tức thời **một file** (exit 153/SIGXFSZ), watchdog `nftw` 1 s chặn **tổng dung lượng** (burst xấu nhất ≈ 1 chu kỳ lấy mẫu). Nói rõ giới hạn này thay vì gọi là quota (§6.3, §7 #15). |
| Bảo mật có tương đương Docker không? | Ba lớp: mount namespace private (`MS_REC\|MS_PRIVATE`) → userns map root trong ↔ 65534 ngoài → seccomp 21 syscall + `NO_NEW_PRIVS`; test `bomb`/`breakout`/`mount`/`ptrace`/`open_by_handle_at` đều bị chặn (§6.4, §8.1). **Không** tương đương: thiếu AppArmor/SELinux, deny-list chứ không allow-list, không `/sys`, không user namespace tuỳ biến. |
| 74 hay 84 test? Có phải tất cả chạy trên box? | 74/74 là **trên box** ở v1.4.0. Suite hiện **84 case**: 10 case `sdk` mới chạy trên laptop (sdk không cần root/namespace). `test.sh` in `TONG=` ở dòng cuối để không lệch số. Phần cứng thật mới verify **aarch64**; 3 arch còn lại mới ở mức QEMU-user (§8.3). |
| Nếu kernel thiếu userns thì sao? | Box này có userns (đã map `0 65534 1` thành công). Thiếu userns thì "root trong container" = root trên host, mất một lớp phòng thủ; khi đó chỉ còn mount ns + seccomp và phải hạ tuyên bố bảo mật. Đây là lý do `FINAL §11` ghi rõ giới hạn thay vì quảng bá "an toàn như VM". |
