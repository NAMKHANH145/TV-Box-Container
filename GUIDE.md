# GUIDE.md — Hướng dẫn sử dụng minicontainer

Hướng dẫn đầy đủ: build, deploy, mọi lệnh CLI kèm ví dụ, profile rootfs, test/soak/bench, kết nối box
và xử lý sự cố. Kiến trúc bên trong xem `FINAL.md`; lịch sử kế hoạch xem `PLAN.md`.

Mọi lệnh chạy **trên box** đều cần `root` (namespaces + cgroup + mount). Mọi lệnh build chạy **trên laptop**.

---

## 1. Yêu cầu môi trường

| Thành phần | Yêu cầu |
|---|---|
| TV Box | aarch64 (S905W), kernel có namespaces + cgroup v2; Ubuntu 22.04+ (thực tế 26.04, kernel 6.18.38-meson64) |
| Box: gói cần có | `ip` (iproute2, cho `--net=bridge`), `systemd` (cho `service create`), `curl` hoặc `wget` (cho `pull`) |
| Box: KHÔNG cần | gcc/make, Docker, runc, nsenter, libc bổ sung — binary là musl static |
| Laptop build | bash, make, gcc; hoặc Docker (cho CI-style build); tuỳ chọn `sshpass` để deploy |
| Rootfs | một thư mục rootfs aarch64 (profile Buildroot ~1.3–22 MB, hoặc OCI bundle) |

Kiểm tra nhanh box trước khi dùng:

```bash
# trên box
uname -m                                  # aarch64
mount | grep cgroup                       # phải thấy "cgroup2 on /sys/fs/cgroup"
cat /sys/fs/cgroup/cgroup.controllers     # phải có cpu memory pids io
ls /proc/self/ns                          # đủ 10 namespace
minicontainer --version
```

`minicontainer --version` in ra `minicontainer 1.4.0 (aarch64, musl, static)` — nếu ghi `glibc` thì binary
đó không phải bản chuẩn (bản chuẩn 100–137 KB, glibc thường 440–930 KB).

---

## 2. Build

### 2.1. Build thường (dùng toolchain Buildroot có sẵn trên laptop)

```bash
cd build/
make                 # -> build/minicontainer (musl static, aarch64)
make clean && make   # build sạch
make tools           # build bomb + breakout (công cụ test)
```

`make` mặc định dùng `buildroot/output/host/bin/aarch64-buildroot-linux-musl-gcc` (biến `TOOLCHAIN`).
Nếu thư mục đó không còn, chỉ định toolchain khác:

```bash
make CC=aarch64-linux-gnu-gcc STRIP=aarch64-linux-gnu-strip
```

Kiểm tra kết quả:

```bash
file build/minicontainer   # ELF 64-bit ... ARM aarch64, statically linked, stripped
ls -l build/minicontainer  # ~125 KB
```

### 2.2. Build giống CI (4 kiến trúc, không cần cài cross-toolchain)

CI dùng image Docker Hub `muslcc/x86_64:<target>` (musl.cc chặn IP của GitHub Actions nên không tải trực tiếp):

```bash
cd build
docker run --rm -v "$PWD/..":/io -w /io/build muslcc/x86_64:aarch64-linux-musl \
  sh -c 'rm -f minicontainer && gcc -static -O2 -Wall -o minicontainer src/*.c && strip -s minicontainer'
stat -c '%n %s bytes' minicontainer
```

Thay tag bằng `x86_64-linux-musl`, `armv7l-linux-musleabihf`, `riscv64-linux-musl` khi cần.
Gate CI: binary > 200 KB bị coi là hồi quy (thường là lẫn glibc) và build fail.

### 2.3. Build + kiểm thử bằng QEMU (cho kiến trúc không có board thật)

```bash
# chạy được --version/ps/validate (không tạo được container vì cần root + kernel của chính arch)
qemu-aarch64-static -L /usr/aarch64-linux-gnu build/minicontainer --version
qemu-riscv64-static -L /usr/riscv64-linux-gnu build/minicontainer --version
```

---

## 3. Deploy lên box

```bash
cd build
make deploy BOX=<IP_BOX> SSHPASS_FILE=/path/to/passfile
# hoặc một lần:
make deploy BOX=<IP_BOX> SSHPASS=<password>
```

