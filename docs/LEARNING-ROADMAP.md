# LỘ TRÌNH KIẾN THỨC & KẾ HOẠCH 2 TUẦN
## Dự án chia sẻ file Client-Server (Topic 6 — Group 7)

> Tài liệu này đi kèm [SRS.md](SRS.md) v3.0. Mục tiêu: mỗi thành viên **hiểu và tự giải thích được** phần mình làm trước giảng viên, không chỉ "code chạy được".

---

## 1. SÁU NGUYÊN TẮC CỨNG CỦA DỰ ÁN

Mỗi nguyên tắc gồm: **quy tắc**, **vì sao cần**, **kiểm chứng bằng test nào**, và **kiến thức tối thiểu phải nắm**.

### NT1 — Đọc/gửi đủ byte, không giả định 1 lần `recv` = 1 thông điệp
* **Quy tắc:** Mọi thao tác đọc/gửi dùng vòng lặp `recv_exact` / `send_exact` cho tới khi đủ số byte cần thiết. `recv` trả về `0` (đối phương đóng) hoặc `< 0` (trừ `EINTR`) → đóng kết nối, giải phóng tài nguyên.
* **Vì sao:** TCP là *dòng byte*, không có ranh giới thông điệp. Một lần `recv` có thể nhận nửa header, hoặc 2 thông điệp dính nhau.
* **Kiểm chứng:** Gửi 2 frame trong 1 lần `send` → bên nhận tách đúng 2 frame. Gửi 1 frame thành 3 lần, mỗi lần vài byte → bên nhận ghép đúng.
* **Kiến thức:** TCP stream vs message; `send`/`recv` trả về số byte thực tế; byte order (`htonl`/`ntohl`).
* **Liên quan:** M1 (cài đặt), mọi người (sử dụng).

### NT2 — Mọi thao tác phải qua xác thực và kiểm tra quyền
* **Quy tắc:** Trừ `AUTH_REGISTER`/`AUTH_LOGIN`, mọi yêu cầu phải có token hợp lệ (nếu không → `401`) và qua `perm_can_access` (nếu không → `403`). Không có đường tắt "tạm bỏ qua kiểm tra".
* **Vì sao:** Đây là điểm khác biệt chính so với FTP server thông thường (yêu cầu R1, R6).
* **Kiểm chứng:** Token giả/đã logout → `401`. User B đọc file `PRIVATE` của A → `403`. Mỗi bước (0–7) trong bảng `can_access` có ít nhất 1 test.
* **Kiến thức:** Hash + salt là gì và vì sao không lưu mật khẩu gốc; bitmask (`perm & 0400`); quyền Linux `rwx` đọc thế nào.
* **Liên quan:** M2 (cài đặt), M3 (gọi đúng chỗ).

### NT3 — Client không bao giờ ra khỏi thư mục gốc của server
* **Quy tắc:** Đường dẫn từ client là đường dẫn ảo. Server ghép với `./storage/server_root`, chuẩn hóa (xử lý `.`, `..`), rồi kiểm tra kết quả vẫn bắt đầu bằng thư mục gốc. Sai → `403`.
* **Vì sao:** Nếu không, gửi `../../etc/passwd` là đọc được file hệ thống (lỗi Path Traversal).
* **Kiểm chứng:** `cd ../../..` từ `/home/an` → dừng ở `/`. Download `../../etc/passwd` → `403`.
* **Kiến thức:** Đường dẫn tuyệt đối/tương đối; `realpath()` (lưu ý: chỉ dùng được với đường dẫn đã tồn tại — với file sắp tạo thì `realpath` thư mục cha rồi nối tên file); so sánh tiền tố chuỗi.
* **Liên quan:** M3.

### NT4 — Truyền file theo khối cố định, kiểm tra từng khối
* **Quy tắc:** Đọc/gửi tối đa 64 KB mỗi lần với bộ đệm cố định; mỗi khối có CRC32; sai CRC32 → hủy, xóa file tạm. Upload ghi vào `.part`, xong mới đổi tên.
* **Vì sao:** File lớn không thể nạp hết vào RAM; mạng/bug có thể làm hỏng dữ liệu; file dở dang không được xuất hiện như file hoàn chỉnh.
* **Kiểm chứng:** File 500 MB truyền xong khớp `sha256sum`, bộ nhớ tiến trình không tăng; sửa 1 bit trong khối → bị phát hiện; ngắt kết nối giữa chừng → không còn `.part`.
* **Kiến thức:** `fopen/fread/fwrite` hoặc `open/read/write`; CRC32 dùng để làm gì (phát hiện lỗi, không phải bảo mật); `rename()`.
* **Liên quan:** M3 (logic), M1 (frame).

