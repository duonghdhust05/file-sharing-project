# BỘ KHUNG & CHECKLIST YÊU CẦU PHI CHỨC NĂNG (NFR FRAMEWORK & CHECKLIST)
## Dự án: Ứng dụng chia sẻ file Client-Server qua Socket (Topic 6 — Group 7)

> **Trạng thái:** `v1.0 Chính thức (Baseline) — Đã chốt 20 Core (MUST) + 7 Bổ trợ (SHOULD) = 27 NFR`  
> **Cập nhật theo:** [SRS.md](../pdf-document/SRS.md) v3.1 · [SDD.md](../pdf-document/SDD.md) v2.1 · [UC-CHECKLIST.md](UC-CHECKLIST.md) · [uc-details/](README.md)  
> **Bối cảnh thực tế:** C11 + POSIX Raw TCP Socket + `pthread` + GTK 3 trên Linux/WSL2. M1 & M2 đã hoàn thiện lõi C11; M3 & M4 triển khai bám sát bộ 27 NFR này.

---

## QUY ƯỚC PHÂN LOẠI ĐÃ CHỐT

* `[x]` 🟢 **TIER 1 — Bộ Lõi Bắt Buộc (20 mục — `MUST`):** Đã chốt bắt buộc để đạt yêu cầu đồ án, đo kiểm chứng được trực tiếp qua demo & CTest.
* `[x]` 🟡 **TIER 2 — Bộ Bổ Trợ Hợp Lý (7 mục — `SHOULD`):** Đã chốt đưa vào phạm vi chính thức (đa phần M1/M2 đã có sẵn nền tảng hoặc tốn ít công bổ sung).
* `[ ]` 🔴 **TIER 3 — Loại Bỏ / Anti-Illusion (8 mục — `OUT-OF-SCOPE`):** Chủ động loại khỏi đồ án để tránh phình phạm vi và tránh đặt mục tiêu ảo.

---

## NHÓM A — TOÀN VẸN DỮ LIỆU & ĐỘ CHÍNH XÁC GIAO THỨC (DATA INTEGRITY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-A1** | 🟢 MUST | **Toàn vẹn file truyền tải:** File upload/download (1 KB, 10 MB, 500 MB) khớp 100% từng byte với file gốc. | `sha256sum` hoặc `cmp` giữa file nguồn và file đích. | `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-A2** | 🟢 MUST | **Phát hiện hỏng gói từng khối:** Mọi `FILE_CHUNK` (≤ 64 KB) đều gắn và kiểm tra CRC32; sai CRC32 hủy truyền ngay. | Probe 4: lật 1 bit trong payload chunk → phát hiện sai CRC32 và trả lỗi. | `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-A3** | 🟢 MUST | **Tính nguyên tử file đích (`.part`):** Chỉ ghi vào `<file>.part`; chỉ `rename()` sang tên thật khi nhận đủ & hợp lệ. | Ngắt kết nối hoặc sai CRC giữa chừng → trên đĩa không còn `.part` hay file lỗi. | `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-A4** | 🟢 MUST | **Chính xác ranh giới TCP Frame:** Không mất/trộn byte khi 2 frame dính trong 1 `recv()` hoặc 1 frame tách nhiều mảnh. | Probe 1 (`ctest`: `fsp_test_packet`, `fsp_test_network_core`). | `UC01`–`UC13` |
| [x] | **NFR-A5** | 🟡 SHOULD | **Lưu bền vững nguyên tử CSDL JSON:** `users.json`, `permissions.json` ghi qua file tạm `.tmp` + `rename()`. | Kiểm tra cơ chế ghi nguyên tử trong `fsp_user.c` & `fsp_perm.c` (đã có ở M2). | `UC01`, `UC06`, `UC07`, `UC08`, `UC10`, `UC13` |
| [ ] | **NFR-A6** | 🔴 LOẠI | *(Loại bỏ)* Bắt buộc SHA-256 toàn file và hỗ trợ **Resume** truyền tiếp. | Ngoài phạm vi (`SHA-256` toàn file giữ là `COULD` tùy chọn, `Resume` = `WON'T`). | — |

