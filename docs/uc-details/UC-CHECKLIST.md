# CHECKLIST USE CASE CHÍNH THỨC & RÀ SOÁT TIẾN ĐỘ DỰ ÁN
## Dự án: Ứng dụng chia sẻ file Client-Server qua Socket .Topic 6 — Group 7)

* **Nguồn sự thật yêu cầu:** [SRS.md].../pdf-document/SRS.md) v3.1
* **Checklist NFR chính thức .27 NFR):** [NFR-CHECKLIST.md].NFR-CHECKLIST.md)
* **Đặc tả chi tiết từng UC .v1.0):** [uc-details/README.md].README.md)
* **Phân công & WBS:** [WBS.md].../WBS.md) v2.0
* **Ngày rà soát hiện trạng repo:** `2026-11-16` .đối chiếu thực tế mã nguồn trên nhánh `develop` tại commit `973ffe4`)

> **Quy ước trạng thái:**
> `[x]` Hoàn thành ở tầng tương ứng · `[~]` Đã có một phần / còn thiếu chi tiết tích hợp · `[ ]` Chưa triển khai

---

## 1. BẢNG CHECKLIST TỔNG HỢP 13 USE CASE CHÍNH THỨC .`UC01`–`UC13`)

| Mã UC | Tên Use Case | R | Ưu tiên | Phụ trách | Đặc tả .`uc-details`) | Server .`src/server`) | Client API .`client_api`) | CLI / GTK GUI .M4) | Test / Probe | Trạng thái UC tổng thể |
|:-:|:---|:-:|:-:|:-:|:---:|:---:|:---:|:---:|:---:|:---|
| **UC01** | [Đăng ký tài khoản].UC01-dang-ky-tai-khoan.md) | R1 | MUST | M2 .+M3) | [x] | [~] *.1)* | [x] | [ ] | [~] | **Đang hoàn thiện** *.thiếu tạo thư mục đĩa `/home/<user>`)* |
| **UC02** | [Đăng nhập].UC02-dang-nhap.md) | R1 | MUST | M2 | [x] | [x] *.2)* | [x] | [ ] | [x] | **Xong Server + API** *.chờ CLI/GUI)* |
| **UC03** | [Đăng xuất].UC03-dang-xuat.md) | R1 | MUST | M2 | [x] | [~] *.3)* | [x] | [ ] | [x] | **Xong cơ bản** *.thiếu cleanup hook khi disconnect)* |
| **UC04** | [Liệt kê nội dung thư mục .`ls`)].UC04-liet-ke-thu-muc.md) | R2 | MUST | M3 | [x] | [ ] | [ ] | [ ] | [ ] | **Chưa triển khai** *.chờ M3 VFS + API)* |
| **UC05** | [Đổi thư mục hiện tại .`cd`, `pwd`)].UC05-doi-thu-muc-hien-tai.md) | R2 | MUST | M3 | [x] | [ ] | [x] *.4)* | [ ] | [ ] | **Chờ Server M3** *.API `pwd`/`cd` đã có)* |
| **UC06** | [Tạo thư mục .`mkdir`)].UC06-tao-thu-muc.md) | R2 | MUST | M3 | [x] | [ ] | [x] | [ ] | [ ] | **Chờ Server M3** *.API `mkdir` đã có)* |
| **UC07** | [Xóa .`rm`) / Đổi tên .`mv`)].UC07-xoa-doi-ten.md) | R2 | SHOULD | M3 | [x] | [ ] | [~] *.5)* | [ ] | [ ] | **Chờ Server M3** *.chỉ mới có API `delete`)* |
| **UC08** | [Upload một file].UC08-upload-file.md) | R3 | MUST | M3 + M1 | [x] | [ ] | [ ] *.6)* | [ ] | [ ] | **Chưa triển khai** *.API đang là stub)* |
| **UC09** | [Download một file].UC09-download-file.md) | R3 | MUST | M3 + M1 | [x] | [ ] | [ ] *.6)* | [ ] | [ ] | **Chưa triển khai** *.API đang là stub)* |
| **UC10** | [Upload cả thư mục].UC10-upload-thu-muc.md) | R4 | MUST | M3 | [x] | [ ] | [ ] | [ ] | [ ] | **Chưa triển khai** |
| **UC11** | [Download cả thư mục].UC11-download-thu-muc.md) | R4 | MUST | M3 | [x] | [ ] | [ ] | [ ] | [ ] | **Chưa triển khai** |
| **UC12** | [Tìm kiếm file theo tên + tải từ kết quả].UC12-tim-kiem-file.md) | R5 | MUST | M3 + M2 | [x] | [ ] | [ ] | [ ] | [ ] | **Chưa triển khai** |
| **UC13** | [Xem / đặt quyền và mức chia sẻ].UC13-phan-quyen-chia-se.md) | R6 | MUST | M2 | [x] | [~] *.7)* | [~] *.7)* | [ ] | [x] | **Cần đồng bộ JSON & `share_list` qua mạng** |

