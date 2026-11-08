# WORK BREAKDOWN STRUCTURE (WBS) & CHECKLIST DỰ ÁN
## PHÂN CHIA CÔNG VIỆC CHO NHÓM 4 THÀNH VIÊN ĐỘC LẬP (GROUP 7)

* **Dự án:** Client-Server Online File Sharing Application (Topic 6)
* **Phiên bản:** 2.0 (Đồng bộ với [SRS.md](SRS.md) v3.0 và [SDD.md](SDD.md) v2.0)
* **Ngôn ngữ thống nhất:** **C (Chuẩn C11)** — POSIX Raw TCP Sockets (`<sys/socket.h>`), `pthread`, GTK 3
* **Nguyên tắc làm việc:** Module hóa cao độ (High Cohesion, Loose Coupling) — Làm việc song song dựa trên hợp đồng giao thức (`FSPProtocol.h`) và lộ trình 2 tuần tại [LEARNING-ROADMAP.md](LEARNING-ROADMAP.md).

---

## 1. NGUYÊN TẮC HOẠT ĐỘNG SONG SONG ĐỘC LẬP

1. **Tuân thủ Hợp đồng Giao thức (Contract-First)**:
   * Mọi thành viên sử dụng chung các cấu trúc dữ liệu nhị phân (`FSPHeader` 42 byte, `FileChunkHeader` 88 byte) trong `include/common/FSPProtocol.h`.
   * Giao tiếp điều khiển dùng JSON phẳng, danh sách trả về định dạng dòng kèm tab (`\t`) theo quy ước SDD §3.3.
2. **Thống nhất Ngôn ngữ C (C11)**:
   * Toàn bộ mã nguồn Server, Shared Core Library (`libfsp_common`), CLI test client và Client GUI sử dụng C11.
   * Các module M1, M2 viết bằng C++ ở Checkpoint 1 được port sang C11 thuần (`struct`, con trỏ hàm, `pthread_mutex_t`).
3. **Quy tắc Git Branching**:
   * Mỗi thành viên phát triển trên nhánh riêng (`feat/m1-network-engine`, `feat/m2-auth-permissions`, `feat/m3-filesystem-transfer`, `feat/m4-gui-client`).
   * Chỉ tạo PR vào nhánh `develop` sau khi đã chạy sạch `clang-format` và toàn bộ test CTest qua được cờ `-fsanitize=address,undefined`.

---

## 2. BẢN PHÂN CÔNG CHI TIẾT THEO THÀNH VIÊN

