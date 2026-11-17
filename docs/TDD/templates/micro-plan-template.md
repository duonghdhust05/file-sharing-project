# 📋 1-PAGE MICRO-PLAN: [TÊN TÍNH NĂNG / USE CASE]
## DỰ ÁN: CLIENT-SERVER FILE SHARING (C11 POSIX SOCKETS)

> **Kỷ luật:** Toàn bộ bản kế hoạch giữ gọn **DƯỚI 60–80 DÒNG**.  
> **Nguyên tắc:** Áp dụng **Light Dual-Gate (Gate 1 Red → Gate 2 Green)** cho các module nghiệp vụ/mạng (M2, M3, tích hợp `fsp_client_api`).

---

### 1. PHÂN ĐỊNH RANH GIỚI (STRICT vs FLEXIBLE)
- **STRICT (Bắt buộc 100%):**
  - [ ] **Protocol Boundary:** Tuân thủ `FSPHeader` (42 bytes) và `FileChunkHeader` (88 bytes) trong `include/common/FSPProtocol.h`.
  - [ ] **Sans-IO Seam:** Tách logic xử lý buffer/trạng thái khỏi `recv`/`send`/`open` để test độc lập.
  - [ ] **VFS Sandbox:** Không truy cập ngoài `./storage/server_root` (chuẩn hóa + `realpath()`).
  - [ ] **Access Control:** Kiểm tra `fsp_can_access()` (bước 0–7 theo SRS FR-6) trước mọi thao tác.
  - [ ] **Memory, FD & `.part` Hygiene:** Giải phóng mọi `malloc`, `fd`, `FILE*` trên tất cả nhánh lỗi (`goto cleanup`); `CHECK_NO_FD_LEAK` và `CHECK_NO_PART_FILES`.
  - [ ] **Data Integrity:** Bộ đệm cố định 64 KB; kiểm tra CRC32 từng khối; upload dùng `.part` (xóa `.part` khi lỗi/ngắt kết nối). *(SHA-256 toàn file là COULD.)*
- **FLEXIBLE (Linh hoạt tinh chỉnh):**
  - [Thông điệp log hoặc bố cục widget GTK]

---

### 2. CÁC TẬP TIN TÁC ĐỘNG & MÃ YÊU CẦU PHỦ (`@covers`)
1. **Header Interface:** `include/[server|client|common]/[module].h`
2. **Implementation:** `src/[server|client|common]/[module].c`
3. **Adversarial Test File:** `tests/test_[module].c` (dùng `test_util.h` + `c11_test_template.c`)
4. **Phạm vi `@covers`:** `[UCxx, FR-x.y, NFR-xx]`

---

### 3. MA TRẬN TEST 4 TẦNG & PROBES ĐỐI KHÁNG
- [ ] **Tầng 1 — Sans-IO Unit (`@covers`):** Parse/serialize buffer, `fsp_can_access`, chuẩn hóa path không cần socket.
- [ ] **Tầng 2 — Component (`socketpair` + `mkdtemp`):** Bơm thiếu byte / dính gói / ngắt kết nối giữa chừng; kiểm tra `CHECK_NO_PART_FILES`.
- [ ] **Tầng 3 — In-Process Loopback (`port = 0`):** Client socket thật qua `fsp_client_api`, `401`/`403`/`409`.
- [ ] **Tầng 4 — Concurrency & Hygiene:** ≥ 5 client `pthread` song song; `CHECK_NO_FD_LEAK`; file lớn (500 MB) RSS ổn định.

---

### 4. QUY TRÌNH LIGHT DUAL-GATE
- **GATE 1 (SPEC & RED):** Chỉ sửa `.h`, viết hàm stub trả lỗi mặc định trong `.c`, viết đầy đủ `tests/test_[module].c` (có `/* @covers: ... */`) → chạy `ctest` xác nhận **FAIL (RED)**.
- **GATE 2 (GREEN & HYGIENE):** Khóa assertion trong `tests/test_[module].c`; cài đặt C11 trong `src/` cho tới khi `ctest` **PASS 100%** dưới cả `ASan + UBSan` và `TSan` (`-fsanitize=thread`).
