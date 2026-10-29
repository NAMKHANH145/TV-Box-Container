# progress/ — Nhật ký lịch sử

Thư mục này là **kho lưu trữ**: nhật ký từng phase và bằng chứng đo theo phiên bản.
Tài liệu chính ở thư mục gốc; chỉ mở file ở đây khi cần truy nguyên chi tiết.

| File | Nội dung |
|---|---|
| `PHASE1.md` | PLANNING: scope, Buildroot rootfs, toolchain, Alpine baseline |
| `PHASE2.md` | IMPLEMENT I1–I5: clone/chroot/cgroup/CLI, test T1–T5/T7/T9 |
| `PHASE3.md` | TESTING mở rộng + đối chứng SOTA 2026 (Docker 213 MB / 2.076 s) |
| `PHASE4.md` | P2-1: `pids.max` + `CLONE_NEWIPC` (fork-bomb bị chặn) |
| `PHASE5.md` | P2-2: `io.max` (20 MB/s → 1 MB/s) |
| `PHASE6.md` | P2-6: `/dev` + `/sys` + fix mount leak |
| `PHASE7.md` | P2-11: điều tra SSH chậm sau reboot (Tailscale double-NAT ~4.5 phút) |
| `PHASE8.md` | P2-3: chroot → pivot_root, test tấn công chroot-break |
| `PHASE9.md` | P2-4: user namespace, root trong / nobody ngoài (3 bẫy kernel) |
| `PHASE10.md` | P2-5: seccomp-bpf deny-list viết tay |
| `PHASE11.md` | P2-8: musl, binary 658 KB → 57 KB |
| `PHASE12.md` | P2-7: `--detach` + `stats` |
| `PHASE13.md` | CLI `ps/stats/stop/rm` — vòng đời đầy đủ |
| `PHASE14.md` | P2-10: `test.sh` tự động, 21/21 |
| `PHASE15.md` | P2-9: OCI-bundle mode tối thiểu |
| `PHASE16.md` | Dọn rác + tách module (10 module, Makefile) |
| `PLAN-2.md` | Backlog P2-1…P2-11 (đã đóng hết) |
| `PLAN-3.md` | Đánh giá điểm yếu I1–I7 / F1–F9 và 5 tầng testing (đã đóng hết) |
| `PLAN-4.md` | Lộ trình R1–R5 + định hướng sản phẩm (đã đóng R1–R5, R5-1) |
| `CHANGELOG.md` | Lịch sử thay đổi chi tiết v1.0.0 → v1.4.0 (Added/Changed/Fixed/Verified) |
| `VERIFICATION_LOG.md` | Bằng chứng đo trên box theo từng phiên bản: test suite, exec/bridge, MQTT/lighttpd, soak, đa kiến trúc, CI musl, dọn đĩa |

Nội dung đã được hợp nhất vào tài liệu chính:

- `PLAN.md` ← PLAN/PLAN-2/PLAN-3/PLAN-4 (kế hoạch), IDEA.md, SEARCH.md (khảo sát phần cứng)
- `FINAL.md` ← BAOCAO.md, FINAL.md cũ, VERIFICATION_LOG.md (phần kết quả)
- `GUIDE.md` ← README.md cũ, GUIDE.md cũ (kết nối SSH)
- `TASK.md` ← TASKS.md

Các file gốc đã xoá khỏi cây làm việc (IDEA.md, SEARCH.md, BAOCAO.md, TASKS.md) vẫn còn trong
lịch sử git nếu cần đối chiếu (`git log --all -- <path>`).