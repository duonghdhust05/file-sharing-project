# BẢN ĐẶC TẢ YÊU CẦU PHẦN MỀM (SRS)
## DỰ ÁN: CLIENT-SERVER FILE SHARING APPLICATION (TOPIC 6)
**Phiên bản:** 1.0.0  
**Giao thức truyền dẫn:** Custom Protocol (FSP) over Raw TCP Socket C/C++  
**Tiêu chuẩn định dạng:** IEEE Std 830-1998  

---

## 1. GIỚI THIỆU (INTRODUCTION)

### 1.1. Mục đích (Purpose)
Tài liệu này đặc tả chi tiết toàn bộ các yêu cầu chức năng (Functional Requirements), yêu cầu phi chức năng (Non-Functional Requirements), thiết kế dữ liệu và các ràng buộc kỹ thuật cho ứng dụng chia sẻ file trực tuyến theo mô hình Client-Server bằng kỹ thuật lập trình Socket trong C/C++. Tài liệu là cơ sở pháp lý và kỹ thuật để nhóm 4 thành viên đối chiếu trong suốt quá trình phát triển và kiểm thử.

### 1.2. Phạm vi sản phẩm (Product Scope)
Ứng dụng bao gồm:
* **FSP Server**: Tiến trình daemon chạy nền trên máy chủ, quản lý lưu trữ tập trung (Storage Sandbox), xác thực tài khoản, kiểm soát quyền truy cập kiểu Linux (POSIX permissions) và xử lý đồng thời nhiều client kết nối qua Raw TCP Sockets.
* **FSP Client (Desktop GUI & CLI)**: Ứng dụng phía máy trạm cung cấp giao diện trực quan (Qt C++) và công cụ dòng lệnh (CLI), cho phép người dùng đăng ký, đăng nhập, duyệt file (ls, cd, pwd, mkdir), upload/download file khối lớn, truyền cả thư mục đệ quy, tìm kiếm và phân quyền chia sẻ.
* **FSP Web Client (Mở rộng)**: Giao diện web chạy trên trình duyệt (TypeScript + React) kết nối với C++ Server thông qua Protocol Gateway phục vụ thuyết trình trực quan.

### 1.3. Định nghĩa và Thuật ngữ (Definitions & Acronyms)
* **FSP**: File Sharing Protocol (Giao thức truyền file tự định nghĩa ở tầng ứng dụng).
* **TCP Framing**: Kỹ thuật đóng gói phân định biên giới thông điệp trên luồng byte-stream của TCP bằng Header có trường chỉ định độ dài (Length-prefixed framing).
* **Chunking**: Kỹ thuật chia nhỏ file thành các khối cố định (64 KB) để truyền tải, kiểm soát tiến trình và chống tràn RAM.
* **ACL**: Access Control List (Danh sách kiểm soát quyền truy cập chi tiết cho từng file).
* **Path Traversal**: Lỗ hổng bảo mật cho phép client truy cập trái phép ra ngoài thư mục sandbox bằng các chuỗi như `../`.

---

## 2. MÔ TẢ TỔNG QUAN (OVERALL DESCRIPTION)

### 2.1. Phối cảnh hệ thống (System Perspective)
Hệ thống hoạt động theo mô hình Client-Server tập trung qua mạng TCP/IP:
```
+-------------------+           +-------------------+
|  Desktop Client   |           |    Web Client     |
|   (Qt C++ GUI)    |           |    (TypeScript)   |
+---------+---------+           +---------+---------+
          |                               | (WebSocket)
          | (Raw TCP FSP)                 v
          |                     +---------+---------+
          |                     | Protocol Gateway  |
          |                     +---------+---------+
          |                               | (Raw TCP FSP)
          v                               v
    +-----+-------------------------------+-----+
    |                 FSP SERVER                |
    |  - Raw TCP Listener & Worker ThreadPool   |
    |  - Authentication & Session Store         |
    |  - POSIX-like Permission Engine           |
    |  - Virtual File System (Sandbox Storage)  |
    +-------------------------------------------+
```

### 2.2. Các tác nhân người dùng (User Classes & Roles)
1. **Khách (Guest)**: Chỉ có thể thực hiện đăng ký và đăng nhập.
2. **Người dùng thông thường (Authenticated User)**:
   * Có thư mục cá nhân riêng (`/home/{username}`).
   * Có quyền sở hữu (Owner) đối với các file/thư mục do mình tạo hoặc upload.
   * Có thể truy cập các file/thư mục được người khác chia sẻ (Group/Public).
3. **Quản trị viên (Superuser / Admin)**: Có quyền tối cao trên toàn bộ hệ thống file, quản lý người dùng và nhóm.

---

## 3. CÁC YÊU CẦU CHỨC NĂNG (FUNCTIONAL REQUIREMENTS - FR)

