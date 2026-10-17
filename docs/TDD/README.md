# 🛡️ BỘ QUY CHUẨN KIỂM THỬ THỰC CHIẾN (ADVERSARIAL TESTING & TDD FRAMEWORK)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (C++17 SOCKETS)

> **Phạm vi & Tính linh hoạt:**  
> ⚠️ **Quy trình TDD (Test-Driven Development) là một TÙY CHỌN PHÁT TRIỂN (Optional Workflow).**  
> Các thành viên trong nhóm có thể linh hoạt chọn viết test trước (TDD) hoặc viết code trước rồi viết test sau (Test-After).  
> ⛔ **TUY NHIÊN, CÁC QUY CHUẨN VỀ THIẾT KẾ TEST LÀ BẮT BUỘC 100% (Strict Test Design Standards).**  
> Tuyệt đối không chấp nhận test đối phó, test sinh ra chỉ để "fake kết quả" qua mặt CI. Mỗi thành viên phải hiểu sâu bản chất kiểm thử hệ thống và bao phủ toàn diện các góc cạnh thực tế của đề tài Lập trình Mạng Socket C/C++.

---

## 1. TRIẾT LÝ: CHỐNG ẢO TƯỞNG VÀ TIÊU DIỆT TEST ĐỐI PHÓ (ANTI-ILLUSION TESTING)

Trong lập trình mạng tầng thấp bằng C/C++, nguy hiểm lớn nhất không phải là code có lỗi, mà là **ảo tưởng "Code chạy ngon" vì test viết quá hời hợt**:
* Test tự khởi tạo biến `bool success = true;` rồi `assert(success == true)` $\to$ **Test vô nghĩa (Tautological Test)**.
* Mock toàn bộ hàm socket `send/recv` trả về đúng kích thước mà không hề kiểm tra xem byte truyền trên mạng có bị cắt mảnh hay dính gói không.
* Bỏ qua các trường hợp tiêu cực: ngắt kết nối giữa chừng, chunk 64KB bị sai checksum, gửi payload `../../etc/passwd` để tấn công leo thang thư mục.

### 4 Nguyên Tắc "Bất Khả Xâm Phạm" Trong Thiết Kế Test:
1. **Không Test Giả Lập Tự Hài Lòng (Zero Tautology)**: Mọi assertion phải kiểm tra trên **dữ liệu thực tế** (file nhị phân ghi trên đĩa thật, chuỗi byte thực sự nhận từ socket buffer, mã băm SHA-256 / CRC32 được tính toán thực tế).
2. **Fail-Closed Trên Mọi Lỗi Mạng & Quyền Hạn**: Khi gặp lỗi (mất kết nối, file hỏng, thiếu quyền), hệ thống phải từ chối dứt khoát (`403 FORBIDDEN`, `CRC_MISMATCH`). Nghiêm cấm nuốt lỗi âm thầm bằng cách gán giá trị mặc định bừa bãi.
3. **Thực Chứng Đa Chiều (Multi-Dimensional Evidence)**: Một tính năng chỉ được coi là đạt khi thỏa mãn bộ ba bằng chứng:
   * **Protocol Evidence:** Packet Frame chuẩn 42 bytes, không bị dính gói.
   * **Storage Evidence:** File trên đĩa đích nguyên vẹn 100% so với file nguồn.
   * **Memory Evidence:** Không rò rỉ bộ nhớ (AddressSanitizer / Valgrind sạch 0 byte leak).
4. **Bao Phủ Ma Trận 6 Probes Đối Kháng (The 6 Adversarial Probes)**: Xem chi tiết ở Mục 3.

---

## 2. CẤU TRÚC THƯ MỤC `docs/TDD/`

