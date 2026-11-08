# TÀI LIỆU THIẾT KẾ CI/CD CHUẨN CÔNG NGHIỆP (CI/CD PIPELINE DESIGN)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING (FSP - GROUP 7)

**Mục tiêu:** Thiết lập quy trình Tích hợp Liên tục (CI) và Phân phối Liên tục (CD) tự động hóa 100%, bảo đảm chất lượng mã nguồn **C (C11)**, ngăn chặn lỗi bộ nhớ (buffer overflow, memory leak, data race) và tự động đóng gói sản phẩm phát hành.

---

## 1. BỐI CẢNH & THÁCH THỨC ĐẶC THÙ CỦA LẬP TRÌNH HỆ THỐNG MẠNG TRONG C

Trong đồ án **Lập trình Mạng Hệ thống bằng C (POSIX Socket & `pthread`)**, có 4 thách thức kỹ thuật sống còn mà pipeline CI/CD phải kiểm soát:

1. **Quản lý Bộ nhớ Thủ công trong C (Manual Memory Management)**:
   * Lập trình raw socket trong C thao tác trực tiếp trên mảng byte (`uint8_t buffer[]`), cấp phát động (`malloc`/`calloc`/`free`) và ép kiểu gói tin (`__attribute__((packed))`).
   * Lỗi tràn bộ đệm (Buffer Overflow), rò rỉ bộ nhớ (Memory Leak), hay đọc vùng nhớ đã giải phóng (Use-After-Free) rất khó phát hiện bằng mắt thường.
2. **An toàn Đa luồng (`pthread` Concurrency & Race Conditions)**:
   * Server phục vụ đồng thời nhiều client qua `pthread` (1 thread / client), truy cập đồng thời vào bảng Session, Permission và VFS. Dữ liệu dùng chung bắt buộc phải được bảo vệ bằng `pthread_mutex_t`.
3. **Xung đột Định dạng Code ("Formatting Wars")**:
   * 4 thành viên sử dụng các môi trường biên tập khác nhau. Nếu không có chuẩn format tự động (`.clang-format`), các file merge sẽ bị xung đột dòng.
4. **Phá vỡ Hợp đồng Giao thức (Protocol Regression)**:
   * Nếu một thành viên vô tình thay đổi kích thước `FSPHeader` (vượt quá 42 bytes) hoặc làm sai lệch thuật toán CRC32 / SHA-256, toàn bộ giao tiếp giữa Server và Client sẽ sụp đổ.

---

## 2. KIẾN TRÚC PIPELINE CI/CD (PIPELINE ARCHITECTURE)

Hệ sinh thái CI/CD được xây dựng bằng **GitHub Actions** với mô hình 3 tầng phòng thủ:

```
                  [ THÀNH VIÊN TẠO PULL REQUEST / PUSH CODE ]
                                      |
                                      v
+-----------------------------------------------------------------------------+
| TẦNG 1: KIỂM SOÁT ĐỊNH DẠNG & PHÂN TÍCH TĨNH (CODE FORMAT GATEKEEPER)       |
| • Công cụ: clang-format (theo chuẩn .clang-format)                          |
| • Kiểm tra: find ... -exec clang-format --dry-run --Werror {} +             |
| • Mục đích: Từ chối ngay lập tức nếu code thụt lề sai hoặc lộn xộn           |
+-----------------------------------------------------------------------------+
                                      | (Passed)
                                      v
+-----------------------------------------------------------------------------+
| TẦNG 2: BUILD & MEMORY SANITIZERS (AUTOMATED VERIFICATION)                  |
| • Biên dịch CMake (Release & Debug)                                         |
| • Công cụ kiểm tra bộ nhớ (Linux Debug):                                    |
|   - AddressSanitizer (ASan): Phát hiện Buffer Overflow, Heap-out-of-bounds  |
|   - UndefinedBehaviorSanitizer (UBSan): Bắt lỗi số học, tràn số, nullptr    |
| • Automated Test Suite (CTest):                                             |
|   - Kiểm tra _Static_assert sizeof(FSPHeader) == 42 bytes, Chunk == 88 bytes|
|   - Kiểm tra tính toán CRC32 IEEE 802.3 & FIPS 180-4 SHA-256                |
|   - Kiểm tra 6 Adversarial Probes (Sticky packets, Auth, POSIX+ACL, VFS)    |
+-----------------------------------------------------------------------------+
                                      | (Passed)
                                      v
+-----------------------------------------------------------------------------+
| TẦNG 3: TỰ ĐỘNG ĐÓNG GÓI & PHÁT HÀNH (CONTINUOUS DELIVERY - RELEASE)       |
| • Điều kiện kích hoạt: Push Git Tag (vd: v1.0.0, v2.0.0)                    |
| • Tự động biên dịch bản tối ưu Release và đóng gói binary                   |
| • Tự động tạo GitHub Release và đính kèm gói thực thi                       |
+-----------------------------------------------------------------------------+
```

---

## 3. CÁC TẬP TIN CẤU HÌNH TRONG DỰ ÁN

| File | Đường dẫn | Chức năng |
| :--- | :--- | :--- |
| **Code Style** | `.clang-format` | Chuẩn hóa thụt dòng 4 spaces, Column limit 100 |
| **CI Workflow** | `.github/workflows/ci.yml` | Tự động chạy Format check, Build, Sanitizers và CTest |
| **CD Workflow** | `.github/workflows/release.yml` | Tự động đóng gói nhị phân khi gắn tag phiên bản `v*.*.*` |
| **Unit Test Suite** | `tests/` | Kiểm tra header 42B/88B, CRC32, Sticky Packets, Auth & Permission Matrix |
| **CMake Integration** | `CMakeLists.txt` | Cấu hình `enable_testing()` và các mục tiêu kiểm thử CTest |

---

## 4. HƯỚNG DẪN DÀNH CHO CÁC THÀNH VIÊN TRƯỚC KHI PUSH CODE

### Bước 1: Format code tự động
```bash
find include src tests -type f \( -name "*.h" -o -name "*.c" -o -name "*.cpp" \) -exec clang-format -i {} +
```

### Bước 2: Biên dịch và chạy CTest kèm AddressSanitizer
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cd build && ctest --output-on-failure
```
Chỉ khi toàn bộ bài kiểm thử trong `ctest` báo **100% tests passed**, thành viên mới thực hiện `git push`.
