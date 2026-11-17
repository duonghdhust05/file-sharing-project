# WORK BREAKDOWN STRUCTURE (WBS) & CHECKLIST DỰ ÁN
## PHÂN CHIA CÔNG VIỆC CHO NHÓM 4 THÀNH VIÊN ĐỘC LẬP (GROUP 7)

* **Dự án:** Client-Server Online File Sharing Application (Topic 6)
* **Phiên bản:** 2.1 (Đồng bộ với [SRS.md](pdf-document/SRS.md) v3.1, [SDD.md](pdf-document/SDD.md) v2.1, [UC-CHECKLIST.md](uc-details/UC-CHECKLIST.md), [NFR-CHECKLIST.md](uc-details/NFR-CHECKLIST.md) và 13 UC v1.0 tại [uc-details/](uc-details/README.md))
* **Ngôn ngữ thống nhất:** **C (Chuẩn C11)** — POSIX Raw TCP Sockets (`<sys/socket.h>`), `pthread`, GTK 3
* **Nguyên tắc làm việc:** Module hóa cao độ (High Cohesion, Loose Coupling) — Làm việc song song dựa trên hợp đồng giao thức (`FSPProtocol.h`) và lộ trình 2 tuần tại [LEARNING-ROADMAP.md](archive/LEARNING-ROADMAP.md).

---

## 1. NGUYÊN TẮC HOẠT ĐỘNG SONG SONG ĐỘC LẬP

1. **Tuân thủ Hợp đồng Giao thức (Contract-First)**:
   * Mọi thành viên sử dụng chung các cấu trúc dữ liệu nhị phân (`FSPHeader` 42 byte, `FileChunkHeader` 88 byte) trong `include/common/FSPProtocol.h`.
   * Giao tiếp điều khiển dùng JSON phẳng, danh sách trả về định dạng dòng kèm tab (`\t`) theo quy ước SDD §3.3.
2. **Thống nhất Ngôn ngữ C (C11) & Sans-IO Seam**:
   * Toàn bộ mã nguồn Server, Shared Core Library (`libfsp_common`), CLI test client và Client GUI sử dụng C11.
   * Tách logic xử lý buffer/trạng thái khỏi `recv`/`send`/`open` để test độc lập ở Tầng 1–2.
3. **Quy trình Kiểm thử & Git Branching (Light Dual-Gate)**:
   * Mỗi thành viên phát triển trên nhánh riêng (`feat/m1-network-engine`, `feat/m2-auth-permissions`, `feat/m3-filesystem-transfer`, `feat/m4-gui-client`).
   * Với module nghiệp vụ/mạng (M2, M3, tích hợp `fsp_client_api`), áp dụng **Light Dual-Gate**: Gate 1 viết `.h` + stub `.c` + test (`ctest` FAIL) → Gate 2 khóa assertion test và triển khai `src/*.c` theo [c11_test_template.c](TDD/templates/c11_test_template.c) & [test_util.h](../tests/test_util.h).
   * Mỗi hàm `test_*()` gắn comment `/* @covers: UCxx, FR-x.y, NFR-xx */` và kiểm tra `CHECK_NO_FD_LEAK` / `CHECK_NO_PART_FILES`.
   * Chỉ tạo PR vào nhánh `develop` sau khi chạy sạch `clang-format` (v18.1.3) và toàn bộ test CTest qua được cả `-fsanitize=address,undefined` (`NFR-G1`) lẫn `-fsanitize=thread` (`NFR-C3`).

---

## 2. BẢN PHÂN CÔNG CHI TIẾT THEO THÀNH VIÊN

