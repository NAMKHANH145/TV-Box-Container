# PHASE12.md — P2-7: `--detach` + `stats` (xong, đã test)

## 1. Thiết kế (vẫn zero-daemon)

- `run -d --name ID`: launcher tạo cgroup, clone, ghi state vào `/run/minicontainer/<id>/` (`pid`, `cgdir`), in id rồi **thoát luôn** — không `waitpid`. Con mồ côi về init, sống trong cgroup/ns của nó. Không process nào ở lại.
- `stats <id>`: đọc state + `memory.current` / `pids.current` / `cpu.stat`, kiểm tra sống/chết qua `/proc/<pid>`. Trạng thái STOPPED khi pid mất (cgroup còn số 0).
- Trùng tên tự thêm hậu tố (`demo-1`, `mc-<pid>-1`).

## 2. Bug: `open()` thiếu `O_CREAT`

`write_file()` viết cho pseudo-file có sẵn (cgroupfs/proc) nên không có `O_CREAT` — state file mới tạo toàn báo `ENOENT` dù `mkdir` thành công. Triệu chứng đánh lừa (dir thì có, file thì không). Fix 1 flag: `O_WRONLY|O_CREAT|O_TRUNC` (vô hại với pseudo-file — `echo >` vẫn làm vậy).

## 3. Kết quả test

| Test | Kết quả | Đạt? |
|---|---|---|
| `run -d --name demo /bin/sleep 60` | in `demo`, launcher thoát ngay | ✅ |
| `stats demo` (đang chạy) | RUNNING, memory 258KB, pids 1, cpu usec | ✅ |
| `stats demo` (đã chết) | STOPPED, số 0 | ✅ |
| `stats` tên không tồn tại | báo rõ, exit 1 | ✅ |
| Stop+rm thủ công (kill theo `cgroup.procs`, `rmdir`, xóa state) | `stats` báo hết container | ✅ |
| Regression foreground | `FG_OK` | ✅ |

## 4. Đánh giá

- Binary 57KB → 87KB cho cả detach+stats — vẫn nhỏ hơn dockerd ~1500 lần.
- Đây là nền cho CLI quản lý (Phase 13): `ps/stop/rm` chỉ là đọc state có sẵn + kill theo cgroup (mẫu đã làm tay ở trên).
