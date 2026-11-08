# 🤖 AGENTS.MD: HƯỚNG DẪN DÀNH CHO AI AGENTS & DEVELOPERS
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (TOPIC 6 - GROUP 7)

> **Mục đích:** Tài liệu này quy định cách làm việc, nguyên tắc viết mã, chuẩn kiểm thử và ranh giới an toàn cho mọi AI Coding Agent (Cursor, Claude, Copilot, Gemini, ChatGPT...) và 4 thành viên trong dự án.
> **Nguyên tắc tối cao:** Developer là người quyết định (Lead / Gatekeeper). Agent là cộng sự thi công có kiểm soát: không tự ý vượt phạm vi, không sinh code rác, không tạo test đối phó.
> **Nguồn sự thật duy nhất về yêu cầu:** [docs/SRS.md](docs/SRS.md) v3.0. Nếu tài liệu khác mâu thuẫn với SRS, **SRS thắng**.

---

## 1. BỐI CẢNH DỰ ÁN & PHÂN CÔNG

* **Đề tài:** Topic 6 — Ứng dụng chia sẻ file client-server bằng lập trình socket. Phần mềm tham chiếu: **FileZilla** (xem SRS §1.4).
* **Phạm vi:** đồ án môn học, hoàn thành trong **2 tuần**. Ưu tiên theo MoSCoW trong SRS: làm hết **MUST** trước, chỉ làm **COULD** khi MUST đã xong và có test.
* **Công nghệ:** **C11**, POSIX TCP socket (`<sys/socket.h>`), `pthread`, GTK 3, CMake 3.16+. Server & client chạy trên Linux/WSL.
* **Giao thức:** FSP — header cố định **42 byte**, `FileChunkHeader` cố định **88 byte** ([include/common/FSPProtocol.h](include/common/FSPProtocol.h)).
* **Phân công:**
  * **Trần Sỹ Nguyên (M1 - 20235985)** — `feat/m1-network-engine`: socket, `send_exact`/`recv_exact`, frame 42 byte + CRC32, server 1 thread/client, dispatcher, PING/PONG.
  * **Bùi Đức Hiếu (M2 - 20235931)** — `feat/m2-auth-permissions`: đăng ký/đăng nhập (salted SHA-256), phiên (token), phân quyền `can_access` (SRS FR-6), `PERM_GET`/`PERM_SET`.
  * **Vũ Nhật Minh Dương (M3 - 20235925)** — `feat/m3-filesystem-transfer`: VFS + chặn path traversal, lệnh kiểu FTP, truyền file 64 KB, truyền thư mục (manifest), tìm kiếm lọc theo quyền.
  * **Hà Đức Dương (M4 - 20235923)** — `feat/m4-gui-client`: GUI GTK (luồng GTK + luồng mạng qua `g_idle_add`), CLI client để test, tích hợp.

---

## 2. NGUYÊN TẮC "CẤM TUYỆT ĐỐI" (ZERO-TOLERANCE RULES)

1. ⛔ **CẤM THƯ VIỆN MẠNG CẤP CAO**
   * Không dùng Boost.Asio, libcurl, libuv, POCO, ZeroMQ, socket.io hay tương tự.
   * Mã mạng chỉ dùng `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<pthread.h>`.
2. ⛔ **CẤM TEST GIẢ / TEST ĐỐI PHÓ**
   * Không viết `assert(1)`, không mock hàm luôn trả về thành công, không tự viết lại logic cần test bên trong file test rồi so sánh với chính nó.
   * Mọi assertion phải kiểm tra **dữ liệu thật**: byte nhận được, file trên đĩa, CRC32 tính thật, mã trạng thái do **hàm thật của module** trả về.
3. ⛔ **CẤM PHÁ HỢP ĐỒNG GIAO THỨC (`FSPProtocol.h`)**
   * `FSPHeader` đúng **42 byte**, `FileChunkHeader` đúng **88 byte**, bảo vệ bằng `#pragma pack(1)` / `__attribute__((packed))` và `_Static_assert`.
   * Không thêm/bớt/đổi trường, đổi giá trị hằng hoặc tên sự kiện nếu chưa có sự đồng ý của **cả 4 thành viên**.
4. ⛔ **CẤM NUỐT LỖI (FAIL-CLOSED)**
   * `recv()`/`send()` trả về `<= 0` (trừ `EINTR` được thử lại) → đóng kết nối, giải phóng phiên và tài nguyên.
   * Chunk sai CRC32 → hủy phiên truyền, xóa file `.part`, trả lỗi. Không âm thầm bỏ qua.
   * Không có quyền / token sai → trả `403` / `401`. Không có nhánh "tạm bỏ qua kiểm tra quyền".
5. ⛔ **CẤM VƯỢT SANDBOX (PATH TRAVERSAL)**
   * Mọi đường dẫn từ client là đường dẫn ảo; server chuẩn hóa (`realpath()` trên đường dẫn đã tồn tại hoặc thư mục cha) và kiểm tra tiền tố với `./storage/server_root`.
   * Không bao giờ truy cập ra ngoài `./storage/server_root`.