### THÀNH VIÊN 1 (M1 - Trần Sỹ Nguyên - 20235985): MẠNG CỐT LÕI & SERVER CORE
> **Trọng tâm:** POSIX Raw TCP Socket, Framing 42-byte (`send_exact`/`recv_exact`), CRC32 IEEE 802.3, Server 1 thread / client (`pthread`), Event Dispatcher, Heartbeat `PING`/`PONG`.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/common/fsp_socket.h`, `include/common/fsp_packet.h`, `include/common/fsp_crc32.h`, `include/server/fsp_dispatcher.h`
  * Source: `src/common/fsp_socket.c`, `src/common/fsp_packet.c`, `src/common/fsp_crc32.c`, `src/server/server_main.c`, `src/server/fsp_dispatcher.c`
  * Test: `tests/test_packet.c`, `tests/test_network_core.c`
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M1.1**: Viết bộ bọc Raw TCP Socket C11 (`fsp_listen`, `fsp_connect`, `send_exact`, `recv_exact`, `SO_REUSEADDR`, `SIGPIPE` ignore).
  - [x] **M1.2**: Hiện thực đóng/mở gói Frame 42-byte (`fsp_send_frame`, `fsp_recv_frame`, `fsp_parse_frames`):
    - Đọc đủ 42 bytes Header $\to$ kiểm tra Magic byte `0xAA`.
    - Kiểm tra `payload_len <= BUFFER_SIZE` (128 KB).
    - Đọc đủ `payload_len` bytes qua `recv_exact`.
    - Kiểm tra checksum IEEE 802.3 CRC32 của payload.
  - [x] **M1.3**: Xây dựng kiến trúc `EventDispatcher` bằng C11:
    - Bảng ánh xạ tuyến tính tĩnh `{event_name, handler_fn}`.
    - Cung cấp `dispatcher_register(event, fn)` và `dispatcher_handle(ctx, req)`.
  - [x] **M1.4**: Hiện thực vòng lặp máy chủ (`server_main.c`):
    - Vòng lặp `accept()` lắng nghe cổng 8088.
    - Tạo 1 thread riêng cho mỗi client (`pthread_create` + `pthread_detach`).
    - Khởi tạo bảng CRC32 một lần trước khi tạo thread để tránh data race.
    - Dọn dẹp tài nguyên khi client ngắt kết nối (giải phóng phiên, đóng socket, xóa transfer dở dang).
    - *(Thread pool là COULD khi hoàn thành sớm các mục MUST).*
  - [ ] **M1.5**: Triển khai cơ chế Heartbeat (`PING`/`PONG`) xử lý tự động trong vòng lặp client thread.
  - [x] **M1.6**: Viết test CTest Probe 1: chứng minh tách đúng 2 frame trong 1 buffer (dính gói) và ghép đúng frame nhận thành nhiều mảnh.
  - [x] **M1.7**: Kiểm thử tải đồng thời ≥ 5 client cùng kết nối và truyền nhận dữ liệu ổn định (NFR-3).

---

### THÀNH VIÊN 2 (Bùi Đức Hiếu - 20235931): XÁC THỰC NGƯỜI DÙNG, PHIÊN & PHÂN QUYỀN
> **Trọng tâm:** Quản lý tài khoản, băm Salted SHA-256, Session Token, bảng kiểm tra quyền `can_access` (bước 0–7 theo SRS FR-6), API `PERM_GET`/`PERM_SET` hỗ trợ đệ quy cho thư mục.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/server/fsp_sha256.h`, `include/server/fsp_user.h`, `include/server/fsp_session.h`, `include/server/fsp_perm.h`
  * Source: `src/common/fsp_sha256.c`, `src/server/fsp_user.c`, `src/server/fsp_session.c`, `src/server/fsp_perm.c`
  * Test: `tests/test_auth.c`, `tests/test_perm.c`
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M2.1**: Thiết kế lưu trữ đĩa bền vững dạng JSON-lines (`data/users.json`, `data/permissions.json`) ghi nguyên tử qua file `.tmp` + `rename()`:
    - Bảng user: `id`, `username`, `full_name`, `salt_hex`, `password_hash`, `group_id`, `created_at`.
    - User đầu tiên đăng ký tự động gán `id = 1` (Admin).
  - [x] **M2.2**: Hiện thực băm mật khẩu Salted SHA-256 (FIPS 180-4) bằng C thuần:
    - Đọc 16 bytes ngẫu nhiên từ `/dev/urandom` (hoặc `getrandom`) làm salt.
    - So sánh hash thời gian hằng (`constant_time_memcmp`).
    - Đăng nhập sai 5 lần liên tiếp khóa tạm 5 phút (COULD, đã có).
  - [x] **M2.3**: Quản lý phiên làm việc trong RAM (`SessionManager`):
    - Cấp `token` 32 bytes ngẫu nhiên (64 hex chars).
    - Bảng phiên bảo vệ bằng `pthread_mutex_t`: tra cứu token ra `user_id`.
    - Thu hồi khi đăng xuất hoặc client ngắt kết nối; timeout 30 phút không hoạt động (COULD).
  - [x] **M2.4**: Hiện thực bộ xử lý sự kiện `AUTH_REGISTER`, `AUTH_LOGIN`, `AUTH_LOGOUT`:
    - Đăng ký thành công tự tạo quyền cho `/home/<username>` (`0755`, `PRIVATE`, owner = user).
  - [x] **M2.5**: Hiện thực bộ máy phân quyền 9-bit Linux + Mức chia sẻ (`perm_can_access`):
    - Cài đặt chính xác bảng quyết định bước 0–7 theo SRS FR-6.
    - Bitmask 9-bit: `r` (4), `w` (2), `s` (1) cho Chủ / Nhóm / Người khác.
    - Cấp độ chia sẻ: `PRIVATE (0)`, `SHARED (1)`, `PUBLIC (2)`.
    - `share_list` quản lý theo danh sách username kèm quyền (`r`/`w`/`s`).
  - [ ] **M2.6**: Hiện thực `PERM_GET` và `PERM_SET`:
    - Hỗ trợ đổi `perm`, `level`, cấp/thu hồi quyền qua `grant_user`/`revoke_user`.
    - Hỗ trợ cờ `recursive=true` (FR-6.4): đổi quyền hàng loạt cho thư mục và mọi mục con bên trong mà không ảnh hưởng tới các đường dẫn tương tự (như `/a-b` khi sửa `/a`).
  - [x] **M2.7**: Viết bộ test CTest Probe 2 (token hợp lệ / giả / đã logout) và Probe 3 (mỗi bước 0–7 của `can_access`, file `PRIVATE` có bit `0666` vẫn từ chối người ngoài).

