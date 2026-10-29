# Đề tài: Xây dựng hệ thống quản lý container siêu nhẹ trên nền tảng phần cứng hạn chế (TV Box)

## 1. Lý do chọn đề tài

### 1.1. Áp dụng lý thuyết môn Hệ điều hành vào hệ thống thực tế

Trong môn Hệ điều hành, nhóm được tiếp cận với các khái niệm như: Process Management, Memory Management, File System. Trên thực tế, sinh viên chỉ sử dụng các công cụ ảo hóa hoặc container như Docker dưới dạng blackbox có sẵn.
Đề tài này được chọn nhằm tự lập trình và gọi trực tiếp các System calls của kernel Linux như clone(), chroot(), mount(). Qua đó, nhóm có thể hiểu rõ cách hệ điều hành cấp phát, kiểm soát và thu hồi tài nguyên một tiến trình.

### 1.2. Giải quyết bài toán tối ưu tài nguyên trên phần cứng nhúng

Trong các dự án nhúng, sinh viên thường tận dụng các thiết bị như TV Box, RAM chỉ từ 1GB - 2GB làm Gateway. Nếu cài đặt các phần mềm quản lý container như Docker, hệ thống phải gánh một daemon chạy liên tục. Tiến trình này tốn hàng chục đến hàng trăm MB RAM ngay cả khi không có ứng dụng nào hoạt động, gây lãng phí tài nguyên và làm thiết bị dễ bị treo hệ điều hành.
Do đó, nhu cầu về một công cụ nhẹ, được tinh chỉnh riêng cho thiết bị hạn chế tài nguyên là hoàn toàn hợp lý.

### 1.3. Tìm hiểu cơ chế bảo mật và giới hạn tài nguyên của Linux Kernel

Một trong những mục tiêu quan trọng của hệ điều hành là đảm bảo an toàn và công bằng giữa các tiến trình.
Đề tài này nghiên cứu sâu hai cơ chế của Linux:
Namespaces: dùng để đánh lừa tiến trình đang chạy trên một hệ điều hành độc lập
Cgroups: cơ chế siết trần RAM, CPU mà tiến trình được phép dùng.

### 1.4. Tính khả thi

Thay vì xây dựng một hệ thống lớn, việc phát triển một công cụ theo mô hình dòng lệnh bằng ngôn ngữ C/C++ là hoàn toàn phù hợp. Công cụ không yêu cầu kiến trúc client-server, không chạy ngầm. Khi gõ lệnh, hệ điều hành tạo môi trường cô lập; khi tiến trình bên trong kết thúc, mọi bộ nhớ được hệ điều hành dọn dẹp sạch.

### 1.5. Điểm mới: tự build rootfs bằng Buildroot, không ảnh hưởng Ubuntu trên box

Các đồ án tương tự thường tải rootfs Alpine có sẵn về dùng — tiện nhưng chỉ là "đồ mua sẵn", không chứng minh được hiểu biết về hệ thống tệp. Đề tài này **tự build rootfs riêng bằng Buildroot trên laptop** (target aarch64 Cortex-A53 cho đúng SoC S905W, chỉ gồm BusyBox + sh/ls/ps, vài MB), rồi `scp` sang box đặt tại `/opt/rootfs`.
Toàn bộ quá trình build diễn ra trên laptop — Ubuntu trên TV-Box nguyên vẹn 100%, không cài thêm toolchain, không tốn eMMC. Alpine mini chỉ giữ làm **baseline đối chứng** (so kích thước, tốc độ boot). Câu chuyện thành: tự làm cả "khách" (rootfs) lẫn "chủ" (runtime) — full-stack đúng chất môn Hệ điều hành.

## 2. Các giải pháp tương tự hiện nay

Hiện nay, công nghệ ảo hóa đã phát triển rất mạnh, nhưng hầu hết các giải pháp đều hướng tới hệ thống máy chủ có tài nguyên lớn. Khi áp dụng lên phần cứng hạn chế như TV Box, các giải pháp này lộ rõ những điểm nghẽn về kiến trúc.

