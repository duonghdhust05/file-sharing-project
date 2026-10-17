# 🤖 AGENTS.MD: HƯỚNG DẪN DÀNH CHO AI AGENTS & DEVELOPERS
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (TOPIC 6)

> **Mục đích:** Tài liệu này là "Hiến pháp kỹ thuật" (Engineering Constitution) quy định cách thức làm việc, nguyên tắc thiết kế mã nguồn, quy chuẩn kiểm thử và ranh giới an toàn cho tất cả các AI Coding Agents (Cursor, Claude, Copilot, Gemini, ChatGPT...) và 4 thành viên trong dự án.  
> **Nguyên tắc tối cao:** Developer là Kiến trúc sư trưởng / Người gác cổng (Lead Architect / Gatekeeper). Agent đóng vai trò là cộng sự thi công có kiểm soát. Tuyệt đối không tự ý quyết định vượt quyền, không sinh code rác, không tạo test đối phó.

---

## 1. BỐI CẢNH DỰ ÁN & PHÂN CÔNG THÀNH VIÊN

* **Đề tài:** Topic 6 - Xây dựng ứng dụng chia sẻ file Client-Server bằng Lập trình Socket C/C++.
* **Công nghệ cốt lõi:** C++17, Raw TCP Sockets (Winsock2 trên Windows, POSIX trên Linux), CMake 3.16+.
* **Giao thức độc quyền:** FSP (File Sharing Protocol) với Frame cố định 42 bytes (`include/common/FSPProtocol.h`).
* **Phân công 4 thành viên độc lập:**
  * **Trần Sỹ Nguyên (M1)**: Phụ trách `feat/m1-network-engine` (Raw TCP Socket, Framing 42-byte, Anti-Sticky Loop, Server ThreadPool).
  * **Bùi Đức Hiếu (M2)**: Phụ trách `feat/m2-auth-permissions` (Quản lý User, Salted SHA-256, Session Token, Phân quyền Linux POSIX $rwx$, ACL).
  * **Vũ Nhật Minh Dương (M3)**: Phụ trách `feat/m3-filesystem-transfer` (Virtual Sandbox, Lệnh FTP, Chunking 64KB, Truyền Folder đệ quy, Search Engine).
  * **Hà Đức Dương (M4)**: Phụ trách `feat/m4-gui-client` (Desktop Qt6 Client, Multi-threading UI, Bảng tiến trình, Mở rộng Web TypeScript).

---

## 2. NGUYÊN TẮC "CẤM TUYỆT ĐỐI" (ZERO-TOLERANCE RULES)

Khi hỗ trợ lập trình, AI Agent bắt buộc phải tuân thủ nghiêm ngặt 5 điều cấm sau:

1. ⛔ **CẤM SỬ DỤNG FRAMEWORK MẠNG CẤP CAO**:
   * Tuyệt đối không dùng Boost.Asio, libcurl, POCO, socket.io library hay bất kỳ thư viện mạng cấp cao nào.
   * Toàn bộ mã nguồn mạng bắt buộc sử dụng **Raw Socket nguyên thủy**: `<winsock2.h>` / `ws2_32.lib` (Windows) và `<sys/socket.h>` / `pthread` (Linux).
2. ⛔ **CẤM SINH TEST GIẢ TẠO / TEST ĐỐI PHÓ (NO FAKE / TAUTOLOGICAL TESTS)**:
   * Nghiêm cấm viết test kiểu `ASSERT_TRUE(true)` hoặc mock một hàm luôn trả về `true` chỉ để test xanh qua mặt CI.
   * Mọi assertion bắt buộc phải kiểm tra trên **dữ liệu thực tế** (mảng byte nhận được, file ghi trên đĩa, mã băm SHA-256 / CRC32 tính toán thực tế).
3. ⛔ **CẤM PHÁ VỠ HỢP ĐỒNG GIAO THỨC (`FSPProtocol.h`)**:
   * Kích thước `FSPHeader` bắt buộc đúng **chính xác 42 bytes** (bảo vệ bằng `#pragma pack(1)` và `static_assert`).
   * Kích thước `FileChunkHeader` bắt buộc đúng **chính xác 88 bytes**.
   * Không được tự ý thêm/bớt trường trong Header nếu không có sự đồng ý của cả 4 thành viên.
4. ⛔ **CẤM NUỐT LỖI MẠNG & LỖI BỘ NHỚ (FAIL-CLOSED PRINCIPLE)**:
   * Khi `recv()` hoặc `send()` trả về `<= 0`, bắt buộc phải ngắt kết nối và giải phóng tài nguyên.
   * Khi chunk dữ liệu không khớp CRC32, bắt buộc phải hủy ghi đĩa và thông báo lỗi. Cấm nuốt lỗi âm thầm.