### NT5 — Thư mục truyền đi phải giữ nguyên cấu trúc
* **Quy tắc:** Duyệt đệ quy tạo manifest (danh sách đường dẫn tương đối) → bên nhận tạo đủ thư mục trước → truyền lần lượt từng file bằng NT4.
* **Vì sao:** Yêu cầu R4; tránh trường hợp nhận file trước khi thư mục chứa nó tồn tại.
* **Kiểm chứng:** Thư mục 3 cấp, 20 file, có thư mục rỗng → so sánh bằng `diff -r` giống hệt.
* **Kiến thức:** Đệ quy; `opendir/readdir/closedir`, `stat` (phân biệt file/thư mục); bỏ qua `.` và `..`.
* **Liên quan:** M3 (server), M4 (phía client).

### NT6 — Giao diện không bao giờ gọi socket trực tiếp
* **Quy tắc:** Luồng GTK chỉ vẽ giao diện và nhận sự kiện người dùng. Mọi `send`/`recv` chạy trên luồng mạng riêng; luồng mạng trả kết quả về giao diện **chỉ** qua `g_idle_add()`.
* **Vì sao:** `recv` có thể chờ lâu; nếu gọi trên luồng GTK, cửa sổ sẽ "Not Responding". Hàm GTK không an toàn khi gọi từ luồng khác.
* **Kiểm chứng:** Đang download file 500 MB vẫn cuộn/bấm được ngăn Local; thanh tiến trình chạy mượt.
* **Kiến thức:** Vòng lặp sự kiện (main loop) của GTK; `pthread_create`; hàng đợi có mutex + condition variable; `g_idle_add`.
* **Liên quan:** M4.

---

## 2. KIẾN THỨC NỀN CHUNG (cả nhóm — Ngày 1–2)

| Chủ đề | Tự kiểm tra: trả lời được câu này là đạt | Tài liệu |
|:---|:---|:---|
| Mô hình client-server, TCP vs UDP | Vì sao chọn TCP cho truyền file? | Beej's Guide to Network Programming, chương 1–2 |
| Vòng đời socket | Kể thứ tự gọi hàm ở server và client | Beej's Guide, chương 5; `man 2 socket` |
| Dòng byte & đóng gói | Vì sao cần header có `payload_len`? | Beej's Guide, mục "Son of Data Encapsulation" |
| Giao thức FSP của nhóm | Vẽ lại header 42 byte và giải thích từng trường | [FSPProtocol.h](../include/common/FSPProtocol.h) |
| Quản lý bộ nhớ C | Mỗi `malloc`/`fopen` được giải phóng ở đâu khi lỗi? | Mẫu `goto cleanup` trong code nhóm |
| Git theo nhánh | Tạo nhánh, commit, mở Pull Request vào `develop` | [CICD.md](CICD.md) |
| Build & test | Chạy `cmake`, `ctest`, bật ASan | [AGENTS.md](../AGENTS.md) mục 5 |

---

## 3. LỘ TRÌNH RIÊNG TỪNG THÀNH VIÊN

### M1 — Trần Sỹ Nguyên: Mạng & giao thức
| Thứ tự | Kiến thức | Áp dụng vào | Đầu ra kiểm chứng |
|:-:|:---|:---|:---|
| 1 | `socket/bind/listen/accept/connect`, `SO_REUSEADDR` | `fsp_socket.c` | Echo server nhận 1 client |
| 2 | Vòng lặp `recv_exact`/`send_exact`, xử lý `EINTR`, trả về 0 | `fsp_socket.c` | Test dính gói / tách gói (NT1) |
| 3 | Đóng/mở header 42 byte, `htonl`, CRC32 | `fsp_packet.c`, `fsp_crc32.c` | Test frame sai magic / sai CRC bị từ chối |
| 4 | `pthread_create`, `pthread_detach`, truyền tham số vào thread | `server_main.c` | 5 client kết nối cùng lúc (NFR-3) |
| 5 | Bảng con trỏ hàm theo tên sự kiện | `fsp_dispatcher.c` | M2/M3 đăng ký handler và nhận đúng frame |

