# 📋 ĐÁNH GIÁ PULL REQUEST (CODE REVIEW)

* **Nhánh:** `feat/m1-network-engine` $\rightarrow$ `develop`
* **Tiêu đề PR:** `feat(m1): C11 network core — M1.1 to M1.4, M1.6, M1.7`
* **Thành viên thực hiện:** Trần Sỹ Nguyên (M1 - 20235985)
* **Người đánh giá (Lead / Gatekeeper):** AI Pairing Partner & Lead Reviewer
* **Kết quả tổng quan:** **APPROVE (CHẤP THUẬN - XUẤT SẮC)** ⭐⭐⭐⭐⭐

---

## 1. TỔNG QUAN KIỂM ĐỊNH THỰC TẾ (VERIFICATION RUN)

PR đã được biên dịch và kiểm thử trực tiếp trong môi trường POSIX Linux (WSL Ubuntu):

| Hạng mục kiểm thử | Cờ biên dịch / Công cụ | Số ca test | Kết quả | Ghi chú |
|:---|:---|:---:|:---:|:---|
| **`fsp_test_protocol`** | `-std=c11 -Wall -Wextra -Wpedantic` | 4 / 4 | **PASS** | `FSPHeader` 42B, `FileChunkHeader` 88B, offset, CRC32 vector mẫu, bitmask Linux. |
| **`fsp_test_packet`** *(Probe 1)* | ASan + UBSan (`-fsanitize=address,undefined`) | 8 / 8 | **PASS** | Tách 2 frame/buffer, ghép frame 3 mảnh qua `socketpair`, bit-flip CRC32, chặn header độc hại. |
| **`fsp_test_json`** | ASan + UBSan | 6 / 6 | **PASS** | JSON phẳng, surrogate UTF-8, chặn `\u0000`, bỏ qua dòng TSV phía sau theo SDD §3.3. |
| **`fsp_test_network_core`** *(M1.7 / NFR-3)* | ASan + UBSan | 8 / 8 | **PASS** | 10 client đồng thời $\times$ 50 req; 500 frame dồn qua TCP; Fail-Closed đóng đúng socket lỗi. |
| **`ThreadSanitizer`** | TSan (`-fsanitize=thread`) | 8 / 8 | **PASS** | 0 data race, các mutex và atomic stop hoạt động chính xác. |
| **Server Daemon** | `-std=c11 -Wall -Wextra -Wpedantic` | Build target | **PASS** | `src/server/server_main.c` biên dịch sạch 100% không cảnh báo. |

---

## 2. ĐÁNH GIÁ CHI TIẾT THEO YÊU CẦU DỰ ÁN & ZERO-TOLERANCE RULES

### 2.1. Tuân thủ Nguyên tắc cốt lõi (AGENTS.md)
* **Không dùng thư viện cấp cao:** 100% sử dụng `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<pthread.h>`, `<sys/uio.h>`, `<poll.h>`.
* **Hợp đồng giao thức bất biến:** `include/common/FSPProtocol.h` giữ nguyên kích thước **42B** và **88B** với `#pragma pack(1)` và `_Static_assert`.
* **Fail-Closed:** Mọi lỗi `recv()`/`send()` $\le 0$, CRC32 lệch, payload vượt 128 KB đều lập tức đóng socket, chạy cleanup hook và giải phóng bộ nhớ sạch sẽ.
* **Không test đối phó:** Toàn bộ test chạy trên byte thật, kiểm tra buffer bộ nhớ, `socketpair()` và socket TCP loopback thật.

### 2.2. Điểm sáng về Kiến trúc & Kỹ thuật C11
* **Tách module Server (`fsp_server.c` và `server_main.c`):**
  Tách `include/server/fsp_server.h` và `src/server/fsp_server.c`. Nhờ vậy, suite `tests/test_network_core.c` có thể khởi chạy server thật in-process trên cổng ngẫu nhiên (`port = 0`), chạy test tích hợp song song mà không cần fork/exec phức tạp.
* **Gửi Frame một lần (`sendmsg` với 2 `iovec`):**
  Hàm `send_two()` trong `src/common/fsp_packet.c` gom Header 42B và Payload vào một `msghdr` duy nhất, giúp gói tin đi trọn vẹn trong một segment TCP, triệt tiêu độ trễ do thuật toán Nagle mà không cần ép `TCP_NODELAY`.
* **Vòng đời dừng Server an toàn (Graceful Shutdown):**
  Trong `fsp_server_destroy()`, server gọi `shutdown(fd, SHUT_RDWR)` lên toàn bộ client đang mở dưới `server->lock`, đánh thức các thread đang kẹt ở `recv()`, sau đó chờ qua `pthread_cond_wait(&s->all_gone, &s->lock)`. Đây là thiết kế mẫu mực cho server đa luồng POSIX.
* **Dispatcher không nghẽn:**
  Trong `src/server/fsp_dispatcher.c`, con trỏ hàm được lấy dưới lock nhưng được thực thi bên ngoài lock. Điều này đảm bảo khi M3 thực hiện truyền file lớn hoặc M2 truy vấn DB/file, luồng đó không hề block các client khác.