### Ghi chú kỹ thuật phát hiện khi rà soát mã nguồn
1. **UC01 .`fsp_auth_handler.c`):** `handle_auth_register` đã gọi `fsp_perm_register."/home/<username>", ...)` nhưng **chưa tạo thư mục vật lý** `./storage/server_root/home/<username>` trên đĩa.
2. **UC02 .`fsp_auth_handler.c`):** `handle_auth_login` trả về `token`, chưa kèm `cwd` trong JSON phản hồi .mặc dù `fsp_session.c` khởi tạo `cwd`).
3. **UC03 .`fsp_auth_handler.c`):** `fsp_auth_register_handlers.)` mới gọi `dispatcher_register....)` cho 5 sự kiện, **chưa đăng ký `dispatcher_register_cleanup....)`** để tự hủy session trong `fsp_session` khi client ngắt kết nối TCP đột ngột .FR-1.5).
4. **UC05 .`fsp_client_api.c` vs `SRS.md`):** `fsp_client_pwd.)` đọc key JSON `"path"` — cần thống nhất giữa M3 và M4 là `"path"` hay `"cwd"`.
5. **UC07 .`fsp_client_api.h`):** Đã có `fsp_client_delete.)`, chưa có hàm `fsp_client_rename.)` .`FS_RENAME`).
6. **UC08–UC09 .`fsp_client_api.c`):** `fsp_client_upload_file.)` và `fsp_client_download_file.)` hiện là **stub** .`return 0;`).
7. **UC13 .`fsp_auth_handler.c` vs `fsp_client_api.c`):**
   * `fsp_perm.c` đã cài đủ 8 bước `can_access`, `grant`/`revoke` ACL và `recursive=true`, nhưng `handle_perm_get` mới trả `owner_id`, `group_id`, `perm_bits`, `sharing_level` .chưa trả `share_list` theo `username`); `handle_perm_set` chưa nhận `share_list`/`grant`/`revoke` từ JSON.
   * `fsp_client_set_perm.)` gửi key JSON `"mode"`, trong khi `handle_perm_set.)` trên server đọc `"perm_bits"` .lệch key JSON); đồng thời chưa có `fsp_client_get_perm.)` .`PERM_GET`).
8. **Đường dẫn DB .`server_main.c` vs `SRS.md`):** `server_main.c` khởi tạo `"storage/users.json"` và `"storage/permissions.json"`, trong khi `SRS.md §8` ghi `data/users.json` và `data/permissions.json`. Cần thống nhất một chuẩn đường dẫn.

---

## 2. CHECKLIST NGHIỆM THU CHI TIẾT TỪNG USE CASE

### Nhóm R1 — Quản lý người dùng .`UC01`–`UC03`)
- **UC01 — Đăng ký tài khoản .`AUTH_REGISTER` — MUST)**
  - [x] Đặc tả chuẩn tại [UC01-dang-ky-tai-khoan.md].UC01-dang-ky-tai-khoan.md)
  - [x] `fsp_user_register`: validate `username` `[A-Za-z0-9_]{3,32}`, `password` 6–128 ký tự, salt 16 byte + SHA-256, user đầu tiên `id = 1` .Admin), trùng tên trả `409`
  - [x] `handle_auth_register`: đăng ký bản ghi quyền `/home/<username>` .`0755`, `PRIVATE`)
  - [ ] Tạo thư mục vật lý `./storage/server_root/home/<username>` khi đăng ký .FR-1.2)
  - [x] Client API `fsp_client_register`
  - [ ] CLI & GTK UI màn hình Đăng ký .FR-7.1)
