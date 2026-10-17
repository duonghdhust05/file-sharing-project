# FSP: Client-Server Online File Sharing Application
> **Đồ án Môn học: Lập trình Mạng (Networking Program) - Đề tài 6 (Topic 6)**  
> **Ngôn ngữ:** C++17 (Raw TCP Sockets - Winsock2 / POSIX)  
> **Giao diện:** Qt6 / Qt5 Desktop Client & TypeScript Web Client  

[![CI Pipeline](https://github.com/duonghdhust05/file-sharing-project/actions/workflows/ci.yml/badge.svg)](https://github.com/duonghdhust05/file-sharing-project/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-brightgreen.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 1. TỔNG QUAN DỰ ÁN
Dự án xây dựng một hệ thống chia sẻ tệp tin trực tuyến hoàn chỉnh theo mô hình Client-Server. Hệ thống không sử dụng bất kỳ thư viện mạng cấp cao nào mà tự hiện thực toàn bộ từ Raw TCP Sockets, bao gồm:
* **Giao thức Tự tạo (Custom Protocol)**: Thiết kế chuẩn frame 42-byte, giải quyết triệt để vấn đề dính gói (sticky packets) và phân mảnh gói (packet fragmentation) của TCP.
* **Cơ chế Hướng sự kiện (Event-driven)**: Lấy cảm hứng từ kiến trúc Socket.IO/RPC (`on`, `emit`).
* **Hệ thống Quản lý Người dùng & Phân quyền POSIX/Linux**: Đăng ký, đăng nhập bảo mật (Salted SHA-256), phân quyền Owner-Group-Others ($rwx$) và mức độ chia sẻ Private / Shared / Public.
* **Truyền Tệp & Thư mục Khối lớn**: Chunking 64KB, kiểm soát tiến trình truyền tải, chống tràn RAM và hỗ trợ đệ quy cả cây thư mục.
* **Giao diện Trực quan (GUI)**: Ứng dụng Desktop Qt C++ đa luồng mượt mà và Giao diện Web (TypeScript + React) phục vụ thuyết trình trực quan.
* **Hạ tầng CI/CD Chuẩn Công nghiệp**: Tự động hóa kiểm tra định dạng code (clang-format), matrix build đa nền tảng (Ubuntu/Windows), kiểm tra an toàn bộ nhớ (AddressSanitizer) và tự động đóng gói release.

---

## 2. CẤU TRÚC THƯ MỤC DỰ ÁN

```
file-sharing-project/
├── .github/                      # CI/CD PIPELINE (GITHUB ACTIONS)
│   └── workflows/
│       ├── ci.yml                # CI: Matrix Build, Clang-Format, Sanitizers & CTest
│       └── release.yml           # CD: Tự động đóng gói ZIP/TAR khi release tag
├── .clang-format                 # Chuẩn hóa format code C++17 cho toàn nhóm
├── CMakeLists.txt                # Cấu hình build C++17 cross-platform & CTest
├── README.md                     # Hướng dẫn dự án, setup môi trường & Git workflow
├── .gitignore                    # Bỏ qua file build, tạm, sandbox data
├── docs/                         # BỘ TÀI LIỆU CHUẨN MỰC CỦA DỰ ÁN
│   ├── SRS.md                    # Bản Đặc tả Yêu cầu Phần mềm (IEEE 830)
│   ├── WBS.md                    # Phân rã Công việc & Checklist 4 thành viên độc lập
│   ├── SDD.md                    # Bản Thiết kế Phần mềm (HLD + LLD)
│   └── CICD.md                   # Bản Thiết kế Kiến trúc CI/CD Chuẩn Công nghiệp
├── include/                      # CÁC FILE HEADER & INTERFACE CONTRACTS
│   ├── common/
│   │   ├── FSPProtocol.h         # [QUAN TRỌNG NHẤT] Định nghĩa Frame, Events, Bitmasks
│   │   └── Config.h              # Hằng số cấu hình hệ thống & CRC32 IEEE 802.3
│   ├── server/                   # Headers dành riêng cho Server
│   └── client/                   # Headers dành riêng cho Client
├── src/                          # MÃ NGUỒN C++
│   ├── common/                   # Code dùng chung (Framing, CRC, Serialization)
│   ├── server/
│   │   └── main.cpp              # Entry point của FSP Server Daemon
│   ├── client_cli/
│   │   └── main.cpp              # Entry point của Tool dòng lệnh test
│   └── client_gui/               # Mã nguồn Desktop GUI (Qt)
├── tests/                        # HỆ THỐNG KIỂM THỬ TỰ ĐỘNG (UNIT TESTS)
│   └── test_protocol.cpp         # Test header 42-byte, CRC32, bitmask quyền hạn
├── storage/                      # VÙNG LƯU TRỮ SANDBOX (Được gitignore)
│   ├── server_root/              # Thư mục gốc lưu trữ của Server
│   └── client_downloads/         # Thư mục tải về mặc định của Client
└── web/                          # MỞ RỘNG WEB TYPESCRIPT (GIAI ĐOẠN 3-4)
    └── README.md                 # Hướng dẫn chạy WebSocket Gateway & React UI
```

---

## 3. PHÂN CÔNG VAI TRÒ NHÓM 4 THÀNH VIÊN

| Thành viên | Phụ trách chính | Module cốt lõi | Checklist & Báo cáo SDD |
| :--- | :--- | :--- | :--- |
| **Trần Sỹ Nguyên** | **Core Network Engine** | Raw TCP Socket, Framing 42-byte, Anti-Sticky Loop, Server ThreadPool | [docs/WBS.md](docs/WBS.md) (M1) & [docs/SDD.md](docs/SDD.md) (Chương 3) |
| **Bùi Đức Hiếu** | **Auth & Permission** | Salted SHA-256, Session Token, Linux-like 3-tier Permissions ($rwx$), ACL | [docs/WBS.md](docs/WBS.md) (M2) & [docs/SDD.md](docs/SDD.md) (Chương 4) |
| **Vũ Nhật Minh Dương** | **Filesystem & Transfer**| Virtual Sandbox, Chunking 64KB, Recursive Folder Transfer, Search Engine | [docs/WBS.md](docs/WBS.md) (M3) & [docs/SDD.md](docs/SDD.md) (Chương 5) |
| **Hà Đức Dương** | **Client GUI & Integration** | Desktop Qt6 Client, Multi-threading UI, Progress Bar, Web Client TypeScript | [docs/WBS.md](docs/WBS.md) (M4) & [docs/SDD.md](docs/SDD.md) (Chương 6) |

---

## 4. HƯỚNG DẪN CÀI ĐẶT MÔI TRƯỜNG (DEV-SETUP)

### 4.1 Yêu cầu hệ thống
* **Trình biên dịch C++**: Hỗ trợ C++17 (MSVC v142+ trên Windows, GCC 9+ hoặc Clang 10+ trên Linux/macOS).
* **Công cụ Build**: CMake 3.16 trở lên.
* **Giao diện GUI**: Qt 6 (khuyên dùng) hoặc Qt 5.
* **Công cụ Git**: Git v2.25+.

### 4.2 Hướng dẫn Build từ Command Line

#### Trên Windows (PowerShell / CMD với MSVC hoặc MinGW):
```powershell
# 1. Tạo thư mục build
mkdir build
cd build

# 2. Cấu hình CMake
cmake ..

# 3. Biên dịch (Build Release hoặc Debug)
cmake --build . --config Release
```

#### Trên Linux / macOS (Terminal):
```bash
# 1. Cài đặt các gói thư viện cần thiết (Ubuntu/Debian)
sudo apt update && sudo apt install -y build-essential cmake qt6-base-dev

# 2. Tạo thư mục build và biên dịch
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## 5. QUY TRÌNH LÀM VIỆC GIT (WORKFLOW & BRANCHING STRATEGY)

Để tránh xung đột và đảm bảo 4 thành viên làm việc độc lập 100%:

### 5.1 Quy tắc Nhánh (Branching Model)
* `main`: Nhánh chính thức, chỉ merge code khi đã hoàn thiện và kiểm thử đạt chuẩn.
* `develop`: Nhánh tích hợp chung của cả nhóm.
* **Nhánh tính năng của từng thành viên**:
  * Trần Sỹ Nguyên: `feat/m1-network-engine`
  * Bùi Đức Hiếu: `feat/m2-auth-permissions`
  * Vũ Nhật Minh Dương: `feat/m3-filesystem-transfer`
  * Hà Đức Dương: `feat/m4-gui-client`

### 5.2 Các bước bắt đầu công việc hàng ngày
```bash
# 1. Clone repository về máy
git clone <URL_REPOSITORY>
cd file-sharing-project

# 2. Chuyển sang nhánh develop và kéo code mới nhất
git checkout develop
git pull origin develop

# 3. Tạo nhánh làm việc cá nhân từ develop
git checkout -b feat/m1-network-engine   # (Ví dụ cho M1)

# 4. Sau khi code và test xong trên máy cá nhân
git add .
git commit -m "feat(network): implement 42-byte frame receiver loop"
git push origin feat/m1-network-engine

# 5. Tạo Pull Request (PR) vào nhánh develop trên GitHub để nhóm cùng review
```

### 5.3 Quy chuẩn viết Commit Message (Conventional Commits)
* `feat(...)`: Thêm tính năng mới (vd: `feat(auth): add sha256 password hashing`).
* `fix(...)`: Sửa lỗi (vd: `fix(transfer): fix off-by-one error in chunk index`).
* `docs(...)`: Cập nhật tài liệu (vd: `docs(sdd): complete chapter 4 on permissions`).
* `test(...)`: Bổ sung mã kiểm thử (vd: `test(framing): add unit test for sticky packets`).
* `refactor(...)`: Tối ưu hóa code nhưng không đổi logic nghiệp vụ.

---

## 6. HẠ TẦNG CI/CD VÀ QUY CHUẨN CHẤT LƯỢNG (CI/CD PIPELINE)

Dự án tích hợp sẵn hệ thống CI/CD tự động bằng **GitHub Actions** nhằm đảm bảo mã nguồn ổn định 100% trước khi merge:
* **Kiểm tra định dạng (`clang-format`)**: Bắt buộc tuân thủ quy tắc trong `.clang-format` (thụt đầu dòng 4 spaces, tối đa 100 ký tự/dòng).
* **Matrix Build đa nền tảng**: Tự động biên dịch trên cả `ubuntu-latest` (GCC) và `windows-latest` (MSVC) ở cả 2 chế độ `Release` và `Debug`.
* **An toàn Bộ nhớ (AddressSanitizer)**: Trên Linux Debug, CI tự động kích hoạt `ASan` và `UBSan` để bắt lỗi tràn bộ nhớ (Buffer Overflow) trong các mảng đệm socket.
* **Tự động Kiểm thử (`CTest`)**: Chạy bộ unit test trong `tests/test_protocol.cpp` để kiểm tra kích thước header 42 bytes, thuật toán CRC32 và ma trận quyền hạn.
* **Tự động Đóng gói Release (CD)**: Khi gắn tag phiên bản `v*.*.*`, hệ thống tự động build và tạo bản phát hành đính kèm file nén `.zip` (Windows) và `.tar.gz` (Linux).

👉 Xem chi tiết cấu trúc và hướng dẫn tại: [docs/CICD.md](docs/CICD.md).

---

## 7. TÀI LIỆU QUAN TRỌNG CẦN ĐỌC TRƯỚC KHI CODE
1. 📄 **Đặc tả Yêu cầu Phần mềm**: Đọc [docs/SRS.md](docs/SRS.md) để nắm chắc 7 yêu cầu chức năng bắt buộc.
2. 📋 **Checklist Nhiệm vụ 4 Thành viên**: Đọc [docs/WBS.md](docs/WBS.md) để theo dõi tiến độ theo từng tuần.
3. 📘 **Bản Thiết kế Phần mềm (HLD + LLD)**: Xem [docs/SDD.md](docs/SDD.md) và tự hoàn thiện chương được phân công.
4. ⚙️ **Hợp đồng Giao thức Cốt lõi**: Đọc kỹ [include/common/FSPProtocol.h](include/common/FSPProtocol.h) trước khi viết lệnh mạng.
5. 🚀 **Kiến trúc CI/CD & Kiểm thử**: Đọc [docs/CICD.md](docs/CICD.md) để biết cách test trên máy trước khi push.
6. 🛡️ **Quy chuẩn Kiểm thử & TDD Thực chiến**: Xem [docs/TDD/README.md](docs/TDD/README.md) để nắm ma trận 6 Probes đối kháng.
7. 🤖 **Chỉ dẫn Kỹ thuật cho Agent & Dev**: Đọc [AGENTS.md](AGENTS.md) để tuân thủ 5 nguyên tắc cấm tuyệt đối khi pair-programming.
