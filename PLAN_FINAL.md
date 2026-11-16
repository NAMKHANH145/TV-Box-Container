# PLAN_FINAL.md — minicontainer v2.0: runtime OCI cho thiết bị nhúng

> Mục tiêu: **runtime tương thích OCI, chạy được image Docker không sửa gì**, vẫn là 1 binary C
> tĩnh dưới 300 KB, 0 daemon, 0 MB RAM khi rảnh, và **mọi tuyên bố đều có lệnh kiểm chứng**.
>
> Hiện trạng: v1.5.4 · 3.300 dòng C · 169 KB (aarch64 musl) · 84/84 test · soak 6000 vòng.
> Phạm vi v2.0: **P0–P4 (kỹ thuật) + P6 (adoption)** — đủ để ~100 người ngoài cài và dùng được.
> Tài liệu này là kế hoạch thực thi + hồ sơ bảo vệ. Cập nhật lần cuối: 2026-11-16.

---

## 0. Definition of Done — "hoàn hảo" nghĩa là gì

Không có "hoàn hảo tuyệt đối". Định nghĩa dùng được: **hoàn hảo trong phạm vi đã khai báo**, với 5 cổng.

### 0.1 Cổng chức năng

```bash
minicontainer load nginx.tar
minicontainer run -d --name web -p 8080:80 nginx:alpine
curl -s localhost:8080 | grep -q "Welcome to nginx"      # OK

minicontainer pull alpine:3.20                            # từ Docker Hub
minicontainer pull nginx:alpine                           # trên aarch64 -> manifest arm64
minicontainer images && minicontainer logs web && minicontainer rmi nginx:alpine
minicontainer save nginx:alpine -o nginx-out.tar          # air-gap: mang đi máy khác

# build tại chỗ, không cần Docker (Dockerfile tập con)
minicontainer build -t myapp:1.0 .                        # FROM/COPY/RUN/ENV/CMD
minicontainer build -t myapp:1.0 --platform=linux/arm64 . # build cho arch khác

# ship cả đội bằng 1 file, có chữ ký
minicontainer export -o fleet.tar.zst --arch=aarch64
minicontainer import fleet.tar.zst                        # trên thiết bị đích
```

Bộ image phải chạy được (corpus tối thiểu 6 image, 3 nhóm):

| Nhóm | Image | Chứng minh điều gì |
|---|---|---|
| cơ bản | `alpine:3.20`, `busybox:latest` | layer, shell, user root |
| service | `nginx:alpine`, `redis:alpine` | port, USER khác root, ghi thư mục |
| runtime nặng | `python:3.12-slim`, `node:20-slim` | image lớn, nhiều layer, page cache |
| build tại chỗ | `testdata/Dockerfile` (FROM alpine + COPY + RUN rm) | parser, layer diff, whiteout |

### 0.2 Cổng chất lượng

- `build/test.sh` cũ: **84/84 PASS** sau **mỗi** phase (không được tụt).
- `build/soak.sh` 6000 vòng: 0 rò cgroup/mount/state, RSS không tăng.
- Fault injection: `kill -9` tại 8 điểm, sau đó `gc` phải sạch.
- Reboot/power-cut: không còn cgroup/veth/iptables/state rác.

### 0.3 Cổng tài nguyên (ngưỡng đỏ, vượt là fail)

| Chỉ số | Hiện tại | Mục tiêu v2.0 | Ngưỡng đỏ |
|---|---|---|---|
| Binary aarch64 | 169 KB | **≤ 280 KB** | 320 KB |
| RAM idle | 0 MB | **0 MB** | 5 MB |
| `run /bin/true` p50 | 10–65 ms | **≤ 80 ms** | 100 ms |
| Thư viện ngoài | 0 | **0** | 1 |
| Daemon | 0 | **0** | 1 |
| Thời gian `check` | — | **< 2 s** | 5 s |

### 0.4 Cổng minh bạch

- Mỗi tuyên bố trong README/AGENTS phải có lệnh tái lập được.
- Số đo ghi vào `progress/VERIFICATION_LOG.md`, kèm arch + kernel + hash binary.
- Không nhận "OCI compliant" chung chung — chỉ nhận theo bảng §1.2.

### 0.5 Cổng adoption (đủ cho ~100 người ngoài)

Ba bài test, phải qua trước khi nói "dùng được":

| Test | Cách đo | Ngưỡng |
|---|---|---|
| **Người lạ cài** | 3 người chưa từng xem repo, chỉ đọc README | cài + chạy `nginx` trong **< 10 phút**, không hỏi gì |
| **Recipe chạy** | 5 recipe trong `recipes/` trên 2 distro (Ubuntu 22.04, 24.04) | 10/10 lượt chạy được |
| **Báo lỗi đủ thông tin** | issue mới theo template | có `check --json` + `version --json` + log |

Nguyên tắc: nếu người ngoài phải hỏi "cài thế nào", đó là lỗi tài liệu, không phải lỗi người dùng.

---

## 1. Phạm vi tuân thủ OCI

### 1.1 Ba spec, ba mức khác nhau

| Spec | Nội dung | Mức làm | Ghi chú |
|---|---|---|---|
| **image-spec** | `index.json`, `manifest.json`, `config.json`, layer tar.gz | **đầy đủ** | phần quyết định "chạy được image Docker" |
| **distribution-spec** | Registry v2 API | **tập con: pull + resolve manifest list** | không push, không auth nâng cao |
| **runtime-spec** | `config.json` → namespace, mount, cgroup, seccomp | **tập con, khai báo từng trường** | §1.2 |

### 1.2 Bảng đối chứng runtime-spec (in ra khi bị hỏi)