5. ⛔ **CẤM VI PHẠM SANDBOX LƯU TRỮ (PATH TRAVERSAL)**:
   * Mọi đường dẫn file từ client gửi lên bắt buộc phải được chuẩn hóa qua `std::filesystem::weakly_canonical`.
   * Tuyệt đối không cho phép truy cập ra ngoài `./storage/server_root`.

---

## 3. QUY CHUẨN KIỂM THỬ THỰC CHIẾN (TESTING STANDARDS)

> 💡 **Lưu ý quan trọng:** Quy trình TDD (viết test trước) là **tùy chọn**, thành viên có thể chọn TDD hoặc viết code trước rồi viết test sau. Tuy nhiên, **quy chuẩn chất lượng của bài test là bắt buộc 100%**.

Khi sinh test hoặc bổ sung tính năng mới, Agent phải đảm bảo bao phủ **Ma trận 6 Probes Đối Kháng** (xem chi tiết tại `docs/TDD/ADVERSARIAL-TDD-GUIDE.md`):
* **Probe 1 (Sticky Packets):** Kiểm thử khả năng tách 2 frame dính chùm trong 1 buffer TCP.
* **Probe 2 (Session Isolation):** Kiểm thử từ chối token giả mạo hoặc token đã logout (`401 UNAUTHORIZED`).
* **Probe 3 (POSIX Permissions):** Kiểm thử từ chối user không có quyền truy cập file `PRIVATE` (`403 FORBIDDEN`).
* **Probe 4 (Corrupted Chunk):** Kiểm thử phát hiện lệch CRC32 khi dữ liệu bị đổi 1 bit.
* **Probe 5 (Path Traversal):** Kiểm thử chặn đứng các chuỗi `../../../../etc/passwd`.
* **Probe 6 (Memory Safety):** Chạy kiểm thử sạch lỗi bộ nhớ dưới **AddressSanitizer (ASan)**.

---

## 4. QUY TRÌNH PHỐI HỢP KHI LẬP TRÌNH (AGENT WORKFLOW)

Mỗi khi nhận yêu cầu hỗ trợ từ một thành viên:
1. **Xác định đúng nhánh làm việc**: Kiểm tra thành viên đang làm module nào (M1, M2, M3, hay M4) để sinh code vào đúng phạm vi trách nhiệm.
2. **Khuyến nghị lập Kế hoạch Vi mô (Micro-Plan)**: Đối với các tính năng phức tạp, Agent nên phác thảo 1 bản Micro-Plan ngắn gọn (< 80 dòng theo mẫu `docs/TDD/templates/micro-plan-template.md`) để thành viên duyệt trước khi sinh mã nguồn.
3. **Tuân thủ Clean C++17**:
   * Dùng RAII (`std::unique_ptr`, `std::vector`, `std::string`) để tự động quản lý bộ nhớ.
   * Hạn chế tối đa con trỏ thô (raw pointer) và `malloc/free`.
   * Sử dụng `std::filesystem` cho mọi thao tác tệp/thư mục.
4. **Kiểm tra Formatting & Build**:
   * Đảm bảo code tuân thủ `.clang-format`.
   * Đảm bảo cập nhật `CMakeLists.txt` khi thêm file mã nguồn hoặc file test mới.

---

## 5. CÁC LỆNH KIỂM THỬ VÀ ĐIỀU HÀNH MÔI TRƯỜNG

```bash
# 1. Format code tự động theo chuẩn nhóm:
find include src tests -type f \( -name "*.h" -o -name "*.cpp" \) -exec clang-format -i {} +

# 2. Biên dịch và chạy nhanh Unit Test C++ trên máy:
g++ -std=c++17 -Iinclude tests/test_protocol.cpp -lws2_32 -o run_tests.exe
./run_tests.exe

# 3. Chạy toàn bộ test qua CMake & CTest:
cd build && ctest --output-on-failure -C Release

# 4. Kiểm tra an toàn bộ nhớ bằng AddressSanitizer (Linux / WSL):
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build
cd build && ctest --output-on-failure
```

---

## 6. DANH MỤC TÀI LIỆU CẦN ĐỐI CHIẾU
* 📄 **SRS (Yêu cầu nghiệp vụ)**: [docs/SRS.md](docs/SRS.md)
* 📋 **WBS (Phân công công việc)**: [docs/WBS.md](docs/WBS.md)
* 📘 **SDD (Bản thiết kế phần mềm)**: [docs/SDD.md](docs/SDD.md)
* 🚀 **CI/CD Pipeline Design**: [docs/CICD.md](docs/CICD.md)
* 🛡️ **TDD & Testing Standards**: [docs/TDD/README.md](docs/TDD/README.md)
* ⚙️ **Unified Protocol Header**: [include/common/FSPProtocol.h](include/common/FSPProtocol.h)