### 2.1. Docker và Container

Đây là giải pháp phổ biến nhất nhưng lại nặng nhất đối với thiết bị biên.
Nhược điểm của Docker là kiến trúc Daemon-based. Tiến trình dockerd hoặc containerd phải chạy liên tục để lắng nghe các API. Thực tế đo lường trên các TV Box cũ cho thấy, ngay cả khi không có bất kỳ container nào đang chạy, tiến trình nền này vẫn tiêu thụ từ 50MB đến 100MB RAM, đồng thời liên tục sinh ra các chu kỳ đánh thức CPU.
Đối với thiết bị chỉ có 1GB RAM, việc mất đi 10% bộ nhớ chỉ để duy trì công cụ quản lý là sự lãng phí.

### 2.2. Podman

Podman giải quyết được nhược điểm chạy nền của Docker, giúp tiết kiệm RAM khi hệ thống nghỉ.
Tuy nhiên, nó yêu cầu một hệ sinh thái dependencies cực kỳ phức tạp từ môi trường Host. Podman dựa vào các runtime trung gian như runc hoặc crun, đồng thời cần các công cụ mạng như slirp4netns hay cấu hình dbus/systemd.
Linux Kernel trên TV Box thường được mod lại rất sơ sài, bị lược bỏ nhiều module để tiết kiệm dung lượng ROM, dẫn đến việc cài đặt Podman thường thất bại do thiếu thư viện hệ thống.

### 2.3. LXC / LXD

Thay vì chạy một ứng dụng đơn lẻ, LXC khởi chạy toàn bộ một hệ điều hành thu nhỏ bên trong không gian cô lập.
Hệ quả là khi khởi động, nó sinh ra hàng loạt tiến trình phụ như cron, syslog, udev. Điều này gây độ trễ rất lớn trên các chip ARM yếu, không phù hợp với tiêu chí nhanh và nhẹ.

### 2.4. Research Gap

Trên thực tế, ý tưởng viết trình quản lý container siêu nhẹ không phải mới. Đã có những dự án mã nguồn mở mang tính giáo dục như bocker viết bằng bash script hoặc các mã nguồn minh họa viết bằng C do các kỹ sư Linux kernel trình bày tại các hội thảo.
Tuy nhiên, các dự án này thường chỉ dừng ở mức minh họa lý thuyết API, hoặc là các công cụ nội bộ của các hãng sản xuất thiết bị nhúng. Việc đóng gói thành một CLI Micro Engine hoàn chỉnh, tối ưu cho giới hạn của TV Box và kernel cũ, đồng thời hệ thống hóa thành một tài liệu giáo dục vẫn là một khoảng trống có giá trị thực tiễn.
Điểm mới của đề tài này so với các minh họa đó: không dùng rootfs tải sẵn mà **tự build rootfs bằng Buildroot** cho đúng SoC S905W — tức tự làm cả "khách" lẫn "chủ", Alpine chỉ giữ làm baseline đối chứng.

#### 2.4.1. Ưu điểm

Hoạt động như một lệnh thực thi bình thường. Gọi lệnh → Kernel cấp phát không gian cô lập → Tiến trình chạy → Kết thúc lệnh → Giải phóng RAM. Không có service nào chạy ngầm.
Không qua các runtime trung gian như runc. Công cụ gọi thẳng các system call cấp thấp giúp file thực thi sinh ra có kích thước vài chục KB, chạy trên kernel Linux cũ không cần cài thêm thư viện phụ thuộc.

#### 2.4.2. Nhược điểm

##### 2.4.2.1. Không hỗ trợ Pull Image từ mạng lưới như Docker Hub

Công cụ yêu cầu người dùng phải tự chuẩn bị sẵn một thư mục Rootfs trên ổ cứng.
Phản biện: Việc tích hợp tính năng tải Image cần thư viện xử lý mạng, HTTPS, JSON và giải nén Tar/Gzip, làm file thực thi lên hàng chục MB. Người dùng mục tiêu của đề tài thường có thói quen tự build các Rootfs siêu nhỏ bằng Buildroot hoặc nạp sẵn qua thẻ nhớ/USB. Do đó, việc tách rời tải Image và chạy Container tuân thủ triết lý UNIX: Một công cụ chỉ làm tốt một việc.

