# BẢN THIẾT KẾ PHẦN MỀM (SOFTWARE DESIGN DOCUMENT - SDD)
## DỰ ÁN: ỨNG DỤNG CHIA SẺ FILE TRỰC TUYẾN QUA RAW SOCKET C/C++ (TOPIC 6)

**Tên hệ thống:** File Sharing Protocol (FSP) System  
**Ngôn ngữ cốt lõi:** C++17 (Raw Sockets: Winsock2 / POSIX Sockets)  
**Giao diện:** Qt6 / Qt5 C++ Desktop Client & TypeScript Web Client  
**Nhóm thực hiện:** Trần Sỹ Nguyên, Bùi Đức Hiếu, Vũ Nhật Minh Dương, Hà Đức Dương  

---

## MỤC LỤC
1. [Chương 1: Tổng quan Dự án & Bối cảnh](#chương-1-tổng-quan-dự-án--bối-cảnh)
2. [Chương 2: Thiết kế Kiến trúc Cấp cao (High-Level Design - HLD)](#chương-2-thiết-kế-kiến-trúc-cấp-cao-high-level-design---hld)
3. [Chương 3: Chi tiết Tầng Mạng & Giao thức FSP (LLD - Trần Sỹ Nguyên)](#chương-3-chi-tiết-tầng-mạng--giao-thức-fsp-lld---trần-sỹ-nguyên)
4. [Chương 4: Hệ thống Người dùng & Phân quyền Linux (LLD - Bùi Đức Hiếu)](#chương-4-hệ-thống-người-dùng--phân-quyền-linux-lld---bùi-đức-hiếu)
5. [Chương 5: Virtual File System, Chunk Transfer & Search (LLD - Vũ Nhật Minh Dương)](#chương-5-virtual-file-system-chunk-transfer--search-lld---vũ-nhật-minh-dương)
6. [Chương 6: Kiến trúc Client GUI & Mở rộng Web TypeScript (LLD - Hà Đức Dương)](#chương-6-kiến-trúc-client-gui--mở-rộng-web-typescript-lld---hà-đức-dương)
7. [Chương 7: Kiểm thử Hệ thống & Đánh giá Hiệu năng](#chương-7-kiểm-thử-hệ-thống--đánh-giá-hiệu-năng)
8. [Chương 8: Kết luận & Hướng phát triển](#chương-8-kết-luận--hướng-phát-triển)

---

## CHƯƠNG 1: TỔNG QUAN DỰ ÁN & BỐI CẢNH

### 1.1 Mục tiêu dự án
Xây dựng một hệ thống chia sẻ tệp tin đa nền tảng theo mô hình Client-Server. Điểm mấu chốt của dự án là **không sử dụng các thư viện mạng cấp cao có sẵn** (như libcurl, boost.asio, socket.io framework) mà tự tay thiết kế và lập trình toàn bộ tầng giao tiếp mạng từ raw socket C/C++, giải quyết triệt để các bài toán vận hành ở tầng Transport Layer (TCP).

### 1.2 Yêu cầu nghiệp vụ cốt lõi
* Quản lý người dùng, đăng ký, đăng nhập với mật khẩu được bảo vệ an toàn.
* Điều hướng thư mục và quản lý tệp theo phong cách giao thức FTP (`ls`, `cd`, `pwd`, `mkdir`, `rm`).
* Truyền tệp đơn lẻ kích thước lớn (hàng Gigabyte) mà không gây tràn bộ nhớ.
* Truyền toàn bộ cây thư mục đệ quy (Recursive directory upload/download).
* Tìm kiếm tệp tin thông minh kết hợp kiểm tra quyền truy cập.
* Hệ thống phân quyền chặt chẽ lấy cảm hứng từ POSIX/Linux (Owner - Group - Others với các quyền Read, Write, Share).
* Giao diện đồ họa người dùng mượt mà, phản hồi tức thì (Responsive UI).

---

## CHƯƠNG 2: THIẾT KẾ KIẾN TRÚC CẤP CAO (HIGH-LEVEL DESIGN - HLD)

### 2.1 Sơ đồ Kiến trúc Tổng thể (High-Level Architecture)
Hệ thống được thiết kế theo mô hình 3 tầng phân rã rõ rệt:

```
[ TẦNG TRÌNH DIỄN (PRESENTATION LAYER) ]
    +----------------------------------+       +----------------------------------+
    | Desktop Client GUI (Qt6 / C++)   |       | Web Client (TypeScript / React)  |
    +-----------------+----------------+       +-----------------+----------------+
                      |                                          | (WebSocket)
                      |                                          v
                      |                        +-----------------+----------------+
                      |                        | Protocol Gateway (Node.js/TS)    |
                      |                        +-----------------+----------------+
                      | (Raw TCP FSP)                            | (Raw TCP FSP)
                      v                                          v
[ TẦNG TRUYỀN DẪN & ĐIỀU PHỐI (COMMUNICATION & DISPATCHING LAYER) ]
    +-----------------------------------------------------------------------------+
    | FSP Core Network Engine (Custom 42-Byte Frame, Anti-Sticky Loop, Dispatcher)|
    +--------------------------------------+--------------------------------------+
                                           |
[ TẦNG NGHIỆP VỤ & LƯU TRỮ (BUSINESS LOGIC & STORAGE LAYER) ]
    +--------------------------------------+--------------------------------------+
    | • Auth & Session Manager (M2)        | • Virtual File System Sandbox (M3)   |
    | • Linux-like Permission Engine (M2)  | • Chunked Transfer Engine 64KB (M3)  |
    | • User & Group Database (M2)         | • Permission-Aware Search (M3)       |
    +--------------------------------------+--------------------------------------+
```

### 2.2 Luồng Dữ liệu Tổng quát (Data Flow)
1. **Khởi tạo kết nối**: Client kết nối TCP tới Server cổng `8088`. Server chấp thuận và đưa socket vào ThreadPool.
2. **Xác thực**: Client gửi gói `EVENT_REQ` mang tên `AUTH_LOGIN`. Server xác thực mật khẩu qua Salted SHA-256 và trả về `Session Token`.
3. **Thao tác tệp / Thư mục**: Mọi yêu cầu tiếp theo (`DIR_LIST`, `DIR_MKDIR`, ...) đều đính kèm `Session Token`. Server kiểm tra quyền thông qua `PermissionEngine` trước khi cho phép `VirtualFileSystem` thực thi.
4. **Truyền tệp nhị phân**: Dữ liệu tệp không đi qua chuỗi JSON mà được đóng gói thành các khối nhị phân liên tiếp `FILE_CHUNK` (64KB/chunk), có mã kiểm tra CRC32 riêng cho từng khối.

---

## CHƯƠNG 3: CHI TIẾT TẦNG MẠNG & GIAO THỨC FSP (LLD - TRẦN SỸ NGUYÊN)
*(Phần này do **Trần Sỹ Nguyên** trực tiếp phụ trách và hoàn thiện báo cáo kỹ thuật)*

### 3.1 Thiết kế Cấu trúc Gói tin (FSP Frame Layout)
Giao thức FSP sử dụng cơ chế **Length-Prefixed Framing**. Mỗi frame bắt buộc bắt đầu bằng Header cố định 42 bytes được bảo vệ bởi `#pragma pack(1)`:

0 1 2 3 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ | Magic (0xAA) | Type (0x01) | Payload Length | +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ | |

                Event Name (32 Bytes)                   +

| | +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ | Checksum (CRC32) | +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ | PAYLOAD DATA | | (Biến thiên: payload_len bytes) | +---------------------------------------------------------------+

| Tên trường | Kiểu dữ liệu | Kích thước | Mô tả |
| :--- | :--- | :--- | :--- |
| `magic` | `uint8_t` | 1 byte | Byte định danh cố định `0xAA` |
| `type` | `uint8_t` | 1 byte | Loại thông điệp (`EVENT_REQ`, `EVENT_RESP`, `FILE_CHUNK`, ...) |
| `payload_len`| `uint32_t` | 4 bytes | Độ dài dữ liệu đi sau Header (Network Byte Order - Big Endian) |
| `event` | `char[32]` | 32 bytes | Tên sự kiện (ví dụ: `AUTH_LOGIN`, `DIR_LIST`) |
| `checksum` | `uint32_t` | 4 bytes | Mã CRC32 của phần Payload |

### 
### 3.2 Giải quyết Triệt để Bài toán Dính gói & Phân mảnh TCP
* **Bản chất vấn đề**: TCP là giao thức hướng luồng (byte-stream), không duy trì biên giới gói tin. Khi truyền nhận tốc độ cao, hiện tượng Dính gói (Sticky Packets) hoặc Phân mảnh (Fragmentation) chắc chắn xảy ra.
* **Giải pháp**: Hiện thực lớp `PacketHandler` với giải thuật vòng lặp `readExact`:
  1. Đọc chính xác 42 bytes đầu tiên để dựng `FSPHeader`.
  2. Xác minh `header.magic == 0xAA`. Nếu sai lệch lập tức đóng kết nối (Fail-Closed).
  3. Đọc đúng `ntohl(header.payload_len)` bytes từ luồng TCP vào buffer.
  4. Xác minh mã băm IEEE 802.3 CRC32 trước khi đưa lên tầng xử lý nghiệp vụ.

### 3.3 Kiến trúc Đa luồng Server (Worker ThreadPool)
* **Main Listener Thread**: Khởi tạo socket, `bind()`, `listen()`, chạy vòng lặp vô tận chỉ làm nhiệm vụ `accept()` kết nối mới.
* **Worker ThreadPool**: Sử dụng hàng đợi an toàn đa luồng (`ThreadSafeQueue<int> clientSockets`) kết hợp `std::condition_variable` để đánh thức các worker threads xử lý yêu cầu song song.

---

## CHƯƠNG 4: HỆ THỐNG NGƯỜI DÙNG & PHÂN QUYỀN LINUX (LLD - BÙI ĐỨC HIẾU)
*(Phần này do **Bùi Đức Hiếu** trực tiếp phụ trách và hoàn thiện báo cáo kỹ thuật)*

### 4.1 Cấu trúc Dữ liệu & Quản lý Phiên làm việc
* **Bảng Người dùng (Users)**:
  * `user_id` (Khóa chính), `username`, `password_hash` (Chuỗi hex 64 ký tự), `salt` (Chuỗi ngẫu nhiên 16 bytes), `full_name` (tùy chọn, tối đa 100 byte), `group_id`, `created_at`.
  * Lưu dạng JSON Lines (`users.json`, mỗi dòng một đối tượng phẳng), ghi nguyên tử (file tạm rồi `rename`). File hỏng làm hàm khởi tạo ném lỗi thay vì khởi động với dữ liệu rỗng (fail-closed).
  * `user_id` tăng dần từ 1. **Admin là `user_id == 1`** (người đăng ký đầu tiên), vì bảng Users không có trường admin.
  * Mỗi user bắt đầu trong nhóm riêng có `group_id == user_id`; chỉ Admin được gọi `setGroup` để đổi nhóm.
* **Cơ chế Băm Mật khẩu**:
  $$\text{Hash} = \text{SHA256}(\text{Password} + \text{Salt})$$
  Không bao giờ lưu trữ mật khẩu gốc trên đĩa nhằm đảm bảo chuẩn an toàn thông tin.
* **Session Token**:
  * Khi người dùng đăng nhập thành công, Server sinh ra một chuỗi UUID ngẫu nhiên (Session Token).
  * Session ánh xạ với trạng thái phiên: `{user_id, login_time, last_active, current_virtual_directory}`.
  * Hết thời gian chờ (Timeout 30 phút không hoạt động), phiên tự động bị hủy. Mỗi lần `validate` thành công sẽ làm mới `last_active`.
  * Salt và token lấy từ nguồn ngẫu nhiên của hệ điều hành (`getrandom(2)` trên Linux, `std::random_device` nơi khác). Nếu nguồn lỗi thì ném ngoại lệ, không bao giờ dùng nguồn yếu thay thế.
* **Chống dò mật khẩu**: sau 5 lần đăng nhập sai liên tiếp, tên đăng nhập bị khóa 5 phút (kể cả khi nhập đúng mật khẩu). Tên không tồn tại cũng bị làm chậm như vậy để không lộ thông tin tồn tại. Bảng đếm giới hạn 10.000 tên; khi đầy thì bỏ qua tên mới, không bao giờ giải phóng một khóa đang hiệu lực.
* **Chống tấn công thời gian**: so sánh hash bằng hàm thời gian hằng số; tên không tồn tại vẫn tính một lần băm.

### 4.2 Thuật toán Kiểm tra Quyền Truy cập (POSIX-like Permission Engine)
Mỗi tệp tin và thư mục có metadata lưu trữ: `owner_id`, `group_id`, `permission_bits` (9 bits) và `sharing_level`.

```
                  +--------------------------------+
                  |  Yêu cầu truy cập từ User A    |
                  +---------------+----------------+
                                  |
                                  v
                       [ User A == Admin? ]  ---(Yes)---> [ CHO PHÉP (ALLOW) ]
                                  | (No)
                                  v
                     [ User A == File.Owner? ] --(Yes)--> [ Kiểm tra bit Owner (r/w/s) ]
                                  | (No)
                                  v
                   [ User A cùng Group với File? ] -(Yes)-> [ Kiểm tra bit Group (r/w/s) ]
                                  | (No)
                                  v
                     [ Kiểm tra Sharing Level ]
                                  |
            +---------------------+---------------------+
            |                                           |
    [ LEVEL: PUBLIC ]                           [ LEVEL: SHARED ]
            |                                           |
            v                                           v
[ Cho phép nếu yêu cầu là READ (r) ]        [ Kiểm tra ACL có User A không? ]
                                                        |
                                                 (Có: Cho phép theo quyền ACL)
                                                 (Không: TỪ CHỐI - FORBIDDEN 403)
```

**Quy tắc chi tiết của `canAccess(userId, path, type)`** (thứ tự kiểm tra):
1. Đường dẫn chưa có metadata: **TỪ CHỐI** (fail-closed), kể cả Admin.
2. Admin (`user_id == 1`): CHO PHÉP.
3. Owner: `READ` và `SHARE` luôn được phép (ma trận SRS mục 5); `WRITE` chỉ khi bit `OWNER_WRITE` bật.
4. Mức `PRIVATE`: TỪ CHỐI mọi người còn lại, bất kể bit Group/Others.
5. User không tồn tại: TỪ CHỐI.
6. Cùng Group và bit `GROUP_*` tương ứng bật: CHO PHÉP.
7. Mức `PUBLIC` và yêu cầu là `READ`: CHO PHÉP.
8. Mức `SHARED` và ACL của user chứa quyền yêu cầu: CHO PHÉP.
9. Còn lại: TỪ CHỐI.

Các bit `OTHERS_*` cố ý không dùng: người ngoài nhóm chỉ bị chi phối bởi mức chia sẻ và ACL.

### 4.3 Thay đổi Quyền, Chủ sở hữu và Đổi tên
* `chmod`, `setSharingLevel`, `grantAcl`, `revokeAcl`: cần quyền `SHARE` (Owner luôn có, Admin luôn có, Group/Others thì không). `chmod` chỉ nhận bit trong `0777`.
* `chown`: chỉ Admin (giống Linux); chủ mới phải tồn tại, nhóm mới khác 0.
* `renamePath(actor, old, new)`: cần quyền `WRITE` trên `old`; chuyển metadata của `old` và mọi mục con (`old/...`) sang `new`. Từ chối nếu `new` trùng `old`, nằm trong `old`, hoặc đích đã có metadata (`409`). Thay đổi được hoàn tác nếu ghi đĩa thất bại. M3 chịu trách nhiệm kiểm tra quyền thư mục đích.
* `registerPath` / `removePath`: M3 gọi khi tạo/xóa tệp để metadata luôn khớp với đĩa.
* Mọi thao tác ghi đều lưu `permissions.json` nguyên tử; nếu lưu lỗi thì khôi phục trạng thái cũ và trả `500`.

### 4.4 Xử lý Sự kiện Xác thực (AuthHandler)
Payload là một đối tượng JSON phẳng; token phiên nằm **trong payload** vì header 42 byte không có chỗ chứa.

| Sự kiện | Yêu cầu | Thành công | Lỗi |
| :--- | :--- | :--- | :--- |
| `AUTH_REGISTER` | `{"username","password","full_name"?}` | `201 {"user_id":N}` | `400` dữ liệu sai, `409` trùng tên, `500` |
| `AUTH_LOGIN` | `{"username","password"}` | `200 {"token":"..."}` | `400`, `401` sai thông tin hoặc đang bị khóa |
| `AUTH_LOGOUT` | `{"token"}` | `200 {}` | `400`, `401` token giả/hết hạn/đã đăng xuất |

* Username khớp `[A-Za-z0-9_]{3,32}`, mật khẩu 6 đến 128 ký tự. Thông báo lỗi chung chung, không tiết lộ username có tồn tại hay không.
* Đăng ký thành công còn tạo metadata `/home/<username>` (`PRIVATE`, `0755`, chủ là user mới); thư mục thật trên đĩa do M3 tạo.
* Mọi ngoại lệ nội bộ được chuyển thành `500`.

### 4.5 Kiểm thử và Giới hạn đã biết
* Bộ test `fsp_auth_tests` (190 kiểm tra trên dữ liệu thật) chạy sạch dưới AddressSanitizer + UBSan và ThreadSanitizer (8 luồng đăng ký, đăng nhập, kiểm tra quyền đồng thời). Bao phủ Probe 2 (token giả/đã đăng xuất/hết hạn), Probe 3 (ma trận quyền, file `PRIVATE` của người khác bị `FORBIDDEN`) và Probe 6.
* Vectơ chuẩn NIST cho SHA-256; hash khác nhau cho cùng mật khẩu với salt khác; mật khẩu gốc không bao giờ xuất hiện trong `users.json`.
* **Giới hạn**: chỉ có một loại truy cập `WRITE` nên "Owner luôn được xóa" (SRS) chưa tách riêng khỏi bit ghi; cần thêm `AccessType::DELETE` trong `FSPProtocol.h` (cần cả 4 thành viên đồng ý). Chưa có xử lý `PERM_GET`/`PERM_SET` và chưa nối vào dispatcher của M1.

---

## CHƯƠNG 5: VIRTUAL FILE SYSTEM, CHUNK TRANSFER & SEARCH (LLD - VŨ NHẬT MINH DƯƠNG)
*(Phần này do **Vũ Nhật Minh Dương** trực tiếp phụ trách và hoàn thiện báo cáo kỹ thuật)*

### 5.1 Virtual File System & Cơ chế Chống Path Traversal
* Toàn bộ tệp tin của người dùng được giới hạn trong thư mục gốc trên server: `./storage/server_root`.
* **Giải pháp ngăn chặn tấn công Path Traversal**:
  Mọi đường dẫn gửi lên từ client đều phải đi qua hàm chuẩn hóa:
  ```cpp
  std::filesystem::path resolveVirtualPath(const std::string& virtualPath) {
      std::filesystem::path base = std::filesystem::canonical("./storage/server_root");
      std::filesystem::path full = std::filesystem::weakly_canonical(base / virtualPath);
      // Kiểm tra xem full path có bắt đầu bằng base path không
      if (full.string().rfind(base.string(), 0) != 0) {
          throw std::security_error("Path traversal attack detected!");
      }
      return full;
  }
  ```

### 5.2 Cơ chế Truyền dữ liệu Khối (Chunked Streaming Engine)
* **Kích thước khối**: Chuẩn 64 KB (`65,536 bytes`).
* **Header đính kèm**: Mỗi khối mang theo `FileChunkHeader` (88 bytes) chứa `transfer_id`, `chunk_index`, `total_chunks`, `chunk_size`, và `chunk_crc`.
* **Ưu điểm kiến trúc**:
  * Tiết kiệm bộ nhớ: Cả Server và Client chỉ cần một buffer 64KB, không bao giờ phải nạp toàn bộ file 2GB-10GB vào RAM.
  * Hỗ trợ Pause/Resume và đo tiến trình (Progress Bar) chính xác từng byte.

### 5.3 Thuật toán Truyền Đệ quy Thư mục (Recursive Folder Transfer)
1. **Giai đoạn quét (Manifest Generation)**: Quét đệ quy cây thư mục bằng `std::filesystem::recursive_directory_iterator`.
2. **Giai đoạn đồng bộ cấu trúc**: Gửi cây thư mục dạng JSON về phía nhận để tạo sẵn toàn bộ các thư mục con rỗng.
3. **Giai đoạn truyền tệp**: Kích hoạt vòng lặp truyền tuần tự từng tệp tin theo đường dẫn tương đối đã tạo.

### 5.4 Công cụ Tìm kiếm Tệp kết hợp Bộ lọc Quyền
Khi nhận yêu cầu `SEARCH_QUERY`:
1. Server quét toàn bộ metadata lưu trữ trong Virtual File System.
2. So khớp từ khóa với tên file bằng thuật toán tìm kiếm chuỗi con (hoặc regex).
3. Với mỗi file khớp từ khóa, gọi `PermissionEngine::canAccess(user_id, file, READ)`.
4. Chỉ gom những file có quyền đọc vào danh sách kết quả phản hồi cho Client.

---

## CHƯƠNG 6: KIẾN TRÚC CLIENT GUI & MỞ RỘNG WEB TYPESCRIPT (LLD - HÀ ĐỨC DƯƠNG)
*(Phần này do **Hà Đức Dương** trực tiếp phụ trách và hoàn thiện báo cáo kỹ thuật)*

### 6.1 Kiến trúc Đa luồng Client (Qt6 C++)
Để tránh hiện tượng đóng băng giao diện ("Not Responding") khi truyền tải file lớn:
* **UI Thread**: Chạy `QApplication::exec()`, xử lý giao diện người dùng, bảng điều khiển 2 ngăn, bắt sự kiện click chuột.
* **Network Worker Thread**: Chạy trên `QThread` riêng biệt, duy trì kết nối socket liên tục với Server.
* **Giao tiếp liên luồng**: Sử dụng cơ chế `Qt Signals & Slots` đảm bảo an toàn tuyến tính (Thread-safe queued connections).

### 6.2 Thiết kế Giao diện Desktop Client
* **Dual-Pane Explorer**: Cột trái duyệt máy tính cá nhân, cột phải duyệt Server.
* **Transfer Manager**: Danh sách tác vụ đang truyền kèm Progress Bar, tốc độ tức thời và thời gian còn lại.
* **Permission Dialog**: Giao diện chọn quyền bằng Checkbox trực quan thay cho dòng lệnh phức tạp.

### 6.3 Kiến trúc Mở rộng Web Client (TypeScript + WebSocket Gateway)
*(Triển khai ở Tuần 3-4 phục vụ thuyết trình)*
* Xây dựng một **Protocol Gateway** siêu nhẹ bằng Node.js/TypeScript:
  * Lắng nghe kết nối WebSocket từ trình duyệt Web (`ws://localhost:8080`).
  * Mở kết nối TCP Socket tới Server C++ (`tcp://localhost:8088`).
  * Thực hiện chuyển đổi 1-1 giữa các khung tin WebSocket và FSP Frame 42-byte.
* Giao diện Web được xây dựng bằng **React + Tailwind CSS** hiện đại, hỗ trợ kéo thả file (Drag & Drop) và tương tác thời gian thực.

---

## CHƯƠNG 7: KIỂM THỬ HỆ THỐNG & ĐÁNH GIÁ HIỆU NĂNG

### 7.1 Kịch bản Kiểm thử Tầng Mạng (Stress & Edge Cases)
* **Test Case 1: Chống Dính Gói (Sticky Packets)**: Gửi liên tiếp 500 gói tin nhỏ (50 bytes) trong thời gian 100ms. Kết quả mong đợi: Server nhận đủ 500 sự kiện độc lập, không bị gộp gói.
* **Test Case 2: Truyền Tệp Lớn (Large File Transfer)**: Upload và download file kích thước 2.5 GB. Kết quả mong đợi: RAM Server duy trì ổn định dưới 30MB, mã SHA-256 trước và sau truyền khớp nhau 100%.
* **Test Case 3: Đa kết nối Đồng thời (High Concurrency)**: Giả lập 30 clients đồng thời duyệt file và tải file. Kết quả mong đợi: Không xảy ra deadlock hay crash tiến trình.

### 7.2 Kịch bản Kiểm thử Quyền Truy cập (Security & Permissions)
* **Test Case 4**: User A upload file và đặt chế độ `PRIVATE`. User B tìm kiếm file đó $\implies$ Kết quả: File không xuất hiện trong danh sách tìm kiếm của User B.
* **Test Case 5**: User B cố tình gửi packet giả mạo đòi tải file `PRIVATE` của User A $\implies$ Kết quả: Server từ chối và trả về lỗi `403 FORBIDDEN`.
* **Test Case 6: Path Traversal**: Gửi yêu cầu tải file `../../../../windows/system.ini` $\implies$ Kết quả: Bị Server chặn và ghi nhận log cảnh báo an ninh.

---

## CHƯƠNG 8: KẾT LUẬN & HƯỚNG PHÁT TRIỂN
* Hệ thống đã hiện thực thành công một ứng dụng chia sẻ tệp tin hoàn chỉnh từ nền tảng Socket C/C++ nguyên thủy, tuân thủ đúng các nguyên lý cốt lõi của môn học Lập trình Mạng.
* Mô hình phân rã rõ ràng giữa Giao thức truyền dẫn (Protocol), Quản trị người dùng & Phân quyền (Auth/Perm), Quản lý tệp (Filesystem) và Giao diện (GUI/Web) chứng minh tính mở rộng và khả thi trong các ứng dụng thực tế.