| Trường OCI | Ta xử lý | Phase |
|---|---|---|
| `process.args`, `env`, `cwd`, `terminal` | `--env`, `-w`, argv | P3.1 |
| `process.user.uid/gid`, `additionalGids` | `--user`, map trong userns | P3.1 |
| `process.rlimits` | `RLIMIT_NOFILE/FSIZE/NPROC` | P3.1 |
| `root.path`, `root.readonly` | `--rootfs`, `--read-only` | có |
| `mounts[]`: proc, tmpfs, bind, devpts, sysfs(ro), shm | `namespace_setup.c` | P1.5 |
| `linux.namespaces`: pid/net/mnt/uts/ipc/user | `CLONE_NEW*` | có |
| `linux.resources.memory/cpu/pids` | cgroup v2 | có |
| `linux.resources.memory.high`, `cpu.weight` | thêm | P3.5 |
| `linux.resources.devices` | **không** — không có device cgroup trong userns | non-goal |
| `linux.seccomp` (profile JSON của Docker) | **dịch JSON → BPF của ta** | P3.6 |
| `linux.capabilities` | drop hết, giữ `NET_RAW` + `NET_BIND_SERVICE` | một phần |
| `hooks` | **không** | non-goal |
| `annotations` | bỏ qua | — |
| `linux.apparmorProfile`, `selinuxLabel` | **không** | non-goal |

### 1.3 Điểm khác Docker có chủ đích (phải nói trước)

| Điểm | Docker | Ta | Vì sao |
|---|---|---|---|
| Đổi root | `pivot_root` | `chroot` | `pivot_root` trả `EPERM` dưới userns trên kernel box (đã thử) |
| Seccomp | profile JSON | BPF tự sinh + dịch JSON | không dùng libseccomp |
| Cgroup | v1 + v2 | chỉ v2 | box chỉ có v2; v1 là nhánh code chết |
| Image store | overlay2 + layer DB | sha256 store + overlayfs | không cần DB |
| Build | Dockerfile đầy đủ | tập con trong `build.c` | giữ nhẹ, đủ cho image nhúng |
| `ps -a` | giữ container đã chết | không giữ (container = process) | không cần DB state; `logs` thay thế |

---

## 2. Tuân OCI có làm nặng lên không?

Spec không nặng: image-spec = JSON + tar; distribution = HTTP; runtime = JSON. **Tính năng mới nặng.**

| Hạng mục | Cách làm | LOC | KB |
|---|---|---|---|
| jsmn (JSON) | vendor 1 header MIT, không malloc | 200 | 6 |
| Áp layer (whiteout/xattr/hardlink) | tự viết trên `tar` | 300 | 10 |
| Image store + overlayfs | file + `mount("overlay")` | 250 | 7 |
| Registry client | gọi `/usr/bin/curl`, không link libcurl | 300 | 8 |
| OCI seccomp JSON → BPF | map `defaultAction` + `syscalls[]` | 250 | 7 |
| Logs json-file + rotate | `open/write` | 150 | 4 |
| CLI mới (`load/images/rmi/logs/save/inspect`) | dispatch | 200 | 5 |
| Preflight `check` | đọc `/proc`, `/sys` | 80 | 2 |
| `gc` + timer | quét state/cgroup/veth | 150 | 4 |
| A/B update + rollback | symlink flip + health gate | 200 | 5 |
| Chữ ký gói (minisign) | verify file .sig | 80 | 2 |
| Watchdog phần cứng | `open("/dev/watchdog")` | 50 | 1 |
| **Builder** (Dockerfile tập con) | parse + overlay diff + cache | 750 | 21 |
| **Export/import bundle** | tar + verify chữ ký + cài đặt | 250 | 7 |
| Delta update | `zstd --patch-from` (gọi ngoài) | 150 | 4 |
| **Tổng phần lõi** | | **~3.360** | **+93** |

**Kết luận:** 169 KB → **~265 KB** (ước tính, +55%). Vẫn nhẹ hơn Docker ~780 lần.
Bảng trên chỉ tính phần lõi; CLI/ops còn ~2.200 dòng nữa (xem §4), tổng ~5.600 dòng mới.
RAM idle **0 MB**; latency `run` **không đổi**; builder chỉ chạy trên laptop (hoặc box lúc build, không phải lúc vận hành).

Nếu nhận **đầy đủ** runtime-spec (hooks, device cgroup, AppArmor, cgroups v1, uidMappings tuỳ biến):
+1.500 dòng, +60 KB, và mất "1 binary không phụ thuộc" → **không làm**, khai báo tập con.

---

## 3. Kiến trúc v2.0

```
build/src/
  main.c        dispatch (+load/images/rmi/logs/save/inspect/check/gc)
  cli.c         flag (+--env --user -w -v -p udp --restart --require-signature)
  run.c         supervisor (image ref -> store -> overlay -> rootfs)
  ns.c / namespace_setup.c   (+ mounts[] OCI, /etc/hosts cho peer bridge)
  cgroup_v2.c   (+ memory.high, cpu.weight)
  bridge.c      (+ UDP publish, cấp IP theo state, dọn iptables khi gc)
  disk.c        (+ kiểm tra dung lượng trước extract)
  seccomp.c     (+ chế độ "image": deny-list; + dịch JSON)
  oci.c         runtime-spec tập con
  state.c       (+ metadata image, refcount, inspect JSON)
  log.c         json-file + rotate + follow
  ---- mới ----
  jsmn.h        parser JSON (vendor, MIT)
  img.c         index/manifest/config, digest, ref
  layer.c       whiteout, xattr, hardlink, uid/gid
  store.c       content-addressed + overlayfs + gc
  registry.c    token, manifest list, blob
  preflight.c   check kernel/fs/net
  update.c      A/B + rollback + verify chữ ký
```

Dữ liệu:

```
/var/lib/minicontainer/images/sha256/<digest>/{manifest,config,layers/}
/var/lib/minicontainer/refs/<name>-<arch> -> digest
/var/lib/minicontainer/overlay/<cid>/{upper,work}
/var/lib/minicontainer/current -> slots/A | slots/B      (A/B update)
/var/log/minicontainer/<cid>.log
/run/minicontainer/<id>/                                   (state, tmpfs)
```

Đường dữ liệu:

```
pull/load -> store(sha256) -> overlay(lower=layers, upper=cid) -> run -> chroot -> exec
```

---

## 4. Các phase

### P0 — Nền tảng & an toàn (~660 dòng)

| ID | Việc | File | LOC | Cổng |
|---|---|---|---|---|
| P0.1 | `--arch` khi pack, `manifest.json`, guard `e_machine` vs `uname -m` | `sdk.c` `pull.c` `run.c` | 100 | sai arch → lỗi rõ ràng |
| P0.2 | Log json-file + `logs` + rotate 2×1 MB | `run.c` `log.c` | 150 | log đúng, xoay đúng |
| P0.3 | `gc` + đối soát lúc khởi động + timer systemd | `state.c` `main.c` | 150 | `kill -9` → `gc` → sạch |
| P0.4 | Siết extract: device node, hardlink thoát gốc, giới hạn entry/size | `pull.c` `layer.c` | 100 | tar độc hại bị từ chối |
| P0.5 | `minicontainer check` (preflight) | `preflight.c` | 80 | in đúng 8 dòng, <2 s |
| P0.6 | `--require-signature` + verify minisign | `update.c` `pull.c` | 80 | gói sai chữ ký bị từ chối |

**Gate P0:** 84/84 + case mới (đo thực tế: **108 case, 0 FAIL** trên laptop); binary < 200 KB
(đo thực tế aarch64 musl: **197,5 KB**, tức +28,5 KB so với 169 KB — cao hơn ước tính +2..8 KB
vì P0 gánh cả `log.c` + `update.c` + `gc`; các phase sau còn ~65 KB, vẫn dưới mốc đỏ 320 KB);
`check` chạy trên x86_64 và aarch64.

### P1 — image-spec: `load` từ `docker save`/`skopeo` (~2.300 dòng)

| ID | Việc | File | LOC | Cổng |
|---|---|---|---|---|
| P1.1 | jsmn + parse index/manifest/config + verify digest | `jsmn.h` `img.c` | 350 | `load` in đúng config |
| P1.2 | Áp layer: whiteout, opq, hardlink 2 pass, mode, uid/gid, xattr | `layer.c` | 300 | `ping` trong image chạy |
| P1.3 | Store sha256 + refcount + overlayfs (fallback copy) | `store.c` | 250 | 2 container cùng image → 1 bản layer |
| P1.4 | `load`, `images`, `rmi`, `save` | `main.c` `store.c` | 200 | round-trip load→save→load |
| P1.5 | `mounts[]` OCI: proc, tmpfs, bind, devpts, shm | `namespace_setup.c` | 150 | nginx ghi `/var/cache/nginx` |
| P1.6 | Kiểm tra dung lượng đĩa trước extract | `disk.c` | 50 | image 400 MB trên đĩa trống → từ chối sớm |
| P1.7 | **`build`**: Dockerfile tập con, overlay diff, cache layer | `build.c` | 750 | `build -t myapp .` rồi `run myapp` |
| P1.8 | **`export`/`import`**: bundle 1 file + chữ ký | `ship.c` `update.c` | 250 | import trên máy sạch → chạy được |

**Gate P1:** 6 image corpus chạy; `docker save` → `load` → `run` OK; 84/84; < 230 KB.
Đo thực tế sau P1.1–P1.4 + P1.6: **217,5 KB**, `docker save alpine:3.19` → `load` → `run` OK,
whiteout 2 layer OK, `save`/`rmi`/`load` round-trip OK (116 case, 0 FAIL).

### P2 — distribution-spec: `pull` (~500 dòng)

| ID | Việc | File | LOC | Cổng |
|---|---|---|---|---|
| P2.1 | Token flow (401 → realm/service/scope), manifest list, chọn arch | `registry.c` | 200 | `pull nginx:alpine` trên aarch64 |
| P2.2 | Blob + verify sha256 + resume + giới hạn dung lượng | `registry.c` | 100 | ngắt mạng giữa đường → tải lại |
| P2.3 | `build --platform` + manifest list nhiều arch | `build.c` `img.c` | 200 | build 1 Dockerfile ra 2 arch |

**Gate P2:** pull 3 image; `pull --offline` từ thư mục OCI layout; 84/84; < 220 KB.

### P3 — runtime-spec tập con + CLI parity (~1.270 dòng)

| ID | Việc | File | LOC | Cổng |
|---|---|---|---|---|
| P3.1 | `--env`, `-w`, `--user`, rlimits | `cli.c` `exec.c` | 150 | image `USER nginx` chạy đúng uid |
| P3.2 | `inspect`, `restart`, `-v` volume, `--restart=no/on-failure/always` | `state.c` `main.c` `systemd.c` | 220 | inspect JSON khớp `ps --json` |
| P3.3 | Port < 1024: `ip_unprivileged_port_start=0` trong netns | `namespace_setup.c` | 20 | nginx listen 80 |
| P3.4 | UDP publish | `bridge.c` | 80 | `-p 5353:53/udp` |
| P3.5 | `memory.high`, `cpu.weight` | `cgroup_v2.c` | 60 | image lớn không OOM oan |
| P3.6 | Dịch OCI seccomp JSON → BPF | `seccomp.c` | 250 | dùng profile `default.json` của Docker |
| P3.7 | `/etc/hosts` cho peer bridge (tên container) | `bridge.c` | 60 | container gọi nhau bằng tên |
| P3.8 | `run --emulate`: qemu trên thiết bị (tuỳ chọn, mặc định tắt) | `run.c` `seccomp.c` | 150 | chạy image ngoại arch, có cảnh báo chi phí |
| P3.9 | `cp`: copy file vào/ra container đang chạy | `exec.c` | 120 | `cp web:/etc/nginx/nginx.conf .` |
| P3.10 | `--init`: reaper tí hon cho app không reap zombie | `exec.c` | 60 | shell script không sinh zombie |
| P3.11 | `run --rm`: tự xoá state khi thoát | `run.c` | 20 | `run --rm` không để lại state |
| P3.12 | `start`: chạy lại container đã dừng từ config đã lưu | `run.c` `state.c` | 80 | `stop` rồi `start` chạy lại đúng lệnh |