##### 2.4.2.2. Không có hệ thống mạng ảo phức tạp

Các tiến trình bên trong container sẽ chia sẻ không gian mạng hoặc hoàn toàn bị ngắt mạng, thay vì được cấp IP ảo nội bộ như Docker.
Phản biện: Trên các thiết bị như TV Box làm nhiệm vụ thu thập dữ liệu, các ứng dụng như MQTT broker, script đọc cảm biến thường cần giao tiếp trực tiếp với mạng vật lý hoặc trao đổi qua IPC cục bộ. Việc thiết lập cầu nối mạng thông qua iptables tốn rất nhiều tài nguyên CPU của TV Box để xử lý dịch địa chỉ. Bỏ qua mạng ảo phức tạp giúp độ trễ xử lý gói tin bằng không, cực kỳ phù hợp cho các tác vụ thời gian thực tại biên.

## 3. Triển khai chi tiết

Quá trình này được chia thành 4 giai đoạn.

### 3.1. Cách ly môi trường

Giai đoạn này đánh lừa tiến trình, khiến nó tưởng mình đang chạy độc quyền trên một hệ điều hành riêng biệt.

#### 3.1.1. Khởi tạo vùng nhớ Stack riêng biệt cho tiến trình con

Cấp phát một vùng nhớ heap bằng malloc() và truyền nó làm Stack cho tiến trình con.
Mục đích: Khác với fork() copy toàn bộ bộ nhớ của cha, system call clone() yêu cầu chúng ta phải tự chỉ định vùng nhớ Stack cho tiến trình con để tránh xung đột dữ liệu giữa hai tiến trình.

#### 3.1.2. Gọi System Call clone() với flags Namespaces

Thực thi clone() với các cờ CLONE_NEWPID, CLONE_NEWNS, CLONE_NEWUTS, và CLONE_NEWNET.
Mục đích: Đây là trung tâm của việc tạo container. NEWPID tạo cây tiến trình mới, tiến trình bên trong sẽ có PID=1. NEWNS cách ly hệ thống mount points. NEWUTS cho phép đặt lại hostname. NEWNET cách ly card mạng.

#### 3.1.3. Cài đặt Hostname

Sử dụng hàm sethostname() bên trong hàm thực thi của tiến trình con để đổi tên máy.
Mục đích: Tăng tính trực quan. Ngay khi người dùng vào shell của container, dấu nhắc lệnh sẽ đổi từ host@tvbox sang root@minicontainer-os, minh chứng việc cô lập không gian đã thành công.

### 3.2. Quản lý hệ thống tệp và giam tiến trình

Dù tiến trình đã bị cô lập PID, nó vẫn có thể nhìn thấy hệ thống file của TV Box. Giai đoạn này khóa tiến trình vào một thư mục vật lý duy nhất.

#### 3.2.1. Chuẩn bị môi trường hệ thống tệp gốc: tự build bằng Buildroot

Tự build rootfs riêng bằng Buildroot trên laptop (target AArch64 Cortex-A53, BusyBox cung cấp sh/ls/ps), đóng gói tar vài MB rồi `scp` sang thư mục `/opt/rootfs` trên TV Box. Song song tải Alpine Mini Rootfs (~2.5MB) về `/tmp/alpine` làm baseline đối chứng.
Mục đích: Cung cấp các công cụ dòng lệnh cơ bản để tiến trình bên trong hoạt động mà không phụ thuộc thư viện của TV Box — nhưng rootfs là "nhà làm", chứng minh hiểu File System từ gốc thay vì dùng đồ tải sẵn. Toàn bộ quá trình build nằm trên laptop nên Ubuntu trên box nguyên vẹn, eMMC không tốn thêm gì ngoài vài MB rootfs.

