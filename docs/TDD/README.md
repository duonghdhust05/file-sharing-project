# 🛡️ BỘ QUY CHUẨN KIỂM THỬ THỰC CHIẾN (ADVERSARIAL TESTING & TDD FRAMEWORK)
## DỰ ÁN: CLIENT-SERVER ONLINE FILE SHARING APPLICATION (C11 POSIX SOCKETS)

> **Phạm vi & Tính linh hoạt:**  
> * **Quy trình TDD (viết test trước) là TÙY CHỌN** — thành viên có thể viết test trước hoặc code trước, test sau.  
> * **Chất lượng kiểm thử là BẮT BUỘC 100%** — tuyệt đối không viết test đối phó (`assert(1)`, mock luôn thành công, hay tự viết lại logic cần test trong file test).  
> * **Nguồn sự thật yêu cầu:** [SRS.md](../pdf-document/SRS.md) v3.1 & [AGENTS.md](../../AGENTS.md).

---

## 1. CHỐNG ẢO TƯỞNG & 4 BẪY CHẾT NGƯỜI TRONG KIỂM THỬ SOCKET C11

| Căn bệnh kiểm thử | Cơ chế gây sai lệch | Hậu quả thực tế | Biện pháp bắt buộc |
| :--- | :--- | :--- | :--- |
| **1. Tautological Testing** *(Test tự xưng)* | Tự gán `expected = 0xAA`, không gọi hàm thật hoặc không chạm vào buffer nhị phân thực tế | Test xanh nhưng framing trên socket thật sai offset hoặc thiếu hàm | **No-Tautology:** Mọi assertion phải kiểm tra dữ liệu thật từ hàm module, đĩa thật, socket thật (`tests/test_util.h`) |
| **2. Happy-Path Monoculture** *(Chỉ test màu hồng)* | Chỉ test chuỗi ngắn 10 byte với 1 client tuần tự | Khi truyền file 500 MB hoặc mạng phân mảnh sẽ dính gói/rách frame và crash | Bắt buộc chạy **Probe 1** (dính/tách gói) và **Probe 4** (sai CRC32 chunk) |
| **3. Memory Corruption Blind Spot** *(Vùng mù bộ nhớ)* | Test chỉ kiểm tra logic đúng mà bỏ qua rò rỉ `malloc`/file descriptor hoặc tràn mảng | Chạy nhiều client đồng thời dẫn tới OOM hoặc `SIGSEGV` | Chạy toàn bộ `ctest` dưới `-fsanitize=address,undefined` (ASan + UBSan) |
| **4. Deceptive Fallback** *(Nuốt lỗi socket)* | `recv()`/`send()` trả về `<= 0` nhưng bỏ qua, ghi dữ liệu rỗng xuống đĩa | Hỏng file gốc, để lại file rác `.part` | **Fail-Closed:** Lỗi I/O hoặc sai CRC32 → đóng socket, hủy phiên, `unlink` file `.part`, trả mã lỗi chuẩn (`401`/`403`/`400`/`500`) |

---

## 2. KIẾN TRÚC TEST 4 TẦNG PHỤC VỤ KIỂM THỬ ĐỘC LẬP

1. **Tầng 1 — Pure Unit Test (Zero-IO / Sans-IO):** Hàm xử lý nhị phân, JSON, kiểm tra quyền `fsp_can_access()` và chuẩn hóa chuỗi đường dẫn nhận buffer/struct C thuần, không gọi `recv`/`send`/`open`.
2. **Tầng 2 — Component Socket/FS Test (`socketpair` & `mkdtemp`):** Giả lập phân mảnh TCP, dính gói và ngắt kết nối giữa chừng bằng `socketpair(AF_UNIX, SOCK_STREAM, 0, sv)` trên thư mục tạm `mkdtemp()`, kèm kiểm tra `CHECK_NO_PART_FILES`.
3. **Tầng 3 — Integration & Concurrency Test (Loopback `port = 0` + TSan):** Khởi chạy `fsp_server_create(0, ...)` trên port động và gọi từ client TCP thật (`fsp_client_api` / `pthread`), kiểm tra sạch race condition dưới `-fsanitize=thread` (`NFR-C3`).
4. **Tầng 4 — Resource & FD Hygiene:** Kiểm tra rò rỉ file descriptor bằng `CHECK_NO_FD_LEAK` (`/proc/self/fd`), chạy `ctest` headless dưới `-fsanitize=address,undefined` (`NFR-G1`), và theo dõi RSS ổn định khi truyền file 500 MB (`NFR-B1`).

