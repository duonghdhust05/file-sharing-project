# WORK BREAKDOWN STRUCTURE (WBS) & CHECKLIST DỰ ÁN
## PHÂN CHIA CÔNG VIỆC CHO NHÓM 4 THÀNH VIÊN ĐỘC LẬP
**Dự án:** Client-Server File Sharing Application (Topic 6)  
**Nguyên tắc làm việc:** Module hóa cao độ (High Cohesion, Loose Coupling) - Làm việc song song dựa trên hợp đồng giao thức (`FSPProtocol.h`).

---

## 1. NGUYÊN TẮC HOẠT ĐỘNG SONG SONG ĐỘC LẬP
1. **Tuân thủ Hợp đồng Giao thức (Contract-First)**: Mọi thành viên sử dụng chung các cấu trúc dữ liệu trong `include/common/FSPProtocol.h`.
2. **Kiểm thử Mock / Stub**:
   * Khi Server chưa hoàn thiện, Thành viên 4 (GUI) dùng Mock Server hoặc dữ liệu tĩnh để code UI.
   * Khi Network Engine chưa xong, Thành viên 2 (Auth) và Thành viên 3 (Filesystem) viết các hàm logic nhận tham số `std::string` JSON và test bằng Unit Test độc lập.
3. **Quy tắc Git Branching**: Mỗi người làm trên nhánh riêng (`feat/m1-...`, `feat/m2-...`, `feat/m3-...`, `feat/m4-...`), chỉ merge vào nhánh `develop` sau khi đã pass 100% checklist cá nhân.

---

## 2. BẢN PHÂN CÔNG CHI TIẾT VÀ CHECKLIST CHO TỪNG THÀNH VIÊN

### THÀNH VIÊN 1 (M1 - Trần Sỹ Nguyên): KIẾN TRÚC MẠNG CỐT LÕI & GIAO THỨC TỰ TẠO
> **Trọng tâm:** Tầng Socket C/C++ nguyên thủy, Frame 42-byte, chống dính gói TCP, Event Dispatcher, Đa luồng Server (ThreadPool).

* **Header chịu trách nhiệm:** `include/server/NetworkServer.h`, `include/client/NetworkClient.h`, `include/common/EventDispatcher.h`
* **Checklist Nhiệm vụ Chi tiết:**
  - [ ] **M1.1**: Viết lớp Wrapper cho Socket đa nền tảng (`Cross-platform TCP Socket`: Winsock2 trên Windows, sys/socket trên Linux).
  - [ ] **M1.2**: Cài đặt thuật toán đóng/mở gói Frame 42-byte (`sendPacket`, `recvPacket`):
    - Đọc đúng 42 bytes Header $\to$ kiểm tra Magic byte `0xAA`.
    - Dùng vòng lặp `recv()` chuẩn cho đến khi tích lũy đủ chính xác `payload_len` bytes (giải quyết triệt để vấn đề dính gói và phân mảnh TCP).
  - [ ] **M1.3**: Xây dựng kiến trúc `EventDispatcher` (Mô hình Event-driven giống Socket.IO):
    - Cung cấp hàm đăng ký sự kiện: `registerHandler(eventName, callback)`.
    - Tự động parse payload và kích hoạt đúng callback khi có packet tương ứng đến.
  - [ ] **M1.4**: Xây dựng `ServerWorkerThreadPool` trên Server:
    - Main thread chỉ chạy `accept()`.
    - Client socket được gán cho Worker Thread xử lý non-blocking hoặc thread-per-connection an toàn.
  - [ ] **M1.5**: Triển khai cơ chế Heartbeat (Ping/Pong) để tự động giải phóng tài nguyên khi Client bị mất kết nối đột ngột.
  - [ ] **M1.6**: Viết chương trình kiểm thử độc lập (Dummy Test Tool): Gửi liên tiếp 1000 packets nhỏ và lớn để chứng minh không bị dính gói.
  - [ ] **Báo cáo SDD**: Viết Chương 3 (Low-Level Design - Core Network Architecture).

---

### THÀNH VIÊN 2 (M2 - Bùi Đức Hiếu): XÁC THỰC NGƯỜI DÙNG, QUẢN LÝ PHIÊN & PHÂN QUYỀN LINUX
> **Trọng tâm:** CSDL/Lưu trữ dữ liệu user, mã hóa Salted SHA-256, Session Token, Thuật toán kiểm tra quyền POSIX 3-tier (Owner-Group-Others) và ACL.