**Gate P3:** 6 image; `nginx` + `redis` nói chuyện qua tên; 84/84; < 240 KB; p50 ≤ 80 ms.

### P4 — Vận hành & cập nhật (~850 dòng)

| ID | Việc | LOC | Cổng |
|---|---|---|---|
| P4.1 | A/B rootfs + symlink flip + rollback khi health fail | 200 | cập nhật hỏng → tự quay về bản cũ |
| P4.2 | Manifest có chữ ký + pin key | 80 | sửa 1 byte → từ chối cập nhật |
| P4.3 | Hardware watchdog `/dev/watchdog` | 50 | treo → reset (test trên box) |
| P4.4 | Metrics textfile + event journald | 150 | node_exporter đọc được |
| P4.5 | Compose-lite: N service trong 1 file | 220 | 3 service lên cùng lúc |
| P4.6 | Delta update (`zstd --patch-from`) | 150 | bản vá nhỏ hơn 10–50× |

**Gate P4:** update/rollback 3 lần liên tiếp không hỏng; reboot 5 lần sạch; 84/84.

### P5 — Mở rộng (chỉ làm nếu còn thời gian)

| ID | Việc | LOC | Ghi chú |
|---|---|---|---|
| P5.1 | Rootless (userns + cgroup delegation) | 250 | cần systemd `Delegate=yes`; rủi ro cao |
| P5.2 | Delta update (`zstd --patch-from`) | 150 | giảm băng thông 10–50× |
| P5.3 | Registry push | 200 | không cần cho thiết bị |

### 4.5 P6 — Adoption: đủ cho ~100 người ngoài (~1.200 dòng, 0 dòng core)

Điều kiện tiên quyết: **P1 + P2 + P3 xong** (phải có `build`/`pull`/`logs` thì người ngoài mới dùng được).
P6 chạy song song với P4, không chờ P5.

| ID | Việc | Sản phẩm | Cổng |
|---|---|---|---|
| P6.1 | `install.sh`: phát hiện arch, tải từ release, verify sha256, cài `/usr/local/bin`, tạo thư mục, chạy `check` | 1 script ~80 dòng | 1 lệnh cài trên 4 arch |
| P6.2 | `README.md` tiếng Anh: why, quickstart 60 s, log demo, bảng so sánh, giới hạn nói trước | 1 file | người lạ đọc 3 phút là chạy được |
| P6.3 | `COMPAT.md`: kernel, arch, cgroup, overlayfs, xattr, iptables — cái gì chạy, cái gì không | 1 bảng | mỗi dòng có lệnh kiểm chứng |
| P6.4 | `recipes/`: nginx, Node, Python, Mosquitto MQTT, gateway 2 service (compose-lite) | 5 thư mục + Dockerfile + README | 10/10 lượt chạy trên 2 distro |
| P6.5 | `.github/`: ISSUE_TEMPLATE (bug/feature), PR template, `SECURITY.md`, `CHANGELOG.md`, `CONTRIBUTING.md` | 6 file | issue mới tự có `check --json` |
| P6.6 | Release ký: 4 asset + `SHA256SUMS` + `.sig` + hướng dẫn build tái lập | script `release.sh` mở rộng | verify được từ source |
| P6.7 | CI: build 4 arch + smoke + `local-test` trên Ubuntu 22.04/24.04 + badge | workflow | badge xanh, log công khai |
| P6.8 | Support model: nhãn `supported`/`experimental`, chính sách N-1, quy trình deprecate | 1 mục trong README | người dùng biết cái gì được bảo hành |
| P6.9 | Chẩn đoán máy đọc được: `check --json`, `version --json` | 2 lệnh | dán vào issue là đủ debug |

#### P6.1 `install.sh` — thiết kế

```bash
curl -fsSL https://<release>/install.sh | sh
# 1. uname -m -> aarch64|armv7l|x86_64|riscv64
# 2. tải minicontainer-<ver>-linux-<arch> + .sha256 + .sig
# 3. verify sha256 (+ minisign nếu có)
# 4. install -m755 -> /usr/local/bin/minicontainer
# 5. mkdir -p /var/lib/minicontainer /run/minicontainer /var/log/minicontainer
# 6. minicontainer check  -> in báo cáo, exit != 0 nếu thiếu điều kiện
```

Ba quy tắc: không chạy `sudo` ngầm (in lệnh ra để người dùng tự chạy), không ghi đè nếu đang có
container chạy (hỏi `--force`), luôn in phiên bản + hash sau khi cài.

#### P6.4 Recipe — nội dung mỗi thư mục

```
recipes/nginx/
  Dockerfile            # FROM nginx:alpine + COPY site
  site/index.html
  README.md             # 4 lệnh: build, run, curl, logs
  expected.txt          # output mong đợi, dùng cho test tự động
```

`expected.txt` cho phép CI chạy recipe và so output — biến tài liệu thành test.

#### Định nghĩa "đủ cho 100 người"

| Chỉ số | Ngưỡng |
|---|---|
| Người lạ cài thành công | 3/3 trong < 10 phút, không hỏi |
| Issue "không cài được" | 0 trong 30 ngày đầu |
| Recipe chạy | 10/10 (5 recipe × 2 distro) |
| Bug report có đủ `check --json` | ≥ 80% |
| Thời gian trả lời issue | best-effort ≤ 72 h (ghi rõ là best-effort) |
| Version được hỗ trợ | bản mới nhất + N-1 |

#### Không nằm trong P6 (để khỏi phình)

| Không làm | Vì sao |
|---|---|
| Website docs riêng | `README.md` + `docs/` đủ cho 100 người |
| Đa ngôn ngữ | tiếng Anh là chuẩn; tiếng Việt giữ trong `docs/` |
| Forum/Discord | GitHub Issues đủ ở mức 100 người |
| Telemetry | không thu thập; `check --json` do người dùng tự gửi |
| SLA thương mại | ngoài phạm vi đồ án |

