#ifndef FSP_SERVER_H
#define FSP_SERVER_H

/*
 * Server core (M1.4 - SDD §4.3): vòng lặp accept + 1 thread / client.
 *
 *   luồng chính: fsp_server_run() -> poll + accept -> malloc client_ctx_t
 *                -> pthread_create(client_thread) -> pthread_detach
 *   client_thread: lặp fsp_recv_frame -> PING trả PONG; FILE_CHUNK / EVENT_REQ -> dispatcher
 *                  lỗi / mất kết nối -> dispatcher_run_cleanups -> close(fd) -> free(ctx)
 *
 * Tách khỏi server_main.c để test tích hợp chạy được server thật trong cùng tiến trình.
 */

#include <stdint.h>

typedef struct fsp_server fsp_server_t;

/** Mở cổng lắng nghe (port = 0: OS tự chọn). NULL nếu lỗi (errno giữ nguyên nhân). */
fsp_server_t *fsp_server_create(uint16_t port, int backlog);

/** Cổng thực tế đang lắng nghe. */
uint16_t fsp_server_port(const fsp_server_t *server);

/** Bật/tắt log kết nối ra stdout (mặc định bật). */
void fsp_server_set_verbose(fsp_server_t *server, int verbose);

/**
 * Chạy vòng lặp accept trên luồng gọi, chặn cho tới khi fsp_server_request_stop() được gọi.
 * Kiểm tra cờ dừng mỗi 200 ms. Trả về 0 khi dừng bình thường.
 */
int fsp_server_run(fsp_server_t *server);

/** Yêu cầu dừng. Chỉ ghi một biến atomic lock-free -> gọi được từ signal handler (Ctrl+C). */
void fsp_server_request_stop(fsp_server_t *server);

/** Số client thread đang chạy. */
int fsp_server_active_clients(fsp_server_t *server);

/** Tổng số kết nối đã accept. */
uint64_t fsp_server_total_accepted(fsp_server_t *server);

/**
 * Dọn dẹp sau khi fsp_server_run() đã trả về: ngắt (shutdown) mọi client còn kết nối,
 * CHỜ toàn bộ client thread thoát, đóng cổng lắng nghe rồi giải phóng server.
 */
void fsp_server_destroy(fsp_server_t *server);

#endif /* FSP_SERVER_H */
