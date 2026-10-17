# 🚦 QUY TRÌNH KIỂM THỬ GIÁM SÁT & CÁC CHỐT CHẶN QUYẾT ĐỊNH (DECISION GATES)
## ÁP DỤNG CHO DỰ ÁN LẬP TRÌNH SOCKET C/C++ (TOPIC 6)

> **Tính chất:** Quy trình phát triển Tùy chọn (Optional Development Workflow) cho thành viên muốn tăng tốc độ lập trình và bảo đảm chất lượng code ở mức tối đa.  
> **Nguyên tắc:** Lập trình viên là Kiến trúc sư trưởng / Người gác cổng (Lead Architect / Gatekeeper). Không nhảy vào viết code lan man khi chưa xác định rõ ranh giới bộ nhớ, hợp đồng gói tin và kịch bản lỗi.

---

## 1. CHU TRÌNH 7 BƯỚC & 5 CHỐT CHẶN (DECISION GATES)

```text
[BƯỚC 1: MICRO-PLAN] ──> 🛑 GATE 1: Duyệt Kế hoạch Vi mô (< 80 dòng)
         │
[BƯỚC 2: RED TEST]   ──> 🛑 GATE 2: Xác nhận Test ĐỎ chuẩn xác (Fail đúng lý do)
         │
[BƯỚC 3: MIN GREEN]  ──> Triển khai C++ vừa đủ để Test XANH (Tuân thủ FSPProtocol)
         │
[BƯỚC 4: AUDIT]      ──> 🛑 GATE 3: Soi Diff 60 Giây (4 Bẫy chết người C++)
         │
[BƯỚC 5: VERIFY]     ──> 🛑 GATE 4: Nghiệm thu thực chứng (Trinity Evidence)
         │
[BƯỚC 6 & 7: MERGE]  ──> 🛑 GATE 5: Đồng bộ Checkpoint & Khóa Regression vào CTest
```

---

## 2. CHI TIẾT TỪNG CHỐT CHẶN TRONG LẬP TRÌNH MẠNG C++

### 🛑 GATE 1: Phê Duyệt Kế Hoạch Vi Mô (1-Page Micro-Plan Approval)
* **Mục tiêu:** Chặn đứng tình trạng viết code không định hướng, sửa lan man gây hỏng socket của các thành viên khác.
* **Quy chuẩn Micro-Plan:** Bắt buộc **DƯỚI 60–80 DÒNG** (dùng mẫu `docs/TDD/templates/micro-plan-template.md`). Gồm 4 khối nội dung:
  1. **Ranh giới Bất biến (Strict Bounds):** Khẳng định vùng cấm (ví dụ: buffer bounds, không thoát khỏi `./storage/server_root`, bắt buộc kiểm tra `canAccess`).
  2. **Tập tin tác động (Impacted Files):** Giới hạn tối đa 3–4 files (Header, Source, Test file).
  3. **Ma trận Probes đối kháng:** Nêu rõ Input dị dạng $\to$ Output mong đợi khi test thất bại.
  4. **Lệnh duyệt từ Lead/Dev:** Chỉ bắt đầu viết test khi Micro-plan đã được duyệt.

---

### 🛑 GATE 2: Xác Nhận Test ĐỎ Chuẩn Xác (Red Test Verification)
* **Mục tiêu:** Bảo đảm test được viết TRƯỚC hoặc ĐỒNG THỜI với logic, và test thực sự thất bại vì chưa có code xử lý, chứ không phải lỗi cú pháp.
* **Quy tắc bất biến:**
  - Viết test case đối kháng (ví dụ: gửi payload path traversal `../../` hoặc truyền chunk sai CRC).
  - Chạy test trong terminal $\to$ Test **BẮT BUỘC PHẢI THẤT BẠI (RED)**.
  - Kiểm tra log terminal: Mã lỗi ném ra phải đúng với quy ước giao thức (`403 FORBIDDEN` hoặc `400 BAD_REQUEST`).
* **CẤM:** Không viết assertion vô nghĩa kiểu `ASSERT_TRUE(true)` chỉ để test chạy qua!

---

### 🟢 BƯỚC 3: TRIỂN KHAI MÃ NGUỒN (Minimum Green Rule)
* Chỉ viết lượng code C++ vừa đủ để biến các Test Đỏ thành **XANH (GREEN)**.
* Tuân thủ cấu trúc dữ liệu trong `include/common/FSPProtocol.h`.
* Tuyệt đối không thêm tính năng suy đoán ngoài yêu cầu của Micro-Plan.
* Sử dụng `std::filesystem` an toàn, quản lý bộ nhớ tự động bằng RAII (`std::unique_ptr`, `std::vector`), hạn chế cấp phát thô `new/delete`.

---

### 🛑 GATE 3: Soi Diff 60 Giây & Kiểm Tra 4 Bẫy Chết Người C++
Trước khi commit hoặc mở Pull Request, thành viên phải tự soi diff mã nguồn qua **4 Bẫy Chết Người**:

| Bẫy chết người | Hiện tượng nguy hiểm | Cách phát hiện & Khắc phục |
| :--- | :--- | :--- |
| **1. Memory Leak & Unsafe Pointer** | Dùng `malloc` / `new` mà quên `free` / `delete`, hoặc con trỏ trỏ vào vùng nhớ stack đã hủy. | Bắt buộc dùng RAII (`std::vector<char>`, `std::string`). Chạy kiểm tra bằng AddressSanitizer. |
| **2. Buffer Overflow trên Socket** | Đọc dữ liệu từ socket trực tiếp vào mảng tĩnh mà không kiểm tra giới hạn `payload_len`. | Bắt buộc kiểm tra `payload_len <= BUFFER_SIZE` trước khi nhận dữ liệu. |
| **3. Fake Return / Hardcode** | Viết code đối phó kiểu `bool canAccess(...) { return true; }` để test xanh nhanh. | Loại bỏ ngay lập tức, viết logic kiểm tra bitmask và ACL thật sự. |
| **4. Nuốt Lỗi Socket (Silenced Errors)**| Gọi `recv()` hoặc `send()` trả về `<= 0` nhưng bỏ qua không ngắt kết nối. | Phải đóng socket và giải phóng phiên làm việc ngay khi phát hiện lỗi I/O. |

---

### 🛑 GATE 4: Nghiệm Thu Thực Chứng (The Trinity Evidence)
Một tính năng chỉ được chấp thuận nghiệm thu khi xuất trình đủ **Bộ Ba Bằng Chứng Thực Tế**:
1. **Network / Socket Evidence:** In log hex dump hoặc thông điệp FSP Frame 42 bytes cho thấy packet gửi/nhận đúng cấu trúc, không bị gộp/dính gói.
2. **Storage Evidence:** Kiểm tra trực tiếp trên đĩa cứng:
   - File upload/download xong có dung lượng và mã hash SHA-256 trùng khớp 100% với file gốc.
   - Các file có quyền `PRIVATE` không thể bị xem trộm bởi user khác.
3. **Memory / Sanitizer Evidence:** Chạy qua `ctest` với cờ `-fsanitize=address,undefined` trên Linux/WSL đạt kết quả **0 error, 0 leak**.

---

### 🛑 GATE 5: Đồng Bộ Checkpoint & Khóa Regression vào CTest
* Cập nhật checklist trong `docs/WBS.md`.
* Bổ sung test case mới vào `CMakeLists.txt` (nếu có file test riêng) để GitHub Actions CI tự động kiểm tra cho các lần commit tiếp theo.
* Tạo Pull Request vào nhánh `develop` theo quy chuẩn `feat(module): description`.
