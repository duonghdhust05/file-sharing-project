# FSP: Client-Server Online File Sharing Application
**Network Programming (IT4062E) — Topic 6 — Group 7**  
*Build a File-Sharing Application using C11 Socket Programming & Custom Application-Layer Protocol*

| Thành viên (Member) | MSSV | Vai trò & Phụ trách Module | Nhánh Git (`feat/*`) |
| :--- | :---: | :--- | :--- |
| **Trần Sỹ Nguyên** | `20235985` | **M1: Core Network Engine** (POSIX Socket, 42B Framing, Dispatcher, `1 thread/client`) | `feat/m1-network-engine` |
| **Bùi Đức Hiếu** | `20235931` | **M2: Auth & Permissions** (Salted SHA-256, Session Token, 8-Step `0–7` POSIX+Sharing Engine) | `feat/m2-auth-permissions` |
| **Vũ Nhật Minh Dương** | `20235925` | **M3: VFS & Chunk Transfer** (Sandbox `realpath`, FTP Cmds, 64 KB Stream, Search) | `feat/m3-filesystem-transfer` |
| **Hà Đức Dương** | `20235923` | **M4: Client GUI & Integration** (C11 GTK 3 Dual-Pane UI, Worker Thread, CLI, CI/CD) | `feat/m4-gui-client` |

> Tài liệu này đã được đồng bộ theo [SRS.md](../pdf-document/SRS.md) v3.1 và [SDD.md](../pdf-document/SDD.md) v2.1 (loại bỏ các chỉ tiêu v1 chưa kiểm chứng để bảo đảm tính chính xác khi bảo vệ).

---

## 1. Project Topic, Core Problems & Targets

### 1.1. Core Engineering Problems Addressed
Xây dựng ứng dụng chia sẻ file hoàn chỉnh trên **POSIX Raw TCP Sockets (`<sys/socket.h>`)** bằng **C11 thuần** (tham chiếu bố cục **FileZilla**), giải quyết 5 bài toán cốt lõi:

1. **Dòng byte TCP không ranh giới (Sticky Packets & Fragmentation):** Dùng header cố định `42 byte` (`FSPHeader`) + vòng lặp `send_exact`/`recv_exact` để tách đúng frame dính nhau và ghép đúng frame bị chia mảnh.
2. **Truyền file lớn ổn định bộ nhớ (Bounded-Memory Streaming):** Truyền qua bộ đệm cố định tối đa `64 KB` (`FSP_DEFAULT_CHUNK_SIZE`) kèm `FileChunkHeader` `88 byte` và CRC32 từng khối; upload ghi vào file tạm `.part` rồi đổi tên nguyên tử.
3. **Phục vụ đồng thời nhiều client:** Mô hình **1 thread / 1 client** (`pthread_create` + `pthread_detach`), bảo vệ dữ liệu dùng chung (users, sessions, permissions) bằng `pthread_mutex_t`. *(Thread pool là mục `COULD`.)*
4. **Phân quyền kiểu Linux + Mức chia sẻ & Sandbox:** Áp dụng bảng quyết định `can_access` 8 bước (`0–7`) kết hợp bit `r/w/s` và mức `PRIVATE`/`SHARED`/`PUBLIC`, chặn đứng Path Traversal (`../../etc/passwd`) tại `./storage/server_root` bằng `realpath()`.
5. **Giao diện GTK 3 không treo (Non-Blocking UI):** Tách luồng GTK vẽ giao diện khỏi luồng mạng `pthread`, trả kết quả về UI duy nhất qua `g_idle_add()`.

### 1.2. Verification Targets (SRS §5)
* **Data Integrity (NFR-1):** File 1 KB, 10 MB, 500 MB upload/download khớp `sha256sum`/`cmp` (`CRC32` từng chunk là MUST; `SHA-256` toàn file là COULD).
* **Memory Stability (NFR-2):** Bộ nhớ RAM tiến trình không tăng theo kích thước file khi truyền file 500 MB (theo dõi qua `top`/`/usr/bin/time -v`).
* **Concurrency Capacity (NFR-3):** Phục vụ ổn định **ít nhất 5 client** upload/download đồng thời.
* **Resilience (NFR-4):** Client ngắt kết nối đột ngột không làm sập server và tự xóa sạch file `.part`.
* **Protocol Overhead:** Header frame `42 byte` + header chunk `88 byte` (≈ `0.19%` trên chunk `64 KB`).

---

## 2. Technology Stack & Architecture

| Thành phần | Công nghệ chuẩn (SRS v3.0) |
| :--- | :--- |
| **Ngôn ngữ** | **C11** (`-std=c11 -Wall -Wextra -Wpedantic`), không dùng thư viện mạng cấp cao |
| **Mạng & Luồng** | `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<pthread.h>`, `<poll.h>` |
| **Hệ thống file** | POSIX `realpath()`, `stat()`, `mkdir()`, `opendir()`/`readdir()`, `fopen()`/`fread()`/`fwrite()` |
| **Desktop GUI & CLI** | **GTK 3** (`g_idle_add`) + CLI test client (`fsp_client_cli`) dùng chung `libfsp_common` |
| **Build & QA** | CMake 3.16+, CTest, `clang-format==18.1.3`, GitHub Actions (ASan + UBSan) |