---

## NHÓM B — HIỆU NĂNG & GIỚI HẠN BỘ NHỚ (PERFORMANCE & MEMORY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-B1** | 🟢 MUST | **Bộ nhớ O(1) theo kích thước file:** Truyền file 500 MB không làm RSS RAM tăng tuyến tính theo dung lượng file (nhờ bộ đệm cố định 64 KB). | `/usr/bin/time -v` hoặc `top`/`htop` trước, trong, sau truyền file 500 MB. | `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-B2** | 🟢 MUST | **Chặn cấp phát gói dị dạng:** Từ chối ngay mọi frame có `payload_len > FSP_BUFFER_SIZE` (128 KB). | Gửi header giả `payload_len` > 128 KB → đóng kết nối ngay, không OOM. | `UC04`, `UC08`, `UC09`, `UC10`, `UC11`, `UC12` |
| [x] | **NFR-B3** | 🟡 SHOULD | **Thời gian phản hồi lệnh điều khiển & tìm kiếm:** `DIR_LIST`, `DIR_CHANGE`, `DIR_MKDIR`, `PERM_*`, `SEARCH_QUERY` (≤ 100 kết quả) phản hồi **≤ 1 giây** trên LAN (kho test < 1.000 file). | Đo thời gian thực thi qua CLI client hoặc log timestamp khi demo. | `UC04`–`UC07`, `UC12`, `UC13` |
| [ ] | **NFR-B4** | 🔴 LOẠI | *(Loại bỏ)* Ngưỡng RAM cứng `≤ 50 MB` cho cả GTK GUI, tốc độ tối thiểu `≥ 100 MB/s`, hay file demo `≥ 2–5 GB`. | Không đặt con số cứng dễ lệch trên WSL2/GTK3. | — |

---

## NHÓM C — ĐỒNG THỜI & KHẢ NĂNG PHỤC VỤ (CONCURRENCY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-C1** | 🟢 MUST | **Phục vụ đồng thời tối thiểu 5 client:** Server dùng `1 thread / client` phục vụ ≥ 5 client upload/download cùng lúc không crash, không treo. | Mở ≥ 5 client test chạy đồng thời, tất cả hoàn tất đúng. | `UC02`, `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-C2** | 🟢 MUST | **An toàn vùng dữ liệu dùng chung:** Bảng `user`, `session`, `permission` và trạng thái transfer được khóa `pthread_mutex_t` theo thứ tự cố định, không deadlock. | Rà soát thứ tự khóa và chạy unit/integration test đa luồng. | `UC01`–`UC13` |
| [x] | **NFR-C3** | 🟡 SHOULD | **Kiểm thử Race Condition bằng ThreadSanitizer:** Các bộ test đa luồng chạy sạch dưới `-fsanitize=thread`. | Build CMake với `-fsanitize=thread` và chạy `ctest` đạt 0 data race. | `UC01`, `UC02`, `UC08`, `UC09`, `UC13` |
| [ ] | **NFR-C4** | 🔴 LOẠI | *(Loại bỏ)* Phục vụ `≥ 50–1000` kết nối đồng thời, bắt buộc `ThreadPool` + `epoll`. | Giữ kiến trúc `1 thread / client`; `ThreadPool` là `COULD` ngoài bộ cam kết. | — |

---