### FR-01: Quản lý Người dùng & Xác thực (User Management & Authentication)
* **FR-01.1 Đăng ký**: Cho phép người dùng đăng ký tài khoản với `username`, `password`, `full_name`. Server kiểm tra tính duy nhất của username.
* **FR-01.2 Mã hóa mật khẩu**: Tuyệt đối không lưu mật khẩu dạng thô (plain-text). Mật khẩu phải được băm bằng thuật toán SHA-256 kết hợp chuỗi ngẫu nhiên (Salt 16 bytes).
* **FR-01.3 Đăng nhập & Tạo phiên**: Khi đăng nhập thành công, Server sinh ra một chuỗi UUID làm `Session Token` có thời hạn (timeout) và gắn liền với phiên kết nối hiện tại.
* **FR-01.4 Đăng xuất**: Hủy `Session Token` trên Server và đóng các phiên truyền file dở dang.

### FR-02: Lệnh Quản lý File kiểu FTP (FTP-like File Browsing)
* **FR-02.1 Liệt kê thư mục (`ls` / `dir`)**: Trả về danh sách các tệp tin và thư mục con trong thư mục làm việc hiện tại kèm các thuộc tính: Tên, Kích thước (bytes), Thời gian sửa đổi, Người sở hữu (Owner), và Chuỗi quyền (ví dụ: `rwxr-xr--`).
* **FR-02.2 Chuyển thư mục (`cd`)**: Cho phép chuyển vào thư mục con (`cd folder_name`) hoặc lùi lại thư mục cha (`cd ..`). Server phải kiểm tra và chặn triệt để nếu đường dẫn vượt ra ngoài thư mục sandbox root.
* **FR-02.3 In đường dẫn hiện tại (`pwd`)**: Trả về đường dẫn ảo (Virtual Relative Path) mà người dùng đang đứng.
* **FR-02.4 Tạo thư mục (`mkdir`)**: Cho phép tạo thư mục mới nếu người dùng có quyền Write trên thư mục cha.
* **FR-02.5 Xóa File/Thư mục (`rm` / `rmdir`)**: Xóa tệp tin hoặc thư mục (hỗ trợ xóa rỗng hoặc đệ quy) khi người dùng có quyền hợp lệ.

### FR-03: Upload & Download File Đơn lẻ (Single File Transfer)
* **FR-03.1 Cơ chế Chunking**: File upload/download được chia nhỏ thành các khối kích thước chuẩn 64 KB (`FSP::DEFAULT_CHUNK_SIZE`).
* **FR-03.2 Bắt đầu phiên truyền (Handshake)**:
  * Client gửi `FILE_UPLOAD_REQ` kèm metadata: tên file, kích thước tổng, mã SHA-256 tổng.
  * Server kiểm tra dung lượng đĩa, quyền ghi và trả về `transfer_id`.
* **FR-03.3 Truyền dữ liệu khối**: Các khối dữ liệu được truyền liên tục bằng gói `FILE_CHUNK` có header `FileChunkHeader` chứa số thứ tự chunk (`chunk_index`), tổng số chunk và mã `chunk_crc`.
* **FR-03.4 Kiểm tra tính toàn vẹn (Integrity Check)**: Khi hoàn tất, cả hai phía tính lại mã checksum của toàn bộ file sau khi ghép. Nếu không khớp, hủy file hỏng và báo lỗi.

### FR-04: Upload & Download Cả Thư mục (Folder Transfer)
* **FR-04.1 Duyệt đệ quy (Recursive Scanning)**: Client (hoặc Server) quét toàn bộ cây thư mục con và các file bên trong, tạo ra bản đồ cấu trúc JSON (Folder Tree Manifest).
* **FR-04.2 Dựng khung thư mục trước**: Bên nhận đọc manifest và tự động tạo toàn bộ cấu trúc thư mục rỗng tương ứng trên đĩa.
* **FR-04.3 Truyền tuần tự các file**: Hệ thống tự động kích hoạt truyền lần lượt từng file trong cây thư mục cho đến khi hoàn tất 100%.

### FR-05: Tìm kiếm File & Tải Trực tiếp (File Search & Direct Download)
* **FR-05.1 Tìm kiếm linh hoạt**: Hỗ trợ tìm kiếm theo chuỗi con (substring) của tên file, phần mở rộng (extension) hoặc biểu thức chính quy (regex).
* **FR-05.2 Lọc theo quyền truy cập (Permission-Aware Search)**: Kết quả tìm kiếm chỉ được hiển thị các file mà tài khoản người dùng hiện tại **có quyền đọc (READ access)**. Các file `PRIVATE` của người khác sẽ bị ẩn hoàn toàn.
* **FR-05.3 Tải trực tiếp từ kết quả**: Cho phép người dùng click đúp hoặc chọn file trong danh sách kết quả tìm kiếm để tải về ngay lập tức mà không cần phải tự điều hướng `cd` đến thư mục chứa file.