### 4.6 Đối chiếu CLI với Docker — "tính năng cốt lõi" nghĩa là gì

Bảng này là câu trả lời cho "core phải có". Cột "Ta" chỉ trạng thái **sau plan**.

| Lệnh Docker | Ta | Ghi chú |
|---|---|---|
| `run`, `run -d`, `run --rm` | ✅ P3.11 | |
| `ps`, `ps -a` | ✅ / ❌ | ta không giữ container đã chết (container = process); xem `logs` thay thế |
| `stop`, `kill`, `rm`, `restart` | ✅ | `restart` = stop + chạy lại config |
| `start` | ✅ P3.12 | chạy lại config đã lưu |
| `exec` | ✅ | |
| `logs`, `logs -f` | ✅ P0.2 | json-file + xoay vòng |
| `inspect` | ✅ P3.2 | JSON |
| `stats` | ✅ | memory/pids/cpu |
| `cp` | ✅ P3.9 | container đang chạy |
| `images`, `rmi`, `pull`, `save`, `load` | ✅ P1/P2 | |
| `build` | ✅ P1.7 | Dockerfile tập con + cache |
| `tag` | ✅ P1.4 | local, không push |
| `--init` | ✅ P3.10 | reaper cho app không reap |
| `compose` | ⚠️ P4.5 | compose-lite, không đầy đủ |
| `--platform` | ⚠️ build P2.3, run P3.8 | |
| `push`, `login` | ❌ | non-goal (ship bằng bundle) |
| `network`, `volume` đầy đủ | ❌ | bridge + bind/named đơn giản |
| `top`, `diff`, `commit`, `export` container | ❌ | non-goal |
| `--privileged`, `--device` | ❌ | non-goal, có lý do bảo mật |
| `swarm`, `stack` | ❌ | ngoài phạm vi |

Kết luận: **15/20 lệnh cốt lõi có** (13 đầy đủ, 2 một phần: `compose`, `--platform`);
5 lệnh thiếu đều là non-goal có lý do.
`ps -a` là khác biệt thiết kế (không có container "đã chết"), phải nói trước khi bị hỏi.

### 4.7 Build & Ship — thiết kế chi tiết

Đây là phần trả lời trực tiếp hai câu: *"lấy image ở đâu ra"* và *"ship cả đội bằng gì"*.

#### 4.7.1 `minicontainer build` — Dockerfile tập con, không cần Docker

Hỗ trợ (đúng những gì 90% image nhúng cần):

| Lệnh | Xử lý | Ghi chú |
|---|---|---|
| `FROM` | image trong store, hoặc `scratch` | không hỗ trợ `AS` multi-stage |
| `COPY` | copy từ build context, giữ mode/symlink | không hỗ trợ `--from` |
| `RUN` | chạy `/bin/sh -c` **trong container của ta** | cần `/bin/sh` trong base image |
| `ENV`, `WORKDIR`, `USER`, `EXPOSE` | ghi vào `config.json` | |
| `CMD`, `ENTRYPOINT` | ghi vào `config.json` | |
| `ARG`, `LABEL`, `HEALTHCHECK` | bỏ qua, cảnh báo | ghi rõ trong output |

Thuật toán (giống buildah, không cần daemon):

```
1. parse Dockerfile -> danh sách bước
2. base = image FROM (giải nén layer vào build_root) hoặc thư mục rỗng
3. với mỗi bước:
   COPY   -> copy context vào build_root, tar phần mới -> layer
   RUN    -> overlay: lower=build_root, upper=step_N, work=work_N
             fork+exec chính binary này: "run --rootfs=merged /bin/sh -c <cmd>"
             đóng overlay -> tar upper -> layer (xem 4.7.2)
   ENV... -> chỉ sửa config, không tạo layer
4. ghi layer digest, manifest.json, config.json, tag ref
```

Cache: khóa = `sha256(parent_digest + nguyên văn lệnh)`. Trùng → dùng lại layer, bỏ qua `RUN`.
Đây là lý do build lần 2 gần như tức thì.

Chi phí: 750 dòng. Không daemon, không Docker, không cần root nếu chỉ COPY;
`RUN` cần root như `run` (vì chroot + cgroup).

#### 4.7.2 Sinh layer từ overlay (phần khó nhất, ~120 dòng)

```
trước RUN:  upper rỗng
sau RUN:    upper chứa file mới/sửa + whiteout (char dev 0:0) cho file bị xoá
tar upper:
  - file thường  -> thêm như bình thường
  - char dev 0:0 -> đổi thành entry ".wh.<tên>" (đúng chuẩn OCI)
  - opq dir      -> thêm ".wh..wh..opq" vào đầu thư mục
  - xattr        -> giữ security.capability nếu có
```

Nếu bỏ bước đổi whiteout, image build ra sẽ **giữ lại file đã xoá** — đây là lỗi phổ biến nhất
khi tự viết builder, và là case test bắt buộc (`RUN rm /etc/secret`).

#### 4.7.3 Cross-arch: 3 mức, mặc định **0 MB RAM** trên thiết bị

| Mức | Cơ chế | RAM thêm trên thiết bị | Đĩa thêm | Khi nào dùng |
|---|---|---|---|---|
| **0 (mặc định)** | manifest list + image riêng từng arch | **0 MB** | 0 | luôn dùng nếu có bản đúng arch |
| **1 (khuyến nghị)** | `build --platform=linux/arm64` chạy qemu **trên laptop** | **0 MB** | 0 | khi cần tạo ảnh cho arch khác |
| 2 (tuỳ chọn) | `run --emulate` dùng qemu trên thiết bị | +15–40 MB | +4–10 MB | chỉ khi bắt buộc chạy ảnh ngoại arch |

Điểm cốt lõi: **qemu chỉ cần ở nơi build, không cần ở nơi chạy.** Laptop 16 GB RAM dịch được
`RUN` của image arm64; box 1,8 GB chỉ nhận layer đã build sẵn. Đây là cách giải cross-arch
"đúng" mà không tốn RAM thiết bị.

