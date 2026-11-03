# Release v1.5.0 — kiểm tra asset musl (Track B: Huy)

Tag: `v1.5.0` (commit `52381d2` → CI run `41657038848`, status **success**).
Asset tải từ `gh release download v1.5.0`, kiểm `SHA256SUMS.txt`: **tất cả OK**.

## 1. Bảng arch (xác minh bằng qemu-user trên laptop)

| Arch | File | Size (B) | sha256 | `--version` (qemu) | `smoke-sdk.sh` |
|---|---|---:|---|---|---|
| aarch64 | `minicontainer-linux-aarch64` | 133.584 | `…` (xem SHA256SUMS) | `minicontainer 1.5.0 (aarch64, musl, static)` | **11/11 PASS** |
| armv7l | `minicontainer-linux-armv7l` | 120.552 | OK | `minicontainer 1.5.0 (armv7l, musl, static)` | **11/11 PASS** |
| riscv64 | `minicontainer-linux-riscv64` | 108.904 | OK | `minicontainer 1.5.0 (riscv64, musl, static)` | **11/11 PASS** |
| x86_64 | `minicontainer-linux-x86_64` | 108.896 | OK | `minicontainer 1.5.0 (x86_64, musl, static)` | **11/11 PASS** |

Ghi chú: qemu-user chỉ chạy được `--version` và `sdk` (không namespace/cgroup), nên smoke-sdk là
bằng chứng chính cho 3 arch không có board. Tất cả 4 arch đều ≤ 200 KB (gate CI).

## 2. Bằng chứng trên phần cứng thật (aarch64, S905W)

- Binary deploy: `minicontainer 1.5.0 (aarch64, musl, static)` (deploy qua `make deploy`).
- `bash test.sh` → **`PASS=84 FAIL=0 TONG=84`** (2026-11-03).
- Ctrl-C 20 lần liên tiếp → **0 leak** (cgroup + `.oldroot`).
- Soak 24 h: đang chạy (`ITER=6000`, ~2 h) — cập nhật sau khi xong.

## 3. Còn lại cho Huy

- [ ] Chạy `qemu-<arch>-static` trên 3 arch còn lại với `--version` + `ps` (đã làm ở §1, có thể bổ sung output thô).
- [ ] Nếu có board armv7l/riscv64/x86_64: chạy `test.sh` trên đó và ghi số.
- [ ] So sánh MIT vs Apache-2.0 (xem `progress/LICENSE-COMPARISON.md`).
- [ ] Cập nhật bảng này nếu soak 24 h cho số mới.

PR chỉ chứa `progress/RELEASE-v1.5.0.md` (và file bằng chứng nếu có). Không sửa `src/`, không tag.