---

### THÀNH VIÊN 3 (Vũ Nhật Minh Dương - 20235925): HỆ THỐNG FILE ẢO, TRUYỀN DỮ LIỆU & TÌM KIẾM
> **Trọng tâm:** VFS Sandbox (`realpath`), lệnh duyệt file FTP (`ls`, `cd`, `pwd`, `mkdir`, `rm`, `mv`), truyền file 64 KB (.part + CRC32), truyền thư mục đệ quy qua Manifest, tìm kiếm lọc theo quyền.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/server/fsp_vfs.h`, `include/server/fsp_transfer.h`, `include/server/fsp_search.h`
  * Source: `src/server/fsp_vfs.c`, `src/server/fsp_transfer.c`, `src/server/fsp_search.c`
  * Test: `tests/test_vfs.c`, `tests/test_transfer.c`, `tests/test_search.c`
* **Checklist Nhiệm vụ Chi tiết:**
  - [ ] **M3.1**: Xây dựng VFS Sandbox & hàm chuẩn hóa đường dẫn (`vfs_resolve`):
    - Ánh xạ đường dẫn ảo `/` thành `./storage/server_root`.
    - Chuẩn hóa từ vựng `.` và `..`. Chặn đứng `..` vượt quá gốc ảo $\to$ trả `403 FORBIDDEN`.
    - Kiểm tra `realpath()` đảm bảo không thoát sandbox qua symbolic link (Probe 5).
  - [ ] **M3.2**: Hiện thực các lệnh duyệt file kiểu FTP qua POSIX:
    - `DIR_PWD`: in thư mục ảo hiện tại.
    - `DIR_CHANGE`: đổi `cwd` trong `client_ctx_t` (kiểm tra quyền `READ`).
    - `DIR_LIST`: liệt kê nội dung thư mục, trả metadata kèm quyền từ M2. Bỏ qua file `*.part`.
    - `DIR_MKDIR`: tạo thư mục đĩa thật và gọi M2 đăng ký quyền (`0755`, `PRIVATE`).
    - `FS_DELETE`: xóa file (`unlink`) hoặc thư mục rỗng (`rmdir`) + hủy quyền ở M2.
    - `FS_RENAME`: đổi tên trên đĩa (`rename`) và gọi M2 cập nhật đường dẫn (`perm_rename`).
  - [ ] **M3.3**: Hiện thực cơ chế truyền file phân khối 64 KB (`fsp_transfer.c`):
    - Upload: Nhận `FILE_UPLOAD_REQ` $\to$ kiểm tra quyền `WRITE` $\to$ mở `<file>.part`. Nhận từng `FILE_CHUNK` $\to$ kiểm tra CRC32 $\to$ ghi đĩa. Nhận `FILE_UPLOAD_FINISH` $\to$ đổi tên `.part` thành file thật và đăng ký quyền M2.
    - Sai CRC32 hoặc ngắt kết nối giữa chừng $\to$ hủy phiên, xóa sạch `.part` (NFR-4).
    - Download: Nhận `FILE_DOWNLOAD_REQ` $\to$ kiểm tra quyền `READ` $\to$ đọc từng khối ≤ 64 KB bằng bộ đệm cố định $\to$ gửi `FILE_CHUNK` kèm CRC32.
    - Bộ nhớ tiến trình ổn định khi truyền file lớn (NFR-2).
  - [ ] **M3.4**: Hiện thực truyền thư mục đệ quy (`FOLDER_UPLOAD_REQ` / `FOLDER_DOWNLOAD_REQ`):
    - Quét thư mục đệ quy qua `opendir`/`readdir`, bỏ qua symlink, dựng Manifest.
    - Upload thư mục: bên nhận tạo trước toàn bộ thư mục con $\to$ truyền tuần tự từng file qua cơ chế M3.3.
    - Download thư mục: server chỉ đưa vào manifest các mục user có quyền `READ` (FR-4.3).
  - [ ] **M3.5**: Hiện thực công cụ tìm kiếm lọc theo quyền (`fsp_search.c`):
    - Duyệt kho tệp, tìm theo chuỗi con không phân biệt hoa thường và lọc đuôi file.
    - Gọi `perm_can_access(user, vpath, READ)`: file không có quyền đọc bị ẩn hoàn toàn (Zero-Leak).
    - Trả về tối đa 100 kết quả có đường dẫn ảo đầy đủ để client tải ngay.
  - [ ] **M3.6**: Viết test CTest Probe 4 (lật 1 bit chunk $\to$ phát hiện sai CRC32) và Probe 5 (`../../etc/passwd` $\to$ 403).

---

### THÀNH VIÊN 4 (M4 - Hà Đức Dương - 20235923): CLIENT GUI (GTK 3), CLI & TÍCH HỢP
> **Trọng tâm:** Giao diện đồ họa GTK 3 (2 ngăn giống FileZilla), đa luồng không treo (`pthread` luồng mạng + `g_idle_add`), thanh tiến trình, hộp thoại phân quyền, tìm kiếm, công cụ CLI test, tích hợp hệ thống.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/client/client_net.h`, `include/client/gui.h`
  * Source: `src/client_net.c`, `src/client_gui/gui_main.c`, `src/client_gui/gui_login.c`, `src/client_gui/gui_dialogs.c`, `src/client_cli/cli_main.c`
  * Test: `tests/test_concurrency.c`, kịch bản test end-to-end qua CLI
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M4.0**: Quản trị tài liệu dự án (SRS v3.0, SDD v2.0, WBS v2.0, LEARNING-ROADMAP, AGENTS.md), cấu hình CI/CD và khung kiểm thử.
  - [ ] **M4.1**: Xây dựng CLI Client (`src/client_cli/cli_main.c`):
    - Sử dụng `libfsp_common`, đọc lệnh từ bàn phím (`login`, `ls`, `cd`, `mkdir`, `put`, `get`, `find`, `chmod`...).
    - Hoàn thành sớm ở Ngày 4–5 để M2 và M3 kiểm thử server độc lập không cần chờ GUI.
  - [ ] **M4.2**: Xây dựng kiến trúc Client đa luồng an toàn (`client_net.c`):
    - Luồng GTK chỉ nhận sự kiện người dùng và vẽ widget.
    - Luồng mạng riêng (`pthread`) xử lý socket, gửi nhận frame và streaming chunk.
    - Giao tiếp qua hàng đợi lệnh an toàn luồng (`pthread_mutex_t` + `pthread_cond_t`) và trả kết quả về UI bằng `g_idle_add()`.
    - Đảm bảo GUI không bao giờ bị đơ khi truyền file lớn (NFR-6).
  - [ ] **M4.3**: Xây dựng Màn hình Đăng nhập & Đăng ký (GTK):
    - Nhập host, port, username, password; hiển thị rõ thông báo lỗi từ server.
  - [ ] **M4.4**: Xây dựng Màn hình chính 2 ngăn (Dual-Pane Explorer):
    - Ngăn trái: hệ thống file cục bộ (Local), duyệt nhanh bằng `opendir`/`readdir`.
    - Ngăn phải: hệ thống file từ xa (Remote), hiển thị tên, kích thước, quyền, mức chia sẻ.
    - Thanh điều hướng đường dẫn, nút lên thư mục cha, nút Upload / Download.
  - [ ] **M4.5**: Xây dựng Bảng Hàng đợi truyền tải (Transfer Manager):
    - Hiển thị danh sách file đang truyền, chiều mũi tên, thanh tiến trình % (`GtkCellRendererProgress`) và trạng thái.
  - [ ] **M4.6**: Xây dựng Hộp thoại Phân quyền & Hộp thoại Tìm kiếm:
    - Dialog Phân quyền: lưới tick `r/w/s`, radio chọn mức chia sẻ, danh sách chia sẻ, checkbox "áp dụng cho toàn bộ bên trong".
    - Dialog Tìm kiếm: ô từ khóa, ô đuôi file, bảng kết quả kèm nút Download trực tiếp.
  - [ ] **M4.7**: Viết test đa luồng Probe 6 (≥ 5 client đồng thời) và kịch bản demo end-to-end theo SRS §3.4.

