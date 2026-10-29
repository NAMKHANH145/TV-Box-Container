# PHASE7.md — P2-11: điều tra SSH lên chậm sau reboot (xong điều tra)

## 1. Thiết kế thí nghiệm

Reboot box, poll 3 kênh có timestamp từ laptop: ping LAN, SSH LAN, SSH Tailscale. Sau khi thông, đối chiếu journal 2 đầu (box `systemd-networkd/sshd/tailscaled`, laptop `NetworkManager/dnsmasq/tailscaled`).

## 2. Bằng chứng (reboot 23:08:29, thông lại ~23:13:4x — 2/2 lần quan sát)

| Mốc | Box | Laptop |
|---|---|---|
| +6s | `sshd` listening (:36) | — |
| +16s | eth0 có IP `<IP_BOX>` via DHCP (:35) | link up lần cuối + dnsmasq cấp lease (:45) |
| +20s | tailscaled nối DERP derp-3 (:38) | thấy disco-key box đổi (:51) |
| +~5min | — | tailscaled disco ra direct path `<IP_BOX>:41641` (:33); SSH 2 đường thông |

## 3. Kết luận

1. **Box vô tội**: boot nhanh và đúng (net/ssh/tailscale đều lên trong ~20s). Không phải do box yếu hay kernel mod.
2. **Tailscale**: box reboot → disco-key đổi → laptop mất ~4.5 phút mới disco ra direct path qua double-NAT (box sau NAT share của laptop, laptop sau NAT wifi nhà). DERP relay trong lúc đó không gánh được SSH. Đây là bản chất NAT traversal, không phải bug của đề tài.
3. **LAN (ping cũng chết ~5 phút dù L3 hai đầu đã lên)**: chưa chốt root cause. Nghi phạm chính là laptop: 3 lần link flap trong 10s khiến NM profile share/route/ARP chập chờn (trùng thời điểm wifi renew lease ngay lúc thông lại). Cần 1 reboot có log `ip route` + `ip neigh` mỗi poll để kết luận (để dành, không chặn việc khác).
4. Giả thuyết clock-skew đã **bác bỏ**: box chạy CEST, NTP `yes`, giờ đúng.

## 4. Quy tắc vận hành sau reboot (ghi vào GUIDE nếu cần)

- Sau `reboot`, poll cả 2 đường, worst-case quan sát **5 phút** mới SSH được — không phải box treo, đừng rút điện.
- Muốn nhanh: kiểm tra `tailscale status` thấy `active; direct` hãy SSH; LAN thường thông cùng lúc với direct path.
