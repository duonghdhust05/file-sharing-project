# Đặc tả chi tiết các Use Case (`docs/uc-details` — Baseline v1.0)

Thư mục này chứa bản đặc tả chi tiết **14 mục + biểu đồ Mermaid + tích hợp 27 NFR chính thức** cho toàn bộ **13 Use Case chính thức (`v1.0`)** của dự án, đối chiếu với [SRS.md](../pdf-document/SRS.md) v3.1, [NFR-CHECKLIST.md](NFR-CHECKLIST.md) và [FSPProtocol.h](../../include/common/FSPProtocol.h).

> * Theo dõi tiến độ nghiệm thu tổng thể tại: [UC-CHECKLIST.md](UC-CHECKLIST.md)
> * Tài liệu NFR đã chốt (20 Core MUST + 7 Bổ trợ SHOULD): [NFR-CHECKLIST.md](NFR-CHECKLIST.md)

## Danh sách Use Case

| Mã | Tên Use Case | Yêu cầu gốc | Ưu tiên | FR / NFR liên quan | Phụ trách | Tài liệu |
|:-:|:---|:-:|:-:|---|:-:|:---|
| UC01 | Đăng ký tài khoản | R1 | MUST | FR-1.1, FR-1.2, FR-1.3 | M2 | [UC01-dang-ky-tai-khoan.md](UC01-dang-ky-tai-khoan.md) |
| UC02 | Đăng nhập | R1 | MUST | FR-1.4, FR-1.6, FR-1.7, FR-7.1, NFR-7 | M2 | [UC02-dang-nhap.md](UC02-dang-nhap.md) |
| UC03 | Đăng xuất | R1 | MUST | FR-1.5 | M2 | [UC03-dang-xuat.md](UC03-dang-xuat.md) |
| UC04 | Liệt kê nội dung thư mục (`ls`) | R2 | MUST | FR-2.1, FR-6.1, FR-7.2, NFR-5 | M3 | [UC04-liet-ke-thu-muc.md](UC04-liet-ke-thu-muc.md) |
| UC05 | Đổi thư mục hiện tại (`cd`, `pwd`) | R2 | MUST | FR-2.2, FR-2.3, FR-6.1, FR-7.2, NFR-5 | M3 | [UC05-doi-thu-muc-hien-tai.md](UC05-doi-thu-muc-hien-tai.md) |
| UC06 | Tạo thư mục (`mkdir`) | R2 | MUST | FR-2.4, FR-6.1, FR-6.2, FR-6.7, NFR-5 | M3 | [UC06-tao-thu-muc.md](UC06-tao-thu-muc.md) |
| UC07 | Xóa (`rm`) / Đổi tên (`mv`) | R2 | SHOULD | FR-2.5, FR-2.6, FR-6.1, NFR-5 | M3 | [UC07-xoa-doi-ten.md](UC07-xoa-doi-ten.md) |
| UC08 | Upload một file | R3 | MUST | FR-3.1–3.6, FR-6.1, FR-6.2, FR-7.3, FR-7.4, NFR-1–4, NFR-6 | M3 + M1 | [UC08-upload-file.md](UC08-upload-file.md) |
| UC09 | Download một file | R3 | MUST | FR-3.1–3.3, FR-3.6, FR-6.1, FR-7.3, FR-7.4, NFR-1–3, NFR-6 | M3 + M1 | [UC09-download-file.md](UC09-download-file.md) |
| UC10 | Upload cả thư mục | R4 | MUST | FR-4.1, FR-4.2, FR-4.4, FR-3, FR-6.1, FR-6.2, NFR-1 | M3 | [UC10-upload-thu-muc.md](UC10-upload-thu-muc.md) |
| UC11 | Download cả thư mục | R4 | MUST | FR-4.1–4.4, FR-3, FR-6.1, NFR-1 | M3 | [UC11-download-thu-muc.md](UC11-download-thu-muc.md) |
| UC12 | Tìm kiếm file theo tên + tải từ kết quả | R5 | MUST | FR-5.1–5.5, FR-6.1, FR-7.5 (include UC09) | M3 + M2 | [UC12-tim-kiem-file.md](UC12-tim-kiem-file.md) |
| UC13 | Xem / đặt quyền và mức chia sẻ | R6 | MUST | FR-6.1–6.6, FR-7.6, NFR-5 | M2 | [UC13-phan-quyen-chia-se.md](UC13-phan-quyen-chia-se.md) |