#### 3.2.2. Thay đổi gốc hệ thống bằng pivot_root hoặc chroot

Tiến trình con gọi hàm chroot và sau đó gọi chdir để chuyển hướng thư mục làm việc.
Mục đích: Thiết lập ranh giới bảo mật. Lệnh này biến thư mục `/opt/rootfs` (rootfs Buildroot tự build) thành thư mục gốc `/`. Từ thời điểm này, dù tiến trình có gõ lệnh cd .. bao nhiêu lần, nó cũng không thoát ra ngoài để đọc hoặc xóa file cấu hình của hệ điều hành TV Box.

#### 3.2.3. Mount hệ thống tệp ảo /proc bên trong Container

Gọi lệnh mount bên trong môi trường đã bị chroot.
Mục đích: Hệ điều hành Linux giao tiếp thông tin tiến trình qua thư mục ảo /proc. Do đã dùng CLONE_NEWPID ở giai đoạn 1, việc mount lại /proc đảm bảo lệnh ps khi gõ trong container sẽ chỉ đọc danh sách tiến trình của riêng nó, hoàn toàn không biết các tiến trình bên ngoài của TV Box.

### 3.3. Giới hạn tài nguyên bằng Control Groups

Ngăn chặn hiện tượng một tiến trình lỗi bên trong container ngốn RAM và làm sập toàn bộ TV Box.

#### 3.3.1. Giao tiếp với Virtual File System của Cgroups

Lập trình C mở và tạo một thư mục mới bên trong cấu trúc của kernel.
Mục đích: Cấu hình giới hạn tài nguyên trong Linux là việc ghi dữ liệu vào các tệp tin ảo do Kernel quản lý. Việc tạo thư mục đồng nghĩa với việc yêu cầu Kernel cấp một hồ sơ giới hạn tài nguyên mới.

#### 3.3.2. Áp đặt trần tài nguyên

Mở file memory.limit_in_bytes trong thư mục vừa tạo và ghi vào số byte giới hạn. Tương tự ghi vào cpu.shares để giới hạn CPU.
Mục đích: Ra lệnh trực tiếp cho Memory Management Unit của kernel Linux. Nếu tiến trình trong container tiêu thụ vượt 50MB, Kernel sẽ kích hoạt OOM Killer để tiêu diệt ngay tiến trình đó, bảo vệ an toàn cho hệ thống Host.

#### 3.3.3. Gắn tiến trình vào nhóm kiểm soát

Ghi PID của tiến trình con vào file tasks hoặc cgroup.procs.
Mục đích: Kích hoạt việc siết tài nguyên. Báo cho Kernel biết chính xác tiến trình nào phải chịu các mức trần tài nguyên vừa cấu hình ở bước 3.3.2.

### 3.4. Hoàn thiện CLI Tool

#### 3.4.1. Xây dựng bộ phân tích cú pháp

Sử dụng thư viện getopt trong C để bắt các tham số đầu vào.
Mục đích: Cung cấp giao diện sử dụng linh hoạt. Người dùng có thể tự định nghĩa giới hạn khi gọi lệnh, ví dụ: ./minicontainer run --ram 50M --rootfs /opt/rootfs /bin/sh.

#### 3.4.2. Chuyển giao quyền thực thi

Cuối hàm của tiến trình con, gọi hàm execvp() để chạy chương trình đích.
Mục đích: execvp() sẽ đè toàn bộ ảnh bộ nhớ của tiến trình C hiện tại bằng đoạn mã của shell. Lúc này container đã hoàn tất việc setup và chính thức nhường quyền điều khiển cho người dùng thông qua terminal.

#### 3.4.3. Bắt sự kiện thoát và dọn dẹp hệ thống

Tiến trình cha sử dụng hàm waitpid() để lắng nghe. Khi tiến trình con thoát, tiến trình cha sẽ tiến hành xóa thư mục /sys/fs/cgroup/memory/minicontainer, unmount /proc ở Rootfs.
Mục đích: Đảm bảo tính chất nhẹ. Khi container không còn hoạt động, mọi dấu vết về giới hạn tài nguyên và cấu trúc thư mục ảo được xóa sạch, trả lại 100% tài nguyên cho hệ điều hành TV Box.