## NHÓM D — ĐỘ TIN CẬY & CHỊU LỖI (RELIABILITY & RESILIENCE)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-D1** | 🟢 MUST | **Cô lập lỗi theo từng client (Fail-Closed):** 1 client ngắt đột ngột hoặc gửi gói rác không làm sập server; cleanup hook hủy phiên và xóa `.part` trong < 1s. | Tắt ngang 1 client đang upload dở trong khi các client khác vẫn chạy. | `UC03`, `UC08`, `UC09`, `UC10`, `UC11` |
| [x] | **NFR-D2** | 🟢 MUST | **Chống chết tiến trình & tái sử dụng cổng nhanh:** Bỏ qua `SIGPIPE` (`SIG_IGN`) và bật `SO_REUSEADDR`. | Khởi động lại server ngay trên cổng `8088` không dính `Address already in use`. | `UC01`–`UC03`, `UC08`, `UC09` |
| [x] | **NFR-D3** | 🟡 SHOULD | **Thu hồi kết nối treo (Timeout & Heartbeat):** Hỗ trợ `PING`/`PONG` và đặt `SO_RCVTIMEO` (`FSP_SOCKET_TIMEOUT_S = 30s`) để giải phóng kết nối chết/im lặng. | Kết nối socket im lặng quá 30s → server đóng kết nối và thu hồi session (M1.5). | `UC03`, `UC08`, `UC09` |
| [ ] | **NFR-D4** | 🔴 LOẠI | *(Loại bỏ)* Auto-reconnect, Multi-server failover, Uptime 99.9%. | Ngoài phạm vi đồ án mạng LAN 1 server. | — |

---

## NHÓM E — BẢO MẬT & KIỂM SOÁT TRUY CẬP (SECURITY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-E1** | 🟢 MUST | **Cô lập Sandbox 100% (Chống Path Traversal):** Mọi đường dẫn chỉ giới hạn trong `./storage/server_root`, chặn `../`, symlink và kiểm tra `realpath()` trả `403`. | Probe 5: gửi `../../../../etc/passwd`, `cd ../../..` → trả `403` / kẹt ở `/`. | `UC04`–`UC13` |
| [x] | **NFR-E2** | 🟢 MUST | **Kiểm soát truy cập Fail-Closed & Zero-Leak:** Thiếu/sai token trả `401`; sai quyền theo 8 bước `can_access` (0–7) trả `403`; tìm kiếm & tải thư mục không lộ tên/nội dung file `PRIVATE`. | Probe 2 + Probe 3 + test `SEARCH_QUERY` / `FOLDER_DOWNLOAD_REQ`. | `UC02`–`UC13` |
| [x] | **NFR-E3** | 🟢 MUST | **Lưu mật khẩu an toàn:** Không lưu plaintext; dùng SHA-256 + 16 byte salt ngẫu nhiên (`/dev/urandom`), so sánh constant-time. | Kiểm tra `users.json` chỉ lưu `salt_hex` và `password_hash`. | `UC01`, `UC02` |
| [x] | **NFR-E4** | 🟡 SHOULD | **Chống Brute-Force, Hết hạn phiên & Lọc JSON an toàn:** Khóa 5 phút sau 5 lần sai mật khẩu; token TTL 30 phút; từ chối `\u0000`/control char trong JSON. | Chạy `test_auth_permissions` và `test_json`. | `UC01`, `UC02`, `UC12` |
| [ ] | **NFR-E5** | 🔴 LOẠI | *(Loại bỏ)* Mã hóa đường truyền TLS/SSL hoặc mã hóa file AES trên đĩa. | SRS §5 ghi rõ giới hạn không TLS trên LAN. | — |

---