6. ⛔ **CẤM PHÌNH PHẠM VI (SCOPE CREEP)**
   * Không thêm tính năng, thư viện, tầng kiến trúc ngoài SRS (ví dụ: web client, TLS, thread pool, regex search, resume) khi các mục MUST chưa xong.
   * Không đưa vào tài liệu các con số/khẳng định chưa được đo hoặc kiểm chứng.

---

## 3. CHUẨN KIỂM THỬ (TESTING STANDARDS)

> TDD (viết test trước) là **tùy chọn**. Nhưng **chất lượng test là bắt buộc 100%** — xem [docs/TDD/README.md](docs/TDD/README.md).

Mỗi tính năng phải được bao phủ bởi **6 Probe** sau (chi tiết: [docs/TDD/ADVERSARIAL-TDD-GUIDE.md](docs/TDD/ADVERSARIAL-TDD-GUIDE.md)):

| Probe | Nội dung | Phụ trách |
|:-:|:---|:-:|
| 1 | **Dính gói / tách gói:** 2 frame trong 1 buffer tách đúng; 1 frame đến thành nhiều mảnh ghép đúng | M1 |
| 2 | **Phiên:** không token, token giả, token đã logout → `401` | M2 |
| 3 | **Phân quyền:** mỗi bước 0–7 của `can_access` có test; file `PRIVATE` có bit `0666` vẫn từ chối người khác (`403`) | M2 |
| 4 | **Chunk hỏng:** lật 1 bit trong chunk → phát hiện sai CRC32, không còn file `.part` | M3 |
| 5 | **Path traversal:** `../../../../etc/passwd`, `cd ../../..` không thoát khỏi `/` ảo | M3 |
| 6 | **Bộ nhớ & đồng thời:** test chạy sạch dưới ASan + UBSan; ≥ 5 client đồng thời hoạt động đúng | Cả nhóm |

Kiểm thử end-to-end bắt buộc trước demo: bảng truy vết SRS §9.

---

## 4. QUY TRÌNH LÀM VIỆC CỦA AGENT

1. **Xác định module & nhánh** (M1–M4) trước khi sinh mã; chỉ sửa file thuộc phạm vi đó, trừ khi được yêu cầu tích hợp.
2. **Đối chiếu SRS** (mã FR/NFR, mức ưu tiên). Nếu yêu cầu của thành viên vượt SRS → hỏi lại trước khi làm.
3. **Micro-plan** cho tính năng phức tạp (< 60 dòng, mẫu [docs/TDD/templates/micro-plan-template.md](docs/TDD/templates/micro-plan-template.md)) để thành viên duyệt.
4. **C11 sạch:**
   * Quyền sở hữu con trỏ rõ ràng; mọi nhánh lỗi (`goto cleanup`) giải phóng `malloc` và đóng `fd`/`FILE*`.
   * Truyền file bằng **bộ đệm cố định 64 KB**, không đọc cả file vào RAM.
   * Dữ liệu dùng chung giữa các thread (bảng user, phiên, quyền) bảo vệ bằng `pthread_mutex_t` (hoặc `pthread_rwlock_t`), khóa theo một thứ tự cố định để tránh deadlock.
   * Dùng `snprintf`/`memcpy` có kiểm tra độ dài; kiểm tra `payload_len` trước khi cấp phát/đọc.
5. **Giải thích được:** mã sinh ra phải đủ đơn giản để thành viên phụ trách tự giải thích trước giảng viên (xem [docs/LEARNING-ROADMAP.md](docs/LEARNING-ROADMAP.md)). Tránh kỹ thuật "khoe" không cần thiết.
6. **Format & build:** tuân thủ `.clang-format`; cập nhật `CMakeLists.txt` khi thêm file nguồn/test.

---

## 5. LỆNH THƯỜNG DÙNG

```bash
# 1. Format code:
find include src tests -type f \( -name "*.h" -o -name "*.c" \) -exec clang-format -i {} +

# 2. Build & chạy test:
cmake -B build -S .
cmake --build build --parallel
cd build && ctest --output-on-failure

# 3. Kiểm tra bộ nhớ (Linux / WSL):
cmake -B build-asan -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined -g"
cmake --build build-asan --parallel
cd build-asan && ctest --output-on-failure
```

---

## 6. TÀI LIỆU CẦN ĐỐI CHIẾU
* 📄 **SRS (yêu cầu — nguồn sự thật):** [docs/SRS.md](docs/SRS.md)
* 🎓 **Lộ trình kiến thức & kế hoạch 2 tuần:** [docs/LEARNING-ROADMAP.md](docs/LEARNING-ROADMAP.md)
* 📘 **SDD (thiết kế):** [docs/SDD.md](docs/SDD.md)
* 📋 **WBS (phân công & checklist):** [docs/WBS.md](docs/WBS.md)
* 🔄 **Chuyển C++ → C11:** [docs/C-MIGRATION-PLAN.md](docs/C-MIGRATION-PLAN.md)
* 🚀 **CI/CD:** [docs/CICD.md](docs/CICD.md)
* 🛡️ **Chuẩn kiểm thử:** [docs/TDD/README.md](docs/TDD/README.md)
* ⚙️ **Hợp đồng giao thức:** [include/common/FSPProtocol.h](include/common/FSPProtocol.h)
