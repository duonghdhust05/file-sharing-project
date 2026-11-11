# KẾ HOẠCH THỐNG NHẤT NGÔN NGỮ C (C11) & BÁO CÁO KHẢO SÁT NHÁNH (CHECKPOINT 1)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (FSP - GROUP 7)

**Ngày khảo sát:** 20/10/2026 (Checkpoint 1)  
**Quyết định kỹ thuật:** Thống nhất chuyển đổi toàn bộ ngôn ngữ lập trình cốt lõi từ **C++17** sang **Pure C (Chuẩn C11 - ISO/IEC 9899:2011)** kết hợp **POSIX API (`<sys/socket.h>`, `<pthread.h>`)** và **GTK (Desktop GUI)**.

---

## 1. BÁO CÁO KHẢO SÁT HIỆN TRẠNG CÁC NHÁNH (`feat/*`) TẠI CHECKPOINT 1

Qua rà soát toàn bộ lịch sử commit và cây mã nguồn trên tất cả các nhánh tính năng của 4 thành viên tính đến ngày 20/10/2026:

| Nhánh tính năng | Thành viên phụ trách | Trạng thái mã nguồn hiện tại | Khối lượng đã hoàn thành | Đánh giá mức độ ảnh hưởng khi chuyển sang C11 |
| :--- | :--- | :--- | :--- | :--- |
| **`origin/feat/m1-network-engine`** | **Trần Sỹ Nguyên** (20235985) | Đã có 2 commits (`b463950`, `db2e6c3`) với **639 dòng code C++** | • Hoàn thành `Socket.h/.cpp` (RAII TCP Socket wrapper).<br>• Hoàn thành `PacketHandler.h/.cpp` (`readExact`, `sendExact`, `packFrame`, `unpackFrame`, `unpackFromBuffer`).<br>• Hoàn thành Unit Test `test_network_core.cpp` (pass Probe 1 Sticky Packets).<br>• Cập nhật SDD Chương 3.1 & 3.2. | **Trung bình (Thấp về thuật toán):** Toàn bộ lời gọi hệ thống (`socket`, `bind`, `listen`, `accept`, `recv`, `send`, `setsockopt`, `htonl`, `ntohl`) vốn đã là hàm C nguyên thủy. Chỉ cần chuyển `class Socket` $\to$ `struct fsp_socket` và thay `std::vector<uint8_t>` bằng con trỏ buffer `uint8_t*` + `size_t`. |
| **`origin/feat/m2-auth-permissions`** | **Bùi Đức Hiếu** (20235931) | Đã có 4 commits (`a89f0cf`..`b977236`) với **1,620 dòng code C++** + 144 dòng tài liệu API | • Hoàn thành 100% logic M2 (`Sha256`, `AuthUtil`, `UserManager`, `SessionManager`, `PermissionEngine`, `AuthHandler`).<br>• Hoàn thành 9-step `canAccess()` POSIX + ACL.<br>• Hoàn thành `test_auth_permissions.cpp` với **190 assertions** pass sạch dưới ASan, UBSan và TSan.<br>• Hoàn thành SDD Chương 4 & `docs/M2-FUNCTION-REFERENCE.md`. | **Trung bình - Cao (Cần refactor cấu trúc lưu trữ):** Thuật toán `Sha256`, CSPRNG (`/dev/urandom`), định dạng lưu đĩa JSON-lines (`users.json`, `permissions.json`) và 9 bước kiểm tra quyền giữ nguyên 100%. Cần chuyển các cấu trúc `std::map` / `std::string` / `std::shared_mutex` sang mảng động C (`struct`), chuỗi `char[]` cố định và `pthread_rwlock_t` / `pthread_mutex_t`. |
| **`origin/feat/m3-filesystem-transfer`** | **Vũ Nhật Minh Dương** (20235925) | Đang ở commit nền `f10f539` (Chưa push code C++) | • Đã hoàn thành thiết kế kiến trúc VFS Sandbox, Chunking 64KB, Folder Manifest và Search trong SDD Chương 5. | **Không mất công viết lại (0% Rework):** Bắt đầu viết trực tiếp 100% bằng **C11** sử dụng POSIX `<sys/stat.h>`, `<dirent.h>`, `realpath()`, `fopen`/`fread`/`fwrite`/`fseeko`. |
| **`origin/feat/m4-gui-client`** | **Hà Đức Dương** (20235923) | Đang ở commit nền `f10f539` (Chưa push code C++ Qt) | • Hoàn thành toàn bộ khung dự án, SRS, WBS, SDD, CI/CD GitHub Actions, bộ quy chuẩn Adversarial Testing (`docs/TDD/*`, `AGENTS.md`) và Slide báo cáo Checkpoint 1. | **Không mất công viết lại (0% Rework):** Triển khai trực tiếp Desktop GUI bằng **C11 + GTK3/GTK4**, dùng `pthread` cho luồng mạng nền và `g_idle_add()` để cập nhật giao diện an toàn. |