```
[GTK 3 UI Thread] <──(queue + g_idle_add)──> [Client Net Thread (pthread)]
                                                        │
                                              libfsp_common (42B + CRC32)
                                                        │ TCP :8088
                                                        ▼
[accept() loop] ──> [1 thread / client] ──> [Event Dispatcher] ──> [M2 Auth/Perm | M3 VFS/Transfer/Search]
```

---

## 3. Functional Scope (7 Core Requirements — SRS §4)

| Mã | Nhóm chức năng | Sự kiện FSP chính |
|:-:|:---|:---|
| **R1** | User Management | `AUTH_REGISTER`, `AUTH_LOGIN`, `AUTH_LOGOUT` (Salted SHA-256, token; lockout & 30m TTL là COULD) |
| **R2** | FTP-like File Commands | `DIR_LIST` (`ls`), `DIR_CHANGE` (`cd`), `DIR_PWD` (`pwd`), `DIR_MKDIR` (`mkdir`), `FS_DELETE`/`FS_RENAME` (SHOULD) |
| **R3** | File Upload / Download | `FILE_UPLOAD_REQ`, `FILE_CHUNK` (≤ 64 KB + CRC32, `.part`), `FILE_UPLOAD_FINISH`, `FILE_DOWNLOAD_REQ` |
| **R4** | Folder Upload / Download | `FOLDER_UPLOAD_REQ`, `FOLDER_DOWNLOAD_REQ` (duyệt đệ quy dựng manifest, tải thư mục lọc theo quyền `READ`) |
| **R5** | File Search | `SEARCH_QUERY` (chuỗi con không phân biệt hoa thường + đuôi file SHOULD; không regex; lọc quyền `READ`; tối đa 100 kết quả) |
| **R6** | Linux-like Permissions | `PERM_GET`, `PERM_SET` (9-bit `r/w/s` + `PRIVATE`/`SHARED`/`PUBLIC` + `share_list` + `recursive`; thuật toán `can_access` bước `0–7`) |
| **R7** | Desktop GUI | Màn hình Login, Màn hình chính 2 ngăn Local/Remote giống FileZilla, Hàng đợi truyền %, Dialog Quyền & Tìm kiếm |

---

## 4. Binary Wire Format (`include/common/FSPProtocol.h`)

* **`FSPHeader` (42 byte cố định, `#pragma pack(1)` + `_Static_assert`):**
  * `magic` (`1B`, `0xAA`) · `type` (`1B`, `0x01`..`0x07`) · `payload_len` (`4B` big-endian) · `event[32]` (`32B`) · `checksum` (`4B` CRC32 big-endian).
* **`FileChunkHeader` (88 byte cố định ở đầu payload `FILE_CHUNK`):**
  * `transfer_id[64]` (`64B`) · `chunk_index` (`8B`) · `total_chunks` (`8B`) · `chunk_size` (`4B`, ≤ 65536) · `chunk_crc` (`4B`).

---

## 5. Tiến độ & Quy chuẩn Kiểm thử 6 Probes

| Probe | Nội dung kiểm thử bắt buộc | Phụ trách | Trạng thái hiện tại trong Repo |
|:-:|:---|:-:|:---|
| **Probe 1** | Dính gói / tách mảnh TCP frame 42B | M1 | **Đạt** (`tests/test_packet.c`, `tests/test_network_core.c`) |
| **Probe 2** | Token giả / thiếu / đã logout → `401` | M2 | **Đạt ở mức unit/module** (`tests/test_auth_permissions.c`) |
| **Probe 3** | Đủ bước `0–7` của `can_access`; `PRIVATE` bit `0666` vẫn từ chối `403` | M2 | **Đạt ở mức unit/module** (`tests/test_auth_permissions.c`) |
| **Probe 4** | Lật 1 bit trong chunk 64 KB → sai CRC32, xóa `.part` | M3 | Chờ triển khai `fsp_transfer.c` |
| **Probe 5** | Path traversal `../../../../etc/passwd`, `cd ../../..` → `403` / giữ `/` | M3 | Chờ triển khai `fsp_vfs.c` |
| **Probe 6** | Chạy sạch dưới ASan + UBSan; ≥ 5 client đồng thời | Cả nhóm | M1 + M2 đã pass CI ASan/UBSan; chờ tích hợp M3 + M4 |

> Chi tiết trạng thái từng Use Case và tồn đọng tích hợp xem tại [UC-CHECKLIST.md](../uc-details/UC-CHECKLIST.md).