---

## 3. LỘ TRÌNH THỰC HIỆN 2 TUẦN & 3 MỐC DEMO (2-WEEK SPRINT PLAN)

> Chi tiết mục tiêu từng ngày và nội dung tự kiểm tra kiến thức: xem [docs/LEARNING-ROADMAP.md](LEARNING-ROADMAP.md).

```
[Ngày 1-2] Chốt giao thức & JSON ──────► Nền tảng chung vững chắc
    │
[Ngày 3-5] Socket + Auth + VFS cơ bản ──► 🎯 MỐC 1 (Ngày 5): CLI Đăng nhập + Duyệt thư mục thật qua mạng
    │
[Ngày 6-10] Chunk 64KB + Quyền + GTK ──► 🎯 MỐC 2 (Ngày 10): Đủ 7 tính năng qua GUI GTK
    │
[Ngày 11-14] Sửa lỗi + ASan + Tổng duyệt ► 🎯 MỐC 3 (Ngày 14): Sẵn sàng Báo cáo & Demo trước Giảng viên
```

| Cột mốc (Milestone) | Thời điểm | Tiêu chí nghiệm thu (Acceptance Criteria) |
| :--- | :---: | :--- |
| **Mốc 1: Thông luồng Mạng & CLI** | Cuối Ngày 5 | Server chạy 1 thread/client. Dùng CLI client đăng ký, đăng nhập và gọi thành công các lệnh `ls`, `cd`, `mkdir`, `pwd` qua kết nối TCP thật. |
| **Mốc 2: Tính năng Hoàn chỉnh trên GUI** | Cuối Ngày 10 | Toàn bộ 7 yêu cầu cốt lõi (User, FTP, File, Folder, Search, Perm, GUI) hoạt động thông suốt qua giao diện GTK 2 ngăn. |
| **Mốc 3: Tổng duyệt Demo & Báo cáo** | Cuối Ngày 14 | Pass 100% các ca kiểm thử trong bảng truy vết SRS §9, chạy sạch 0 leak dưới AddressSanitizer, hoàn thành kịch bản demo chia sẻ file giữa 3 user (An, Bình, Chi). |