```text
docs/TDD/
├── README.md                           # Tổng quan triết lý kiểm thử & quy chuẩn bắt buộc (File này)
├── TDD-WORKFLOW-GATES.md               # Quy trình 7 bước & 5 Chốt chặn quyết định (Dành cho ai chọn TDD)
├── ADVERSARIAL-TDD-GUIDE.md            # Cẩm nang chuyên sâu về kiểm thử đối kháng trong Socket C/C++
├── probes/
│   └── adversarial_socket_probes.hpp   # Mã nguồn C++17 chứa sẵn 6 Probes đối kháng mẫu cho đề tài
└── templates/
    ├── micro-plan-template.md          # Mẫu kế hoạch vi mô 1 trang (< 80 dòng) trước khi code
    └── socket_test_template.cpp        # Mẫu file Unit Test C++ chuẩn CTest không dùng fake assertion
```

---

## 3. MA TRẬN 6 KIỂM THỬ SỐNG CÒN (THE 6 ADVERSARIAL SOCKET PROBES)

Dù bạn chọn phương pháp TDD hay Test-After, toàn bộ 4 module của 4 thành viên bắt buộc phải thiết kế test bao phủ các khía cạnh sau:

| Mã Probe | Tên Probe Kiểm Thử | Tình Huống Đối Kháng (Adversarial Scenario) | Tiêu Chí Đạt (Passing Criteria) | Thành Viên Phụ Trách |
| :---: | :--- | :--- | :--- | :---: |
| **PROBE-1** | **Sticky Packets & Fragmentation** | Gửi liên tiếp 2 gói tin trong 1 lần `send()`, hoặc xé 1 gói 42-byte Header thành 3 lần `recv()` rời rạc. | Hàm bóc tách frame phải gom/tách đúng 2 gói độc lập, không bị lệch byte hay crash. | **Trần Sỹ Nguyên (M1)** |
| **PROBE-2** | **Session Isolation & Token Expiry** | User A dùng token đã logout hoặc cố tình gửi request với token của User B. | Server phải từ chối lập tức với mã `401 UNAUTHORIZED`, không cho phép truy cập tài nguyên. | **Bùi Đức Hiếu (M2)** |
| **PROBE-3** | **Linux POSIX Permission Enforcement** | User B cố tình gửi packet đòi tải hoặc xóa file mà User A đã đặt cờ `PRIVATE (0)` hoặc `0600`. | Hàm `canAccess()` phải trả về `false`, Server trả về lỗi `403 FORBIDDEN`. | **Bùi Đức Hiếu (M2)** |
| **PROBE-4** | **Corrupted Chunk & CRC32 Tampering** | Cố tình lật 1 bit trong khối dữ liệu 64KB khi đang truyền file (`FILE_CHUNK`). | Bộ kiểm tra tính toàn vẹn phát hiện `chunk_crc` không khớp, hủy ghi đĩa và yêu cầu gửi lại. | **Vũ Nhật Minh Dương (M3)** |
| **PROBE-5** | **Path Traversal Security Boundary** | Gửi yêu cầu mở file với đường dẫn `../../../../windows/system.ini` hoặc `../../etc/passwd`. | Virtual File System sandbox phát hiện và ném ngoại lệ bảo mật, tuyệt đối không lọt ra ngoài `./storage/server_root`. | **Vũ Nhật Minh Dương (M3)** |
| **PROBE-6** | **Concurrent Transfer & ASan Safety** | 2 luồng cùng ghi đè 1 file, hoặc 1 luồng đọc khi 1 luồng đang xóa. Chạy dưới AddressSanitizer. | Hệ thống sử dụng `std::mutex` / `std::shared_mutex` an toàn, 0 data race, 0 memory leak. | **Cả nhóm (M1, M2, M3, M4)** |

---

## 4. HƯỚNG DẪN THỰC THI KIỂM THỬ TRÊN MÁY CỤC BỘ

### Chạy nhanh bộ test C++ có sẵn:
```bash
# Biên dịch test suite
g++ -std=c++17 -Iinclude tests/test_protocol.cpp -lws2_32 -o fsp_test.exe

# Chạy test
./fsp_test.exe
```

### Chạy qua CMake & CTest:
```bash
cd build
ctest --output-on-failure -C Release
```

### Chạy kèm AddressSanitizer (Bắt lỗi tràn bộ nhớ trên Linux / WSL):
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build
cd build && ctest --output-on-failure
```
