# PLAN-2.md — Backlog khắc phục điểm yếu (triển khai tiếp sau phase 3)

Thứ tự theo giá trị/bảo vệ trên môn HĐH: làm từ trên xuống. Mỗi mục xong phải có test tương ứng trong `test.sh` (P2-10).

## Đợt 1 — dễ, ăn điểm ngay (mỗi mục < 1 giờ)

- [x] **P2-1. Siết thêm 2 controllers**: ghi `pids.max` (chống fork-bomb, fix W4) + thêm `CLONE_NEWIPC` vào flags clone (fix W12). Test: bomb `while fork` bị kill ở ngưỡng; `ipcs` trong/ngoài khác nhau.
- [x] **P2-2. Giới hạn disk I/O**: ghi `io.max` cho device eMMC (fix W5). Test: `dd` trong container bị bóp tốc độ, host vẫn mượt.
- [x] **P2-6. Hoàn thiện `/sys` + `/dev`**: mount `sysfs` read-only, `mknod null/zero/urandom` tối thiểu (fix W8). Test: app đọc `/dev/urandom` chạy được, ghi `/sys` bị từ chối.
- [x] **P2-11. Điều tra SSH LAN lên chậm sau reboot** (W13): `systemd-analyze blame` trên box + kiểm tra DHCP phía laptop, chốt kết luận vào SEARCH.md.

## Đợt 2 — bảo mật sâu kernel (trọng tâm bảo vệ)

- [x] **P2-3. `chroot` → `pivot_root`** (fix W2): mount --bind rootfs thành mount point riêng, `pivot_root` + `put_old`, kèm `nosuid,nodev`. Test tấn công chroot-break mẫu phải thất bại.
- [x] **P2-4. User namespace** (fix W3): `CLONE_NEWUSER` + `uid_map 0 100000 65536`, container tưởng root nhưng host chỉ là user thường. Test riêng vì kernel mod có thể chặn mapping.
- [x] **P2-5. Seccomp-bpf cơ bản** (fix W6): profile chặn `mount/reboot/kexec/ptrace`... theo cách crun làm. Test: gọi syscall bị chặn trả `EPERM`, app thường vẫn chạy.

## Đợt 3 — hoàn thiện công cụ

- [x] **P2-8. Build musl cho binary nhỏ** (fix W10): biên dịch bằng toolchain musl của Buildroot thay vì glibc cross Ubuntu, mục tiêu < 100KB. So `ls -la` trước/sau vào báo cáo.
- [x] **P2-7. Detach + quan sát** (fix W9): `--detach` + pidfile, lệnh `ministat <id>` đọc `memory.current`/`cpu.stat`. Không làm daemon — detach là fork thoát, không process nào ở lại.
- [x] **P2-10. `test.sh` tự động** (fix W11): script chạy T1–T10 + đợt 1–2, assert exit code/log, in bảng PASS/FAIL. Chạy 1 lệnh là có toàn bộ số liệu báo cáo.

## Đợt 4 — câu hỏi lớn (cân nhắc, có thể chốt "ngoài scope")

- [x] **P2-9. OCI-bundle mode** (fix W1): đọc `config.json` tối thiểu (rootfs path, hostname, mounts, linux.resources) để chạy bundle OCI có sẵn. Nếu tốn > 2 ngày → ghi rõ "ngoài scope có lập luận" thay vì làm dở.

## Tiêu chí đóng PLAN-2

Đợt 1 + 2 xong, `test.sh` xanh toàn bộ, số liệu mới (pids/io/user-ns/seccomp) bổ sung vào PHASE3.md §1. Khi đó đề tài hết điểm yếu nghiêm trọng đã biết.