* **Bộ JSON phẳng an toàn cao (`fsp_json.c`):**
  Đầy đủ scanner kiểm tra ký tự điều khiển, giải mã surrogate pair UTF-8, chủ động từ chối `\u0000` để tránh cắt cụt chuỗi C. Cơ chế chỉ đọc object ở dòng đầu và bỏ qua các dòng TSV phía sau tuân thủ chính xác quy ước giao thức SDD §3.3.

---

## 3. GIẢI QUYẾT CÁC VẤN ĐỀ "CẦN NHÓM TRƯỞNG XEM"

1. **Thêm `fsp_server.h` / `fsp_server.c`:**
   > 👉 **Chấp thuận 100%.** Đây là kiến trúc sạch, rất tiện cho CI và CTest.
2. **`client_ctx_t.upload` tạm thời là `void*`:**
   > 👉 **Hợp lý.** Đây là pattern Opaque Pointer kinh điển trong C. M3 sẽ ép kiểu sang `fsp_upload_t*` khi hoàn thành module transfer.
3. **Ghim `clang-format==18.1.3` qua pip trong `ci.yml`:**
   > 👉 **Rất chuẩn xác.** Tránh tình trạng lệch phiên bản giữa máy cục bộ và GitHub Actions runner.
4. **Xóa `src/client_cli/main.cpp`:**
   > 👉 **Chấp thuận.** Dọn sạch tàn dư C++ để chuẩn bị cho M4 viết `cli_main.c`.
5. **Về `release.yml`:**
   > 👉 Vì FSP v2.0 đã chuyển hoàn toàn sang POSIX C11, workflow release hiện tại (vốn có build Windows và phụ thuộc `fsp_client_cli`) chắc chắn sẽ báo lỗi nếu gắn tag. Đề xuất: tạm thời comment matrix Windows hoặc chỉ đóng gói `fsp_server` Linux cho đến khi M4 hoàn thành CLI/GUI.

---

## 4. CÁC ĐIỂM CẦN CHỈNH SỬA NHẸ TRƯỚC KHI MERGE (ACTION ITEMS)

> [!WARNING]
> ### ⚠️ Lưu ý 1: Sửa lỗi gạch nhầm WBS (`docs/WBS.md`)
> Trong commit `69b90c1`, khi tick M1.3, M1.4, M1.7, mục **M1.6** (Probe 1) đã vô tình bị đổi từ `[x]` thành `[ ]`:
> ```diff
> -  - [x] **M1.6**: Viết test CTest Probe 1...
> +  - [ ] **M1.6**: Viết test CTest Probe 1...
> ```
> **Hành động:** Cần tick lại `[x]` cho M1.6 vì test `tests/test_packet.c` đã hoàn thành trọn vẹn 8 ca kiểm thử.

> [!NOTE]
> ### ⚠️ Lưu ý 2: Kế hoạch cho M1.5 (Heartbeat timeout)
> Hiện tại server đã phản hồi `PING` $\rightarrow$ `PONG` tức thời. Để hoàn tất M1.5, cần bổ sung socket timeout (sử dụng `setsockopt` với `SO_RCVTIMEO` đặt 30 giây theo `FSP_SOCKET_TIMEOUT_S`) trên client socket trong `client_thread`, để tự động ngắt và thu hồi tài nguyên nếu client im lặng quá thời gian quy định. (Có thể làm tiếp ở PR sau hoặc bổ sung trước khi đóng M1).

---

## 5. HƯỚNG DẪN TÍCH HỢP CHO THÀNH VIÊN 2 (M2) & THÀNH VIÊN 3 (M3)

Nhờ bộ khung Dispatcher và JSON trong PR này, M2 và M3 có thể cắm trực tiếp module vào hệ thống rất dễ dàng:

```c
/* 1. Mẫu Handler cho M2 (Đăng nhập) */
static int handle_auth_login(client_ctx_t *ctx, const fsp_frame_t *req) {
    char user[64], pass[64];
    if (fsp_json_get_str((const char *)req->payload, "username", user, sizeof(user)) != 0 ||
        fsp_json_get_str((const char *)req->payload, "password", pass, sizeof(pass)) != 0) {
        return fsp_send_error(ctx->fd, req->event, FSP_STATUS_BAD_REQUEST, "Thiếu thông tin đăng nhập");
    }
    // Xác thực salt + SHA-256 ...
    char fields[128];
    fsp_json_fields_t jf;
    fsp_jf_init(&jf, fields, sizeof(fields));
    fsp_jf_str(&jf, "token", "session_token_xyz");
    return fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, fields, NULL);
}

/* 2. Đăng ký lúc server khởi động (trong server_main.c hoặc hàm init của module) */
dispatcher_register(FSP_EV_AUTH_LOGIN, handle_auth_login);
dispatcher_register_chunk(handle_file_chunk);             // Dành cho M3
dispatcher_register_cleanup(handle_session_disconnect);    // M2 dọn session, M3 xóa .part
```

---

## 6. KẾT LUẬN

PR đạt chất lượng rất cao, đáp ứng đầy đủ các tiêu chuẩn kỹ thuật C11, giải quyết dứt điểm các probe kiểm thử khó (dính gói, phân mảnh, tải đồng thời, Fail-Closed) và mở đường thuận lợi cho M2, M3, M4 làm việc song song.

**Nhóm trưởng duyệt chấp thuận (Approve).** Sau khi tick lại M1.6 trong `docs/WBS.md`, bạn có thể merge vào `develop`.
