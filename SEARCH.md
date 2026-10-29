# SEARCH.md — Khảo sát phần cứng / hệ thống TV-Box

Ngày khảo sát: 2026-10-06. Phương pháp: SSH trực tiếp vào box (`root@<IP_BOX>`), đọc `/proc`, `/sys`, `dmesg`, `lsmod`.
Box: `tvbox` — SSH `root@<IP_BOX>` (LAN hoặc Tailscale; xem `GUIDE.md`, mật khẩu không lưu trong repo).

## 1. Tổng quan (1 bảng nhìn là đủ)

| Hạng mục | Thông số thực đo |
|---|---|
| Board | Amlogic Meson GXL (S905W) P281 Development Board (`/proc/device-tree/model`) |
| SoC | Amlogic S905W: 4x ARM Cortex-A53 64-bit @ max 1.2GHz |
| RAM | 1.8GB (MemTotal 1848204 kB), **không Swap** |
| Lưu trữ | eMMC 16GB Toshiba 016GE2 (dùng được ~14.7GB): `/boot` 512MB vfat + `/` 11.3GB ext4 (trống ~7.9GB) |
| Mạng | Ethernet 100Mbps/Full (meson8b-dwmac), **không có wifi**; Tailscale <TAILSCALE_IP> |
| GPU | Mali-450 (Utgard) — driver `lima` đã load, `/dev/dri` có card0/card1/renderD128 |
| Âm thanh | HDMI codec (`snd_soc_hdmi_codec`) |
| USB | 1 bus USB (`usb1`), chưa cắm thiết bị gì |
| OS | Ubuntu 26.04 LTS, kernel `6.18.38-meson64` aarch64 |
| Nền tảng container | cgroup **v2 only**, đủ 10 loại namespaces, Docker 29.7.2 sẵn (portainer + samba) |
| Nhiệt độ idle | 48°C, load ~0.45 |

## 2. CPU — Amlogic S905W (Meson GXL)

- 4 nhân ARM Cortex-A53 đồng nhất (implementer `0x41` = ARM, part `0xd03`, revision r0p4), kiến trúc ARMv8 64-bit, chạy được cả binary 32/64-bit.
- Xung nhịp theo nấc: `100 / 250 / 500 / 667 / 1000 / 1200 MHz` (cpufreq), max 1.2GHz — yếu, mỗi nhân chỉ ~48 BogoMIPS.
- Flag đáng chú ý: có `aes pmull sha1 sha2 crc32` — mã hóa/hashing bằng phần cứng, tốt cho MQTT-TLS, checksum cảm biến mà không tốn CPU.
- Cache: L1d+L1i 128KB mỗi loại (chia 4 nhân), L2 chung 512KB — working set của tool C vài chục KB nằm gọn trong cache, đó là lý do `minicontainer` khởi động tính bằng ms.
- Ý nghĩa cho dự án: không sợ thiếu CPU cho bản thân tool (clone/chroot/mount là syscall nhẹ), cái cần giới hạn là **tiến trình bên trong** container (vòng lặp `while(1)` 1 nhân là đủ treo cảm nhận) → test T5 bắt buộc.

## 3. RAM — 1.8GB, không Swap

- `MemTotal 1848204 kB`, lúc idle còn ~1.3GB available; Docker (portainer+samba) đang ăn ~300MB AnonPages.
- Không có Swap, không zswap → vượt RAM là OOM-killer giết ngay, không có vùng đệm. Đây chính là lý do test memory bomb (T4) an toàn trên box này: kernel giết đúng tiến trình vượt `memory.max`, host không treo.
- Hệ quả thiết kế: `--ram` mặc định nên để nhỏ (30–50MB như đề xuất), tổng RAM các container chạy cùng lúc không quá ~500MB để chừa chỗ cho hệ thống + Docker hiện có.

## 4. Lưu trữ — eMMC Toshiba 16GB

- Chip `016GE2`, manfid `0x11` (Toshiba), CID đọc từ `/sys/block/mmcblk1/device/*`; 2 phân vùng boot 4MB (mmcblk1boot0/1) + `/boot` 512MB + `/` 11.3GB ext4.
- Đã dùng 2.7GB, còn trống **~7.9GB**; riêng `/var/lib/docker` mới 225MB.
- `/tmp` là tmpfs 903MB (nằm trên RAM!) — đặt rootfs Alpine (~2.5MB giải nén ~10MB) ở `/tmp` thì nhanh nhưng ăn RAM; để lâu dài nên đặt ở `/opt/rootfs` trên eMMC.
- Lưu ý eMMC: ghi xóa nhiều (log, layer Docker) làm mòn chip — tool zero-daemon của đề tài **không ghi gì khi idle** nên thân thiện eMMC hơn Docker (vốn ghi log daemon liên tục). Không bật Swap trên eMMC vì lý do này.

## 5. Mạng — dây LAN 100Mbps, không wifi