## NHÓM F — TRẢI NGHIỆM NGƯỜI DÙNG & ĐỘ MƯỢT UI (USABILITY & GTK UI)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-F1** | 🟢 MUST | **Giao diện GTK 3 không treo (Non-Blocking):** Mọi I/O mạng chạy trên `pthread` riêng và cập nhật UI duy nhất qua `g_idle_add()`; ngăn Local vẫn cuộn/bấm được khi truyền 500 MB. | Thao tác trên ngăn Local khi đang upload/download file 500 MB trên GTK 3. | `UC04`–`UC13` |
| [x] | **NFR-F2** | 🟢 MUST | **Thông báo lỗi rõ ngữ nghĩa:** Mọi lỗi (`400`, `401`, `403`, `404`, `409`) hiển thị thông điệp tiếng Việt dễ hiểu kèm lý do, không chỉ mã số. | Kiểm tra toàn bộ luồng lỗi trên màn hình Login, Main, Quyền, Tìm kiếm. | `UC01`–`UC13` |
| [x] | **NFR-F3** | 🟢 MUST | **Chống thao tác lặp trên UI:** Nút bấm gọi mạng (`Đăng nhập`, `Đăng ký`, `Lưu quyền`, `Tìm`) bị vô hiệu hóa khi đang chờ phản hồi. | Bấm liên tiếp nút gửi khi server phản hồi chậm → chỉ phát 1 yêu cầu. | `UC01`, `UC02`, `UC06`, `UC12`, `UC13` |
| [x] | **NFR-F4** | 🟡 SHOULD | **Phản hồi tiến trình & nhật ký trực quan:** Hàng đợi truyền cập nhật `%` theo chunk 64 KB; vùng nhật ký hiển thị lệnh gửi/kết quả kiểu FileZilla. | Quan sát thanh tiến trình `0% → 100%` và vùng Nhật ký trên Màn hình chính. | `UC04`–`UC13` |
| [ ] | **NFR-F5** | 🔴 LOẠI | *(Loại bỏ)* Kéo-thả file (Drag & Drop), biểu đồ băng thông real-time, đa ngôn ngữ i18n, Dark Mode. | Ngoài phạm vi bắt buộc để bảo đảm tiến độ M4. | — |

---

## NHÓM G — CHẤT LƯỢNG MÃ, BẢO TRÌ & KIỂM THỬ (MAINTAINABILITY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-G1** | 🟢 MUST | **An toàn bộ nhớ C11 (Zero Leak / Zero UB):** Toàn bộ test headless CTest chạy sạch dưới `-fsanitize=address,undefined` (không áp ASan lên tiến trình GTK3 GUI do singleton của `libgtk`). | `cmake -B build-asan ... && ctest --output-on-failure` (0 leak, 0 UB). | `UC01`–`UC13` |
| [x] | **NFR-G2** | 🟢 MUST | **Chuẩn biên dịch & định dạng nghiêm ngặt:** Biên dịch sạch không cảnh báo với `-std=c11 -Wall -Wextra -Wpedantic` và đạt chuẩn `clang-format`. | GitHub Actions CI (`code-formatting` & `build-and-test`) màu xanh. | `UC01`–`UC13` |
| [x] | **NFR-G3** | 🟢 MUST | **Không phụ thuộc thư viện mạng cấp cao:** 100% tầng mạng chỉ dùng POSIX `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<pthread.h>`. | Kiểm tra `CMakeLists.txt` và `#include` trong `include/` & `src/`. | `UC01`–`UC13` |
| [ ] | **NFR-G4** | 🔴 LOẠI | *(Loại bỏ)* Code Coverage `≥ 90%`, Fuzzing tự động, hoặc ép 0 leak từ thư viện GTK3 nội bộ. | Không phù hợp phạm vi đồ án 2 tuần. | — |

---

## NHÓM H — TRIỂN KHAI & TƯƠNG THÍCH MÔI TRƯỜNG (PORTABILITY & OPERABILITY)

| Chốt | Mã | Mức | Chỉ tiêu NFR chính thức | Cách đo / Kiểm chứng thực tế | Tích hợp vào UC |
|:---:|:-:|:---:|:---|:---|:---|
| [x] | **NFR-H1** | 🟢 MUST | **Build chuẩn hóa trên Linux / WSL2:** Biên dịch và chạy test tái lập được qua CMake ≥ 3.16 trên Ubuntu 20.04/22.04/24.04 (hoặc WSL2). | `cmake -B build -S . && cmake --build build && ctest`. | `UC01`–`UC13` |
| [x] | **NFR-H2** | 🟡 SHOULD | **CLI test client độc lập X11:** Hỗ trợ chạy toàn bộ kịch bản kiểm thử và demo dự phòng qua terminal bằng `fsp_client_cli` (không phụ thuộc GTK/X11). | Chạy script kiểm thử E2E qua `fsp_client_cli`. | `UC01`–`UC13` |
| [ ] | **NFR-H3** | 🔴 LOẠI | *(Loại bỏ)* Cross-platform Native Windows (WinSock2) hoặc Docker Swarm / Kubernetes. | `CMakeLists.txt` chỉ hỗ trợ UNIX/POSIX C11. | — |

