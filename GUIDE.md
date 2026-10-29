# Hướng dẫn kết nối SSH vào TV-Box

Không lưu mật khẩu trong repo. Chọn một trong hai cách:

## Cách 1 (khuyên dùng): SSH key

```bash
ssh-keygen -t ed25519 -C "minicontainer"
ssh-copy-id root@<IP_BOX>
ssh root@<IP_BOX>
```

Sau đó `make deploy BOX=<IP_BOX>` chạy không cần mật khẩu.

## Cách 2: sshpass với biến môi trường

```bash
export SSHPASS='...'                  # nhập mật khẩu, không ghi vào file
sshpass -e ssh root@<IP_BOX>
# hoặc dùng file quyền 600:
# echo '...' > ~/.tvbox-pass && chmod 600 ~/.tvbox-pass
# make deploy BOX=<IP_BOX> SSHPASS_FILE=~/.tvbox-pass
```

Không truyền mật khẩu trực tiếp trên command line (`SSHPASS=...` trong lệnh)
vì nó lộ trong `ps`/shell history.

## Kết nối

- LAN: máy tính share mạng `10.42.0.1/24`, box nhận IP qua DHCP (ví dụ
  `<IP_BOX>` trên `eth0`). Kiểm tra: `ping -c 3 <IP_BOX>`.
- Tailscale: box tên `tvbox`; lấy IP bằng `tailscale status | grep tvbox`.

```bash
ssh root@<IP_BOX>
```

## Xử lý lỗi thường gặp

- `Connection timed out`: kiểm tra dây LAN, `tailscale status` xem box có
  `active` không.
- Lần đầu SSH hỏi `Are you sure you want to continue connecting`: gõ `yes`.
- `Permission denied (publickey,password)`: key chưa copy hoặc `sshpass` chưa
  được dùng; kiểm tra lại cách 1/2.
