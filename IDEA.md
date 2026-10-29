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

#### 3.2.1. Chuẩn bị môi trường hệ thống tệp gốc

Tải và giải nén một bộ Rootfs cực nhẹ như Alpine Linux Mini Rootfs, kích thước chỉ ~2.5MB vào một thư mục trên TV Box.
Mục đích: Cung cấp các công cụ dòng lệnh cơ bản như ls, ps, sh để tiến trình bên trong có thể hoạt động mà không phụ thuộc vào thư viện của TV Box.

#### 3.2.2. Thay đổi gốc hệ thống bằng pivot_root hoặc chroot

Tiến trình con gọi hàm chroot và sau đó gọi chdir để chuyển hướng thư mục làm việc.
Mục đích: Thiết lập ranh giới bảo mật. Lệnh này biến thư mục /tmp/alpine-rootfs thành thư mục gốc /. Từ thời điểm này, dù tiến trình có gõ lệnh cd .. bao nhiêu lần, nó cũng không thoát ra ngoài để đọc hoặc xóa file cấu hình của hệ điều hành TV Box.

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
Mục đích: Cung cấp giao diện sử dụng linh hoạt. Người dùng có thể tự định nghĩa giới hạn khi gọi lệnh, ví dụ: ./minicontainer run --ram 50M --rootfs /path/to/alpine /bin/sh.

#### 3.4.2. Chuyển giao quyền thực thi

Cuối hàm của tiến trình con, gọi hàm execvp() để chạy chương trình đích.
Mục đích: execvp() sẽ đè toàn bộ ảnh bộ nhớ của tiến trình C hiện tại bằng đoạn mã của shell. Lúc này container đã hoàn tất việc setup và chính thức nhường quyền điều khiển cho người dùng thông qua terminal.

#### 3.4.3. Bắt sự kiện thoát và dọn dẹp hệ thống

Tiến trình cha sử dụng hàm waitpid() để lắng nghe. Khi tiến trình con thoát, tiến trình cha sẽ tiến hành xóa thư mục /sys/fs/cgroup/memory/minicontainer, unmount /proc ở Rootfs.
Mục đích: Đảm bảo tính chất nhẹ. Khi container không còn hoạt động, mọi dấu vết về giới hạn tài nguyên và cấu trúc thư mục ảo được xóa sạch, trả lại 100% tài nguyên cho hệ thống Host.

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
Thay đổi chỉ xảy ra bên trong thư mục /tmp/alpine-rootfs - thư mục được cấp làm gốc cho container.
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