Quy ước bảo mật của dự án: **không** hard-code IP/mật khẩu trong code, script hay commit — luôn truyền
qua biến môi trường hoặc file (đặt `SSHPASS_FILE` ngoài repo, `chmod 600`).

Deploy thủ công nếu không dùng `make`:

```bash
scp build/minicontainer root@<IP_BOX>:/usr/local/bin/minicontainer
scp build/test.sh build/soak.sh build/bench.sh root@<IP_BOX>:/root/
ssh root@<IP_BOX> 'chmod +x /usr/local/bin/minicontainer'
```

Từ release GitHub (asset mất bit `+x` sau khi tải — phải `chmod`):

```bash
gh release download v1.4.0 --pattern 'minicontainer-linux-aarch64'
chmod +x minicontainer-linux-aarch64
scp minicontainer-linux-aarch64 root@<IP_BOX>:/usr/local/bin/minicontainer
```

---

## 4. Chuẩn bị rootfs

### 4.1. Dùng profile có sẵn

| Profile | Dung lượng giải nén | Có gì |
|---|---|---|
| `base-mini` | 1.3 MB | BusyBox static (sh, ls, ps, …) — đủ để chạy toàn bộ test |
| `web-proxy` | 2.6 MB (tar.gz 1.4 MB) | lighttpd 1.4.85 |
| `iot-gateway` | 14.1 MB (tar.gz 6.9 MB) | mosquitto MQTT broker |
| `python-edge` | 22.2 MB (tar.gz 9.7 MB) | Python 3.14 + `pyserial`, `spidev` |

Tải bằng `pull` (xem §5.8) hoặc build tại máy:

```bash
./profiles/build.sh base-mini     # -> build/rootfs-base-mini/images/rootfs.tar.gz
```

Build lần đầu có thể mất hàng giờ trên laptop (Buildroot), các lần sau dùng cache `build/buildroot/dl`.

### 4.2. Đặt rootfs trên box

```bash
tar xzf rootfs-base-mini.tar.gz -C /opt/rootfs-base-mini     # rootfs lâu dài: ext4 (/opt)
```

**Đừng** đặt rootfs dưới `/tmp` nếu định dùng `--read-only`: `/tmp` trên box là tmpfs, kernel từ chối
remount read-only cho bind của tmpfs (`EPERM`) và tool sẽ báo lỗi (cố ý fail loudly).

### 4.3. Rootfs tối thiểu cần gì?

- `/bin/sh` và lệnh bạn muốn chạy (static hoặc có đủ lib trong rootfs).
- `/etc/passwd` (một số chương trình cần), `/dev` **không cần** — runtime tự tạo `/dev/null`, `zero`,
  `urandom`, `random`, `tty`, `console` + `fd/stdin/stdout/stderr` bằng bind từ host.
- `/proc` không cần có sẵn — tool mount vào.

---

## 5. Các lệnh

Cú pháp chung: `minicontainer [run] [flags] <cmd> [args...]` — chữ `run` có thể bỏ.

### 5.1. `run` (foreground)

```bash
# shell tương tác
minicontainer run --ram=30M --cpu=20 --rootfs=/opt/rootfs-base-mini /bin/sh

# một lệnh rồi thoát
minicontainer run --rootfs=/opt/rootfs-base-mini /bin/true

# đầy đủ giới hạn
minicontainer run --ram=64M --cpu=20 --pids=64 --io=1M --disk=100M \
  --hostname=edge-01 --rootfs=/opt/rootfs-base-mini /bin/sh -c 'echo $$; hostname; ps'
```

| Flag | Ý nghĩa | Mặc định |
|---|---|---|
| `--ram=SIZE` | trần RAM cứng (`memory.max`) | không đặt |
| `--cpu=N` | % của **1 nhân** (1–100) → `cpu.max "$N*1000 100000"` | không đặt |
| `--pids=N` | số tiến trình tối đa (`pids.max`) | 64 |
| `--io=SIZE` | throughput đĩa (`io.max` rbps/wbps) | không đặt |
| `--disk=SIZE` | trần dung lượng rootfs, watchdog userspace (xem §5.5) | không đặt |
| `--rootfs=DIR` | thư mục làm `/` của container | `/opt/rootfs` |
| `--hostname=NAME` | hostname trong UTS ns | `minicontainer-os` |
| `--net=host\|isolated\|bridge` | chế độ mạng (xem §5.6) | `host` |
| `--read-only` | rootfs chỉ đọc + tmpfs cho `/tmp`, `/run`, `/dev/shm` | tắt |
| `-d`, `--detach` | chạy nền, có state để `ps/stop/rm/exec` | tắt |
| `--name=ID` | tên container (chữ, số, `-`, `_`) | sinh từ PID |
| `--health-cmd CMD` | lệnh kiểm tra sức khoẻ, chạy **trên host** mỗi 5 s | không |