Nếu bật mức 2, phải tự hạ seccomp xuống deny-list (qemu dùng nhiều syscall ngoài allow-list)
và chấp nhận chậm 4–20×. Mặc định **tắt**, có cờ rõ ràng.

#### 4.7.4 `export` / `import` — ship cả đội bằng 1 file

```
minicontainer export -o fleet.tar.zst --arch=aarch64 --sign key.sec
  fleet.tar.zst gồm:
    bin/minicontainer            (binary đúng arch)
    images/sha256/...            (layer + manifest + config)
    refs/                        (tên -> digest)
    manifest.json                (arch, phiên bản, danh sách image, sha256 từng phần)
    manifest.json.sig            (minisign)

minicontainer import fleet.tar.zst
  1. verify chữ ký + sha256 từng phần
  2. cài binary vào /usr/local/bin (atomic: ghi .tmp rồi rename)
  3. giải nén image vào store (staging -> rename)
  4. chạy check, in báo cáo
  5. (tuỳ) start service đã khai báo
```

Ba tính chất đáng nói khi bảo vệ:

1. **1 file duy nhất**, không cần mạng, không cần registry — hợp thiết bị hiện trường.
2. **Có chữ ký**, sửa 1 byte là `import` từ chối.
3. **Rollback**: giữ slot cũ, `import` lỗi giữa đường không làm hỏng hệ thống đang chạy.

Delta: `export --delta-from=fleet-old.tar.zst` dùng `zstd --patch-from` → giảm 10–50× băng thông
(đúng bài toán của balena), và vẫn verify bằng sha256 của bản đầy đủ sau khi vá.

---

## 5. Mô hình bảo mật

### 5.1 Tài sản & mối đe doạ

| # | Mối đe doạ | Kiểm soát | Rủi ro còn lại |
|---|---|---|---|
| T1 | App trong container thoát ra host | userns map root↔65534, mount ns private, `chroot`, cap tối thiểu | kernel bug (ngoài tầm) |
| T2 | App gọi syscall nguy hiểm | seccomp deny-list mặc định; allow-list khi `--seccomp=strict` | syscall mới chưa có trong list |
| T3 | Image độc hại (path traversal, device node, zip bomb) | `check_tar` + giới hạn size/entry + staging + rename | tar parser bug (fuzz ở §7) |
| T4 | Image bị thay đổi trên đường truyền | sha256 + chữ ký minisign + pin key | key compromise |
| T5 | Container chiếm tài nguyên | cgroup v2 memory/cpu/pids/io + disk watchdog | burst trong 1 chu kỳ 1 s |
| T6 | Rò rỉ sau crash | `gc` + timer + đối soát lúc boot | cửa sổ tới lần timer kế |

### 5.2 Nguyên tắc

1. **Mặc định từ chối**: không mount `/sys`, không device node, cap tối thiểu, `no_new_privs`.
2. **Bề mặt nhỏ**: 1 binary, không daemon, không socket quản trị, không API HTTP.
3. **Kiểm chứng được**: mỗi lớp bảo vệ có test tương ứng trong `test.sh`.
4. **Không tự nhận ngang Docker về cách ly**: thiếu AppArmor/SELinux, ghi rõ.

---

## 6. Vận hành (ops model)

| Việc | Cách làm | Lệnh |
|---|---|---|
| Cài đặt | `install.sh` (verify sha256) + `check` | `curl -fsSL <release>/install.sh | sh` |
| Chạy dịch vụ | systemd unit sinh tự động | `service create <name>` |
| Xem log | json-file + follow | `logs -f web` |
| Giám sát | `stats --json` + textfile metrics | `stats web --json` |
| Dọn rác | `gc` + timer 1 h | `systemctl enable mc-gc.timer` |
| Cập nhật | A/B + chữ ký + rollback | `update apply pkg.tar` |
| Sự cố | watchdog phần cứng | tự động |

---

## 7. Chiến lược kiểm chứng (kể cả khi không có box)

### 7.1 Trên laptop x86_64 (làm được ngay)

| Việc | Cách | Giá trị |
|---|---|---|
| 84 case cũ | `sudo bash build/local-test.sh` | chứng minh không regression |
| `load`/`pull`/overlayfs/xattr | image corpus | chứng minh OCI đúng |
| `check` | chạy trực tiếp | chứng minh preflight |
| Fault injection | `kill -9` 8 điểm + `gc` | chứng minh tự hồi phục |
| **aarch64 userland** | binary aarch64 chạy qua binfmt + qemu (đã thử được) | chứng minh code aarch64 chạy, không cần box |

### 7.2 Chỉ box mới chứng minh được

- kernel `6.18.38-meson64` (khác 6.8 của laptop)
- ext4/ubifs trên `/opt` có xattr hay không
- OOM kill thật (box không swap; laptop có swap)
- nhiệt/điện/reboot đột ngột
- p50/p95 thật trên Cortex-A53

→ Trước khi có box: nói **"verified on x86_64, pending aarch64"**, không nhận "đã chạy trên box".

### 7.3 Ma trận test

| Loại | Số case | Ghi chú |
|---|---|---|
| Chức năng cũ | 84 | giữ nguyên, cổng bắt buộc |
| Image OCI | 12 | 6 image × (load, run) |
| Registry | 6 | token, manifest list, resume, sai digest |
| Bảo mật | 8 | traversal, device, xattr, seccomp JSON |
| Vòng đời | 8 | kill -9, reboot, update/rollback |
| Tài nguyên | 5 | memory/cpu/pids/io/disk |
| **Tổng** | **~123** | mục tiêu 120+ |

---

## 8. So sánh với Docker sau khi hoàn thành

