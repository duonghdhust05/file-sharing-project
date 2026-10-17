# TÀI LIỆU THIẾT KẾ CI/CD CHUẨN CÔNG NGHIỆP (CI/CD PIPELINE DESIGN)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING (FSP)

**Mục tiêu:** Thiết lập quy trình Tích hợp Liên tục (CI) và Phân phối Liên tục (CD) tự động hóa 100%, bảo đảm chất lượng mã nguồn C++17 đa nền tảng, ngăn chặn lỗi bộ nhớ và tự động đóng gói sản phẩm phát hành.

---

## 1. BỐI CẢNH & THÁCH THỨC ĐẶC THÙ CỦA LẬP TRÌNH HỆ THỐNG C/C++

Trong các dự án phần mềm thông thường, CI/CD chủ yếu kiểm tra cú pháp và unit test. Tuy nhiên, trong đồ án **Lập trình Mạng C/C++ Hệ thống (Systems/Socket Programming)**, có 4 thách thức kỹ thuật sống còn:

1. **Khác biệt Nền tảng (Platform Divergence)**:
   * Windows sử dụng Winsock2 (`<winsock2.h>`, thư viện `ws2_32.lib`, hàm `WSAStartup()`, kiểu `SOCKET`).
   * Linux/POSIX sử dụng Socket chuẩn (`<sys/socket.h>`, `<netinet/in.h>`, thư viện `pthread`, kiểu descriptor `int`).
   * *Nguy cơ:* Thành viên code trên Windows chạy bình thường nhưng khi nộp bài cho giảng viên chấm trên Linux thì lỗi biên dịch.
2. **Lỗi An toàn Bộ nhớ (Memory Safety & Corruptions)**:
   * Lập trình raw socket liên quan trực tiếp đến mảng byte (`char buffer[]`), con trỏ và ép kiểu gói tin.
   * Lỗi tràn bộ đệm (Buffer Overflow), rò rỉ bộ nhớ (Memory Leak), hay đọc vùng nhớ đã giải phóng (Use-After-Free) rất khó phát hiện bằng mắt thường.
3. **Xung đột Định dạng Code ("Formatting Wars")**:
   * 4 thành viên sử dụng các IDE khác nhau (Visual Studio, VS Code, CLion). Nếu không có chuẩn format tự động, các file merge sẽ bị xung đột dòng (merge conflicts) chỉ vì dấu cách hay thụt đầu dòng.
4. **Phá vỡ Hợp đồng Giao thức (Protocol Regression)**:
   * Nếu một thành viên vô tình thay đổi kích thước `FSPHeader` (vượt quá 42 bytes do quên `#pragma pack(1)`) hoặc làm sai lệch thuật toán CRC32, toàn bộ hệ thống sẽ sụp đổ.

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
| TẦNG 2: MATRIX BUILD & MEMORY SANITIZERS (CROSS-PLATFORM TESTING)          |
| • Ma trận: [Ubuntu (GCC) & Windows (MSVC)] x [Release & Debug]              |
| • Công cụ kiểm tra bộ nhớ (Linux Debug):                                    |
|   - AddressSanitizer (ASan): Phát hiện Buffer Overflow, Heap-out-of-bounds  |
|   - UndefinedBehaviorSanitizer (UBSan): Bắt lỗi số học, tràn số, nullptr    |
| • Automated Test Suite (CTest):                                             |
|   - Kiểm tra sizeof(FSPHeader) == 42 bytes                                  |
|   - Kiểm tra tính toán CRC32 IEEE 802.3                                     |
|   - Kiểm tra tính hợp lệ của bitmask Linux (0755, 0644)                     |
+-----------------------------------------------------------------------------+
                                      | (Passed)
                                      v