---

## 2. LÝ DO KỸ THUẬT THỐNG NHẤT SANG NGÔN NGỮ C (C11)

1. **Chuẩn mực môn học Lập trình Mạng (Network Programming)**:
   * Các API hệ thống mạng trên Linux/POSIX (`<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<poll.h>`, `<sys/epoll.h>`, `<pthread.h>`) đều có giao diện gốc là ngôn ngữ C.
   * Viết bằng C11 giúp sinh viên kiểm soát tường minh từng byte trên wire buffer, hiểu sâu sắc cách quản lý vùng nhớ đệm (buffer lifecycle), con trỏ, căn chỉnh bộ nhớ (`__attribute__((packed))`) và xử lý mã lỗi hệ thống (`errno`, `EINTR`, `EAGAIN`).
2. **Tái sử dụng 100% Thư viện Cốt lõi (Zero-Overhead Shared Core Library)**:
   * Khi viết bằng C11, thư viện tĩnh `libfsp_common.a` (chứa Framing 42-byte, CRC32, SHA-256, Socket I/O) được liên kết trực tiếp vào cả **FSP Server Daemon**, **FSP CLI Client** và **FSP GTK Desktop Client** mà không gặp rào cản name-mangling hay phụ thuộc runtime của C++.
3. **Kiểm soát chặt chẽ giới hạn bộ nhớ (Bộ đệm cố định 64 KB, RAM không tăng theo kích thước file)**:
   * Không có chi phí ẩn từ việc cấp phát/nhân bản ngầm của `std::string`, `std::vector`, hay `std::map`. Mọi vùng đệm truyền chunk ($64\text{ KB}$) được quản lý bằng bộ đệm cố định, đảm bảo bộ nhớ tiến trình ổn định khi truyền tệp tin lớn.

---

## 3. BẢNG ÁNH XẠ KỸ THUẬT TỪ C++17 SANG C11 (PORTING MATRIX)

Để đảm bảo quá trình chuyển đổi code của M1 và M2 diễn ra nhanh chóng, an toàn và không làm hỏng logic đã kiểm thử, toàn nhóm áp dụng bảng quy đổi chuẩn sau:

| Thành phần | C++17 (Hiện tại trên nhánh M1, M2) | C11 Chuẩn hóa (Mục tiêu thống nhất) |
| :--- | :--- | :--- |
| **Tiêu chuẩn biên dịch** | `CMAKE_CXX_STANDARD 17` (`g++` / `clang++`) | `CMAKE_C_STANDARD 11` (`gcc -std=c11` / `clang -std=c11`) |
| **Đóng gói Struct nhị phân** | `#pragma pack(push, 1)` + `static_assert` | `__attribute__((packed))` (hoặc `#pragma pack(1)`) + `_Static_assert` (C11 `<assert.h>`) |
| **Kiểu dữ liệu cơ bản** | `<cstdint>`, `<cstdbool>`, `enum class` | `<stdint.h>`, `<stdbool.h>`, `typedef enum { ... } fsp_..._t;` |
| **Quản lý Socket (M1)** | `class FSP::Socket` (RAII destructor) | `typedef struct { int fd; } fsp_socket_t;` kèm hàm `fsp_socket_init()`, `fsp_socket_close(&sock)` |
| **Bộ đệm Gói tin (M1)** | `std::vector<uint8_t>` | `typedef struct { uint8_t *data; size_t len; size_t cap; } fsp_buffer_t;` hoặc truyền buffer cấp phát sẵn |
| **Điều phối Sự kiện (M1)** | `std::function<void(...)>` + `std::map` | Con trỏ hàm `typedef int (*fsp_event_handler_fn)(fsp_conn_t *conn, const fsp_header_t *hdr, const uint8_t *payload, void *ctx);` + bảng tra tĩnh |
| **Đồng bộ Đa luồng (M1, M2, M3)** | `std::mutex`, `std::shared_mutex`, `std::condition_variable` | `pthread_mutex_t`, `pthread_rwlock_t`, `pthread_cond_t` (`<pthread.h>`) |
| **Lưu trữ User / Session / Perm (M2)** | `std::map<uint32_t, UserRecord>`, `std::string` | Mảng động các struct cố định (`char username[64]`, `char token[65]`, `char path[512]`) + tìm kiếm tuyến tính/bảng băm (Hash Table) |
| **Thao tác Hệ thống File (M3)** | `std::filesystem::weakly_canonical`, `recursive_directory_iterator`, `std::ifstream` | POSIX `realpath()`, `stat()`, `mkdir()`, `opendir()`/`readdir()`, `fopen()`/`fread()`/`fwrite()`/`fseeko()` |
| **Giao diện Đồ họa (M4)** | Qt6 (`QMainWindow`, `QThread`, `Signals & Slots`) | **GTK 3** (`GtkWidget`, `pthread_create` cho worker thread + `g_idle_add()` để đẩy sự kiện về UI thread) |

