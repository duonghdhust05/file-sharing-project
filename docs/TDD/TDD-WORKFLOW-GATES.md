# 🚦 QUY TRÌNH KIỂM THỬ GIÁM SÁT & CÁC CHỐT CHẶN QUYẾT ĐỊNH (DECISION GATES)
## ÁP DỤNG CHO DỰ ÁN LẬP TRÌNH SOCKET C11 (TOPIC 6 - GROUP 7)

> **Tính chất:** Quy trình phát triển Tùy chọn (Optional Development Workflow) cho thành viên muốn tăng tốc độ lập trình và bảo đảm chất lượng code C11 ở mức tối đa.  
> **Nguyên tắc:** Lập trình viên là Kiến trúc sư trưởng / Người gác cổng (Lead Architect / Gatekeeper). Không nhảy vào viết code lan man khi chưa xác định rõ ranh giới bộ nhớ, hợp đồng gói tin và kịch bản lỗi.

---

## 1. CHU TRÌNH 7 BƯỚC & 5 CHỐT CHẶN (DECISION GATES)

```text
[BƯỚC 1: MICRO-PLAN] ──> 🛑 GATE 1: Duyệt Kế hoạch Vi mô (< 80 dòng)
         │
[BƯỚC 2: RED TEST]   ──> 🛑 GATE 2: Xác nhận Test ĐỎ chuẩn xác (Fail đúng lý do)
         │
[BƯỚC 3: MIN GREEN]  ──> Triển khai C11 vừa đủ để Test XANH (Tuân thủ FSPProtocol)
         │
[BƯỚC 4: AUDIT]      ──> 🛑 GATE 3: Soi Diff 60 Giây (4 Bẫy chết người trong C)
         │
[BƯỚC 5: VERIFY]     ──> 🛑 GATE 4: Nghiệm thu thực chứng (Trinity Evidence)
         │
[BƯỚC 6 & 7: MERGE]  ──> 🛑 GATE 5: Đồng bộ Checkpoint & Khóa Regression vào CTest
```

---

## 2. CHI TIẾT TỪNG CHỐT CHẶN TRONG LẬP TRÌNH MẠNG C11

### 🛑 GATE 1: Phê Duyệt Kế Hoạch Vi Mô (1-Page Micro-Plan Approval)
* **Quy chuẩn Micro-Plan:** Bắt buộc **DƯỚI 60–80 DÒNG** (dùng mẫu `docs/TDD/templates/micro-plan-template.md`). Gồm 4 khối nội dung:
  1. **Ranh giới Bất biến (Strict Bounds):** Khẳng định vùng cấm (buffer bounds, không thoát khỏi `./storage/server_root`, bắt buộc kiểm tra `canAccess`, giải phóng `free()`/`close()`).
  2. **Tập tin tác động (Impacted Files):** Giới hạn tối đa 3–4 files (Header `.h`, Source `.c`, Test file).
  3. **Ma trận Probes đối kháng:** Nêu rõ Input dị dạng $\to$ Output mong đợi khi test thất bại.
  4. **Lệnh duyệt từ Lead/Dev.**

---

### 🛑 GATE 2: Xác Nhận Test ĐỎ Chuẩn Xác (Red Test Verification)
* **Quy tắc bất biến:**
  - Viết test case đối kháng (ví dụ: gửi payload path traversal `../../` hoặc truyền chunk sai CRC).
  - Chạy test trong terminal $\to$ Test **BẮT BUỘC PHẢI THẤT BẠI (RED)**.
  - Kiểm tra log terminal: Mã lỗi trả về phải đúng với quy ước giao thức (`403 FORBIDDEN` hoặc `400 BAD_REQUEST`).
* **CẤM:** Không viết assertion vô nghĩa kiểu `ASSERT_TRUE(true)` chỉ để test chạy qua!

---

### 🟢 BƯỚC 3: TRIỂN KHAI MÃ NGUỒN (Minimum Green Rule)
* Chỉ viết lượng code C11 vừa đủ để biến các Test Đỏ thành **XANH (GREEN)**.
* Tuân thủ cấu trúc dữ liệu trong `include/common/FSPProtocol.h`.
* Sử dụng POSIX `realpath()` an toàn, quản lý bộ nhớ tường minh (`goto cleanup`), đóng mọi file descriptor và giải phóng mọi con trỏ heap trên tất cả nhánh lỗi.

---

### 🛑 GATE 3: Soi Diff 60 Giây & Kiểm Tra 4 Bẫy Chết Người trong C

| Bẫy chết người | Hiện tượng nguy hiểm | Cách phát hiện & Khắc phục |
| :--- | :--- | :--- |
| **1. Memory Leak & Dangling Pointer** | Gọi `malloc`/`calloc`/`fopen`/`socket` nhưng quên `free`/`fclose`/`close` khi nhảy vào nhánh `return` lỗi sớm. | Áp dụng mẫu `goto cleanup` chuẩn trong C và chạy kiểm tra bằng AddressSanitizer (ASan). |
| **2. Buffer Overflow trên Socket / String** | Đọc từ socket vượt quá kích thước mảng, hoặc dùng `strcpy`/`sprintf` không giới hạn độ dài. | Bắt buộc kiểm tra `payload_len <= BUFFER_SIZE`; dùng `snprintf`, `memcpy` có kiểm tra biên. |
| **3. Fake Return / Hardcode** | Viết code đối phó kiểu `bool can_access(...) { return true; }` để test xanh nhanh. | Loại bỏ ngay lập tức, viết logic kiểm tra bitmask 9-bit và ACL thật sự. |
| **4. Nuốt Lỗi Socket (Silenced Errors)**| Gọi `recv()` hoặc `send()` trả về `<= 0` nhưng bỏ qua không ngắt kết nối. | Phải đóng socket và thu hồi phiên làm việc ngay khi phát hiện lỗi I/O. |

---

### 🛑 GATE 4: Nghiệm Thu Thực Chứng (The Trinity Evidence)
Một tính năng chỉ được chấp thuận nghiệm thu khi xuất trình đủ **Bộ Ba Bằng Chứng Thực Tế**:
1. **Network / Socket Evidence:** Log thông điệp FSP Frame 42 bytes cho thấy packet gửi/nhận đúng cấu trúc, không bị gộp/dính gói.
2. **Storage Evidence:** File upload/download xong có dung lượng và mã hash SHA-256 trùng khớp 100% với file gốc; file `PRIVATE` không bị lộ.
3. **Memory / Sanitizer Evidence:** Chạy qua `ctest` với cờ `-fsanitize=address,undefined` đạt kết quả **0 error, 0 leak**.
