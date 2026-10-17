# FSP Web Client (TypeScript Expansion - Phase 3 & 4)

Thư mục này dành cho module **Web Client (TypeScript + React/Vite + Tailwind CSS)** và **WebSocket Protocol Gateway**.

## 1. Kiến trúc Cầu nối (Gateway Architecture)
Do trình duyệt Web không thể mở trực tiếp Raw TCP Socket, hệ thống sử dụng một Gateway siêu nhẹ:
```
[Browser (TypeScript)] <--- (WebSocket: ws://localhost:8080) ---> [Node.js Gateway] <--- (Raw TCP FSP) ---> [C++ Server :8088]
```

## 2. Kế hoạch triển khai
- **Giai đoạn**: Khởi động vào cuối Tuần 3 sau khi C++ Server đã hoàn thành và test ổn định upload/download file.
- **Mục đích**: Phục vụ bài thuyết trình trực quan, hỗ trợ kéo thả file, demo đa nền tảng thời gian thực.
- **Tài liệu tham khảo**: Xem chi tiết trong [docs/SDD.md](../docs/SDD.md) (Chương 6.3) và [docs/WBS.md](../docs/WBS.md).
