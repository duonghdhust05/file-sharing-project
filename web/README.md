# FSP Web Client (LƯU TRỮ / NGOÀI PHẠM VI ĐỒ ÁN - WON'T)

> ⚠️ **LƯU Ý QUAN TRỌNG:**  
> Theo [docs/SRS.md](../docs/SRS.md) v3.0 và [AGENTS.md](../AGENTS.md), mô-đun Web Client là **WON'T (Không thực hiện trong phạm vi đồ án môn học 2 tuần)**.  
> Trọng tâm duy nhất của đề tài Topic 6 là: **C11 Raw Socket Server, CLI test client và Desktop GUI GTK 3**.  
> Thư mục này chỉ mang tính chất lưu trữ ý tưởng định hướng mở rộng trong tương lai sau khi đồ án đã hoàn thành bảo vệ.

---

## 1. Ý tưởng mở rộng tương lai (Future Concept)
Nếu mở rộng sang Web, do trình duyệt Web không thể mở trực tiếp Raw TCP Socket, hệ thống sẽ cần một Gateway chuyển đổi:
```
[Browser (TypeScript)] <--- (WebSocket: ws://localhost:8080) ---> [Node.js Gateway] <--- (Raw TCP FSP) ---> [C11 Server :8088]
```

## 2. Trạng thái hiện tại
* **Đồ án môn học:** KHÔNG triển khai. Mọi nỗ lực tập trung 100% vào các yêu cầu cốt lõi (MUST) của C11 + GTK 3.