## 4. Kiểm chứng kết quả

### 4.1. Cô lập môi trường

Cô lập không gian tiến trình
Kịch bản: Chạy shell (/bin/sh) thông qua công cụ. Gõ lệnh ps aux bên trong.
Tiêu chí:
Tiến trình shell hiện tại bắt buộc phải mang PID = 1.
Danh sách tiến trình chỉ hiển thị đúng 2 dòng Shell và lệnh ps.
Tuyệt đối không nhìn thấy bất kỳ tiến trình nền nào của hệ điều hành TV Box như systemd, sshd.
Cô lập hệ thống tệp và quyền truy cập
Kịch bản: Gõ lệnh tạo một file rác touch /etc/hack.txt hoặc thử xóa thư mục gốc rm -rf /bin từ bên trong container. Thoát ra ngoài TV Box để kiểm tra.
Tiêu chí:
Thay đổi chỉ xảy ra bên trong thư mục rootfs được cấp làm gốc cho container (`/opt/rootfs` Buildroot tự build, hoặc `/tmp/alpine` khi chạy baseline đối chứng).
Hệ thống file thực tế của TV Box tại /etc và /bin nguyên vẹn 100%. Tiến trình không thể dùng lệnh cd ../.. để thoát ra ngoài.
Cô lập định danh hệ thống
Kịch bản: Gõ lệnh hostname bên trong và bên ngoài container.
Tiêu chí: Hostname bên trong trả về tên ảo (ví dụ: minicontainer-os), trong khi TV Box bên ngoài vẫn giữ nguyên tên gốc (ví dụ: tvbox-ubuntu).

### 4.2. Giới hạn tài nguyên

Ngăn chặn tràn bộ nhớ bằng Hard Limit
Kịch bản: Khởi chạy container với cờ --ram 30M. Bên trong container, chạy một chương trình C dùng lệnh malloc liên tục để xin cấp phát 50MB RAM.
Tiêu chí:
Chương trình Memory Bomb lập tức bị kernel Linux kill,
Gõ lệnh dmesg trên TV Box sẽ thấy log của hệ thống: "Memory cgroup out of memory: Killed process...".
Hệ điều hành TV Box không hề bị treo, giật lag hay bị ảnh hưởng bởi tiến trình ảo bên trong.
Kiểm soát trần sử dụng
Kịch bản: Khởi chạy container với cờ --cpu 20 giới hạn 20% năng lực 1 nhân CPU. Chạy một vòng lặp vô hạn while(1) bên trong container.
Tiêu chí: Mở công cụ htop trên TV Box (Host), quan sát tiến trình của container không bao giờ được cấp phát vượt quá ngưỡng 20% CPU, để dành 80% còn lại cho hệ điều hành xử lý các tác vụ khác.

### 4.3. Hiệu năng và cơ chế Zero-Daemon

Tiêu thụ tài nguyên nền
Kịch bản: Khởi động lại TV Box, không chạy lệnh container nào. Dùng htop kiểm tra lượng RAM bị chiếm dụng.
Tiêu chí: 0 MB RAM bị chiếm dụng cho hệ thống quản lý container. Không có bất kỳ process nào tên là minicontainerd chạy ngầm.
Độ trễ khởi động
Kịch bản: Dùng lệnh time ./minicontainer run ... để tính thời gian từ khi gõ lệnh đến khi dấu nhắc lệnh xuất hiện.
Tiêu chí: Thời gian khởi động phải tính bằng ms, nhanh hơn nhiều lần so với việc boot một System Container của LXC hoặc máy ảo.
Dọn dẹp hệ thống
Kịch bản: Người dùng gõ exit để thoát container. Kiểm tra lại hệ thống thư mục ảo /sys/fs/cgroup/ và lệnh mount.
Tiêu chí:
Thư mục quản lý cgroup của container đó phải tự động biến mất.
Không có tiến trình rác nào bị bỏ lại.
Thư mục ảo /proc bên trong Rootfs phải được unmount hoàn toàn để tránh rò rỉ tài nguyên.