SIZE nhận hậu tố `K`/`M`/`G` (nhị phân). Giá trị sai (`--ram=abc`, `--cpu=200`) bị từ chối ngay với exit ≠ 0.
Flag nhận cả hai dạng `--ram 30M` và `--ram=30M`.

### 5.2. `run -d` (detached)

```bash
minicontainer run -d --name broker --ram=32M --cpu=20 --net=bridge \
  --rootfs=/opt/rootfs-iot /usr/sbin/mosquitto -c /etc/mosquitto/mc.conf
```

State nằm trong `/run/minicontainer/<id>/` (PID, cgroup, IP nếu bridge). Không có daemon:
tiến trình supervisor chính là tiến trình cha của container và tự thoát khi container dừng.
Detached có thể chạy trong ngữ cảnh không có terminal; `/dev` được tạo trong container nên
lệnh như `sleep 120` không chết vì thiếu `/dev/null`.

### 5.3. `ps`

```bash
minicontainer ps
minicontainer ps --json
```

Liệt kê container đang chạy từ state trên `/run`. Container foreground không xuất hiện (không ghi state).

### 5.4. `stats`

```bash
minicontainer stats broker
minicontainer stats broker --json
```

Đọc trực tiếp cgroup v2: RAM hiện tại/trần, CPU dùng, số PID, IO. JSON để script hoá.

### 5.5. `stop`, `rm`

```bash
minicontainer stop broker              # SIGTERM tới cgroup, chờ tối đa 3 s, rồi cgroup.kill
minicontainer stop broker --timeout 1
minicontainer rm broker                # chỉ khi đã dừng
minicontainer rm -f broker             # dừng (nếu cần) + xoá
```

Sau `stop`/`rm` mong đợi: không còn `/sys/fs/cgroup/minicontainer-*`, không còn `/run/minicontainer/<id>`,
không còn veth `mcv*`/`mcc*`, không còn mount dưới rootfs. Nếu còn → dấu hiệu leak, chạy `soak.sh` (§7).

### 5.6. `--net` chi tiết

| Mode | Hành vi | Dùng khi |
|---|---|---|
| `host` | không tạo netns; container thấy mạng của box | MQTT cần mạng LAN, script cảm biến, độ trễ 0 |
| `isolated` | netns rỗng, `lo` được UP, không ra ngoài | sandbox, test loopback, dịch vụ nội bộ |
| `bridge` | netns + `eth0` nối bridge `mcbr0` (10.88.0.1/16) | nhiều container nói chuyện với nhau |

`bridge`: tạo `veth` cặp `mcv<pid>`/`mcc<pid>`; phía host dùng `ip` (iproute2) để gắn vào `mcbr0`,
phía container cấu hình bằng ioctl (rootfs không cần `ip`). IP dẫn xuất từ PID host
(`10.88.<pid/256>.<pid%256>`) và ghi tại `/run/minicontainer/<id>/ip`.

```bash
minicontainer run -d --name web --net=bridge --read-only \
  --rootfs=/opt/rootfs-web /usr/sbin/lighttpd -f /etc/lighttpd.conf -D
cat /run/minicontainer/web/ip          # 10.88.x.y
curl http://10.88.x.y:8080/            # từ host
```

Hạn chế đã biết: **không NAT, không DNS** (container chỉ tới subnet 10.88/16 trừ khi host route/forward);
**không bind được port < 1024** (mapped root thiếu `CAP_NET_BIND_SERVICE`) → dùng 8080+.

### 5.7. `exec <id> <cmd...>`

```bash
minicontainer exec broker /bin/sh -c 'id -u'          # 0
minicontainer exec web /bin/cat /proc/net/route
minicontainer exec broker /usr/bin/mosquitto_pub -h 127.0.0.1 -t t -m hello
```

