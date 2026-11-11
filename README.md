# FSP: Client-Server Online File Sharing Application
> **Đồ án Môn học: Lập trình Mạng (Networking Program) - Đề tài 6 (Topic 6) - Nhóm 7**  
> **Ngôn ngữ chính:** C (Chuẩn C11 - Raw TCP Sockets POSIX `<sys/socket.h>`, `pthread`)  
> **Giao diện:** GTK Desktop Client (C11) & CLI Client  

[![CI Pipeline](https://github.com/duonghdhust05/file-sharing-project/actions/workflows/ci.yml/badge.svg)](https://github.com/duonghdhust05/file-sharing-project/actions/workflows/ci.yml)
[![C11](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-brightgreen.svg)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## 1. TỔNG QUAN DỰ ÁN
Dự án xây dựng một hệ thống chia sẻ tệp tin trực tuyến hoàn chỉnh theo mô hình Client-Server bằng ngôn ngữ **C (C11)**, tham chiếu thiết kế và trải nghiệm từ **FileZilla** (FTP Client/Server) và tùy biến cho mạng LAN. Hệ thống không sử dụng bất kỳ thư viện mạng cấp cao nào mà tự hiện thực toàn bộ từ **POSIX Raw TCP Sockets (`<sys/socket.h>`)**, bao gồm:
* **Giao thức Tự tạo (Custom Application-Layer Protocol - FSP)**: Thiết kế chuẩn frame cố định 42-byte (`__attribute__((packed))`), giải quyết triệt để vấn đề dính gói (sticky packets) và phân mảnh gói (packet fragmentation) của luồng TCP.
* **Cơ chế Điều phối Sự kiện (Event Dispatcher)**: Định tuyến gói tin theo tên sự kiện (`AUTH_LOGIN`, `DIR_LIST`, `FILE_UPLOAD_REQ`...) qua bảng con trỏ hàm C.
* **Hệ thống Quản lý Người dùng & Phân quyền POSIX/Linux**: Đăng ký, đăng nhập bảo mật (Salted SHA-256 + CSPRNG salt), phân quyền 9-bit Owner-Group-Others ($rwsrwsrws$) kết hợp mức độ chia sẻ Private / Shared (với `share_list`) / Public.
* **Truyền Tệp & Thư mục Khối lớn**: Chunking $64\text{ KB}$ kèm header nhị phân 88-byte, kiểm tra CRC32 từng chunk (file tạm `.part`), truyền bằng bộ đệm cố định không làm RAM tăng theo kích thước file, hỗ trợ truyền đệ quy cả cây thư mục qua Manifest.
* **Giao diện Trực quan (Desktop GUI)**: Ứng dụng Desktop viết bằng **C11 + GTK 3** bố cục 2 ngăn (Local/Remote) giống FileZilla, tách biệt luồng giao diện (UI Thread) và luồng mạng (Worker Thread qua `pthread` + `g_idle_add`), dùng chung 100% thư viện C socket & framing với Server.
* **Hạ tầng CI/CD Chuẩn Công nghiệp**: Tự động hóa kiểm tra định dạng code (`clang-format`), biên dịch CMake, kiểm tra an toàn bộ nhớ (`AddressSanitizer`, `UBSan`) và chạy bộ kiểm thử tự động (`CTest`).

---

## 2. CẤU TRÚC THƯ MỤC DỰ ÁN

```
file-sharing-project/
├── .github/                      # CI/CD PIPELINE (GITHUB ACTIONS)
│   └── workflows/
│       ├── ci.yml                # CI: Format Check, Build, Sanitizers (ASan/UBSan) & CTest
│       └── release.yml           # CD: Tự động đóng gói binary khi release tag
├── .clang-format                 # Chuẩn hóa format code C cho toàn nhóm
├── CMakeLists.txt                # Cấu hình build CMake & CTest
├── AGENTS.md                     # Hiến pháp kỹ thuật & 5 nguyên tắc cấm tuyệt đối
├── README.md                     # Hướng dẫn dự án, setup môi trường & Git workflow
├── fsp_presentation_slides_md.md # Nội dung Slide thuyết trình & Báo cáo tiến độ Checkpoint 1
├── docs/                         # BỘ TÀI LIỆU CHUẨN MỰC CỦA DỰ ÁN
│   ├── README.md                 # [BẮT BUỘC ĐỌC ĐẦU TIÊN] Hướng dẫn đọc tài liệu & Bàn giao nhiệm vụ
│   ├── SRS.md                    # Bản Đặc tả Yêu cầu Phần mềm (v3.0 - Nguồn sự thật duy nhất)
│   ├── LEARNING-ROADMAP.md       # Lộ trình kiến thức nền, 6 nguyên tắc cứng & 11 câu hỏi vấn đáp
│   ├── WBS.md                    # Phân rã Công việc & Checklist 4 thành viên (v2.0)
│   ├── SDD.md                    # Bản Thiết kế Phần mềm (v2.0 - HLD + LLD)
│   ├── CICD.md                   # Bản Thiết kế Kiến trúc CI/CD Chuẩn Công nghiệp
│   ├── C-MIGRATION-PLAN.md       # Kế hoạch chuẩn hóa ngôn ngữ C11 & Báo cáo khảo sát nhánh
│   └── TDD/                      # Quy chuẩn Kiểm thử Đối kháng (6 Adversarial Probes)
├── include/                      # CÁC FILE HEADER & INTERFACE CONTRACTS (.h)
│   ├── common/
│   │   ├── FSPProtocol.h         # [QUAN TRỌNG NHẤT] Định nghĩa Frame 42B, Chunk 88B, Events, Bitmasks
│   │   └── Config.h              # Hằng số cấu hình hệ thống & CRC32 IEEE 802.3
│   ├── server/                   # Headers dành riêng cho Server (Auth, Perm, VFS, Transfer)
│   └── client/                   # Headers dành riêng cho Client (CLI & GTK GUI)
├── src/                          # MÃ NGUỒN THỰC THI
│   ├── common/                   # Thư viện cốt lõi dùng chung (Socket, Framing, CRC32, SHA-256)
│   ├── server/                   # FSP Server Daemon (ThreadPool, Auth, Permission, VFS, Search)
│   ├── client_cli/               # Công cụ dòng lệnh CLI phục vụ kiểm thử tự động/thủ công
│   └── client_gui/               # Ứng dụng Desktop GUI (C11 + GTK)
├── tests/                        # HỆ THỐNG KIỂM THỬ TỰ ĐỘNG (UNIT & ADVERSARIAL TESTS)
└── storage/                      # VÙNG LƯU TRỮ SANDBOX (Được gitignore)
    ├── server_root/              # Thư mục gốc lưu trữ cô lập của Server
    └── client_downloads/         # Thư mục tải về mặc định của Client
```

---

## 3. PHÂN CÔNG VAI TRÒ NHÓM 4 THÀNH VIÊN (GROUP 7)

| Thành viên | MSSV | Phụ trách chính | Module cốt lõi (C11) | Checklist & Báo cáo SDD |
| :--- | :---: | :--- | :--- | :--- |
| **Trần Sỹ Nguyên** | 20235985 | **M1: Core Network Engine** | Raw POSIX TCP Socket, Framing 42-byte, `send_exact`/`recv_exact`, Event Dispatcher, Server 1 thread/client, Heartbeat | [docs/WBS.md](docs/WBS.md) (M1) & [docs/SDD.md](docs/SDD.md) (Chương 4) |
| **Bùi Đức Hiếu** | 20235931 | **M2: Auth & Permissions** | Quản lý User, Salted SHA-256, Session Token, Thuật toán phân quyền Linux 9-bit bước 0–7, `PERM_GET`/`PERM_SET` đệ quy | [docs/WBS.md](docs/WBS.md) (M2) & [docs/SDD.md](docs/SDD.md) (Chương 5) |
| **Vũ Nhật Minh Dương** | 20235925 | **M3: Filesystem & Transfer** | Virtual Sandbox (`realpath`), Lệnh FTP, Chunking 64KB, Truyền Folder đệ quy qua Manifest, Tìm kiếm lọc theo quyền | [docs/WBS.md](docs/WBS.md) (M3) & [docs/SDD.md](docs/SDD.md) (Chương 6) |
| **Hà Đức Dương** | 20235923 | **M4: Client GUI & Integration** | GTK 3 Desktop Client (C11), Đa luồng UI (`pthread` + `g_idle_add`), Transfer Manager, Permission UI, CLI test client | [docs/WBS.md](docs/WBS.md) (M4) & [docs/SDD.md](docs/SDD.md) (Chương 7) |

---

## 4. HƯỚNG DẪN CÀI ĐẶT MÔI TRƯỜNG (DEV-SETUP)

### 4.1 Yêu cầu hệ thống
* **Hệ điều hành mục tiêu**: Linux (Ubuntu 20.04/22.04/24.04 hoặc WSL2 trên Windows).
* **Trình biên dịch C**: GCC 9+ hoặc Clang 10+ hỗ trợ chuẩn **C11** (`-std=c11`).
* **Thư viện hệ thống**: POSIX Threads (`pthread`), POSIX Sockets (`<sys/socket.h>`), GTK3/GTK4 (`libgtk-3-dev` hoặc `libgtk-4-dev` cho Desktop Client).
* **Công cụ Build**: CMake 3.16 trở lên & CTest.

### 4.2 Hướng dẫn Build trên Linux / WSL2 (Terminal):
```bash
# 1. Cài đặt các gói thư viện cần thiết (Ubuntu/Debian)
sudo apt update && sudo apt install -y build-essential cmake clang-format libgtk-3-dev

# 2. Tạo thư mục build và biên dịch
mkdir -p build && cd build
cmake ..
cmake --build . --parallel

# 3. Chạy kiểm thử tự động
ctest --output-on-failure
```

---

## 5. QUY TRÌNH LÀM VIỆC GIT (WORKFLOW & BRANCHING STRATEGY)

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
git clone https://github.com/duonghdhust05/file-sharing-project.git
cd file-sharing-project

# 2. Chuyển sang nhánh làm việc cá nhân
git checkout feat/m1-network-engine   # (Ví dụ cho M1)
git pull origin develop

# 3. Sau khi code, format và chạy ctest pass 100% trên máy cá nhân
git add .
git commit -m "feat(network): implement 42-byte frame receiver loop in C11"
git push origin feat/m1-network-engine

# 4. Tạo Pull Request (PR) vào nhánh develop trên GitHub để nhóm cùng review
```

### 5.3 Quy chuẩn viết Commit Message (Conventional Commits)
* `feat(...)`: Thêm tính năng mới (vd: `feat(auth): implement salted sha256 in C11`).
* `fix(...)`: Sửa lỗi (vd: `fix(transfer): fix off-by-one error in chunk index`).
* `docs(...)`: Cập nhật tài liệu (vd: `docs(sdd): complete chapter 4 on permissions`).
* `test(...)`: Bổ sung mã kiểm thử (vd: `test(framing): add unit test for sticky packets`).
* `refactor(...)`: Tối ưu hóa hoặc chuyển đổi ngôn ngữ sang C11.

---

## 6. HẠ TẦNG CI/CD VÀ QUY CHUẨN CHẤT LƯỢNG (CI/CD PIPELINE)

Dự án tích hợp sẵn hệ thống CI/CD tự động bằng **GitHub Actions** nhằm đảm bảo mã nguồn ổn định 100% trước khi merge:
* **Kiểm tra định dạng (`clang-format`)**: Bắt buộc tuân thủ quy tắc trong `.clang-format` (thụt đầu dòng 4 spaces, tối đa 100 ký tự/dòng).
* **Build & Kiểm thử tự động (`CTest`)**: Kiểm tra kích thước header 42 bytes, chunk header 88 bytes, thuật toán CRC32 IEEE 802.3 và ma trận quyền hạn.
* **An toàn Bộ nhớ (`AddressSanitizer` & `UBSan`)**: Trên Linux Debug, CI tự động kích hoạt `-fsanitize=address,undefined` để bắt lỗi tràn bộ nhớ (Buffer Overflow) và rò rỉ bộ nhớ (Memory Leak) trong C.

👉 Xem chi tiết cấu trúc và hướng dẫn tại: [docs/CICD.md](docs/CICD.md).

---

## 7. TÀI LIỆU QUAN TRỌNG CẦN ĐỌC TRƯỚC KHI CODE
1. 🧭 **Hướng dẫn Bàn giao & Đọc Tài liệu**: Đọc [docs/README.md](docs/README.md) **đầu tiên** để nắm bản đồ tài liệu, 6 nguyên tắc cứng và bảng phân công cá nhân.
2. 📄 **Đặc tả Yêu cầu Phần mềm**: Đọc [docs/SRS.md](docs/SRS.md) v3.0 để nắm chắc 7 yêu cầu chức năng bắt buộc, mô hình FileZilla và bảng truy vết.
3. 🎓 **Lộ trình Kiến thức & Kế hoạch 2 tuần**: Đọc [docs/LEARNING-ROADMAP.md](docs/LEARNING-ROADMAP.md) để tự kiểm tra kiến thức và chuẩn bị 11 câu hỏi phản biện.
4. 📋 **Checklist Nhiệm vụ 4 Thành viên**: Đọc [docs/WBS.md](docs/WBS.md) để theo dõi tiến độ theo từng mốc.
5. 📘 **Bản Thiết kế Phần mềm (HLD + LLD)**: Xem [docs/SDD.md](docs/SDD.md) và tự hoàn thiện chương được phân công.
6. 🔄 **Kế hoạch Chuẩn hóa C11 & Khảo sát Checkpoint 1**: Đọc [docs/C-MIGRATION-PLAN.md](docs/C-MIGRATION-PLAN.md).
7. ⚙️ **Hợp đồng Giao thức Cốt lõi**: Đọc kỹ [include/common/FSPProtocol.h](include/common/FSPProtocol.h) trước khi viết lệnh mạng.
8. 🚀 **Kiến trúc CI/CD & Kiểm thử**: Đọc [docs/CICD.md](docs/CICD.md) để biết cách test trên máy trước khi push.
9. 🛡️ **Quy chuẩn Kiểm thử & TDD Thực chiến**: Xem [docs/TDD/README.md](docs/TDD/README.md) để nắm ma trận 6 Probes đối kháng.
10. 🤖 **Chỉ dẫn Kỹ thuật cho Agent & Dev**: Đọc [AGENTS.md](AGENTS.md) để tuân thủ các nguyên tắc cấm tuyệt đối.