### THÀNH VIÊN 1 (M1 - Trần Sỹ Nguyên - 20235985): MẠNG CỐT LÕI & SERVER CORE
> **Trọng tâm:** POSIX Raw TCP Socket, Framing 42-byte (`send_exact`/`recv_exact`), CRC32 IEEE 802.3, JSON phẳng, Server 1 thread / client (`pthread`), Event Dispatcher, Heartbeat `PING`/`PONG` + `SO_RCVTIMEO`.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/common/fsp_socket.h`, `include/common/fsp_packet.h`, `include/common/fsp_crc32.h`, `include/common/fsp_json.h`, `include/server/fsp_dispatcher.h`, `include/server/fsp_server.h`
  * Source: `src/common/fsp_socket.c`, `src/common/fsp_packet.c`, `src/common/fsp_crc32.c`, `src/common/fsp_json.c`, `src/server/server_main.c`, `src/server/fsp_server.c`, `src/server/fsp_dispatcher.c`
  * Test: `tests/test_protocol.c`, `tests/test_packet.c`, `tests/test_json.c`, `tests/test_network_core.c`
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M1.1**: Viết bộ bọc Raw TCP Socket C11 (`fsp_listen`, `fsp_connect`, `send_exact`, `recv_exact`, `SO_REUSEADDR`, `SIGPIPE` ignore — `NFR-D2`).
  - [x] **M1.2**: Hiện thực đóng/mở gói Frame 42-byte (`fsp_send_frame`, `fsp_recv_frame`, `fsp_parse_frames`):
    - Đọc đủ 42 bytes Header → kiểm tra Magic byte `0xAA`.
    - Kiểm tra `payload_len <= FSP_BUFFER_SIZE` (128 KB — `NFR-B2`).
    - Đọc đủ `payload_len` bytes qua `recv_exact`.
    - Kiểm tra checksum IEEE 802.3 CRC32 của payload.
  - [x] **M1.3**: Xây dựng kiến trúc `EventDispatcher` bằng C11 (`dispatcher_register`, `dispatcher_register_cleanup`, `dispatcher_register_chunk`, `dispatcher_handle`) và bộ JSON phẳng an toàn (`fsp_json.c`, chặn `\u0000` — `NFR-E4`).
  - [x] **M1.4**: Hiện thực vòng lặp máy chủ (`fsp_server.c` & `server_main.c`):
    - Vòng lặp `accept()` lắng nghe cổng 8088, 1 thread / client (`pthread_create` + `pthread_detach`), Graceful Shutdown.
    - Khởi tạo bảng CRC32 một lần trước khi tạo thread để tránh data race.
    - Dọn dẹp tài nguyên khi client ngắt kết nối qua `dispatcher_run_cleanups()` (`NFR-D1`).
    - *(Thread pool là COULD khi hoàn thành sớm các mục MUST).*
  - [~] **M1.5**: Cơ chế Heartbeat (`PING`/`PONG` đã có trong `fsp_server.c`); **cần bổ sung** `setsockopt(SO_RCVTIMEO)` với `FSP_SOCKET_TIMEOUT_S = 30` để tự thu hồi kết nối treo im lặng (`NFR-D3`, ghi nhận tại [PR1-m1-review.md](reviews/PR1-m1-review.md)).
  - [x] **M1.6**: Viết test CTest Probe 1 (`test_packet.c`, `NFR-A4`): tách đúng 2 frame trong 1 buffer (dính gói) và ghép đúng frame nhận thành nhiều mảnh.
  - [x] **M1.7**: Kiểm thử tải đồng thời ≥ 5 client (`test_network_core.c` test 10 client × 50 req — `NFR-C1`) và sạch dưới TSan (`NFR-C3`).

---

### THÀNH VIÊN 2 (M2 - Bùi Đức Hiếu - 20235931): XÁC THỰC NGƯỜI DÙNG, PHIÊN & PHÂN QUYỀN
> **Trọng tâm:** Quản lý tài khoản, Salted SHA-256, Session Token, bảng kiểm tra quyền `can_access` (bước 0–7 theo SRS FR-6), `PERM_GET`/`PERM_SET` hỗ trợ đệ quy cho thư mục.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/common/fsp_sha256.h`, `include/server/fsp_user.h`, `include/server/fsp_session.h`, `include/server/fsp_perm.h`, `include/server/fsp_auth_handler.h`
  * Source: `src/common/fsp_sha256.c`, `src/server/fsp_user.c`, `src/server/fsp_session.c`, `src/server/fsp_perm.c`, `src/server/fsp_auth_handler.c`
  * Test: `tests/test_auth_permissions.c`
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M2.1**: Lưu trữ đĩa bền vững dạng JSON-lines (`users.json`, `permissions.json`) ghi nguyên tử qua file `.tmp` + `rename()` (`NFR-A5` — `SHOULD` đã chốt). *(Lưu ý tích hợp: thống nhất đường dẫn `storage/*.json` trong `server_main.c` hay `data/*.json` theo SDD §2.4.)*
  - [x] **M2.2**: Băm mật khẩu Salted SHA-256 (FIPS 180-4) bằng C thuần: salt 16 bytes từ `/dev/urandom` (`NFR-E3`), so sánh hash thời gian hằng, khóa tạm 5 phút sau 5 lần sai (`NFR-E4` — `SHOULD` đã chốt).
  - [~] **M2.3**: Quản lý phiên trong RAM (`fsp_session.c`): cấp token 64 hex, mutex (`NFR-C2`), timeout 30 phút (`NFR-E4`). **Còn thiếu:** đăng ký `dispatcher_register_cleanup()` trong `fsp_auth_register_handlers()` để tự hủy token khi TCP ngắt đột ngột (`UC03`, `NFR-D1`).
  - [~] **M2.4**: Xử lý `AUTH_REGISTER`, `AUTH_LOGIN`, `AUTH_LOGOUT` (`fsp_auth_handler.c`): đã tạo quyền `/home/<username>` (`0755`, `PRIVATE`). **Còn thiếu:** tạo thư mục vật lý `./storage/server_root/home/<username>` khi đăng ký (`UC01`, `FR-1.2` — phối hợp M3) và trả kèm `cwd` khi login (`UC02`).
  - [x] **M2.5**: Bộ máy phân quyền 9-bit Linux + Mức chia sẻ (`fsp_can_access`): đúng bảng quyết định bước 0–7 (`NFR-E2`).
  - [~] **M2.6**: `PERM_GET` và `PERM_SET` (`fsp_perm.c` đã hỗ trợ `grant`/`revoke` và `recursive=true` không chạm `/a-b` khi sửa `/a`). **Còn thiếu ở handler JSON (`fsp_auth_handler.c`):** `handle_perm_get` chưa trả `share_list` theo `username`; `handle_perm_set` chưa đọc `share_list`/`grant_user`/`revoke_user` từ JSON (`UC13`).
  - [x] **M2.7**: Bộ test CTest Probe 2 & Probe 3 (`tests/test_auth_permissions.c`). *(Cần bổ sung test tích hợp qua TCP thật giữa `fsp_auth_handler.c` và `fsp_client_api.c`.)*

