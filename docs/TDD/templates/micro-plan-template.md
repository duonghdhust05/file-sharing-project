# 📋 1-PAGE MICRO-PLAN: [TÊN TÍNH NĂNG / ISSUE]
## DỰ ÁN: CLIENT-SERVER FILE SHARING (C11 POSIX SOCKETS)

> **Kỷ luật:** Toàn bộ bản kế hoạch bắt buộc **DƯỚI 60–80 DÒNG**.  
> **Nguyên tắc:** Dừng lại tại **GATE 1**, thảo luận trong nhóm hoặc tự rà soát trước khi viết code sản xuất.

---

### 1. PHÂN ĐỊNH RANH GIỚI (STRICT vs FLEXIBLE)
- **STRICT (Bắt buộc 100%):**
  - [ ] **Protocol Boundary:** Kích thước gói tin bắt buộc tuân thủ `FSPHeader` (42 bytes) và `FileChunkHeader` (88 bytes).
  - [ ] **VFS Sandbox:** Tuyệt đối không cho phép truy cập ngoài `./storage/server_root` (Chặn Path Traversal bằng `realpath`).
  - [ ] **Access Control:** Bắt buộc kiểm tra `canAccess()` (9-step POSIX + ACL) trước khi đọc/ghi/xóa/chia sẻ file.
  - [ ] **Memory & Resource Safety (C11):** Mọi con trỏ cấp phát động (`malloc`/`calloc`) và descriptor (`socket`/`fopen`) phải được giải phóng trên tất cả nhánh lỗi (`goto cleanup`).
  - [ ] **Data Integrity:** Mọi chunk $64\text{ KB}$ bắt buộc tính và kiểm tra CRC32; toàn file kiểm tra SHA-256.
- **FLEXIBLE (Linh hoạt tinh chỉnh):**
  - [Thông điệp hiển thị log console hoặc bố cục GTK Widget]

---

### 2. CÁC TẬP TIN TÁC ĐỘNG (TỐI ĐA 3–4 FILES)
1. **Header Interface:** `include/[server|client|common]/[module].h`
2. **Implementation:** `src/[server|client|common]/[module].c`
3. **Adversarial Test File:** `tests/test_[module].c`

---

### 3. MA TRẬN PROBES ĐỐI KHÁNG DỰ KIẾN (INPUT ➜ OUTPUT MONG ĐỢI)
- **Probe 1 (Happy Path Test):**
  - *Input:* Dữ liệu hợp lệ, đầy đủ tham số.
  - *Expect:* Trả về `StatusCode::OK (200)` hoặc thực thi thành công.
- **Probe 2 (Corrupted / Malformed Input Probe):**
  - *Input:* Gửi payload sai độ dài `payload_len`, magic byte $\neq \text{0xAA}$, hoặc CRC32 không khớp.
  - *Expect:* Phát hiện lỗi và từ chối dứt khoát (`400 BAD_REQUEST` hoặc đóng kết nối).
- **Probe 3 (Security / Boundary Violation Probe):**
  - *Input:* Thử truy cập file `PRIVATE` của người khác hoặc truyền đường dẫn `../../etc/passwd`.
  - *Expect:* Từ chối với `403 FORBIDDEN`.
- **Probe 4 (Resource / Memory Safety Probe):**
  - *Input:* Truyền tệp tin lớn (ví dụ 500 MB) hoặc chạy đồng thời ≥ 5 client `pthread`.
  - *Expect:* Bộ nhớ tiến trình ổn định không tăng theo kích thước file, 0 memory leak và 0 data race dưới ASan/UBSan.

---

### 4. LỆNH DUYỆT (GATE 1)
> Sẵn sàng chuyển sang Bước 2: Viết Red Tests. CẤM viết code logic khi test chưa được định hình rõ ràng!