* **Header chịu trách nhiệm:** `include/server/UserManager.h`, `include/server/PermissionEngine.h`
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M2.1**: Thiết kế mô hình dữ liệu lưu trữ (SQLite hoặc File-based JSON Database):
    - Quản lý tài khoản: `id`, `username`, `salt`, `password_hash`, `group_id`.
    - Quản lý nhóm và danh sách thành viên trong nhóm.
  - [x] **M2.2**: Cài đặt thuật toán băm mật khẩu bảo mật:
    - Sinh Salt ngẫu nhiên 16 bytes.
    - Băm `Password + Salt` bằng SHA-256. Tuyệt đối không lưu plain text.
  - [x] **M2.3**: Xây dựng module quản lý phiên làm việc (`SessionManager`):
    - Sinh UUID làm `session_token` khi user đăng nhập thành công.
    - Lưu trữ trạng thái phiên: `user_id`, thời điểm hoạt động cuối (last_active), đường dẫn hiện tại (`cwd`).
  - [x] **M2.4**: Lập trình bộ xử lý logic sự kiện Auth cho Server:
    - Xử lý các sự kiện: `AUTH_REGISTER`, `AUTH_LOGIN`, `AUTH_LOGOUT`.
    - Validate dữ liệu đầu vào (độ dài, ký tự hợp lệ).
  - [x] **M2.5**: Xây dựng Bộ máy Phân quyền POSIX/Linux (`PermissionEngine`):
    - Quản lý bitmask 9-bit (`rwxrwxrwx` $\to$ Read, Write, Share).
    - Cài đặt 3 mức độ chia sẻ: `PRIVATE (0)`, `SHARED (1)`, `PUBLIC (2)`.
    - Hàm kiểm tra quyền cốt lõi:
      ```cpp
      bool canAccess(int userId, const std::string& virtualPath, FSP::Permission::AccessType access);
      ```
    - Hàm cập nhật quyền: `chmod` (đổi bitmask), `chown` (đổi người sở hữu).
  - [x] **M2.6**: Viết Unit Test độc lập: Kiểm thử ma trận phân quyền (test các trường hợp: file private, file public, user khác nhóm, user cùng nhóm, quyền ghi, quyền đọc).
  - [x] **Báo cáo SDD**: Viết Chương 4 (Security, Authentication & Access Control System).

---

### THÀNH VIÊN 3 (M3 - Vũ Nhật Minh Dương): HỆ THỐNG FILE ẢO, TRUYỀN DỮ LIỆU KHỐI (CHUNKING) & TÌM KIẾM
> **Trọng tâm:** Sandbox lưu trữ, lệnh duyệt file FTP (`ls`, `cd`, `pwd`, `mkdir`, `rm`), truyền file khối 64KB, truyền đệ quy cả thư mục, công cụ tìm kiếm file theo quyền.

* **Header chịu trách nhiệm:** `include/server/VirtualFileSystem.h`, `include/server/TransferManager.h`, `include/server/SearchEngine.h`
* **Checklist Nhiệm vụ Chi tiết:**
  - [ ] **M3.1**: Xây dựng Sandbox thư mục ảo (`VirtualFileSystem`):
    - Đặt gốc lưu trữ tại `./storage/server_root`.
    - Hàm chuẩn hóa đường dẫn: Chống triệt để lỗ hổng Path Traversal (`../`).
  - [ ] **M3.2**: Hiện thực các lệnh quản lý file kiểu FTP:
    - `handleDirList(path)`: Đọc danh sách file kèm metadata (kết hợp M2 để lấy quyền).
    - `handleDirChange(path)` (`cd`), `handleMakeDir(path)` (`mkdir`), `handleDelete(path)` (`rm`).
  - [ ] **M3.3**: Cài đặt cơ chế Truyền file phân khối (Chunked Transfer Engine):
    - Phía gửi: Mở file nhị phân, đọc từng khối 64KB, đóng gói `FileChunkHeader`, tính CRC32 chunk $\to$ gửi.
    - Phía nhận: Nhận chunk, verify CRC32, ghi đúng vị trí file (`seekp`), khi đủ thì verify SHA-256 tổng.
  - [ ] **M3.4**: Hiện thực cơ chế Truyền cả Thư mục (Recursive Folder Transfer):
    - Quét đệ quy cây thư mục bằng `std::filesystem::recursive_directory_iterator`.
    - Xuất Folder Manifest (cấu trúc JSON các thư mục và file con).
    - Tạo trước toàn bộ cây thư mục đích, sau đó stream tuần tự từng file.
  - [ ] **M3.5**: Xây dựng Công cụ Tìm kiếm File (`SearchEngine`):
    - Tìm kiếm theo từ khóa tên file (hỗ trợ substring, extension, wildcard).
    - Tích hợp bộ lọc quyền của M2: **Chỉ trả về các file mà người dùng hiện tại có quyền đọc**.
  - [ ] **M3.6**: Viết Unit Test độc lập: Upload/Download file mẫu 500MB - 1GB để kiểm chứng RAM không bị tràn, hash checksum trước và sau truyền khớp 100%.
  - [ ] **Báo cáo SDD**: Viết Chương 5 (Virtual File System, Chunked Transfer & Search Architecture).

---

### THÀNH VIÊN 4 (M4 - Hà Đức Dương): ỨNG DỤNG CLIENT GUI & TÍCH HỢP HỆ THỐNG
> **Trọng tâm:** Xây dựng Desktop GUI (Qt C++), tách biệt UI Thread và Network Thread, bảng tiến trình Progress Bar, tích hợp Client SDK, mở rộng Web Client (TypeScript).