**Tài liệu:** Beej's Guide (chương 5–7); *The Linux Programming Interface* (Kerrisk) — các chương về sockets và POSIX threads; `man 7 tcp`.

### M2 — Bùi Đức Hiếu: Người dùng, phiên & phân quyền
| Thứ tự | Kiến thức | Áp dụng vào | Đầu ra kiểm chứng |
|:-:|:---|:---|:---|
| 1 | Hàm băm, salt, đọc `/dev/urandom` | `fsp_sha256.c`, `fsp_user.c` | Test vector SHA-256 chuẩn; 2 user cùng mật khẩu có hash khác nhau |
| 2 | Lưu/đọc file JSON đơn giản | `users.json`, `permissions.json` | Khởi động lại server vẫn còn dữ liệu |
| 3 | Sinh token ngẫu nhiên, bảng phiên có mutex | `fsp_session.c` | Token giả / đã logout → `401` |
| 4 | Quyền Linux `rwx`, bitmask, số bát phân | `fsp_perm.c` | Giải thích `0640` bằng lời |
| 5 | Thuật toán `can_access` (bước 0–7, SRS FR-6) + PERM_SET đệ quy | `fsp_perm.c` | ≥ 1 test cho mỗi bước |

**Tài liệu:** `man 1 chmod`, `man 7 credentials`; FIPS 180-4 (chỉ cần phần test vector); code M2 đã có ở Checkpoint 1.

### M3 — Vũ Nhật Minh Dương: Hệ thống file, truyền dữ liệu & tìm kiếm
| Thứ tự | Kiến thức | Áp dụng vào | Đầu ra kiểm chứng |
|:-:|:---|:---|:---|
| 1 | Đường dẫn, `realpath`, kiểm tra tiền tố | `vfs_resolve` | Test Path Traversal (NT3) |
| 2 | `opendir/readdir/stat/mkdir/unlink/rmdir/rename` | `fsp_vfs.c` | `ls`, `cd`, `mkdir` chạy qua CLI client |
| 3 | Đọc/ghi file theo khối, file tạm `.part` | `fsp_transfer.c` | File 500 MB khớp `sha256sum` (NT4) |
| 4 | Đệ quy thư mục, manifest | `manifest_build/apply` | `diff -r` giống hệt (NT5) |
| 5 | So khớp chuỗi không phân biệt hoa thường, gọi `perm_can_access` | `fsp_search.c` | File PRIVATE người khác không xuất hiện |

**Tài liệu:** *The Linux Programming Interface* — các chương về File I/O, thuộc tính file, thư mục; `man 3 realpath`, `man 3 readdir`, `man 2 stat`.

### M4 — Hà Đức Dương: Client GUI & tích hợp
| Thứ tự | Kiến thức | Áp dụng vào | Đầu ra kiểm chứng |
|:-:|:---|:---|:---|
| 1 | GTK 3 cơ bản: cửa sổ, `GtkBox`, `GtkButton`, `GtkEntry`, signal | `gui_login.c` | Màn hình đăng nhập (§6.1 SRS) |
| 2 | `GtkTreeView` + `GtkListStore` | `gui_main.c` | 2 ngăn Local/Remote hiển thị danh sách |
| 3 | Luồng mạng + hàng đợi lệnh (mutex + cond) + `g_idle_add` | `client_net.c` | GUI không treo khi truyền (NT6) |
| 4 | `GtkProgressBar`, `GtkDialog` | `gui_dialogs.c` | Hàng đợi truyền, hộp thoại quyền & tìm kiếm |
| 5 | CLI client dùng lại `libfsp_common` | `cli_main.c` | Cả nhóm dùng để test server khi GUI chưa xong |