| Tiêu chí | Docker 29.7.2 | minicontainer v2.0 |
|---|---|---|
| Image format | OCI | OCI (image-spec đầy đủ) |
| Pull Docker Hub | có | có |
| Multi-arch | manifest list | manifest list theo `uname -m` |
| Build image | Dockerfile đầy đủ (multi-stage, ARG, cache) | Dockerfile **tập con** + cache layer |
| Ship cả đội | registry / `docker save` từng image | **1 file bundle có chữ ký** (`export`/`import`) |
| Delta update | không (image layer diff) | `zstd --patch-from`, giảm 10–50× |
| Engine | 207 MB, 3 daemon | **~265 KB, 0 daemon** |
| RAM idle | 203 MB | **0 MB** |
| Khởi động | 2.076 s | **10–65 ms** |
| Cgroup | v1 + v2 | chỉ v2 |
| Rootless | có | không (P5.1 nếu kịp) |
| AppArmor/SELinux | có | không |
| Seccomp | profile JSON | deny-list + allow-list + **dịch JSON của Docker** |
| Log | nhiều driver | json-file + rotate + follow |
| Volume | named + bind | bind + named đơn giản |
| Network | bridge/host/none/overlay/macvlan | host/isolated/bridge + UDP + tên peer |
| Update | pull image mới | A/B + chữ ký + rollback |
| Compose/k8s | có | compose-lite (P4.5) |
| Kernel | 3.10+ | 5.x+, cgroup v2 + userns |
| Cài đặt | apt / Docker Desktop | **`install.sh` 1 lệnh, 4 arch, verify sha256** |
| Tài liệu | docs site + reference | **README EN + COMPAT + 5 recipe** |
| Code | hàng triệu dòng | **~8.900 dòng C** |

---

## 9. Khảo sát giải pháp đã có

| Giải pháp | Hướng tiếp cận | OCI image | Dấu chân | OTA | Nhận xét |
|---|---|---|---|---|---|
| **Torizon OS** (Toradex) | mang Docker nguyên bản lên Yocto | có | ~200 MB | mạnh | mạnh về công nghiệp, vẫn là Docker đầy đủ |
| **balenaEngine** (balena) | fork Moby cho nhúng, delta update (hãng nói 10–70× ít băng thông, binary ~3× nhỏ hơn) | có | chục MB, Go, có daemon | mạnh | gần ta nhất về mục tiêu |
| **Pantavisor** | container là đơn vị update, dùng LXC | không trực tiếp | nhỏ | mạnh | mô hình update hay |
| **k3s** | Kubernetes nhẹ | có (containerd) | ~70 MB, RAM ≥512 MB | có | orchestration, không phải runtime |
| **podman + crun** | daemonless, OCI đầy đủ | có | ~50 MB | yếu | không nhắm thiết bị 1–2 GB |
| **crun / youki / runc** | low-level OCI runtime | không có image store | vài trăm KB – vài MB | không | ta ngang crun về tốc độ, hơn ở image store + CLI |
| **LXC / systemd-nspawn** | system container | không | vài MB | không | không có image format chuẩn |
| **udocker** | chạy image Docker không cần root (proot) | tập con | vài MB, Python | không | chậm, cô lập yếu |
| **Firecracker / Kata / gVisor** | microVM / sandbox | có | chục MB+ | không | không hợp thiết bị 1,8 GB |
| **Mender / RAUC / SWUpdate** | chỉ OTA | — | nhỏ | mạnh | bài toán khác |

**Khoảng trống:** OCI image + runtime C thuần ≤ 250 KB + 0 daemon + chỉ cgroup v2, cho thiết bị
< 2 GB RAM. balena gần nhất nhưng vẫn là Moby/Go có daemon.

Câu trả lời mẫu:

> "Có ba hướng đã có người làm: mang Docker nguyên bản vào (Torizon), fork Docker cho nhẹ
> (balenaEngine), dùng LXC (Pantavisor). Chưa có dự án nào công bố runtime OCI viết bằng C,
> dưới 250 KB, không daemon, chỉ cần cgroup v2. Em không cạnh tranh hệ sinh thái — em cạnh tranh
> dấu chân và khả năng kiểm chứng."

---

## 10. Non-goals (không làm, và vì sao)

| Không làm | Vì sao | Nếu bị hỏi |
|---|---|---|
| Build nâng cao (multi-stage, `ARG`, `COPY --from`, `.dockerignore`) | +2.500 dòng, ít dùng trên nhúng | "em hỗ trợ tập con; image phức tạp build bằng Docker/skopeo rồi `load`" |
| Registry push | thiết bị chỉ cần pull | "push làm trên CI" |
| Kubernetes / compose đầy đủ | compose-lite đủ cho 3–5 service | "P4.5" |
| Rootless | cần delegation + kernel hỗ trợ | "P5.1, rủi ro cao" |
| AppArmor / SELinux | kernel box không có profile | "bù bằng seccomp + cap" |
| cgroup v1 | box chỉ v2 | "khai báo yêu cầu kernel" |
| qemu fallback | +10 MB, mất allow-list, chậm 4–20× | "có bản đúng arch" |
| Hooks OCI | ít dùng, tăng bề mặt lỗi | "non-goal có ghi" |
| Device cgroup | không khả thi trong userns | "không mount /dev tuỳ ý" |

---

## 11. Sổ rủi ro

| Rủi ro | Xác suất | Tác động | Giảm thiểu |
|---|---|---|---|
| Box kernel không có `overlay` | Trung bình | Cao | `check` phát hiện; fallback copy rootfs |
| `/opt` không hỗ trợ xattr (ubifs) | Trung bình | Cao | test `setfattr`; cảnh báo + chấp nhận mất file cap |
| `memory.max` tính page cache → OOM oan | Cao | Trung bình | `memory.high` + test `node:20-slim` |
| Docker Hub rate limit | Trung bình | Thấp | cache theo digest; test bằng `docker save` |
| Đĩa 2 GB không đủ | Trung bình | Cao | preflight dung lượng; `gc` tự động |
| Whiteout sai → image sai âm thầm | Trung bình | Cao | test image có `RUN rm` |
| Phình kích thước vượt 300 KB | Trung bình | Trung bình | ngưỡng đỏ trong CI, fail build |
| Không có box để verify | **Cao** | **Cao** | verify x86_64 + binfmt aarch64; ghi rõ "pending" |

