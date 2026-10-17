# 🎯 CẨM NANG KIỂM THỬ ĐỐI KHÁNG TRONG LẬP TRÌNH MẠNG SOCKET C/C++
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING (FSP SYSTEM)

> **Mục tiêu:** Tiêu diệt triệt để ảo tưởng "Test xanh 100% nhưng gặp lỗi nghiêm trọng (crash, dính gói, rò rỉ quyền hạn) khi chạy thật".  
> **Nguyên tắc:** Thiết kế test với tư duy "kẻ tấn công" (Adversarial Mindset) – cố tình gửi dữ liệu sai, ngắt mạng bất ngờ, phá vỡ cấu trúc gói tin để kiểm chứng sức bền của hệ thống.

---

## 1. 4 CĂN BỆNH KIỂM THỬ "TỬ HUYỆT" TRONG SOCKET C/C++

| Căn bệnh | Cơ chế gây mù | Hậu quả thực tế khi chạy mạng thật | Cách khắc phục bắt buộc |
| :--- | :--- | :--- | :--- |
| **1. Tautological Testing** *(Test tự xưng)* | Tự khai báo `expected = 0xAA`, gọi hàm giả lập rồi `assert(received == expected)`. Test không chạm vào buffer nhị phân thực tế. | Test luôn xanh trong CI dù hàm bóc tách frame trên socket thật chưa hề được viết hoặc bị sai offset. | **Quy tắc No-Tautology:** Bắt buộc test trên chuỗi byte nhị phân thực tế và con trỏ đệm thật. |
| **2. Happy-Path Monoculture** *(Chỉ test màu hồng)* | Chỉ test gửi 1 chuỗi ngắn 10 bytes qua mạng localhost, 1 client kết nối tuần tự, không có packet loss hay phân mảnh. | Khi gặp mạng chập chờn hoặc truyền file 2GB, gói tin bị dính chùm $\to$ Server đọc sai header $\to$ crash tiến trình. | Bắt buộc chạy **Probe 1 (Sticky Packets)** và **Probe 4 (Corrupted Chunk)**. |
| **3. Memory Corruption Blind Spot** *(Vùng mù bộ nhớ)* | Test chỉ assert kết quả logic đúng nhưng bỏ qua việc kiểm tra tràn mảng bộ đệm (Buffer Overflow) hay rò rỉ heap memory. | Khi có nhiều client kết nối, server hết RAM hoặc bị lỗi bộ nhớ (Segmentation fault / Access Violation). | **Chạy kèm AddressSanitizer (ASan):** Bắt buộc chạy test dưới cờ `-fsanitize=address,undefined` trên Linux/WSL. |
| **4. Deceptive Fallback** *(Che giấu lỗi socket)* | Khi `recv()` trả về 0 hoặc lỗi, dùng fallback gán chuỗi rỗng `""` hoặc bỏ qua mà không ném exception / trả mã lỗi. | Server tiếp tục xử lý rác, ghi file rỗng lên đĩa, phá hỏng dữ liệu gốc của người dùng. | **Fail-Closed Principle:** Khi socket lỗi hoặc chunk sai CRC, bắt buộc hủy phiên, xóa file tạm và trả mã lỗi `400/500`. |

---

## 2. KIẾN TRÚC 3 TẦNG THỰC DỤNG CHO DỰ ÁN SOCKET C++ (DECOUPLED ARCHITECTURE)

Để toàn bộ mã nguồn có thể test độc lập với tốc độ milli-giây mà **không cần phải luôn luôn bật Server mạng thật**, dự án áp dụng kiến trúc phân tầng:

```text
┌────────────────────────────────────────────────────────────────────────┐
│ TẦNG 1: GIAO THỨC & CẤU TRÚC NHỊ PHÂN (PROTOCOL SPECIFICATION)         │
│ • include/common/FSPProtocol.h, Config.h                               │
│ • #pragma pack(1), Frame 42 bytes, Checksum CRC32 IEEE 802.3           │
│ • ZERO Socket I/O: 100% cấu trúc dữ liệu thuần, test siêu tốc          │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│ TẦNG 2: NGHIỆP VỤ HỆ THỐNG ĐỘC LẬP (HEADLESS BUSINESS LOGIC)           │
│ • UserManager, PermissionEngine, VirtualFileSystem, TransferManager   │
│ • Nhận đầu vào là mảng byte hoặc dữ liệu cấu trúc                      │
│ • Kiểm tra Path Traversal, Phân quyền POSIX, Tính toàn vẹn Chunk      │
│ • 100% TESTABLE ĐỘC LẬP KHÔNG CẦN KẾT NỐI MẠNG                         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│ TẦNG 3: TRUYỀN DẪN RAW SOCKET (NETWORK TRANSPORT LAYER)                │
│ • NetworkServer (Winsock / POSIX Socket), NetworkClient, WorkerPool    │
│ • Vòng lặp chống dính gói (Anti-Sticky Packet Loop)                    │
│ • Chịu trách nhiệm đưa đón byte thô giữa Socket và Tầng 2              │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. CHI TIẾT 6 ADVERSARIAL SOCKET PROBES

### 🔹 Probe 1: Sticky Packets & Packet Fragmentation (Chống dính gói & phân mảnh)
* **Bối cảnh:** TCP truyền luồng byte liên tục. Nếu client gửi liên tiếp 2 gói tin (mỗi gói 42 bytes Header + 100 bytes Payload), lệnh `recv()` ở server có thể nhận cả 284 bytes trong 1 lần đọc duy nhất.
* **Kịch bản test đối kháng:**
  1. Nối 2 gói tin hoàn chỉnh `Packet_A` và `Packet_B` vào 1 mảng buffer liên tục.
  2. Truyền buffer này vào hàm xử lý frame của Server.
  3. **Assert:** Hàm bóc tách frame phải bóc được chính xác `Packet_A` (đọc đủ 142 bytes), sau đó con trỏ dịch chuyển đúng vị trí để bóc tiếp `Packet_B`. Không được nuốt mất gói tin thứ 2!

### 🔹 Probe 2: Session Isolation & Impersonation (Cô lập định danh phiên)
* **Bối cảnh:** User A đang đăng nhập có `session_token_A`. User B cố tình dùng token của A hoặc token giả mạo để gọi lệnh tải file.
* **Kịch bản test đối kháng:**
  1. Khởi tạo 2 phiên làm việc cho User A và User B.
  2. Gửi request tạo thư mục mang danh nghĩa User B nhưng đính kèm `session_token_A`.
  3. **Assert:** Server kiểm tra tính hợp lệ giữa socket kết nối và session token, từ chối thực thi và trả về `401 UNAUTHORIZED`.

### 🔹 Probe 3: Linux POSIX Permission Enforcement (Bảo vệ quyền truy cập)
* **Bối cảnh:** User A sở hữu file `/secret.txt`, đặt quyền `0600` (Owner Read/Write, others không có quyền) và mức chia sẻ `PRIVATE`.
* **Kịch bản test đối kháng:**
  1. User B gửi request `FILE_DOWNLOAD_REQ` đến đường dẫn `/secret.txt`.
  2. **Assert:** Hàm `PermissionEngine::canAccess(UserB_id, "/secret.txt", READ)` trả về `false`. Server phản hồi `403 FORBIDDEN`.

### 🔹 Probe 4: Corrupted Chunk & CRC32 Tampering (Kiểm soát toàn vẹn dữ liệu)
* **Bối cảnh:** Trong quá trình truyền file qua mạng, 1 bit trong chunk 64KB bị đảo (bit-flip) do lỗi phần cứng mạng.
* **Kịch bản test đối kháng:**
  1. Tạo 1 khối dữ liệu 64KB hợp lệ kèm `FileChunkHeader` có mã CRC32 chuẩn.
  2. Cố tình thay đổi 1 byte trong dữ liệu payload: `chunk_data[100] ^= 0xFF`.
  3. Đưa khối dữ liệu này vào hàm nhận chunk.
  4. **Assert:** Bộ tính toán CRC32 phát hiện mã checksum không khớp với `chunk_crc` trong header, từ chối ghi vào file trên đĩa và yêu cầu client truyền lại chunk đó.

### 🔹 Probe 5: Path Traversal Attack (Bảo vệ Sandbox lưu trữ)
* **Bối cảnh:** Kẻ tấn công gửi request với tên file: `../../../../windows/system.ini` hoặc `../../../../etc/passwd` nhằm đọc trộm file hệ thống của máy chủ.
* **Kịch bản test đối kháng:**
  1. Gọi hàm chuẩn hóa đường dẫn: `VirtualFileSystem::resolvePath("../../../etc/passwd")`.
  2. **Assert:** Hàm phát hiện đường dẫn vượt ra ngoài `./storage/server_root` $\to$ ném ngoại lệ bảo mật `std::runtime_error` hoặc trả mã lỗi `403/404`.

### 🔹 Probe 6: Concurrency Race & AddressSanitizer Safety (An toàn đa luồng)
* **Bối cảnh:** 2 client đồng thời upload cùng 1 tên file, hoặc 1 client đang tải trong khi 1 client khác xóa file đó.
* **Kịch bản test đối kháng:**
  1. Tạo 2 thread C++ (`std::thread`) chạy song song cùng truy cập vào 1 file ảo trong VFS.
  2. **Assert:** Không xảy ra Race Condition, không bị Deadlock, file không bị hỏng (corrupt).
  3. Chạy kiểm tra với AddressSanitizer: Báo cáo `0 leaks, 0 errors`.

---

## 4. MA TRẬN RANH GIỚI: STRICT VS FLEXIBLE

| Lĩnh vực | Mức độ | Quy chuẩn bắt buộc |
| :--- | :---: | :--- |
| **Kích thước Header FSP** | **STRICT (100%)** | Bắt buộc đúng 42 bytes. Cấm thay đổi cấu trúc `FSPHeader` mà không có sự đồng ý của cả 4 thành viên. |
| **Bảo mật Sandbox VFS** | **STRICT (100%)** | Mọi truy cập file bắt buộc nằm trong `./storage/server_root`. Tuyệt đối cấm lọt ra ngoài. |
| **Phân quyền File** | **STRICT (100%)** | File `PRIVATE` hoặc không có bit `r` thì người khác không bao giờ được tải. |
| **Kiểm tra Checksum** | **STRICT (100%)** | Mọi chunk 64KB phải được verify CRC32. Cấm bỏ qua bước kiểm tra toàn vẹn. |
| **Giao diện Qt GUI (Layout/Màu sắc)**| **FLEXIBLE** | Màu sắc nút bấm, kích thước icon, font chữ có thể tùy biến tự do. |
| **Tốc độ truyền chunk** | **FLEXIBLE** | Buffer size có thể tinh chỉnh giữa 32KB - 128KB tùy điều kiện mạng cụ thể. |