---

## 4. KẾ HOẠCH HÀNH ĐỘNG CHI TIẾT CHO 4 THÀNH VIÊN (MIGRATION & SPRINT PLAN)

### Giai đoạn Chuyển đổi Ngắn hạn (Từ ngày 20/10 đến 24/10 - 4 Ngày):
1. **Hợp đồng Giao thức Chung (`include/common/FSPProtocol.h` & `Config.h`)**:
   * Đảm bảo `FSPProtocol.h` và `Config.h` tương thích 100% với trình biên dịch C11 (`gcc -std=c11`) bằng `_Static_assert` và `typedef struct`.
   * Cập nhật `CMakeLists.txt` hỗ trợ biên dịch C11 chuẩn mực.
2. **Thành viên 1 (Trần Sỹ Nguyên - `feat/m1-network-engine`)**:
   * Port `Socket.cpp` và `PacketHandler.cpp` sang C11 (`src/common/fsp_socket.c` và `src/common/fsp_packet.c`) — ước tính chỉ mất ~2-3 giờ vì phần thân hàm đã gọi trực tiếp API POSIX/Winsock.
   * Port `tests/test_network_core.cpp` sang `tests/test_network_core.c`.
   * Tiếp tục triển khai máy chủ đa luồng 1 thread / 1 client (`server_main.c`) và `fsp_dispatcher.c` (`M1.3`) trực tiếp bằng C11 + `pthread`. Thread pool là COULD.
3. **Thành viên 2 (Bùi Đức Hiếu - `feat/m2-auth-permissions`)**:
   * Chuyển `Sha256.cpp` sang `src/common/fsp_sha256.c` (dùng chung cho cả M2 băm mật khẩu và M3 kiểm tra SHA-256 toàn file).
   * Port `UserManager`, `SessionManager`, `PermissionEngine`, `AuthHandler` sang C11 (`fsp_user.c`, `fsp_session.c`, `fsp_perm.c`, `fsp_auth_handler.c`) dùng `pthread_mutex_t`, bảng quyết định bước 0–7 và PERM_SET đệ quy.
   * Giữ nguyên 100% bộ test vectors (NIST SHA-256, ma trận phân quyền bước 0–7, token timeout, brute-force lockout) trong `tests/test_auth_permissions.c`.
4. **Thành viên 3 (Vũ Nhật Minh Dương - `feat/m3-filesystem-transfer`)**:
   * Viết trực tiếp bằng C11: `src/server/fsp_vfs.c` (sandbox path resolution bằng `realpath()` + `DIR_LIST`, `DIR_MKDIR`, `DIR_CHANGE`, `FS_DELETE`, `FS_RENAME`), `src/server/fsp_transfer.c` ($64\text{ KB}$ chunk streaming), và `src/server/fsp_search.c`.
5. **Thành viên 4 (Hà Đức Dương - `feat/m4-gui-client`)**:
   * Viết trực tiếp bằng C11: `src/client_cli/main.c` (CLI client phục vụ test tích hợp sớm) và dựng khung giao diện GTK (`src/client_gui/`) với mô hình 2 luồng (`UI Thread` + `Network Worker Thread` giao tiếp qua `g_idle_add`).

