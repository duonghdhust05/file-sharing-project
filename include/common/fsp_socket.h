#ifndef FSP_SOCKET_H
#define FSP_SOCKET_H

/*
 * Bộ bọc POSIX TCP socket (M1.1 - SDD §4.1). Mọi hàm làm việc trực tiếp với file descriptor.
 * Quy ước trả về: fd >= 0 hoặc 0 = thành công, -1 = lỗi (errno giữ nguyên nguyên nhân).
 */

#include <stddef.h>
#include <stdint.h>

/**
 * Tạo socket lắng nghe trên mọi interface (0.0.0.0) với SO_REUSEADDR.
 * port = 0: hệ điều hành chọn cổng trống (dùng trong test, xem fsp_local_port).
 * @return fd lắng nghe, hoặc -1
 */
int fsp_listen(uint16_t port, int backlog);

/**
 * Kết nối TCP tới host (IPv4 dạng số hoặc tên miền như "localhost").
 * @return fd đã kết nối, hoặc -1
 */
int fsp_connect(const char *host, uint16_t port);

/**
 * Gửi đủ len byte (lặp tới khi xong, tự xử lý EINTR, không phát SIGPIPE).
 * @return 0 = OK, -1 = lỗi hoặc đối phương đã đóng -> người gọi phải đóng kết nối
 */
int send_exact(int fd, const void *buf, size_t len);

/**
 * Nhận đủ len byte (lặp tới khi đủ, tự xử lý EINTR) - giải quyết dính gói / phân mảnh TCP.
 * @return 0 = OK, -1 = đối phương đóng (recv == 0) hoặc lỗi -> người gọi phải đóng kết nối
 */
int recv_exact(int fd, void *buf, size_t len);

/** Cổng cục bộ thực tế của fd (hữu ích khi fsp_listen với port 0). 0 nếu lỗi. */
uint16_t fsp_local_port(int fd);

/** Ghi địa chỉ "ip:port" của đối phương vào out (cỡ >= 32). Trả về 0 hoặc -1. */
int fsp_peer_address(int fd, char *out, size_t out_size);

/** Đóng fd nếu hợp lệ (bỏ qua EINTR). An toàn khi fd < 0. */
void fsp_close(int fd);

#endif /* FSP_SOCKET_H */
