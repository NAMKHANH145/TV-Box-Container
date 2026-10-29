# PHASE2.md — IMPLEMENT (xong I1–I5, test T1–T5/T7/T9)

Quy trình y phase 1: khảo sát trước → can thiệp → đánh giá → ghi lại.

## 1. Chiến lược: C userspace, KHÔNG viết kernel module (.ko)

Câu hỏi đặt ra: với môn Hệ điều hành, có nên viết `.ko` dùng `kernel.h` không? **Không.** Lý do cụ thể:

- Đề tài cần **dùng** cơ chế kernel (namespaces, cgroups, chroot), không phải **viết lại** chúng. Viết `.ko` nghĩa là re-implement syscall/subsystem đã có — vừa sai scope, vừa không khả thi trên kernel mod `6.18.38-meson64` (không có headers, không có serial console để cứu khi panic).
- Module đã load là **resident trong kernel** → vi phạm chính luận điểm zero-daemon của đề tài (T6 đòi 0 byte khi idle).
- Tool hiện tại là **1 file C userspace duy nhất** (`build/minicontainer.c`, ~200 dòng), gọi thẳng syscall qua libc: `clone`, `sethostname`, `chroot`, `mount`, `execvp`, `waitpid` + ghi file cgroupfs. Bằng chứng syscall phục vụ bảo vệ lấy bằng `strace -f` trên box (đã cài strace 6.19), không cần đụng kernel space.
- `kernel.h` chỉ có nghĩa trong `.ko`; code userspace dùng `<sched.h>` (cờ `CLONE_*`), `<sys/mount.h>` — đúng chuẩn và đủ.

## 2. Khảo sát trước khi viết code (trên box)

| Kiểm tra | Kết quả | Kết luận cho code |
|---|---|---|
| `unshare --pid --mount-proc --uts --fork chroot /opt/rootfs /bin/sh` | shell PID=1, `ps` chỉ thấy nội bộ | Kernel namespaces chạy, thứ tự clone→mount /proc đúng |
| Ghi cgroup v2: `subtree_control`, `memory.max=31457280`, `cpu.max=20000 100000`, `rmdir` | Tất cả OK | Code dùng API v2 như spec P2, không cần fallback v1 |
| Toolchain/debug | box không có gì → đã cài `strace 6.19` qua apt | Debug syscall trực tiếp trên box |

## 3. Code đã viết (`build/minicontainer.c`, binary static 657KB tại `/usr/local/bin/minicontainer`)

- **I1**: parse cả 2 dạng `--opt=value` và `--opt value` (`--ram/--cpu/--rootfs/--net/--hostname`), mặc định `--rootfs /opt/rootfs --net host`.
- **I2**: `malloc` stack 1MB + `clone(SIGCHLD|NEWpid|NEWns|NEWuts[|NEWnet])` + `sethostname` (mặc định `minicontainer-os`).
- **I3**: `chroot` + `chdir(/)` + `mount("proc","/proc")`. Chốt `chroot` (không `pivot_root`) như PLAN.md §5.2.
- **I4**: tạo `/sys/fs/cgroup/minicontainer-<ppid>-<n>`, ghi `memory.max`/`cpu.max`, gắn **host PID** (giá trị `clone()` trả về) vào `cgroup.procs`. Đồng bộ cha–con bằng `pipe()` để con `exec` sau khi cha gắn cgroup xong (tránh race: con trong ns mới `getpid()` luôn =1 nên không tự gắn được).
- **I5**: `waitpid()` + `rmdir` cgroup. Mount `/proc` tự chết theo MNT ns, không cần umount.

## 4. Bug gặp và sửa (đều tìm bằng strace, không đoán)

1. `execvp: No such file or directory` — parser cũ chỉ nhận `--opt=value`, lệnh test dùng `--opt value` nên cả cụm `--rootfs` lọt vào `cmd`. Strace lộ `execve("/usr/local/sbin/--rootfs", ...)` → sửa parser hỗ trợ cả 2 dạng.
2. Warning `-Wformat-truncation` (3 chỗ `snprintf` đường dẫn cgroup) → nới buffer `PATH_MAX+16`, build lại sạch warning.

## 5. Kết quả test trên box (ánh xạ PLAN.md §6.3)

| Test | Kết quả | Đạt? |
|---|---|---|
| T1 PID isolation | shell `$$`=1, `ps` chỉ thấy tiến trình nội bộ, không thấy systemd/sshd | ✅ |
| T2 filesystem jail | `touch /etc/hack.txt` chỉ nằm trong `/opt/rootfs`, `/etc` host nguyên vẹn | ✅ |
| T3 hostname | trong `minicontainer-os`, ngoài `tvbox` | ✅ |
| T4 RAM hard limit | bomb xin 50MB với `--ram 30M` → kernel kill đúng ngưỡng (anon-rss 30592kB), `dmesg`: `Memory cgroup out of memory: Killed process`, host load 0.41 bình thường | ✅ |
| T5 CPU cap | spinner với `--cpu 20` → `cpu.stat` đo đúng **20%** 1 core | ✅ |
| T7 latency | `time minicontainer run ... /bin/true` → **real 0.030s** (30ms) | ✅ |
| T9 đối chứng Docker | `dockerd` RSS **131MB** + `containerd` **82MB** ≈ 213MB khi idle — cao hơn cả ước tính 50–100MB trong đề cương | ✅ (số liệu báo cáo) |
| T6 zero-daemon | không tiến trình `minicontainerd` nào, 0MB nền | ✅ |
| T8 cleanup | sau `exit`, thư mục `/sys/fs/cgroup/minicontainer-*` biến mất | ✅ |

## 6. Bài học vận hành (ghi để không tái phạm)

- `pkill -f "pattern"` tự giết chính shell remote vì pattern nằm trong cmdline của nó → dọn tiến trình container kẹt bằng cách duyệt `cgroup.procs` mà kill PID (shell mình không nằm trong cgroup đó nên an toàn).
- `cmd | tail; echo $?` cho exit code của `tail`, không phải của container → đọc exit code phải tách pipeline.
- Kill parent `minicontainer` giữa chừng để lại con mồ côi + cgroup rác → luôn dọn theo cgroup như trên.

## 7. Còn lại sang phase 3 (TESTING hoàn chỉnh)

- I6 tùy chọn (`--net=isolated` đã có code, chưa test; user ns để sau).
- Chạy lại toàn bộ T1–T10 theo đúng thứ tự PLAN.md §6.3 trên cả 2 rootfs + đo nhiệt độ khi full-load, reboot kiểm tra tự hồi phục.
