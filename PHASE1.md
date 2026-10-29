# PHASE1.md — PLANNING (xong)

Mục tiêu phase: xong P0–P3 trong PLAN.md §6.1, toàn bộ trên laptop, chưa đụng code C.

## P0. Scope tối thiểu (đã chốt)

```
minicontainer run --ram 30M [--cpu 20] [--net=host|isolated] --rootfs <dir> <cmd>
```

- Đúng 1 lệnh duy nhất. Không pull image, không mạng ảo, không daemon.
- `--rootfs` mặc định `/opt/rootfs` (Buildroot tự build). Alpine chỉ dùng khi truyền `--rootfs /tmp/alpine` để đối chứng.
- `--net` mặc định `host` (không NEWNET). `--net=isolated` mới bật NEWNET (container mất mạng — đúng như thiết kế).

## P1. Nguyên liệu (đang chuẩn bị)

| Món | Trạng thái | Ghi chú |
|---|---|---|
| Toolchain cross aarch64 | ✅ xong — `aarch64-linux-gnu-gcc 11.4.0` cài native qua apt, verify compile `hello.c` static ra ELF aarch64, `scp` lên box chạy ra `hello-tvbox`, EXIT 0 | Chạy thật trên laptop, không qua Docker |
| Alpine baseline | ✅ xong — `build/alpine-minirootfs-aarch64.tar.gz` (3.8MB), giải nén `build/baseline-alpine` (8.5MB), busybox động link musl (loader nằm sẵn trong rootfs nên chroot độc lập được) | Đối chứng T10 |
| Buildroot source | ✅ xong — `git clone --depth 1`, `olddefconfig` pass | — |
| `build/tvbox_defconfig` | ✅ xong — target AArch64 Cortex-A53, toolchain musl nội bộ, `BR2_STATIC_LIBS=y`, output duy nhất `rootfs.tar.gz`, hostname `minicontainer-os` | Build `make -j16` xong EXIT 0 |

## P2. Spec mapping cgroup v1 → v2 (đã chốt, từ PLAN.md §5.2)

| Khái niệm (đề cương gốc, v1) | Viết code theo v2 trên box này |
|---|---|
| Tạo nhóm: `/sys/fs/cgroup/memory/minicontainer` | `mkdir /sys/fs/cgroup/minicontainer-<pid>` |
| RAM limit: `memory.limit_in_bytes` | ghi byte vào `memory.max` (vd `31457280` = 30M) |
| CPU limit: `cpu.shares` (trọng số tương đối) | ghi quota vào `cpu.max` dạng `$MAX $PERIOD` (vd `20000 100000` = 20% 1 CPU) |
| Gắn tiến trình: file `tasks` | ghi PID vào `cgroup.procs` |
| Điều kiện tiên quyết | ghi `+cpu +memory +pids` vào `cgroup.subtree_control` của cây cha trước |
| Đọc OOM | `dmesg` hoặc counter `max` trong `memory.events` |
| Dọn dẹp (I5) | `rmdir` thư mục cgroup sau `exit` |

## P3. Chia task (khớp PLAN.md §6.2–6.3, mỗi test ánh xạ 1 bước code)

- I0 (Buildroot) → T10 (so rootfs nhà làm vs Alpine)
- I1 (CLI getopt) → chưa cần test
- I2 (clone+hostname) + I3 (chroot+proc) → T1, T2, T3
- I4 (cgroup v2) → T4, T5
- I5 (waitpid+dọn dẹp) → T8; T6, T7, T9 là đo đạc hiệu năng/đối chứng

## Nhật ký thực hiện

1. Kiểm tra laptop: 16 nhân / 15GB RAM / 53GB trống — đủ build Buildroot.
2. Cài toolchain native `gcc-aarch64-linux-gnu` qua apt (chạy thật trên laptop, không Docker).
3. Tải Alpine minirootfs 3.21.3 aarch64, giải nén, xác minh busybox aarch64 + musl đầy đủ.
4. Viết `build/tvbox_defconfig`.
5. ✅ Build Buildroot xong (EXIT 0, `rootfs.tar.gz` 592KB) → giải nén, `scp` lên box `/opt/rootfs` (1.3MB), verify `chroot /opt/rootfs /bin/sh` chạy được (SHELL_OK). **I0 xong, phase 1 hoàn tất.**

## Kết quả đối chứng sơ bộ (T10 sớm)

| Rootfs | Nén | Giải nén | BusyBox |
|---|---|---|---|
| Buildroot tự build | 592KB | 1.3MB | static, không cần loader host |
| Alpine baseline | 3.8MB | 8.5MB | động link musl (loader nằm trong rootfs) |

Rootfs nhà làm nhỏ hơn Alpine ~6.5 lần — đúng luận điểm "tự build nhẹ hơn đồ tải sẵn".