---

### THÀNH VIÊN 3 (M3 - Vũ Nhật Minh Dương - 20235925): HỆ THỐNG FILE ẢO, TRUYỀN DỮ LIỆU & TÌM KIẾM
> **Trọng tâm:** VFS Sandbox (`realpath`), lệnh duyệt file FTP (`ls`, `cd`, `pwd`, `mkdir`, `rm`, `mv`), truyền file 64 KB (`.part` + CRC32), truyền thư mục đệ quy qua Manifest, tìm kiếm lọc theo quyền. Áp dụng **Light Dual-Gate** + `@covers` + `CHECK_NO_FD_LEAK` / `CHECK_NO_PART_FILES`.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/server/fsp_vfs.h`, `include/server/fsp_transfer.h`, `include/server/fsp_search.h`
  * Source: `src/server/fsp_vfs.c`, `src/server/fsp_transfer.c`, `src/server/fsp_search.c`
  * Test: `tests/test_vfs.c`, `tests/test_transfer.c`, `tests/test_search.c` (dựa trên [c11_test_template.c](TDD/templates/c11_test_template.c))
* **Checklist Nhiệm vụ Chi tiết:**
  - [ ] **M3.1**: Xây dựng VFS Sandbox & `vfs_resolve` (`UC04`–`UC07`, `NFR-E1`):
    - Ánh xạ `/` → `./storage/server_root`, chuẩn hóa `.` và `..`, chặn vượt gốc ảo → `403`.
    - Kiểm tra `realpath()` chống thoát sandbox qua symlink (Probe 5).
    - Hỗ trợ tạo `/home/<username>` khi đăng ký (`UC01`) và thống nhất key JSON `DIR_PWD` (`"path"` vs `"cwd"`) với M4.
  - [ ] **M3.2**: Hiện thực lệnh duyệt file kiểu FTP (`UC04`–`UC07`):
    - `DIR_PWD`, `DIR_CHANGE` (kiểm tra `READ`), `DIR_LIST` (trả metadata kèm quyền từ M2, ẩn `*.part`), `DIR_MKDIR` (`0755`, `PRIVATE`), `FS_DELETE` (file / thư mục rỗng + `fsp_perm_remove`), `FS_RENAME` (cùng thư mục + `fsp_perm_rename`).
  - [ ] **M3.3**: Hiện thực truyền file phân khối 64 KB (`fsp_transfer.c`, `UC08`–`UC09`):
    - Upload: `FILE_UPLOAD_REQ` (`WRITE`, `409` nếu trùng) → `<file>.part` (`NFR-A3`) → từng `FILE_CHUNK` kiểm tra CRC32 (`NFR-A2`) → `FILE_UPLOAD_FINISH` đổi tên `.part` thành file thật + `fsp_perm_register(0644, PRIVATE)`.
    - Sai CRC32 hoặc ngắt kết nối (`cleanup hook`) → hủy phiên, xóa sạch `.part` (`NFR-D1`).
    - Download: `FILE_DOWNLOAD_REQ` (`READ` trên chính file) → đọc từng khối ≤ 64 KB bằng bộ đệm cố định (`NFR-B1`) → gửi `FILE_CHUNK` kèm CRC32.
  - [ ] **M3.4**: Hiện thực truyền thư mục đệ quy (`FOLDER_UPLOAD_REQ` / `FOLDER_DOWNLOAD_REQ`, `UC10`–`UC11`):
    - Duyệt đệ quy `opendir`/`readdir`, bỏ symlink (`lstat`), dựng Manifest.
    - Upload: tạo trước toàn bộ thư mục con + đăng ký quyền → nhận từng file qua `M3.3`.
    - Download: chỉ đưa vào manifest mục có quyền `READ` (`NFR-E2`), báo lại `skipped` (`FR-4.3`).
  - [ ] **M3.5**: Hiện thực tìm kiếm lọc theo quyền (`fsp_search.c`, `UC12`):
    - Tìm chuỗi con không phân biệt hoa thường + lọc đuôi file (`FR-5.2` SHOULD), tối đa 100 kết quả, phản hồi ≤ 1s trên LAN (`NFR-B3`).
    - Lọc 100% qua `fsp_can_access(user, vpath, READ)`: Zero-Leak file `PRIVATE` (`NFR-E2`).
  - [ ] **M3.6**: Viết bộ test 4 tầng (gắn `@covers`, `CHECK_NO_FD_LEAK`, `CHECK_NO_PART_FILES`) phủ Probe 4, Probe 5 và chạy sạch dưới `ASan+UBSan` (`NFR-G1`) & `TSan` (`NFR-C3`).

---

### THÀNH VIÊN 4 (M4 - Hà Đức Dương - 20235923): CLIENT SDK, GUI (GTK 3), CLI & TÍCH HỢP
> **Trọng tâm:** Tầng `fsp_client_api`, CLI client (`NFR-H2`), Desktop GUI GTK 3 (2 ngăn kiểu FileZilla, `pthread` + `g_idle_add()` không treo — `NFR-F1`), hộp thoại Quyền & Tìm kiếm, tích hợp & kiểm thử E2E.

* **Tập tin chịu trách nhiệm:**
  * Header: `include/client/fsp_client_api.h`, `include/client/client_net.h`, `include/client/gui.h`
  * Source: `src/client_api/fsp_client_api.c`, `src/client_net.c`, `src/client_gui/gui_main.c`, `src/client_gui/gui_login.c`, `src/client_gui/gui_dialogs.c`, `src/client_cli/cli_main.c`
  * Test: `tests/test_concurrency.c`, test tích hợp wire `fsp_client_api`, kịch bản E2E qua CLI
* **Checklist Nhiệm vụ Chi tiết:**
  - [x] **M4.0**: Quản trị tài liệu dự án (`SRS.md` v3.1, `SDD.md` v2.1, `WBS.md` v2.1, `UC-CHECKLIST.md`, `NFR-CHECKLIST.md`, 13 đặc tả `uc-details/` v1.0, `LEARNING-ROADMAP.md`, `AGENTS.md`), cấu hình CI/CD (`ci.yml` có `ASan+UBSan` & `TSan`).
  - [~] **M4.0b**: Hoàn thiện Client SDK (`src/client_api/fsp_client_api.c`, đã scaffold ở `df70bf5`):
    - Đã có: `connect/disconnect/ping`, `login/register/logout`, `pwd/cd/mkdir/delete`, `set_perm`.
    - Cần hoàn thiện: streaming 64 KB trong `fsp_client_upload_file` & `fsp_client_download_file` (đang stub), bổ sung `list`, `rename`, `get_perm`, `folder_upload/download`, `search`, và sửa lệch key JSON `"mode"` → `"perm_bits"` (theo `handle_perm_set`).
  - [ ] **M4.1**: Xây dựng CLI Client (`src/client_cli/cli_main.c`, `NFR-H2` — `SHOULD` đã chốt):
    - Đọc lệnh (`register`, `login`, `ls`, `cd`, `pwd`, `mkdir`, `rm`, `mv`, `put`, `get`, `putdir`, `getdir`, `find`, `chmod`) để M2/M3 test độc lập và làm phương án demo dự phòng X11.
  - [ ] **M4.2**: Xây dựng kiến trúc Client đa luồng an toàn (`client_net.c`):
    - Luồng GTK chỉ vẽ widget; luồng mạng `pthread` chạy socket & streaming; đồng bộ bằng hàng đợi lệnh + `g_idle_add()` (`NFR-F1`).
  - [ ] **M4.3**: Màn hình Đăng nhập & Đăng ký GTK (`gui_login.c`): khóa nút khi chờ phản hồi (`NFR-F3`), thông báo lỗi tiếng Việt rõ nghĩa (`NFR-F2`).
  - [ ] **M4.4**: Màn hình chính 2 ngăn (`gui_main.c`): Local (`opendir`/`readdir`), Remote (tên, kích thước, quyền, mức), thanh địa chỉ, `[↑]`, Upload/Download, vùng nhật ký (`NFR-F4`).
  - [ ] **M4.5**: Bảng Hàng đợi truyền tải (`GtkCellRendererProgress` cập nhật `%` theo chunk 64 KB — `NFR-F4`).
  - [ ] **M4.6**: Hộp thoại Phân quyền (`r/w/s`, mức chia sẻ, `share_list`, checkbox đệ quy) & Hộp thoại Tìm kiếm (từ khóa, đuôi file, nút Download trực tiếp — `UC12`).
  - [ ] **M4.7**: Viết test đa luồng Probe 6 (`tests/test_concurrency.c`: ≥ 5 client đồng thời — `NFR-C1`) sạch dưới `ASan+UBSan` (headless, không ép ASan lên `libgtk3` — `NFR-G1`) & `TSan` (`NFR-C3`), gắn `@covers` và chạy kịch bản demo E2E SRS §3.4.

---

## 3. LỘ TRÌNH THỰC HIỆN 2 TUẦN & 3 MỐC DEMO (2-WEEK SPRINT PLAN)

> Chi tiết mục tiêu từng ngày và nội dung tự kiểm tra kiến thức: xem [docs/archive/LEARNING-ROADMAP.md](archive/LEARNING-ROADMAP.md).

```
[Ngày 1-2] Chốt giao thức & JSON ──────► Nền tảng chung vững chắc
    │