---

# 5. Đánh giá tính khả thi trên TV-Box thật (khảo sát 2026-10-06)

Số liệu đo trực tiếp qua SSH (`root@<IP_BOX>`):

- Board: Amlogic Meson GXL (S905W) P281 — 4x Cortex-A53 @ max 1.2GHz
- RAM: 1.8GB total (~1.3GB available lúc idle), **không có Swap**
- Disk: eMMC 14.7GB, `/` còn trống ~7.9GB
- OS: Ubuntu 26.04 LTS, kernel `6.18.38-meson64` aarch64
- Docker 29.7.2 đang chạy (portainer + samba) — chính là cái cần thay thế/đối chứng

## 5.1. Kernel hỗ trợ gì (điểm quyết định)

| Cơ chế | Trạng thái trên box | Kết luận |
|---|---|---|
| Namespaces (pid, mnt, uts, net, ipc, user, cgroup, time) | Có đủ (`/proc/self/ns` liệt kê đủ 10 loại, `unshare` hỗ trợ `-m -u -i -n -p -U -C -T`) | `clone()` với `CLONE_NEWPID/NEWNS/NEWUTS/NEWNET` chạy được |
| Cgroups | **Chỉ có cgroup v2** (`cgroup2 on /sys/fs/cgroup`), controllers: `cpu memory io pids hugetlb cpuset rdma`. `/proc/cgroups` (v1) toàn 0 hierarchy — v1 không mount | Code **phải viết theo API v2**, không dùng đường dẫn v1 trong đề xuất gốc |
| Toolchain | Box **không có gcc/make** | Build trên laptop bằng cross-compiler `aarch64-linux-gnu-gcc`, copy binary sang box |

## 5.2. Điểm phải sửa so với đề xuất gốc (đi sâu kernel)

1. **Cgroup v1 → v2.** Đề xuất ghi `memory.limit_in_bytes`, `cpu.shares`, file `tasks`, thư mục `/sys/fs/cgroup/memory/minicontainer` — đó là API v1, **không tồn tại** trên box này. API v2 tương đương:
   - Tạo thư mục: `mkdir /sys/fs/cgroup/minicontainer-<pid>`
   - RAM hard limit: ghi `31457280` vào `memory.max` (thay vì `memory.limit_in_bytes`); bật `memory.swap.max=0` nếu muốn cấm swap
   - CPU: ghi quota vào `cpu.max` dạng `$MAX $PERIOD` (ví dụ `20000 100000` = 20% 1 CPU) thay vì `cpu.shares` vốn chỉ là trọng số tương đối
   - Gắn tiến trình: ghi PID vào `cgroup.procs` (file `tasks` của v1 đã bỏ)
   - Bật controller trước ở cây cha: ghi `+cpu +memory +pids` vào `cgroup.subtree_control`
   - OOM log đọc từ `dmesg` hoặc `memory.events` (`max` counter tăng)