- **UC02 — Đăng nhập .`AUTH_LOGIN` — MUST)**
  - [x] Đặc tả chuẩn tại [UC02-dang-nhap.md].UC02-dang-nhap.md)
  - [x] `fsp_user_verify_login` .constant-time compare, khóa tạm 5 phút sau 5 lần sai — COULD đã làm)
  - [x] `fsp_session_create` .token ngẫu nhiên, mutex, timeout 30 phút — COULD đã làm)
  - [x] `handle_auth_login` trả `200 OK` + `token` .sai trả `401`)
  - [x] Client API `fsp_client_login` & Test Probe 2 .`tests/test_auth_permissions.c`)
  - [ ] CLI .`login`) & GTK UI màn hình Đăng nhập .FR-7.1)
- **UC03 — Đăng xuất .`AUTH_LOGOUT` — MUST)**
  - [x] Đặc tả chuẩn tại [UC03-dang-xuat.md].UC03-dang-xuat.md)
  - [x] `fsp_session_destroy` & `handle_auth_logout`
  - [ ] Đăng ký `dispatcher_register_cleanup.)` để tự hủy token khi ngắt kết nối TCP đột ngột .FR-1.5)
  - [x] Client API `fsp_client_logout` & Test Probe 2 dùng lại token sau logout → `401`
  - [ ] CLI .`logout`) & nút `[Đăng xuất]` trên GTK GUI

### Nhóm R2 — Lệnh quản lý file kiểu FTP .`UC04`–`UC07`)
- **UC04 — Liệt kê thư mục .`DIR_LIST` — MUST)**
  - [x] Đặc tả chuẩn tại [UC04-liet-ke-thu-muc.md].UC04-liet-ke-thu-muc.md)
  - [ ] `vfs_resolve` + `vfs_list` .chặn sandbox `realpath.)`, ẩn file `.part`, kiểm tra `READ` qua `fsp_can_access`)
  - [ ] Client API `fsp_client_list` + CLI .`ls`) + Ngăn Remote GTK .FR-7.2)
- **UC05 — Đổi thư mục hiện tại .`DIR_CHANGE`, `DIR_PWD` — MUST)**
  - [x] Đặc tả chuẩn tại [UC05-doi-thu-muc-hien-tai.md].UC05-doi-thu-muc-hien-tai.md)
  - [x] Client API `fsp_client_pwd` & `fsp_client_cd`
  - [ ] Server handler `DIR_CHANGE` & `DIR_PWD` + chặn `cd ../../..` .Probe 5)
  - [ ] CLI .`cd`, `pwd`) + Ô đường dẫn & nút `[↑]` trên GTK GUI
- **UC06 — Tạo thư mục .`DIR_MKDIR` — MUST)**
  - [x] Đặc tả chuẩn tại [UC06-tao-thu-muc.md].UC06-tao-thu-muc.md)
  - [x] Client API `fsp_client_mkdir`
  - [ ] Server handler `DIR_MKDIR` .`WRITE` trên cha, `mkdir`, `fsp_perm_register` `0755` `PRIVATE`)
  - [ ] CLI .`mkdir`) + nút `[Thư mục mới]` trên GTK GUI
- **UC07 — Xóa / Đổi tên .`FS_DELETE`, `FS_RENAME` — SHOULD)**
  - [x] Đặc tả chuẩn tại [UC07-xoa-doi-ten.md].UC07-xoa-doi-ten.md)
  - [~] Client API mới có `fsp_client_delete`, thiếu `fsp_client_rename`
  - [ ] Server handler `FS_DELETE` .file / thư mục rỗng + `fsp_perm_remove`) & `FS_RENAME` .cùng thư mục + `fsp_perm_rename`)
  - [ ] CLI .`rm`, `mv`) + menu chuột phải GTK GUI

### Nhóm R3 — Upload / Download một file .`UC08`–`UC09`)
- **UC08 — Upload một file .`FILE_UPLOAD_REQ`, `FILE_CHUNK`, `FILE_UPLOAD_FINISH` — MUST)**
  - [x] Đặc tả chuẩn tại [UC08-upload-file.md].UC08-upload-file.md)
  - [ ] Server `fsp_transfer.c`: kiểm tra `WRITE`, không ghi đè .`409`), file `.part`, buffer 64 KB, kiểm tra CRC32 từng chunk, cleanup xóa `.part` khi lỗi/disconnect, đăng ký quyền `0644` `PRIVATE`
  - [ ] Client API `fsp_client_upload_file` .hiện đang là stub)
  - [ ] Test Probe 4 .lật 1 bit → phát hiện sai CRC32, xóa `.part`) & test file 500 MB .NFR-1, NFR-2)
  - [ ] CLI .`put`) + Hàng đợi truyền GTK không treo .`pthread` + `g_idle_add.)`, NFR-6)