**Tài liệu:** GTK 3 Getting Started (https://docs.gtk.org/gtk3/getting_started.html); tài liệu GLib về `g_idle_add`; ví dụ `GtkTreeView` trong GTK 3 demo (`gtk3-demo`).

---

## 4. KẾ HOẠCH 2 TUẦN

> **Nguyên tắc:** cuối mỗi mốc có **demo chạy được** trên nhánh `develop`. CLI client có sớm để M2/M3 test server mà không phải chờ GUI.

| Ngày | M1 (Mạng) | M2 (Auth/Quyền) | M3 (File/Truyền) | M4 (GUI) |
|:-:|:---|:---|:---|:---|
| 1–2 | Kiến thức nền chung (§2) — **cả nhóm cùng đọc lại FSPProtocol.h và chốt định dạng JSON của từng sự kiện** ||||
| 3 | `send_exact/recv_exact`, frame 42 byte | Port SHA-256, user store sang C | `vfs_resolve` + test traversal | Khung GTK, màn hình đăng nhập |
| 4 | Server accept + thread/client, dispatcher | `AUTH_*` handler, phiên | `DIR_LIST/CHANGE/PWD/MKDIR` | CLI client: login, ls, cd |
| 5 | **Mốc 1:** CLI đăng nhập + `ls/cd/mkdir` qua mạng thật ||||
| 6 | Hỗ trợ M3 về `FILE_CHUNK`, PING/PONG | `perm_can_access` 7 bước + test | Upload/Download file (`.part`, CRC32) | Màn hình chính 2 ngăn, luồng mạng |
| 7 | Test 5 client đồng thời | Tích hợp quyền vào handler M3 | Test file 500 MB | Hàng đợi truyền + progress bar |
| 8 | Xử lý ngắt kết nối giữa chừng (NFR-4) | `PERM_GET/PERM_SET`, lưu JSON | Manifest, upload/download thư mục | Upload/Download qua GUI |
| 9 | Sửa lỗi tích hợp, chạy ASan | Test quyền đầy đủ | Tìm kiếm + lọc quyền | Hộp thoại quyền, hộp thoại tìm kiếm |
| 10 | **Mốc 2:** Đủ 7 yêu cầu gốc chạy qua GUI (có thể còn lỗi) ||||
| 11–12 | Sửa lỗi, chạy lại toàn bộ test trong bảng truy vết (SRS §9) ||||
| 13 | Viết SDD phần mình, chuẩn bị slide, tập trả lời câu hỏi (§5) ||||
| 14 | **Mốc 3:** Tổng duyệt demo theo kịch bản SRS §3.4 ||||

**Nếu chậm tiến độ — thứ tự cắt giảm:** bỏ các mục COULD → bỏ FR-2.5/2.6 (xóa, đổi tên) → bỏ FR-5.2 (lọc đuôi file). **Không cắt** bất kỳ mục MUST nào.

---

## 5. CÂU HỎI GIẢNG VIÊN CÓ THỂ HỎI (tự luyện trả lời)

| Câu hỏi | Ý chính cần trả lời | Ai trả lời |
|:---|:---|:-:|
| Vì sao không dùng FTP có sẵn mà tự làm giao thức? | Đề bài yêu cầu lập trình socket; cần thêm phân quyền theo file & tìm kiếm mà FTP không có; FTP dùng 2 kết nối, FSP dùng 1 | Cả nhóm |
| Ứng dụng tham chiếu là gì, khác gì? | FileZilla; xem bảng SRS §1.4 | Cả nhóm |
| Làm sao biết đã nhận đủ một thông điệp? | Đọc đủ 42 byte header → lấy `payload_len` → đọc đủ chừng đó byte | M1 |
| Hai client cùng lúc thì server xử lý thế nào? | Mỗi client một thread; dữ liệu dùng chung có mutex | M1 |
| Mật khẩu lưu thế nào? Salt để làm gì? | Lưu `SHA256(salt + password)`; salt khác nhau → cùng mật khẩu vẫn khác hash, chống tra bảng băm sẵn | M2 |
| `PRIVATE` mà set bit "Người khác" = `r` thì sao? | Vẫn bị từ chối vì bước 3 của `can_access` chạy trước | M2 |
| Chặn `../../etc/passwd` thế nào? | Chuẩn hóa đường dẫn rồi kiểm tra tiền tố thư mục gốc | M3 |
| File 1 GB có làm tràn RAM không? | Không, chỉ dùng bộ đệm 64 KB đọc/gửi lần lượt | M3 |
| CRC32 có chống bị sửa dữ liệu cố ý không? | Không — CRC32 chỉ phát hiện lỗi ngẫu nhiên; chống sửa cố ý cần chữ ký/TLS (ngoài phạm vi) | M1/M3 |
| Vì sao GUI không bị đơ khi tải file? | Socket chạy ở luồng riêng, cập nhật giao diện qua `g_idle_add` | M4 |
| Hệ thống có bảo mật đường truyền không? | Không có TLS — giới hạn đã ghi rõ trong SRS §5 | Cả nhóm |