---

## 4. QUY TẮC CẮT GIẢM NẾU CHẬM TIẾN ĐỘ

Nếu tiến độ gặp khó khăn, nhóm tuân thủ nghiêm ngặt thứ tự cắt giảm sau (chỉ cắt giảm các tính năng không bắt buộc, **giữ nguyên 100% các mục MUST**):
1. **Cắt giảm cấp 1 (Mục COULD):** Bỏ thread pool (giữ 1 thread/client); bỏ khóa brute-force và timeout 30 phút; bỏ `chown` và đổi nhóm user; bỏ kiểm tra SHA-256 toàn file (chỉ giữ CRC32 từng chunk).
2. **Cắt giảm cấp 2 (Mục SHOULD):** Bỏ lệnh `rm`/`mv` (FR-2.5, FR-2.6); bỏ lọc tìm kiếm theo đuôi file (chỉ tìm theo tên); bỏ vùng nhật ký lệnh trên GUI.
3. **RANH GIỚI BẤT BIẾN:** Tuyệt đối không cắt bỏ bất kỳ mục nào trong 7 yêu cầu gốc (Đăng ký/đăng nhập, Lệnh FTP cơ bản, Upload/download file, Upload/download thư mục, Tìm kiếm có tải, Phân quyền Linux + mức chia sẻ, GUI 2 ngăn).