- **UC09 — Download một file .`FILE_DOWNLOAD_REQ`, `FILE_CHUNK` — MUST)**
  - [x] Đặc tả chuẩn tại [UC09-download-file.md].UC09-download-file.md)
  - [ ] Server `fsp_transfer.c`: kiểm tra `READ` trên chính file, đọc & gửi khối ≤ 64 KB kèm `FileChunkHeader` 88B + CRC32
  - [ ] Client API `fsp_client_download_file` .hiện đang là stub)
  - [ ] CLI .`get`) + Hàng đợi truyền GTK không treo .NFR-6)

### Nhóm R4 — Upload / Download cả thư mục .`UC10`–`UC11`)
- **UC10 — Upload cả thư mục .`FOLDER_UPLOAD_REQ` — MUST)**
  - [x] Đặc tả chuẩn tại [UC10-upload-thu-muc.md].UC10-upload-thu-muc.md)
  - [ ] Client dựng manifest .`opendir`/`readdir`) + Server tạo trước cây thư mục + truyền từng file theo UC08 + tạo bản ghi quyền cho **mọi** file/thư mục con
  - [ ] Test thư mục 3 cấp, 20 file .SRS §9)
- **UC11 — Download cả thư mục .`FOLDER_DOWNLOAD_REQ` — MUST)**
  - [x] Đặc tả chuẩn tại [UC11-download-thu-muc.md].UC11-download-thu-muc.md)
  - [ ] Server dựng manifest lọc theo quyền `READ`, trả kèm danh sách mục bị bỏ qua .`skipped`, FR-4.3)
  - [ ] Client tạo trước cây thư mục + tải từng file theo UC09

### Nhóm R5 — Tìm kiếm .`UC12`)
- **UC12 — Tìm kiếm file theo tên + tải từ kết quả .`SEARCH_QUERY`, include `UC09` — MUST)**
  - [x] Đặc tả chuẩn tại [UC12-tim-kiem-file.md].UC12-tim-kiem-file.md)
  - [ ] Server `fsp_search.c`: tìm chuỗi con không phân biệt hoa thường, lọc đuôi .SHOULD), lọc 100% qua `fsp_can_access...., READ)`, giới hạn ≤ 100 kết quả
  - [ ] Client API + CLI .`find`) + Hộp thoại Tìm kiếm có nút Download .FR-7.5)

### Nhóm R6 — Phân quyền & Chia sẻ .`UC13`)
- **UC13 — Xem / đặt quyền và mức chia sẻ .`PERM_GET`, `PERM_SET` — MUST)**
  - [x] Đặc tả chuẩn tại [UC13-phan-quyen-chia-se.md].UC13-phan-quyen-chia-se.md)
  - [x] `fsp_perm.c`: thuật toán `can_access` bước 0–7, 9-bit `r/w/s`, `PRIVATE`/`SHARED`/`PUBLIC`, `grant`/`revoke`, `recursive`, lưu JSON-lines
  - [x] Test Probe 3 phủ bước 0–7 và `PRIVATE` bit `0666` .`tests/test_auth_permissions.c`)
  - [~] Đồng bộ JSON `PERM_GET`/`PERM_SET` qua mạng: bổ sung trả/nhận `share_list` theo `username` và thống nhất `"perm_bits"` vs `"mode"` giữa server và `fsp_client_api.c`
  - [ ] CLI .`chmod`) + Hộp thoại Phân quyền GTK .FR-7.6)

### Nhóm Nền tảng & Tích hợp cuối .`R7` + NFR)
- [x] **M1 Core:** Socket `send_exact`/`recv_exact`, Frame 42B + CRC32, Dispatcher, Server `1 thread/client`, `PING`/`PONG` .`tests/test_packet.c`, `tests/test_network_core.c`)
- [ ] **M1.5:** Thêm socket timeout .`SO_RCVTIMEO`) tự ngắt kết nối im lặng quá ngưỡng .ghi nhận tại [PR1-m1-review.md].../reviews/PR1-m1-review.md))
- [ ] **E2E Demo SRS §3.4 & §9:** Kịch bản An — Bình — Chi chạy trọn vẹn trên GTK 3 + kiểm tra ≥ 5 client đồng thời & file 500 MB dưới ASan/UBSan
