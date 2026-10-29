# minicontainer

Container runtime siêu nhẹ, **zero-daemon**, viết bằng C (musl static ~125 KB), cho phần cứng hạn chế
như TV Box (Amlogic S905W, 1.8 GB RAM, kernel 6.18.38-meson64).

| | minicontainer | Docker (cùng box) |
|---|---|---|
| RAM khi rảnh | **0 MB** (không process nền) | ~207 MB |
| Kích thước | **125–137 KB** | ~207 MB engine |
| `run` → shell | **10–65 ms** | ~2.076 s |
| Bảo mật | chroot + userns + seccomp 21 syscall | seccomp + AppArmor/SELinux |

Tính năng: namespaces (PID/MNT/UTS/IPC/USER/NET), cgroup v2 (`memory/cpu/pids/io` + disk watchdog),
`run -d` + `ps/stats/stop/rm`, `exec` (setns), `--net=host|isolated|bridge`, `--read-only`, `pull`,
OCI-bundle subset, `service create` (systemd unit), 4 rootfs profile Buildroot.

## Nhanh

```bash
cd build && make                                   # -> build/minicontainer
make deploy BOX=<IP_BOX> SSHPASS_FILE=<file>       # copy sang box
```

```bash
# trên box
minicontainer run --ram=30M --cpu=20 --rootfs=/opt/rootfs-base-mini /bin/sh
bash test.sh        # 74 case
```

## Tài liệu

| File | Nội dung |
|---|---|
| [`PLAN.md`](PLAN.md) | kế hoạch qua từng giai đoạn (lý do, khảo sát phần cứng, P/I/T/R, lịch sử phiên bản) |
| [`GUIDE.md`](GUIDE.md) | hướng dẫn dùng chi tiết: build, deploy, mọi lệnh, profile, test, troubleshooting |
| [`FINAL.md`](FINAL.md) | kiến trúc đầu-cuối: module, luồng syscall, bẫy kernel, bằng chứng, so sánh SOTA |
| [`TASK.md`](TASK.md) | phân chia công việc nhóm 4 người, DoD, việc còn lại, bàn giao |
| [`progress/`](progress/) | nhật ký PHASE1–16, changelog, bằng chứng đo trên box |

Repo: <https://github.com/NAMKHANH145/TV-Box-Container> · Rootfs profiles: [`profiles/README.md`](profiles/README.md)

## Ghi chú vận hành

- Không hard-code IP/mật khẩu: dùng `BOX=`, `SSHPASS_FILE=`, `MC_PULL_URL`.
- Box cần `ip` (bridge), `curl`/`wget` (`pull`), systemd (`service create`); **không** cần gcc/make/Docker.
- Port < 1024 không bind được trong container (dùng 8080+); `--net=bridge` không NAT/DNS.

## License

MIT or Apache-2.0 (to be decided)