+-----------------------------------------------------------------------------+
| TẦNG 3: TỰ ĐỘNG ĐÓNG GÓI & PHÁT HÀNH (CONTINUOUS DELIVERY - RELEASE)       |
| • Điều kiện kích hoạt: Push Git Tag (vd: v1.0.0, v2.0.0)                    |
| • Tự động biên dịch bản tối ưu Release:                                     |
|   - fsp-linux-x86_64.tar.gz (Chứa fsp_server, fsp_client_cli, README)       |
|   - fsp-windows-x64.zip (Chứa fsp_server.exe, fsp_client_cli.exe, README)   |
| • Tự động tạo GitHub Release và đính kèm binary tải về trực tiếp            |
+-----------------------------------------------------------------------------+
```

---

## 3. CÁC TẬP TIN CẤU HÌNH TRONG DỰ ÁN

| File | Đường dẫn | Chức năng |
| :--- | :--- | :--- |
| **Code Style** | `.clang-format` | Chuẩn hóa thụt dòng 4 spaces, Column limit 100, ngoặc nhọn kiểu Google/Modern C++ |
| **CI Workflow** | `.github/workflows/ci.yml` | Tự động chạy Format check, Matrix build đa nền tảng và CTest |
| **CD Workflow** | `.github/workflows/release.yml` | Tự động đóng gói nhị phân khi gắn tag phiên bản `v*.*.*` |
| **Unit Test Suite** | `tests/test_protocol.cpp` | Kiểm tra kích thước header, CRC32, bitmask quyền hạn |
| **CMake Integration** | `CMakeLists.txt` | Cấu hình `enable_testing()` và mục tiêu kiểm thử `fsp_tests` |

---

## 4. CHI TIẾT CÁC CÔNG CỤ ĐƯỢC CHỌN VÀ CƠ SỞ KỸ THUẬT

### 4.1 Clang-Format (Code Consistency)
Mọi tệp tin `.h`, `.hpp`, `.cpp` được kiểm tra thông qua file `.clang-format`. Nếu có bất kỳ dòng nào không tuân thủ định dạng, CI sẽ báo lỗi ngay ở bước đầu tiên kèm vị trí dòng vi phạm.

### 4.2 AddressSanitizer (ASan) & UBSan
Trong quá trình chạy kiểm thử trên môi trường Linux Debug:
```bash
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
```
Khi luồng nhận socket hoặc thao tác chuỗi cố tình đọc/ghi vượt quá mảng bộ đệm (Out-of-bounds Read/Write), ASan sẽ lập tức dừng tiến trình và in ra Stack Trace chi tiết đến từng số dòng mã nguồn.

### 4.3 CTest & Unit Testing Framework
Không cần cài thêm thư viện bên thứ ba nặng nề, hệ thống sử dụng module `CTest` tích hợp sẵn trong CMake kết hợp bộ assertion chuyên biệt trong `tests/test_protocol.cpp`. Điều này giúp thời gian chạy CI cực nhanh (< 2 phút).

---

## 5. QUY TẮC BẢO VỆ NHÁNH (BRANCH PROTECTION RULES TRÊN GITHUB)

Để đảm bảo nhánh `develop` và `main` luôn luôn "xanh" (biên dịch được 100%), nhóm trưởng nên cấu hình trên GitHub Repository (`Settings -> Branches -> Add branch ruleset`):

1. **Require status checks to pass before merging**:
   * Bắt buộc job `Code Formatting & Static Check` phải PASS.
   * Bắt buộc job `Build & Test (ubuntu-latest - Release)` phải PASS.
   * Bắt buộc job `Build & Test (windows-latest - Release)` phải PASS.
2. **Require a pull request before merging**:
   * Không ai (kể cả nhóm trưởng) được `git push --force` trực tiếp vào `main` hoặc `develop`.
   * Mọi thay đổi bắt buộc phải đi qua Pull Request từ nhánh `feat/m*`.
3. **Require review**:
   * Tối thiểu 1 thành viên khác trong nhóm xem qua code trước khi merge.

---

## 6. HƯỚNG DẪN DÀNH CHO CÁC THÀNH VIÊN TRƯỚC KHI PUSH CODE

Để tránh tình trạng đẩy code lên GitHub rồi mới phát hiện CI báo đỏ (Failed):

### Bước 1: Format code tự động
Trước khi commit, chạy lệnh format toàn bộ code:
```bash
# Trên Linux/macOS hoặc Git Bash:
find include src tests -type f \( -name "*.h" -o -name "*.cpp" \) -exec clang-format -i {} +
```
*(Nếu dùng VS Code: Chuột phải chọn "Format Document" hoặc phím tắt `Shift + Alt + F` - VS Code sẽ tự động áp dụng `.clang-format`)*.

### Bước 2: Chạy kiểm thử Unit Test trên máy cục bộ
```bash
# Biên dịch test suite
g++ -std=c++17 -Iinclude tests/test_protocol.cpp -lws2_32 -o run_tests.exe

# Chạy kiểm thử
./run_tests.exe
```
Nếu màn hình xuất hiện thông báo:
```
====================================================
  ALL PROTOCOL UNIT TESTS PASSED SUCCESSFULLY! (4/4)
====================================================
```
thì bạn mới được thực hiện `git push`.