### FR-06: Hệ thống Phân quyền theo kiểu Linux (Linux-like Permissions)
* **FR-06.1 Mô hình Phân quyền (POSIX 3-tier)**:
  * Mỗi file/folder có metadata lưu: `owner_id`, `group_id`, `permission_bits` (9 bits: $rwx$ cho User, Group, Others).
  * Quy ước:
    * `r` (Read = 4): Xem nội dung, tải file về, liệt kê thư mục.
    * `w` (Write = 2): Tạo mới, sửa đổi, xóa, đổi tên, ghi đè.
    * `s` (Share/Execute = 1): Thay đổi quyền (chmod), chia sẻ file cho người khác.
* **FR-06.2 Mức độ chia sẻ (Sharing Levels)**:
  * `PRIVATE (0)`: Chỉ Owner mới có thể nhìn thấy và truy cập.
  * `SHARED (1)`: Cho phép các thành viên trong cùng nhóm (Group) hoặc danh sách tài khoản được chỉ định cụ thể (ACL) truy cập theo quyền cấp riêng.
  * `PUBLIC (2)`: Tất cả người dùng đã đăng nhập hệ thống đều có quyền đọc (`r`).
* **FR-06.3 Lệnh thay đổi quyền (`chmod`)**: Chỉ người sở hữu file (Owner) hoặc Admin mới có quyền đổi bit permission và mức chia sẻ của file/folder đó.

### FR-07: Giao diện Đồ họa Người dùng (GUI)
* **FR-07.1 Màn hình Đăng nhập/Đăng ký**: Giao diện trực quan, lưu cấu hình IP/Port của Server.
* **FR-07.2 Khung duyệt file 2 ngăn (Dual-Pane Explorer)**: Ngăn trái hiển thị thư mục máy cục bộ (Local), ngăn phải hiển thị cây thư mục máy chủ (Remote).
* **FR-07.3 Bảng quản lý truyền tải (Transfer Manager)**: Hiển thị thanh tiến trình (0-100%), tốc độ truyền tức thời (KB/s, MB/s) và ước lượng thời gian còn lại (ETA).
* **FR-07.4 Menu ngữ cảnh & Dialogs**: Chuột phải hỗ trợ Download, Upload, Delete, Rename, Edit Permissions (hộp thoại checkbox $r, w, s$ trực quan).

---

## 4. CÁC YÊU CẦU PHI CHỨC NĂNG (NON-FUNCTIONAL REQUIREMENTS - NFR)

* **NFR-01 (Hiệu năng & Tiết kiệm Bộ nhớ)**: Truyền tải file lớn (>= 2GB) mà lượng RAM chiếm dụng của Server và Client không vượt quá 50MB (nhờ cơ chế chunking stream trực tiếp từ đĩa vào socket).
* **NFR-02 (Tính toàn vẹn Dữ liệu)**: Sai lệch dữ liệu sau truyền tải phải bằng 0% (xác minh bằng CRC32 từng chunk và SHA-256 toàn file).
* **NFR-03 (Khả năng Phục vụ Đồng thời - Concurrency)**: Server sử dụng mô hình Thread Pool kết hợp I/O Multiplexing, đảm bảo xử lý đồng thời tối thiểu 50 clients kết nối ổn định không bị deadlock hay race condition.
* **NFR-04 (Bảo mật - Security)**:
  * Mật khẩu băm an toàn kèm Salt.
  * Virtual File System kiểm tra hàm chuẩn hóa `std::filesystem::canonical` để triệt tiêu 100% lỗ hổng Path Traversal.
* **NFR-05 (Độ mượt Giao diện - UI Responsiveness)**: Tuyệt đối không để xảy ra hiện tượng "Not Responding" trên GUI. Tách riêng UI Thread (Qt Event Loop) và Network Worker Thread.

---

## 5. MA TRẬN PHÂN QUYỀN TRUY CẬP (ACCESS CONTROL MATRIX)

| Hành động | Owner (File) | Cùng Group | Others (Public) | Others (Shared ACL) | Others (Private) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Xem danh sách (`ls`)** | Cho phép | Nếu có bit `r` | Cho phép | Cho phép | **Từ chối** |
| **Tải file (`download`)** | Cho phép | Nếu có bit `r` | Cho phép | Nếu ACL có `r` | **Từ chối** |
| **Ghi đè / Sửa file** | Nếu có bit `w` | Nếu có bit `w` | **Từ chối** | Nếu ACL có `w` | **Từ chối** |
| **Xóa file (`delete`)** | Cho phép | Nếu có bit `w` | **Từ chối** | **Từ chối** | **Từ chối** |
| **Đổi quyền (`chmod`)** | Cho phép | **Từ chối** | **Từ chối** | **Từ chối** | **Từ chối** |