2. **`CLONE_NEWNET` cần cân nhắc.** Đề xuất dùng đồng thời NEWNET nhưng mục 2.4.2.2 lại bảo "chia sẻ mạng host hoặc ngắt mạng". Hai ý này mâu thuẫn: nếu đã NEWNET mà không cấu hình veth/bridge thì container **mất mạng hoàn toàn**. Phương án đúng: mặc định **không** NEWNET (dùng host net, khớp lập luận MQTT/cảm biến), thêm flag `--net=isolated` mới bật NEWNET.
3. **`chroot` vs `pivot_root`.** Đề xuất cho chọn 1 trong 2 — nên chốt: dùng `chroot` cho bản tối thiểu (ít bước, không đòi NEWNS setup kỹ), `pivot_root` để sau (an toàn hơn vì put_old đúng chuẩn, nhưng đòi mount --bind + rshared handling). Về mặt kernel: `pivot_root` yêu cầu rootfs mới là mount point riêng — thêm code xử lý mount, dễ lỗi trên kernel mod sơ sài.
4. **`CLONE_NEWPID` + `mount /proc`.** Đúng như đề xuất, nhưng thứ tự kernel bắt buộc: `clone(NEWPID)` → tiến trình con là PID 1 của namespace mới → **con phải mount lại `/proc`** (proc mount trước đó vẫn trỏ cây PID cũ). Dùng `unshare --mount-proc` khi test thủ công để khỏi quên bước này.
5. **User namespace.** Kernel có `user` ns — nên dùng `CLONE_NEWUSER` + uid_map để chạy container không cần root thật bên trong, tăng điểm bảo mật của đề tài. Nhưng rootfs + uid_map cần test riêng vì kernel TV Box mod có thể chặn một số mapping.
6. **Rootfs Buildroot tương thích ABI.** Rootfs tự build phải dùng đúng libc/ABI mà binary trong đó yêu cầu (Buildroot mặc định uClibc/musl — nếu tool C trên laptop build bằng glibc cross thì chỉ binary runtime cần glibc, còn shell trong rootfs dùng libc của chính rootfs, không xung đột). Ưu tiên build **static BusyBox** để shell trong container không phụ thuộc loader nào của host.

## 5.3. Kết luận khả thi

**Khả thi, với 3 điều chỉnh bắt buộc:** (a) viết cgroup theo v2, (b) build cross-compile từ laptop, (c) tự build rootfs bằng Buildroot trên laptop (tốn ~10-20GB disk laptop + vài giờ build lần đầu; box không tốn gì vì chỉ nhận file tar vài MB qua `scp`, Ubuntu trên box nguyên vẹn). Phần namespaces/chroot/proc hoàn toàn làm được trên kernel này. Mức khó phù hợp đồ án môn học: mỗi syscall (clone, chroot, mount, sethostname, execvp, waitpid) + ghi file cgroup v2 đều demo được riêng lẻ trước khi ráp thành CLI.

---

# 6. Kế hoạch chi tiết: PLANNING → IMPLEMENT → TESTING

## 6.1. PLANNING (trên laptop, không cần box)

- [x] P0. Chốt scope tối thiểu: `minicontainer run --ram 30M [--cpu 20] [--net=host|isolated] --rootfs <dir> <cmd>` — đúng 1 lệnh, không pull image, không mạng ảo.
- [x] P1. Tự build rootfs bằng Buildroot trên laptop (Target AArch64 Cortex-A53, static BusyBox + sh/ls/ps) ra tar vài MB, `scp` sang box tại `/opt/rootfs`; đồng thời tải Alpine mini về `/tmp/alpine` làm baseline đối chứng. Cài toolchain cross `aarch64-linux-gnu-gcc`, kiểm tra `file minicontainer` ra `aarch64`. (Xong hết — xem PHASE1.md)
- [x] P2. Viết spec mapping v1→v2 (bảng ở 5.2) thành hằng số trong code để khi bảo vệ không bị hỏi "sao tài liệu ghi memory.limit_in_bytes mà code ghi memory.max".
- [x] P3. Chia task theo 4 giai đoạn mục 3 (cách ly → filesystem → cgroups → CLI), mỗi giai đoạn có test thủ công bằng `unshare` trước khi viết C (chi tiết ở 6.3).

## 6.2. IMPLEMENT (viết code C, build trên laptop, chạy trên box)