[Ngày 3-5] Socket + Auth + VFS cơ bản ──► 🎯 MỐC 1 (Ngày 5): CLI Đăng nhập + Duyệt thư mục thật qua mạng
    │
[Ngày 6-10] Chunk 64KB + Quyền + GTK ──► 🎯 MỐC 2 (Ngày 10): Đủ 7 tính năng qua GUI GTK
    │
[Ngày 11-14] Sửa lỗi + ASan/TSan + Demo ► 🎯 MỐC 3 (Ngày 14): Sẵn sàng Báo cáo & Demo trước Giảng viên
```

| Cột mốc (Milestone) | Thời điểm | Tiêu chí nghiệm thu (Acceptance Criteria) |
| :--- | :---: | :--- |
| **Mốc 1: Thông luồng Mạng & CLI** | Cuối Ngày 5 | Server chạy `1 thread/client`. Dùng CLI client đăng ký, đăng nhập và gọi thành công `ls`, `cd`, `mkdir`, `pwd` qua kết nối TCP thật. |
| **Mốc 2: Tính năng Hoàn chỉnh trên GUI** | Cuối Ngày 10 | Toàn bộ 7 yêu cầu cốt lõi (`UC01`–`UC13`) hoạt động thông suốt qua giao diện GTK 2 ngăn. |
| **Mốc 3: Tổng duyệt Demo & Báo cáo** | Cuối Ngày 14 | Đạt 100% các mục trên [UC-CHECKLIST.md](uc-details/UC-CHECKLIST.md) và 27 mục trên [NFR-CHECKLIST.md](uc-details/NFR-CHECKLIST.md); chạy sạch `ASan+UBSan` (`NFR-G1`) & `TSan` (`NFR-C3`), hoàn thành demo An — Bình — Chi (SRS §3.4). |

---

## 4. QUY TẮC CẮT GIẢM NẾU CHẬM TIẾN ĐỘ

Nếu tiến độ gặp khó khăn, nhóm tuân thủ nghiêm ngặt thứ tự cắt giảm sau (**giữ nguyên 100% 20 NFR lõi & các mục MUST**):
1. **Cắt giảm cấp 1 (Mục `COULD`):** Bỏ thread pool (giữ `1 thread/client`); bỏ `chown` và đổi nhóm user (`FR-6.6`); bỏ kiểm tra SHA-256 toàn file (`FR-3.6`, giữ CRC32 từng chunk); bỏ kế thừa quyền thư mục cha (`FR-6.7`); bỏ kéo-thả GTK (`FR-7.8`).
2. **Cắt giảm cấp 2 (Mục `SHOULD` chưa cài đặt):** Bỏ lệnh `rm`/`mv` (`UC07`, `FR-2.5/2.6`); bỏ lọc tìm kiếm theo đuôi file (`FR-5.2`). *(Lưu ý: `NFR-A5` atomic JSON và `NFR-E4` lockout/TTL đã có sẵn ở M1/M2 nên không cắt; `NFR-H2` CLI và `NFR-F4` thanh `%` nên giữ để phục vụ demo.)*
3. **RANH GIỚI BẤT BIẾN:** Tuyệt đối không cắt bỏ bất kỳ mục nào trong 7 yêu cầu gốc (Đăng ký/đăng nhập, Lệnh FTP cơ bản, Upload/download file, Upload/download thư mục, Tìm kiếm có tải, Phân quyền Linux + mức chia sẻ, GUI 2 ngăn).