- `eth0` qua driver `meson8b-dwmac`, link **100Mbps/Full** (không phải Gigabit — đúng với S905W giá rẻ).
- Trên box **không có interface wifi nào** (chỉ `eth0 lo tailscale0 docker0 br-* veth*`) → box hiện sống hoàn toàn nhờ dây LAN cắm vào laptop (IP <IP_BOX> do laptop cấp). Rút dây là mất SSH LAN, chỉ còn Tailscale (nếu box có mạng khác — hiện không).
- Sau `reboot`: box lên nhanh (eth0 có IP sau ~16s, sshd sau ~18s) nhưng SSH chỉ thông sau **~5 phút** (quan sát 2/2 lần) — do Tailscale mất ~4.5 phút disco direct path qua double-NAT; LAN cũng im lặng cùng lúc (nghi laptop, chưa chốt — chi tiết PHASE7.md). Quy tắc: đừng rút điện, poll tối đa 5 phút.
- `iptables` + `ip` có sẵn, module `bridge veth xt_MASQUERADE nf_nat` đã load (nhờ Docker) → nếu sau này muốn mạng ảo cho container thì kernel đủ module, chỉ tốn CPU NAT (lý do đề xuất bỏ qua là hợp lý).
- Kết luận cho dự án: mặc định `--net=host` (dùng chung mạng host, zero-cost), đúng như phản biện 2.4.2.2.

## 6. GPU / hiển thị / âm thanh

- GPU Mali-450, driver mã nguồn mở `lima` đã load (`lsmod`), `/dev/dri` có đủ card0/card1/renderD128 (không có `/dev/mali` blob cũ).
- Codec âm thanh HDMI (`snd_soc_hdmi_codec`) — box xuất hình/tiếng qua HDMI bình thường.
- Không liên quan trực tiếp đến đề tài (tool chạy headless qua SSH), nhưng khi bảo vệ có thể kể: không đụng đến GPU nên container không cần device passthrough.

## 7. USB và mở rộng

- 1 bus USB on-board (`usb1`), lệnh `lsusb` chưa cài nên chưa liệt kê chi tiết được; hiện không cắm thiết bị nào.
- Nếu làm NAS/gateway thật: cắm USB flash/HDD để chứa rootfs + data, giảm mòn eMMC — nhưng với đề tài hiện tại (rootfs vài MB) thì **không cần mua thêm gì**.

## 8. Kernel và cơ chế container (phần quan trọng nhất)

- Kernel `6.18.38-meson64` (kernel Amlogic mod, PREEMPT) — không phải kernel Ubuntu stock, nhưng đủ tính năng.
- **Namespaces: đủ cả 10 loại** (`cgroup ipc mnt net pid pid_for_children time time_for_children user uts`), `unshare` hỗ trợ `-m -u -i -n -p -U -C -T` → `clone()` với `CLONE_NEWPID/NEWNS/NEWUTS/NEWNET` chạy được (xem PLAN.md 5.2 về lưu ý NEWNET).
- **Cgroup chỉ v2** (`cgroup2 on /sys/fs/cgroup`), controllers: `cpu memory io pids hugetlb cpuset rdma`; bảng v1 (`/proc/cgroups`) toàn 0 hierarchy → code phải dùng API v2 (`memory.max`, `cpu.max`, `cgroup.procs`).
- Modules liên quan container đã sẵn: `bridge veth` (mạng), `binfmt_misc`, `tun` (Tailscale), `nft_* xt_*` (firewall/NAT).
- Có sẵn `unshare nsenter mount umount ip iptables` → **test mọi bước cô lập thủ công bằng lệnh có sẵn trước khi viết C** (kế hoạch P3 trong PLAN.md).

## 9. Phần mềm đang có trên box

- Docker 29.7.2 + containerd, đang chạy `portainer` (:9000) và `dockurr/samba` (:445) — vừa là đối chứng benchmark (T9), vừa là "kẻ tốn RAM" mà đề tài thay thế.
- `apt 3.2.0 (arm64)` dùng được, `python3` có (tiện viết script test memory bomb / đo đạc).
- Dịch vụ nền: `ssh tailscaled docker systemd-networkd resolved timesyncd` — gọn, không rác.

## 10. Còn thiếu gì cho dự án — cần cài thêm

| Cần gì | Cài ở đâu | Lệnh / cách làm | Lý do không cài lên box |
|---|---|---|---|
| Cross-compiler `aarch64-linux-gnu-gcc` | **Laptop** (x86_64) | `sudo apt install gcc-aarch64-linux-gnu` | Box không có gcc/make, eMMC nhỏ, cài toolchain lên box tốn hàng trăm MB |
| Alpine Mini Rootfs (~2.5MB) | Tải trên laptop → `scp` sang box `/opt/rootfs` | Tải bản `alpine-minirootfs-<ver>-aarch64.tar.gz`, giải nén, `scp -r` | Tránh tải trực tiếp bằng box (mạng box qua share laptop, chậm) |
| `strace` (debug syscall) | Box (nhẹ, ~vài trăm KB) | `sudo apt install -y strace` | Cần để chứng minh `clone/chroot/mount` khi bảo vệ |
| `htop` (quan sát CPU/RAM) | Box | `sudo apt install -y htop` | Test T5 nhìn trực quan |
| `stress-ng` (test tải, tùy chọn) | Box hoặc laptop | `sudo apt install -y stress-ng` | Test chịu tải nhiệt/CPU |
| `usbutils` (`lsusb`, tùy chọn) | Box | `sudo apt install -y usbutils` | Liệt kê USB chi tiết nếu cần |

Tuyệt đối **không** `apt install build-essential` lên box (tốn ~500MB eMMC + biên dịch trên A53 chậm). Quy trình đúng: viết C trên laptop → `aarch64-linux-gnu-gcc -static -o minicontainer` → `scp` sang box → chạy. Build static để khỏi lo thiếu lib trên box.
