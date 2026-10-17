# 📋 1-PAGE MICRO-PLAN: [TÊN TÍNH NĂNG / ISSUE]
## DỰ ÁN: CLIENT-SERVER FILE SHARING (C++17 SOCKETS)

> **Kỷ luật:** Toàn bộ bản kế hoạch bắt buộc **DƯỚI 60–80 DÒNG**.  
> **Nguyên tắc:** Dừng lại tại **GATE 1**, thảo luận trong nhóm hoặc tự rà soát trước khi viết code sản xuất.

---

### 1. PHÂN ĐỊNH RANH GIỚI (STRICT vs FLEXIBLE)
- **STRICT (Bắt buộc 100%):**
  - [ ] **Protocol Boundary:** Kích thước gói tin bắt buộc tuân thủ `FSPHeader` (42 bytes).
  - [ ] **VFS Sandbox:** Tuyệt đối không cho phép truy cập ngoài `./storage/server_root` (Chặn Path Traversal `../`).
  - [ ] **Access Control:** Bắt buộc kiểm tra `PermissionEngine::canAccess()` trước khi đọc/ghi file.
  - [ ] **Memory Safety:** Dùng RAII (`std::vector`, `std::unique_ptr`), không dùng cấp phát thô `malloc/free`, không rò rỉ RAM.
  - [ ] **Data Integrity:** Mọi chunk 64KB bắt buộc tính và kiểm tra CRC32.
- **FLEXIBLE (Linh hoạt tinh chỉnh):**
  - [Thông điệp hiển thị log console hoặc chuỗi mô tả lỗi]
  - [Kích thước buffer tạm thời: 64KB đến 128KB]

---

### 2. CÁC TẬP TIN TÁC ĐỘNG (TỐI ĐA 3–4 FILES)
1. **Header Interface:** `include/[server|client|common]/[module].h`
2. **Implementation:** `src/[server|client|common]/[module].cpp`
3. **Adversarial Test File:** `tests/test_[module].cpp`

---

### 3. MA TRẬN PROBES ĐỐI KHÁNG DỰ KIẾN (INPUT ➜ OUTPUT MONG ĐỢI)
- **Probe 1 (Happy Path Test):**
  - *Input:* Dữ liệu hợp lệ, đầy đủ tham số.
  - *Expect:* Trả về `StatusCode::OK (200)` hoặc thực thi thành công.
- **Probe 2 (Corrupted / Malformed Input Probe):**
  - *Input:* Gửi payload sai độ dài `payload_len`, magic byte sai, hoặc CRC không khớp.
  - *Expect:* Phát hiện lỗi và từ chối dứt khoát với `StatusCode::BAD_REQUEST (400)`.
- **Probe 3 (Security / Boundary Violation Probe):**
  - *Input:* Thử truy cập file `PRIVATE` của người khác hoặc truyền đường dẫn `../../etc/passwd`.
  - *Expect:* Từ chối với `StatusCode::FORBIDDEN (403)` hoặc ném ngoại lệ bảo mật.
- **Probe 4 (Resource / Memory Safety Probe):**
  - *Input:* Truyền tệp tin 1GB liên tục hoặc chạy song song 2 luồng.
  - *Expect:* RAM không tăng đột biến (< 50MB), 0 memory leak dưới AddressSanitizer.

---

### 4. LỆNH DUYỆT (GATE 1)
> Sẵn sàng chuyển sang Bước 2: Viết Red Tests. CẤM viết code logic khi test chưa được định hình rõ ràng!
