# PHASE16.md — Dọn rác + tách module (xong)

## 1. Dọn box (test liên tục để lại ~300MB rác)

| Rác | Nguyên nhân | Xử lý |
|---|---|---|
| `/opt/oci-test` 272M (rootfs 1.3MB phình 200 lần) | `scp -r` dereference symlink → 70 applet × 1MB thành file riêng | Xóa; bài học: chuyển rootfs chỉ dùng `tar pipe` (giữ symlink) — đã verify 1.3M khi test lại |
| `/tmp` 5MB (tarball Alpine, binary cũ, log test) | test để lại trên tmpfs = tốn RAM | Xóa hết, `/tmp` về 0 |
| Disk | 3.0G → **2.7G** | về đúng mốc phase 1 |

Giữ lại có chủ đích: `/usr/local/bin/minicontainer`, `/opt/rootfs/{bomb,breakout}` (tool test), `/root/test.sh`, 2 rootfs.

## 2. Dọn folder local

Xóa: `hello.*` (test toolchain 1 lần), `minicontainer-musl` (trùng output), `bomb/breakout` binary (giữ source + `make tools`), `baseline-alpine` (trùng tarball). Giữ `buildroot/` (7.3G — toolchain, xóa là build lại hàng giờ).

## 3. Tách monolith 635 dòng → 10 module (vẫn 1 binary static)

| Module | Việc |
|---|---|
| `common.h/c` | die, parse_bytes, file I/O, struct cfg |
| `seccomp.c` | filter BPF |
| `ns.c` | child_fn, pivot, userns map |
| `cgroup.c` | setup/attach/cleanup v2 |
| `run.c` | điều phối clone + detach/wait |
| `state.c` | ps/stats/stop/rm |
| `cli.c` | parse flags |
| `oci.c` | bundle mode |
| `main.c` | dispatch |
| `Makefile` | `make` / `make tools` / `make deploy BOX=..` / `make clean` |

Đánh giá có nên tách (bạn hỏi "nếu phù hợp"): **phù hợp** — mỗi module ánh xạ đúng 1 cơ chế kernel (bảo vệ trình bày theo module), biên dịch riêng lẻ, vẫn 1 binary (không phải microservice mạng — cái đó mới là phản luận điểm zero-daemon).

## 4. Verify sau tách: build sạch warning, size **byte-identical** (95,752), `test.sh` **21/21**, `oci-run` OK.