> **Quy ước truy vết (`@covers`):** Mỗi hàm `test_*()` cần ghi chú `/* @covers: UCxx, FR-x.y, NFR-xx */` ngay phía trên để đối chiếu nhanh với [UC-CHECKLIST.md](../uc-details/UC-CHECKLIST.md) và [SRS.md](../pdf-document/SRS.md) §9.

---

## 3. MA TRẬN 6 PROBE ĐỐI KHÁNG (THE 6 ADVERSARIAL PROBES)

Mỗi tính năng trong dự án phải được bao phủ bởi **6 Probe** sau (tổng hợp từ SRS §9 & `AGENTS.md`):

| Mã Probe | Tên Probe | Tình huống đối kháng | Tiêu chí đạt (Passing Criteria) | Phụ trách |
| :---: | :--- | :--- | :--- | :---: |
| **PROBE-1** | **Dính gói & Tách gói** | Gửi 2 frame liên tiếp trong 1 buffer; hoặc xé 1 frame thành nhiều lần `recv()` rời rạc | `fsp_parse_frames` / `fsp_recv_frame` tách và ghép đúng từng frame 42B + payload + CRC32 (`tests/test_packet.c`) | **M1** |
| **PROBE-2** | **Cô lập Phiên & Token** | Không token, token giả, token đã logout (hoặc hết hạn 30 phút / khóa 5 lần sai — COULD) | Server từ chối ngay với `401 Unauthorized` (`tests/test_auth_permissions.c`) | **M2** |
| **PROBE-3** | **Phân quyền Linux + Sharing Level** | Kiểm tra đủ **bước 0–7** của `fsp_can_access`; file `PRIVATE` bật bit `0666` vẫn từ chối người ngoài | `fsp_can_access()` trả `false`, server trả `403 Forbidden` (`tests/test_auth_permissions.c`) | **M2** |
| **PROBE-4** | **Chunk hỏng & CRC32** | Lật 1 bit bất kỳ trong khối `64 KB` (`FILE_CHUNK`) đang upload | Phát hiện `chunk_crc` không khớp, hủy phiên truyền và xóa sạch file `.part` | **M3** |
| **PROBE-5** | **Vượt Sandbox (Path Traversal)** | Gửi `../../../../etc/passwd`, `cd ../../..`, hoặc symlink thoát khỏi `./storage/server_root` | `vfs_resolve` (`realpath()`) chặn đứng tại biên gốc và trả `403 Forbidden` | **M3** |
| **PROBE-6** | **Đồng thời & An toàn bộ nhớ** | Ít nhất **5 client** `pthread` đồng thời đăng ký, đăng nhập, phân quyền, truyền file | 0 data race, 0 memory leak / UB dưới `ctest` với `-fsanitize=address,undefined` | **Cả nhóm** |

---

## 4. QUY TRÌNH LIGHT DUAL-GATE (OPTIONAL WORKFLOW GATES)

Với các module nghiệp vụ/mạng (đặc biệt M3 và phần tích hợp mạng M2/M4; không ép lên code vẽ widget GTK thuần túy), áp dụng chu trình **Light Dual-Gate**:
1. **Micro-Plan (< 80 dòng):** Chốt phạm vi bằng [micro-plan-template.md](templates/micro-plan-template.md) (ranh giới bộ nhớ, Sans-IO seam, file chạm vào, 4 tầng test dự kiến).
2. **Gate 1 — Spec & Red Test:** Chỉ sửa `.h`, viết hàm stub trả lỗi mặc định trong `.c` và viết trước `tests/test_[module].c` theo mẫu [c11_test_template.c](templates/c11_test_template.c) (dựa trên [test_util.h](../../tests/test_util.h), gắn `/* @covers: ... */`) → chạy `ctest` xác nhận **FAIL (RED)**.
3. **Gate 2 — Green & 4-Trap Audit:** Giữ nguyên assertion của test; cài đặt code C11 trong `src/` với mẫu dọn dẹp `goto cleanup` và rà 4 bẫy:
   * Có nhánh `return` sớm nào quên `free()`/`fclose()`/`close()` (kiểm tra thêm bằng `CHECK_NO_FD_LEAK`)?
   * Có kiểm tra `payload_len <= FSP_BUFFER_SIZE` (128 KB) và dùng `snprintf`/`memcpy` an toàn biên không?
   * Có hàm giả/hardcode trả `true` để qua mặt test không?
   * `recv()`/`send()` `<= 0` (khác `EINTR`) có đóng socket, hủy phiên và xóa `.part` (`CHECK_NO_PART_FILES`) ngay không?
4. **Nghiệm thu CI (`ASan+UBSan` & `TSan`):**
   * Chạy `clang-format` (v18.1.3) và `ctest --output-on-failure` dưới cả `-fsanitize=address,undefined` (`NFR-G1`) lẫn `-fsanitize=thread` (`NFR-C3`).
