# 🎯 CẨM NANG KIỂM THỬ ĐỐI KHÁNG TRONG LẬP TRÌNH MẠNG SOCKET C11
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING (FSP SYSTEM - GROUP 7)

> **Mục tiêu:** Tiêu diệt triệt để ảo tưởng "Test xanh 100% nhưng gặp lỗi nghiêm trọng (crash, dính gói, tràn bộ nhớ, rò rỉ quyền hạn) khi chạy thật".  
> **Nguyên tắc:** Thiết kế test với tư duy "kẻ tấn công" (Adversarial Mindset) – cố tình gửi dữ liệu sai, ngắt mạng bất ngờ, phá vỡ cấu trúc gói tin để kiểm chứng sức bền của hệ thống C11.

---

## 1. 4 CĂN BỆNH KIỂM THỬ "TỬ HUYỆT" TRONG SOCKET C

| Căn bệnh | Cơ chế gây mù | Hậu quả thực tế khi chạy mạng thật | Cách khắc phục bắt buộc |
| :--- | :--- | :--- | :--- |
| **1. Tautological Testing** *(Test tự xưng)* | Tự khai báo `expected = 0xAA`, gọi hàm giả lập rồi `assert(received == expected)`. Test không chạm vào buffer nhị phân thực tế. | Test luôn xanh trong CI dù hàm bóc tách frame trên socket thật chưa hề được viết hoặc bị sai offset. | **Quy tắc No-Tautology:** Bắt buộc test trên chuỗi byte nhị phân thực tế và con trỏ đệm thật. |
| **2. Happy-Path Monoculture** *(Chỉ test màu hồng)* | Chỉ test gửi 1 chuỗi ngắn 10 bytes qua mạng localhost, 1 client kết nối tuần tự, không có packet loss hay phân mảnh. | Khi gặp mạng chập chờn hoặc truyền file lớn (ví dụ 500 MB), gói tin bị dính chùm $\to$ Server đọc sai header $\to$ crash tiến trình. | Bắt buộc chạy **Probe 1 (Sticky Packets)** và **Probe 4 (Corrupted Chunk)**. |
| **3. Memory Corruption Blind Spot** *(Vùng mù bộ nhớ)* | Test chỉ assert kết quả logic đúng nhưng bỏ qua việc kiểm tra tràn mảng bộ đệm (Buffer Overflow) hay rò rỉ heap memory (`malloc` quên `free`). | Khi có nhiều client kết nối đồng thời, server hết RAM hoặc bị lỗi bộ nhớ (`SIGSEGV`). | **Chạy kèm AddressSanitizer (ASan) & UBSan:** Bắt buộc chạy test dưới cờ `-fsanitize=address,undefined`. |
| **4. Deceptive Fallback** *(Che giấu lỗi socket)* | Khi `recv()` trả về 0 hoặc lỗi, dùng fallback gán chuỗi rỗng `""` hoặc bỏ qua mà không trả mã lỗi. | Server tiếp tục xử lý rác, ghi file rỗng lên đĩa, phá hỏng dữ liệu gốc của người dùng. | **Fail-Closed Principle:** Khi socket lỗi hoặc chunk sai CRC, bắt buộc hủy phiên, xóa file tạm `.part` và trả mã lỗi `400/500`. |

---

## 2. KIẾN TRÚC 3 TẦNG THỰC DỤNG CHO DỰ ÁN SOCKET C11 (DECOUPLED ARCHITECTURE)

```text
┌────────────────────────────────────────────────────────────────────────┐
│ TẦNG 1: GIAO THỨC & CẤU TRÚC NHỊ PHÂN (PROTOCOL SPECIFICATION)         │
│ • include/common/FSPProtocol.h, Config.h                               │
│ • #pragma pack(1) / __attribute__((packed)), Frame 42B, Chunk 88B      │
│ • ZERO Socket I/O: 100% cấu trúc dữ liệu thuần, test siêu tốc          │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│ TẦNG 2: NGHIỆP VỤ HỆ THỐNG ĐỘC LẬP (HEADLESS BUSINESS LOGIC IN C11)    │
│ • UserManager, SessionManager, PermissionEngine, VFS, TransferManager  │
│ • Nhận đầu vào là mảng byte hoặc struct C                              │
│ • Kiểm tra Path Traversal (realpath), Phân quyền POSIX+ACL, Chunk CRC  │
│ • 100% TESTABLE ĐỘC LẬP KHÔNG CẦN MỞ CỔNG MẠNG                         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
┌───────────────────────────────────▼────────────────────────────────────┐
│ TẦNG 3: TRUYỀN DẪN RAW SOCKET (POSIX NETWORK TRANSPORT LAYER)          │
│ • Socket (<sys/socket.h>), PacketHandler (readExact/sendExact)         │
│ • pthread Concurrency (1 thread / client), Event Dispatcher             │
│ • Chịu trách nhiệm đưa đón byte thô giữa Socket và Tầng 2              │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. CHI TIẾT 6 ADVERSARIAL SOCKET PROBES

1. **Probe 1 (Sticky Packets & Fragmentation):** Ghép 2 gói tin hoàn chỉnh vào 1 mảng buffer liên tục và kiểm tra `unpackFromBuffer` tách chính xác cả 2 gói; cắt 1 gói thành nhiều mảnh để kiểm tra `readExact`.
2. **Probe 2 (Session Isolation & Brute-Force Lockout):** Thử dùng token giả mạo, token đã logout, token quá hạn 30 phút, hoặc đăng nhập sai 5 lần liên tiếp $\to$ bắt buộc trả về `401 UNAUTHORIZED`.
3. **Probe 3 (Linux POSIX + Sharing Level Enforcement):** Kiểm tra đầy đủ bảng bước 0–7 (SRS FR-6); file đặt mức `PRIVATE` với bit `0666` vẫn phải chặn đứng mọi người dùng khác ngoài Owner/Admin (`403 FORBIDDEN`).
4. **Probe 4 (Corrupted Chunk & CRC32 Tampering):** Lật 1 bit trong khối dữ liệu $64\text{ KB}$ (`chunk_data[100] ^= 0xFF`) $\to$ phát hiện lệch `chunk_crc`, hủy ghi đĩa và xóa file `.part`.
5. **Probe 5 (Path Traversal Attack):** Gửi đường dẫn `../../../../etc/passwd` $\to$ `realpath()` phát hiện thoát khỏi `./storage/server_root` và từ chối (`403 FORBIDDEN`).
6. **Probe 6 (Concurrency & ASan/UBSan Safety):** Chạy đồng thời ≥ 5 client `pthread` đăng ký, đăng nhập, phân quyền và truyền file dưới ASan/UBSan với kết quả **0 leaks, 0 errors, 0 races**.