---

## MA TRẬN PHÂN BỔ 27 NFR CHÍNH THỨC VÀO 13 USE CASE

| Mã UC | Core NFR áp dụng trực tiếp (🟢 MUST) | Stretch NFR áp dụng (🟡 SHOULD) | Trọng tâm kiểm chứng NFR tại UC |
|:-:|:---|:---|:---|
| **UC01** | `A4, C2, D2, E3, F2, F3, G1–G3, H1` | `A5, C3, E4, H2` | Salt 16B + SHA-256, khóa mutex chống đăng ký trùng, ghi atomic `users.json` + `permissions.json` |
| **UC02** | `A4, C1, C2, D2, E2, E3, F2, F3, G1–G3, H1` | `C3, E4, H2` | Constant-time hash, cấp token 64 hex, lockout 5 lần sai + TTL 30p, đồng thời ≥ 5 client |
| **UC03** | `A4, C2, D1, D2, E2, G1–G3, H1` | `D3, H2` | Hủy token tức thì khi logout hoặc đứt TCP (`cleanup hook`), `SO_RCVTIMEO = 30s` |
| **UC04** | `A4, B2, C2, E1, E2, F1, F2, G1–G3, H1` | `B3, F4, H2` | `realpath()` sandbox, `can_access(READ)`, ẩn `.part`, phản hồi ≤ 1s |
| **UC05** | `A4, C2, E1, E2, F1, F2, G1–G3, H1` | `B3, F4, H2` | Chặn `cd ../../..` thoát `/`, giữ `cwd` khi `403`/`404` |
| **UC06** | `A4, C2, E1, E2, F1, F2, F3, G1–G3, H1` | `A5, B3, F4, H2` | `realpath()` thư mục cha, `can_access(WRITE)`, ghi atomic `permissions.json` |
| **UC07** | `A4, C2, E1, E2, F1, F2, G1–G3, H1` | `A5, B3, F4, H2` | Chỉ xóa thư mục rỗng, đồng bộ đổi tên cây con trong `permissions.json` dưới mutex |
| **UC08** | `A1–A4, B1, B2, C1, C2, D1, D2, E1, E2, F1, F2, G1–G3, H1` | `A5, C3, D3, F4, H2` | Khớp 500 MB, CRC32 chunk 64 KB, `.part` + xóa khi lỗi/đứt mạng, RAM O(1), UI không treo |
| **UC09** | `A1–A4, B1, B2, C1, C2, D1, D2, E1, E2, F1, F2, G1–G3, H1` | `C3, D3, F4, H2` | Khớp 500 MB, RAM O(1), Local pane vẫn cuộn/bấm mượt, `READ` trên chính file |
| **UC10** | `A1–A4, B1, B2, C1, C2, D1, E1, E2, F1, F2, G1–G3, H1` | `A5, F4, H2` | Manifest 2 pha, bỏ symlink, chặn `..` trong `rel_path`, đủ perm cho mọi mục con |
| **UC11** | `A1–A4, B1, B2, C1, C2, D1, E1, E2, F1, F2, G1–G3, H1` | `F4, H2` | Lọc từng mục con theo `can_access(READ)`, Zero-Leak file `PRIVATE`, báo `skipped` |
| **UC12** | `A4, B2, C2, E1, E2, F1, F2, F3, G1–G3, H1` *(+ kế thừa UC09)* | `B3, E4, F4, H2` | Zero-Leak tìm kiếm, giới hạn 100 kết quả, phản hồi ≤ 1s LAN, chạy nền không treo UI |
| **UC13** | `A4, C2, E1, E2, F1, F2, F3, G1–G3, H1` | `A5, B3, C3, F4, H2` | Đủ 8 bước `0–7`, `PRIVATE`+`0666` vẫn `403`, đệ quy an toàn, lưu bền vững `.tmp` |