---

## 12. Sổ quyết định (ADR ngắn)

| Quyết định | Lý do | Ngày xem lại |
|---|---|---|
| `chroot` thay `pivot_root` | `pivot_root` EPERM dưới userns trên kernel box | khi đổi kernel |
| Không qemu fallback | +10 MB, mất allow-list, chậm 4–20× | nếu có yêu cầu chạy image ngoại arch |
| cgroup v2 only | box chỉ có v2 | nếu cần hỗ trợ thiết bị cũ |
| jsmn thay parser tự viết | 200 dòng, MIT, không malloc | — |
| `curl` ngoài thay libcurl | giữ 0 phụ thuộc, binary nhỏ | nếu cần chạy không có curl |
| overlayfs + fallback copy | tiết kiệm đĩa, nhưng kernel phải hỗ trợ | sau khi test box |
| Seccomp deny-list mặc định cho image | image cần nhiều syscall hơn app tự viết | nếu siết được allow-list theo image |
| Tự viết builder thay vì gọi skopeo | "Docker cho nhúng" phải build được tại chỗ, không phụ thuộc Docker | nếu parser phình > 1.000 dòng |
| qemu chỉ ở build-time, không ở thiết bị | giữ 0 MB RAM trên box; laptop đủ RAM dịch | nếu khách cần chạy ảnh ngoại arch tại chỗ |
| Ship bằng 1 bundle có chữ ký | hiện trường không có mạng, cần kiểm tra nguồn gốc | nếu có registry nội bộ |

---

## 13. Checklist trước khi tag v2.0

```
[x] 84/84 case cũ xanh
[ ] 6 image corpus: load + run + curl/exec
[ ] `build` từ Dockerfile tập con: COPY + RUN + CMD, có cache
[ ] `build --platform=linux/arm64` ra image arm64 (qemu ở laptop)
[ ] `RUN rm /etc/secret` -> file không còn trong image (whiteout)
[ ] `cp` vào/ra container, `--init` không sinh zombie, `stop` rồi `start`
[ ] `run --rm` không để lại state
[ ] `export` -> `import` trên máy sạch -> chạy được
[ ] sửa 1 byte trong bundle -> `import` từ chối
[ ] pull 3 image từ Docker Hub (aarch64 chọn đúng manifest)
[ ] image có USER khác root chạy đúng uid
[ ] ping trong image hoạt động (xattr capability)
[ ] seccomp profile JSON của Docker được chấp nhận
[x] logs + follow + rotate
[x] rmi xoá sạch, không sót overlay/cgroup
[x] kill -9 -> gc -> sạch
[ ] reboot -> không còn cgroup/veth/state rác
[ ] update/rollback 3 lần liên tiếp
[ ] binary < 250 KB, RAM idle 0 MB, p50 <= 80 ms
[ ] soak 6000 vòng không rò
[ ] VERIFICATION_LOG.md ghi số thật + arch + kernel + sha256
[ ] README/AGENTS cập nhật, non-goals ghi rõ
[ ] install.sh cài được trên 4 arch, verify sha256
[ ] 3 người lạ cài + chạy nginx trong < 10 phút
[ ] 5 recipe × 2 distro = 10/10 lượt chạy
[ ] README EN + COMPAT.md + SECURITY.md + CHANGELOG.md
[ ] release ký (.sig) + SHA256SUMS + hướng dẫn build tái lập
[ ] issue template có ô dán `check --json`
```

---

## 14. Chuẩn bị Q&A (16 câu thầy có thể hỏi)

| Câu hỏi | Trả lời ngắn |
|---|---|
| "Khác Docker chỗ nào?" | Cùng hợp đồng image OCI; khác engine: 265 KB C, 0 daemon, chỉ cgroup v2 |
| "Build image bằng gì?" | `minicontainer build` (FROM/COPY/RUN/ENV/CMD) + cache; multi-stage thì dùng Docker rồi `load` |
| "Ship cả đội thế nào?" | `export` ra 1 file có chữ ký, `import` trên máy đích, có delta |
| "Chạy được image Docker thật không?" | Có — `nginx:alpine`, `redis:alpine`, `python:3.12-slim` |
| "Ship sang máy khác?" | Cùng arch: copy 2 file. Khác arch: manifest chọn bản, không qemu |
| "Sao không dùng runc/crun?" | crun ~47 ms, không có image store/CLI; ta gộp cả 3 tầng trong 1 binary |
| "Bảo mật bằng Docker chưa?" | Ngang ở seccomp/cap/userns; kém ở AppArmor/SELinux — có ghi trong non-goals |
| "Đã có ai làm chưa?" | Có 3 hướng (Torizon, balena, Pantavisor); chưa ai làm runtime C <250 KB |
| "Test thế nào?" | 84 case cũ + ~40 case mới, soak 6000, fault injection, 4 arch CI |
| "Chạy trên kernel cũ?" | Không — cần cgroup v2 + userns; `check` báo trước khi cài |
| "Cập nhật firmware?" | A/B + chữ ký + rollback (P4) |
| "Điểm yếu lớn nhất?" | Chưa verify trên phần cứng thật và chưa có OTA như balena |
| "Cài thế nào?" | `curl -fsSL <release>/install.sh \| sh`, script verify sha256 rồi chạy `check` |
| "Ai đang dùng?" | lab của em + mục tiêu ~100 người ngoài sau P6; chưa có khách công nghiệp |
| "Sao không dùng balena?" | balena mạnh hơn về fleet/OTA nhưng engine là Moby chục MB; ta 265 KB và build/ship tại chỗ |
| "Tính năng cốt lõi đủ chưa?" | 15/20 lệnh Docker cốt lõi có (13 đầy đủ, 2 một phần); thiếu `push`, network/volume đầy đủ, `ps -a` — non-goal có ghi (§4.6) |