`exec` **không** tạo container mới: `setns()` vào user → net/uts/ipc → mount → pid namespace của container,
lấy lại root qua `O_PATH` trên `/proc/<pid>/root`, `fork()` rồi `execvp()`. Kết quả: cùng PID namespace
(thấy init là PID 1, chính nó PID ≠ 1), cùng cgroup (giới hạn tài nguyên áp dụng), cùng seccomp filter,
uid 0 trong container. Không cần `nsenter`/iproute2.

Lưu ý: container phải còn sống; `exec` không copy env/cwd của người gọi.

### 5.8. `pull <profile>`

```bash
# từ HTTP(S)
minicontainer pull web-proxy --url=https://images.example.com/rootfs
# từ thư mục local trên box (dùng khi đã scp sẵn)
minicontainer pull iot-gateway --url=/root/mcpull --dest=/opt/rootfs-iot
# kiểm tra toàn vẹn + ghi đè
minicontainer pull base-mini --url=https://images.example.com/rootfs --sha256=<sum> --force
```

File nguồn phải tên `<profile>-rootfs.tar.gz`. Mặc định đích `/opt/rootfs-<profile>`; `--dest` để đổi.
Biến `MC_PULL_URL` đặt base URL mặc định (không cần `--url`).

Cơ chế an toàn: tải bằng `curl`/`wget`/đọc local qua `execvp` với argv literal (URL không thể inject shell);
kiểm tra dung lượng trống ≥ 4× file; `--sha256` so khớp trước khi giải nén; tar member tuyệt đối hoặc chứa
`..` bị **từ chối**; giải nén vào thư mục staging `.mc-pull-*` rồi rename atomic; `--dest` đã tồn tại thì
chỉ thay khi có `--force`.

Đóng gói rootfs của bạn thành đúng định dạng này: xem §5.14 (`sdk pack`).

### 5.9. `oci-run <bundle>`

```bash
minicontainer oci-run /opt/bundle     # bundle có config.json (OCI image spec)
```

Hỗ trợ **tập con** của OCI: process args/env/cwd, root.path, hostname, annotations cho net/limits.
Không hỗ trợ mounts, hooks, seccomp-profile từ OCI.

### 5.10. `service create <name> <cmd...>`

```bash
minicontainer service create broker /usr/sbin/mosquitto -c /etc/mosquitto/mc.conf
systemctl daemon-reload && systemctl enable --now minicontainer-broker.service
```

Sinh `/etc/systemd/system/minicontainer-<name>.service` với `Restart=always`, `RestartSec=5`,
mặc định `--ram=32M --cpu=20 --pids=64 --io=1M --rootfs=/opt/rootfs`. Vẫn **zero-daemon**:
systemd chỉ là cơ chế restart, không có process quản lý container thường trú.

### 5.11. `--read-only`

```bash
minicontainer run --read-only --rootfs=/opt/rootfs-web /usr/sbin/lighttpd -f /etc/lighttpd.conf
```

Trong container: `/` chỉ đọc, `/tmp` tmpfs 32M, `/run` tmpfs 8M, `/dev/shm` tmpfs 64M.
Ghi vào `/etc`, `/usr`, `/opt` → `Read-only file system`. Bản rootfs trên **host** không bị đổi.
Đường dẫn mà distro trỏ bằng symlink (`/var/log -> ../tmp`) được bỏ qua có chủ đích (mount đè lên
symlink sẽ che mất `/tmp`).

### 5.12. `--disk` (giới hạn dung lượng)

Kernel box **không có** ext4 project quota, nên `--disk` là watchdog userspace: supervisor lấy mẫu dung
lượng dưới rootfs mỗi giây (baseline + delta) và `cgroup.kill` khi vượt (exit 137). Đồng thời đặt
`RLIMIT_FSIZE = SIZE` nên **một file đơn lẻ** bị chặn ngay lập tức. Foreground không tốn thêm process;
`-d` fork thêm một watchdog nhỏ, tự thoát cùng container.

### 5.13. `--health-cmd`

Chạy **trên host** bằng `system()` mỗi 5 s. Exit ≠ 0 → kill cgroup; detached ghi marker `health_failed`.
Muốn kiểm tra bên trong container thì dùng `exec` trong một script wrapper.

### 5.14. `sdk init` / `sdk pack` (đóng gói rootfs)