- [x] I0. Buildroot: cấu hình target AArch64, build ra `rootfs.tar`, giải nén lên box `/opt/rootfs`. Verify: `chroot /opt/rootfs /bin/sh` chạy được shell, `du -sh` so với `/tmp/alpine`.
- [x] I1. Khung CLI + `getopt`: parse `--ram`, `--cpu`, `--rootfs`, `--net`. Chưa cần namespace — chỉ in config ra là xong bước này.
- [x] I2. `clone()` + stack `malloc()` + `sethostname()` + `execvp()`: chạy được shell PID=1 trong PID/UTS ns. Verify: `ps` chỉ thấy 2 dòng (cần mount /proc mới, xem I3).
- [x] I3. `chroot` + `chdir` + `mount("proc", "/proc", "proc")` + `umount` khi thoát. Verify: `touch /etc/hack.txt` chỉ nằm trong rootfs.
- [x] I4. Cgroup v2: tạo `/sys/fs/cgroup/minicontainer-<pid>`, ghi `memory.max` / `cpu.max` / `cgroup.procs`, bật `+cpu +memory` ở `cgroup.subtree_control` của cha. Xử lý lỗi khi controller không bật được (kernel mod thiếu) → báo lỗi rõ ràng thay vì crash.
- [x] I5. `waitpid()` + dọn dẹp: `umount /proc`, `rmdir` cgroup. Verify: sau `exit`, `ls /sys/fs/cgroup/minicontainer-*` trống, `mount | grep rootfs` trống.
- [x] I6 (tùy chọn nếu còn thời gian): `--net=isolated` (NEWNET, không cấu hình veth → container mất mạng, demo đúng "ngắt mạng"), hoặc user ns + uid_map. (`--net=isolated` đã test đạt — chỉ còn `lo`; user ns chuyển sang PLAN-2 P2-4)

## 6.3. TESTING (chạy trên box, ánh xạ 1-1 với mục 4)

| Test | Lệnh / kịch bản | Tiêu chí đạt | Liên quan code |
|---|---|---|---|
| T1. PID isolation | `./minicontainer run --rootfs /opt/rootfs /bin/sh` → `ps aux`, `echo $$` | `$$`=1, chỉ 2 dòng, không thấy systemd/sshd | I0 + I2 + I3 |
| T2. Filesystem jail | Trong container: `touch /etc/hack.txt`, `rm -rf /bin`; ngoài host kiểm tra `/etc`, `/bin` | Host nguyên vẹn 100%, file rác chỉ trong rootfs | I3 |
| T3. Hostname | `hostname` trong/ngoài | Trong = `minicontainer-os`, ngoài = `tvbox` | I2 |
| T4. RAM hard limit | `--ram 30M` + memory bomb malloc 50MB | Bomb bị kill, `dmesg` có `Memory cgroup out of memory`, host không lag | I4 |
| T5. CPU cap | `--cpu 20` + `while(1)`, xem `htop`/*`cpu.stat`* trên host | Không vượt ~20% 1 CPU | I4 |
| T6. Zero-daemon | Reboot box, không chạy gì, `ps aux \| grep -i minicontainer`, `free -h` | 0 process, 0 MB nền | Thiết kế (không daemon) |
| T7. Start latency | `time ./minicontainer run ... /bin/true` so với `docker run` | Tính bằng ms, nhanh hơn Docker/LXC rõ rệt | Toàn pipeline |
| T8. Cleanup | `exit` → `ls /sys/fs/cgroup/`, `mount \| grep proc`, `ps aux` | Không còn cgroup dir, không mount thừa, không zombie | I5 |
| T9. Đối chứng Docker | `docker stats` (dockerd+containerd nền ~50-100MB) vs T6 | Số liệu RAM nền đưa vào báo cáo | Đánh giá 2.1 |
| T10. Rootfs nhà làm vs Alpine | `du -sh /opt/rootfs /tmp/alpine`; chạy lại T1–T3 với `--rootfs /tmp/alpine` | Rootfs Buildroot ≤ ~10MB, mọi test cô lập đạt trên **cả hai** rootfs | I0 |

Thứ tự chạy: T1→T2→T3 (không cần cgroup, chạy trên rootfs Buildroot trước) → T10 (lặp lại T1–T3 trên Alpine để đối chứng) → T4→T5 (cần cgroup v2) → T6→T7→T8→T9 (hiệu năng). Test nào fail thì quay về đúng bước I tương ứng, không sửa lan man.
