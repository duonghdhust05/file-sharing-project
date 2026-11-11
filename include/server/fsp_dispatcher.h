#ifndef FSP_DISPATCHER_H
#define FSP_DISPATCHER_H

/*
 * Event Dispatcher (M1.3 - SDD §4.3): bảng tĩnh {tên sự kiện, hàm xử lý}, tìm tuyến tính.
 * Các module đăng ký handler LÚC KHỞI ĐỘNG (trước khi server nhận client):
 *
 *   static int on_login(client_ctx_t *ctx, const fsp_frame_t *req) {
 *       char user[64];
 *       if (fsp_json_get_str((const char *)req->payload, "username", user, sizeof(user)) != 0)
 *           return fsp_send_error(ctx->fd, req->event, FSP_STATUS_BAD_REQUEST, "thiếu username");
 *       ...
 *       return fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, fields, NULL);
 *   }
 *   dispatcher_register(FSP_EV_AUTH_LOGIN, on_login);
 *
 * Giá trị trả về của handler: 0 = giữ kết nối, -1 = đóng kết nối (vd: gửi phản hồi thất bại).
 */

#include <stdint.h>

#include "common/Config.h"
#include "common/fsp_packet.h"

#define FSP_DISPATCHER_MAX_HANDLERS 32
#define FSP_DISPATCHER_MAX_CLEANUPS 8

/** Trạng thái của MỘT kết nối client, chỉ thread của client đó đọc/ghi. */
typedef struct client_ctx {
    int fd;
    uint64_t conn_id;       /* số thứ tự kết nối (để ghi log) */
    char peer[64];          /* "ip:port" của client */
    char token[65];         /* rỗng nếu chưa đăng nhập (M2 điền) */
    uint32_t user_id;       /* 0 nếu chưa đăng nhập (M2 điền) */
    char cwd[FSP_PATH_MAX]; /* thư mục ảo hiện tại, mặc định "/" (M3 dùng) */
    void *upload; /* trạng thái upload dở của M3 (fsp_upload_t*), NULL nếu không */
} client_ctx_t;

typedef int (*fsp_handler_fn)(client_ctx_t *ctx, const fsp_frame_t *req);

/** Gọi khi kết nối đóng (vì bất kỳ lý do gì): M2 hủy phiên, M3 xóa file .part dở... */
typedef void (*fsp_cleanup_fn)(client_ctx_t *ctx);

/**
 * Đăng ký handler cho sự kiện EVENT_REQ.
 * @return 0 = OK; -1 nếu tên rỗng / dài >= 32 ký tự, fn == NULL, tên đã được đăng ký
 *         (tránh 2 module ghi đè nhau) hoặc bảng đã đầy.
 */
int dispatcher_register(const char *event, fsp_handler_fn fn);

/** Đăng ký handler duy nhất cho frame type FILE_CHUNK (M3). -1 nếu đã có. */
int dispatcher_register_chunk(fsp_handler_fn fn);

/** Đăng ký hàm dọn dẹp khi client ngắt kết nối. -1 nếu bảng đầy hoặc fn == NULL. */
int dispatcher_register_cleanup(fsp_cleanup_fn fn);

/**
 * Điều phối 1 frame tới đúng handler:
 *  - FILE_CHUNK -> handler chunk; EVENT_REQ -> handler theo tên sự kiện.
 *  - Không có handler -> tự gửi ERROR_RESP 400 và trả 0 (vẫn giữ kết nối).
 * @return giá trị của handler, hoặc -1 nếu không gửi được phản hồi lỗi.
 */
int dispatcher_handle(client_ctx_t *ctx, const fsp_frame_t *req);

/** Gọi toàn bộ hàm dọn dẹp đã đăng ký (theo thứ tự ngược với lúc đăng ký). */
void dispatcher_run_cleanups(client_ctx_t *ctx);

/** 1 nếu sự kiện đã có handler, 0 nếu chưa. */
int dispatcher_has(const char *event);

/** Xóa toàn bộ đăng ký (chỉ dùng trong test, khi không có client nào đang chạy). */
void dispatcher_reset(void);

#endif /* FSP_DISPATCHER_H */