Tên cũ trong tài liệu là `mc-sdk`; nay là subcommand của chính binary, không thêm file thứ hai.

```bash
# 1. Sinh skeleton dự án
minicontainer sdk init my-app --profile=iot-gateway --ram=64M --cpu=30 --disk=100M
#    -> my-app/{rootfs/, mc.conf, README.md}

# 2. Chép rootfs vào (hoặc pull sẵn)
tar -xzf iot-gateway-rootfs.tar.gz -C my-app/rootfs
#    hoặc: minicontainer pull iot-gateway --dest=./my-app/rootfs

# 3. Đóng gói: -> proj-rootfs.tar.gz + .sha256
minicontainer sdk pack my-app
#   proj-rootfs.tar.gz.sha256 chứa "<sha256>  proj-rootfs.tar.gz"

# 4. Phân phối: đặt file lên web server rồi
minicontainer pull my-app --url=https://images.example.com/rootfs --sha256=<sum>
```

Quy tắc `pack`:

- Member trong tar **luôn tương đối** (`./bin/...`), giữ nguyên bit `+x`, owner 0 — đúng định dạng `pull` nhận.
- **Từ chối** symlink vượt khỏi gốc rootfs (`../../etc/passwd`), kiểm tra lexically; symlink tuyệt đối
  (`/bin/busybox`) và symlink tương đối vẫn nằm trong rootfs (`usr/bin/x -> ../../bin/y`) được giữ.
- Không ghi đè file đã có nếu thiếu `--force`; cảnh báo nếu rootfs thiếu `/bin/sh`.
- `-o/--output` phải có tên `<profile>-rootfs.tar.gz` để `pull` tìm đúng; nếu không, `pack` cảnh báo và
  gợi ý profile theo tên thư mục.

---

## 6. Kết nối box

### 6.1. SSH có mật khẩu (không lưu mật khẩu trong repo)

```bash
# cách 1: file mật khẩu ngoài repo, dùng sshpass
printf '%s\n' "$BOX_PASS" > ~/.ssh/box.pass && chmod 600 ~/.ssh/box.pass
SSHPASS_FILE=~/.ssh/box.pass sshpass -e ssh root@<IP_BOX>

# cách 2: biến môi trường trực tiếp (không ghi ra đĩa)
SSHPASS='<password>' sshpass -e ssh root@<IP_BOX>
```

Trong `make deploy`, `BOX=` và `SSHPASS_FILE=` là hai biến chuẩn của dự án.

### 6.2. SSH bằng key (khuyến nghị)

```bash
ssh-copy-id -i ~/.ssh/id_ed25519.pub root@<IP_BOX>
```

### 6.3. LAN và Tailscale

Box nối dây LAN với laptop (laptop chia mạng `10.42.0.1/24`), và có Tailscale. Khi LAN không thông,
dùng IP Tailscale (`tailscale status` trên box hoặc máy nào cùng tailnet).

**Sau reboot, SSH có thể im ~5 phút** (quan sát 2/2 lần): eth0 lên sau ~16 s, sshd sau ~18 s, nhưng
đường Tailscale direct cần ~4.5 phút để hoàn tất double-NAT. Quy tắc: đừng rút điện, poll tối đa 5 phút
trước khi kết luận box hỏng.

---

## 7. Test, soak, benchmark

```bash
# trên box, trong thư mục chứa test.sh
bash test.sh                      # 74 case, exit != 0 nếu có FAIL
BR=/opt/rootfs-web bash test.sh   # chạy với rootfs khác

bash soak.sh                      # 100 vòng mặc định
ITER=1000 bash soak.sh            # dài hơn
ITER=117000 bash soak.sh          # ~24 h (đo trên box ~1.36 vòng/s)
HOUR=24 bash soak.sh              # ~15 h (3000 vòng/giờ) — muốn đúng 24 h thì dùng ITER

bash bench.sh                     # độ trễ p50/p95/p99 vs Docker, RAM, CPU
```

`soak.sh` luân phiên 6 kiểu vòng (foreground, `--ram/--cpu`, `--net=isolated`, `--pids/--disk`,
`--read-only`, bridge + `exec` + `stop` + `rm`), kiểm tra rác (cgroup/mount/state) mỗi 20 vòng và so
`MemAvailable` đầu/cuối. Kết quả mong đợi cuối: `loi=0 lan-leak=0`, `SOAK OK`.

