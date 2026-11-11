# 🛡️ BỘ QUY CHUẨN KIỂM THỬ THỰC CHIẾN (ADVERSARIAL TESTING & TDD FRAMEWORK)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (C11 POSIX SOCKETS)

> **Phạm vi & Tính linh hoạt:**  
> ⚠️ **Quy trình TDD (Test-Driven Development) là một TÙY CHỌN PHÁT TRIỂN (Optional Workflow).**  
> Các thành viên trong nhóm có thể linh hoạt chọn viết test trước (TDD) hoặc viết code trước rồi viết test sau (Test-After).  
> ⛔ **TUY NHIÊN, CÁC QUY CHUẨN VỀ THIẾT KẾ TEST LÀ BẮT BUỘC 100% (Strict Test Design Standards).**  
> Tuyệt đối không chấp nhận test đối phó, test sinh ra chỉ để "fake kết quả" qua mặt CI. Mỗi thành viên phải hiểu sâu bản chất kiểm thử hệ thống và bao phủ toàn diện các góc cạnh thực tế của đề tài Lập trình Mạng Socket trong ngôn ngữ **C (C11)**.

---

## 1. TRIẾT LÝ: CHỐNG ẢO TƯỞNG VÀ TIÊU DIỆT TEST ĐỐI PHÓ (ANTI-ILLUSION TESTING)

Trong lập trình mạng tầng thấp bằng ngôn ngữ C (C11), nguy hiểm lớn nhất không phải là code có lỗi, mà là **ảo tưởng "Code chạy ngon" vì test viết quá hời hợt**:
* Test tự khởi tạo biến `bool success = true;` rồi `assert(success == true)` $\to$ **Test vô nghĩa (Tautological Test)**.
* Mock toàn bộ hàm socket `send/recv` trả về đúng kích thước mà không hề kiểm tra xem byte truyền trên mạng có bị cắt mảnh hay dính gói không.
* Bỏ qua các trường hợp tiêu cực: ngắt kết nối giữa chừng, chunk $64\text{ KB}$ bị sai checksum, gửi payload `../../etc/passwd` để tấn công leo thang thư mục.

### 4 Nguyên Tắc "Bất Khả Xâm Phạm" Trong Thiết Kế Test:
1. **Không Test Giả Lập Tự Hài Lòng (Zero Tautology)**: Mọi assertion phải kiểm tra trên **dữ liệu thực tế** (file nhị phân ghi trên đĩa thật, chuỗi byte thực sự nhận từ socket buffer, mã băm SHA-256 / CRC32 được tính toán thực tế).
2. **Fail-Closed Trên Mọi Lỗi Mạng & Quyền Hạn**: Khi gặp lỗi (mất kết nối, file hỏng, thiếu quyền), hệ thống phải từ chối dứt khoát (`403 FORBIDDEN`, `CRC_MISMATCH`). Nghiêm cấm nuốt lỗi âm thầm.
3. **Thực Chứng Đa Chiều (Multi-Dimensional Evidence)**: Một tính năng chỉ được coi là đạt khi thỏa mãn bộ ba bằng chứng:
   * **Protocol Evidence:** Packet Frame chuẩn 42 bytes, Chunk Header chuẩn 88 bytes, không bị dính gói.
   * **Storage Evidence:** File trên đĩa đích nguyên vẹn 100% so với file nguồn.
   * **Memory Evidence:** Không rò rỉ bộ nhớ, không tràn vùng đệm (AddressSanitizer & UBSan sạch 0 error).
4. **Bao Phủ Ma Trận 6 Probes Đối Kháng (The 6 Adversarial Probes)**: Xem chi tiết ở Mục 3.

---

## 2. MA TRẬN 6 KIỂM THỬ SỐNG CÒN (THE 6 ADVERSARIAL SOCKET PROBES)

Dù bạn chọn phương pháp TDD hay Test-After, toàn bộ 4 module của 4 thành viên bắt buộc phải thiết kế test bao phủ các khía cạnh sau:

| Mã Probe | Tên Probe Kiểm Thử | Tình Huống Đối Kháng (Adversarial Scenario) | Tiêu Chí Đạt (Passing Criteria) | Thành Viên Phụ Trách |
| :---: | :--- | :--- | :--- | :---: |
| **PROBE-1** | **Sticky Packets & Fragmentation** | Gửi liên tiếp nhiều gói tin trong 1 lần `send()`, hoặc xé 1 gói 42-byte Header thành nhiều lần `recv()` rời rạc. | Hàm bóc tách frame (`readExact` / `unpackFromBuffer`) gom/tách đúng từng gói độc lập, không lệch byte hay crash. | **Trần Sỹ Nguyên (M1)** |
| **PROBE-2** | **Session Isolation & Token Expiry** | User A dùng token giả mạo, đã logout, hết hạn 30 phút hoặc sai mật khẩu 5 lần liên tiếp. | Server từ chối lập tức với mã `401 UNAUTHORIZED`, kích hoạt khóa tạm thời 5 phút khi brute-force. | **Bùi Đức Hiếu (M2)** |
| **PROBE-3** | **Linux POSIX + Sharing Level Enforcement** | User B cố tình gửi packet đòi tải/xóa file mà User A đã đặt cờ `PRIVATE (0)` (kể cả khi bit `0666` bật). Kiểm tra đủ bước 0–7 của `can_access`. | Hàm `perm_can_access()` trả về `false`, Server trả về lỗi `403 FORBIDDEN`. | **Bùi Đức Hiếu (M2)** |
| **PROBE-4** | **Corrupted Chunk & CRC32 Tampering** | Cố tình lật 1 bit trong khối dữ liệu $64\text{ KB}$ khi đang truyền file (`FILE_CHUNK`). | Bộ kiểm tra tính toàn vẹn phát hiện `chunk_crc` không khớp, hủy ghi đĩa và xóa file tạm `.part`. | **Vũ Nhật Minh Dương (M3)** |
| **PROBE-5** | **Path Traversal Security Boundary** | Gửi yêu cầu mở file với đường dẫn `../../../../etc/passwd` hoặc symlink thoát khỏi thư mục gốc. | Virtual File System (`realpath`) phát hiện và chặn đứng tại biên `./storage/server_root` (`403 FORBIDDEN`). | **Vũ Nhật Minh Dương (M3)** |
| **PROBE-6** | **Concurrent Transfer & ASan/UBSan Safety** | Ít nhất 5 client đồng thời đăng ký, đăng nhập, kiểm tra quyền và truyền file qua các luồng `pthread`. | Hệ thống sử dụng `pthread_mutex_t` an toàn, 0 data race, 0 memory leak dưới ASan/UBSan. | **Cả nhóm (M1, M2, M3, M4)** |