* **Header chịu trách nhiệm:** `include/client/MainWindow.h`, `include/client/TransferWorker.h`
* **Checklist Nhiệm vụ Chi tiết:**
  - [ ] **M4.1**: Thiết kế kiến trúc Client đa luồng (Multi-threading Client):
    - Main Thread: Chạy Qt Event Loop, vẽ giao diện, xử lý thao tác người dùng.
    - Worker Network Thread: Kết nối Socket với Server, lắng nghe sự kiện, emit Qt Signal về UI Thread (đảm bảo UI không bao giờ bị đơ/freezing).
  - [ ] **M4.2**: Xây dựng Màn hình Đăng nhập & Đăng ký:
    - Nhập thông tin tài khoản, cấu hình IP/Port Server.
    - Xử lý lưu Session Token và chuyển sang màn hình chính khi thành công.
  - [ ] **M4.3**: Xây dựng Màn hình Duyệt file 2 ngăn (Dual-Pane File Explorer):
    - Cột trái: Local Explorer (duyệt file trên máy Client).
    - Cột phải: Remote Explorer (duyệt file trên Server qua các lệnh `ls`, `cd`, `pwd`).
    - Nút bấm trực quan: `New Folder`, `Refresh`, `Go Up`.
  - [ ] **M4.4**: Xây dựng Bảng Quản lý Truyền tải (Transfer Manager Panel):
    - Thanh tiến trình (Progress Bar 0-100%).
    - Hiển thị tốc độ truyền (KB/s, MB/s) và thời gian ước tính còn lại (ETA).
  - [ ] **M4.5**: Xây dựng Menu chuột phải & Hộp thoại chức năng:
    - Chuột phải: Download, Upload, Delete, Rename.
    - Dialog Phân quyền (Permission Dialog): Checkbox trực quan các quyền Read, Write, Share và radio button chọn Private / Shared / Public.
  - [ ] **M4.6**: Xây dựng Hộp thoại Tìm kiếm:
    - Nhập từ khóa, bảng hiển thị kết quả.
    - Nút "Download Directly": Tải ngay file từ danh sách kết quả tìm kiếm.
  - [ ] **M4.7 (Mở rộng Tuần 3-4)**: Triển khai Giao diện Web (TypeScript + React) kết hợp WebSocket Gateway phục vụ bài thuyết trình trực quan.
  - [ ] **Báo cáo SDD**: Viết Chương 6 (Client Architecture & Graphical User Interface).

---

## 3. TIÊU CHÍ HOÀN THÀNH (DEFINITION OF DONE - DOD)

Một tính năng chỉ được xem là hoàn thành khi thỏa mãn đầy đủ các điều kiện sau:
1. **Code Quality**: Biên dịch không có cảnh báo nghiêm trọng (`-Wall -Wextra /W4`), mã nguồn định dạng chuẩn mực (formatting).
2. **Unit Test**: Đã tự kiểm thử độc lập trên máy cá nhân với các test cases thành công và thất bại.
3. **No Memory Leak / Stability**: Không bị lỗi truy cập bộ nhớ (Segmentation fault, Memory Leak), không làm tràn RAM khi truyền file lớn.
4. **Git Compliance**: Commit rõ ràng theo chuẩn Conventional Commits (ví dụ: `feat(network): implement 42-byte frame receiver`).
5. **Documentation**: Đã viết đầy đủ phần báo cáo kỹ thuật của mình vào file `docs/SDD.md`.

---

## 4. KẾ HOẠCH TÍCH HỢP (4-WEEK SPRINT TIMELINE)

| Giai đoạn | Tuần 1: Khởi động & Hợp đồng | Tuần 2: Logic Độc lập | Tuần 3: Tích hợp Hệ thống | Tuần 4: Nâng cao & Thuyết trình |
| :--- | :--- | :--- | :--- | :--- |
| **M1: Network** | Setup Socket Wrapper, Header 42B | Framing chống dính gói, ThreadPool | Ghép Auth & File Engine vào Server | Stress test chịu tải 50 clients |
| **M2: Auth/Perm** | Thiết kế CSDL, thuật toán SHA-256 | Lập trình Auth & Permission Engine | Tích hợp phân quyền với lệnh file | Test ma trận quyền truy cập |
| **M3: File/Chunk** | Thiết kế Sandbox, logic chunk 64KB | Hoàn thiện FTP commands & Search | Tích hợp Chunking upload/download | Tích hợp truyền đệ quy Folder |
| **M4: GUI Client** | Vẽ UI Mockup, dựng khung Qt | Code UI logic với Mock Data | Tích hợp Qt Client với Server thật | Làm Web Client TypeScript demo |
| **Cột mốc (Milestone)**| **M1: Thống nhất FSPProtocol.h** | **M2: Pass 100% Unit Tests** | **M3: End-to-End File Transfer** | **M4: Final SDD & Live Demo** |