Điều kiện tiên quyết của `test.sh`: `/usr/local/bin/minicontainer`, `$BR/bin/busybox`, `$ALP/bin/busybox`,
`$BR/bomb`, `$BR/breakout` (`BR=/opt/rootfs`, `ALP=/opt/alpine` — override được qua biến môi trường).
Build hai công cụ test trên laptop bằng `make tools`, rồi copy vào rootfs:

```bash
make tools
scp bomb breakout root@<IP_BOX>:/opt/rootfs/
```

---

## 8. Sự cố thường gặp

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| `clone: Operation not permitted` | không chạy root, hoặc kernel thiếu namespace | chạy bằng root; kiểm tra `/proc/self/ns` |
| `write .../cgroup.subtree_control: ...` | cgroup v1 hoặc controller chưa bật | phải là cgroup2 (`mount \| grep cgroup`) |
| `open /dev/null: No such file` | rootfs không có `/dev` | v1.3.0+ tự tạo; nếu vẫn lỗi kiểm tra quyền mount |
| Container detached chết ngay | cùng nguyên nhân trên, hoặc thiếu `--rootfs` đúng | chạy foreground để xem lỗi; kiểm tra tồn tại rootfs |
| `--net=bridge` báo thiếu `ip`/`veth` | box thiếu iproute2 hoặc module | `apt install iproute2`, `modprobe veth bridge` |
| Bind/connect `127.0.0.1` fail trong `--net=isolated`/`bridge` | `lo` DOWN | v1.3.0+ đã UP tự động; nếu tự làm thì `ip link set lo up` |
| `pull` báo không đủ dung lượng | < 4× kích thước file | dọn `/opt` hoặc dùng `--dest` khác |
| `pull`: `tu choi giai nen` | tar chứa path tuyệt đối/`..` | tạo archive đúng: `tar czf x.tar.gz -C dir .` |
| `--read-only` báo `remount ... Operation not permitted` | rootfs nằm trên tmpfs (`/tmp`) | chuyển rootfs ra ext4 (`/opt/...`) |
| Port 80 từ chối trong container | thiếu `CAP_NET_BIND_SERVICE` | dùng ≥ 1024 (ví dụ 8080) |
| `mosquitto` fail `setgroups` | config `user mosquitto` trong userns | đặt `user root` trong config |
| `exec`: no such process | container đã dừng | `ps` kiểm tra; `exec` chỉ dùng với container đang chạy |
| SSH sau reboot im lặng | Tailscale double-NAT ~5 phút | chờ tối đa 5 phút, đừng rút điện |
| Binary tải từ release không chạy (`Permission denied`) | asset mất bit `+x` | `chmod +x` |

Xem log kernel khi container bị OOM: `dmesg | tail -20` → dòng `Memory cgroup out of memory: Killed process …`;
hoặc `memory.events` (`max` tăng) trong thư mục cgroup của container.

---

## 9. Bảo mật khi vận hành

- **Không** ghi IP/mật khẩu vào script, `Makefile`, hay commit — dùng `BOX=`, `SSHPASS_FILE=`, `MC_PULL_URL`.
- `pull` chỉ xác thực khi có `--sha256`; nội dung tải từ mạng ngoài nên có checksum bạn tự publish.
- `--health-cmd` là chuỗi shell chạy trên **host** với quyền của người gọi — chỉ đưa lệnh của chính bạn.
- Container có 3 lớp: chroot (không thấy host fs) → userns (uid ngoài là `nobody`) → seccomp deny 21 syscall.
  Vẫn khuyến nghị chạy container không tin cậy với `--read-only` + `--ram` + `--pids` + `--disk`.
- `--net=host` cho container quyền vào mạng thật của box: chỉ dùng cho workload do bạn kiểm soát.

---

## 10. Gỡ cài đặt / dọn dẹp

```bash
minicontainer ps                      # dừng hết
minicontainer stop <id> && minicontainer rm <id>
rm -f /usr/local/bin/minicontainer
rm -rf /run/minicontainer             # state còn sót (nếu có)
ls /sys/fs/cgroup | grep minicontainer   # phải rỗng
mount | grep rootfs                      # phải rỗng
```

Rootfs và profile nằm ở `/opt/rootfs*`, xoá bằng `rm -rf` khi không dùng.