#ifndef FSP_CLIENT_API_H
#define FSP_CLIENT_API_H

/*
 * ============================================================================
 * FSP Client API SDK (Tầng Client API dùng chung cho CLI và GTK GUI - M4)
 * ============================================================================
 * Đóng gói toàn bộ logic gọi mạng qua Wire Protocol (Header 42B + Payload).
 * Giao diện cấp cao này giúp CLI và GUI chỉ cần gọi các hàm C11 đơn giản,
 * không cần tự thao tác với socket thô hay tự parse JSON thủ công.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/Config.h"
#include "common/FSPProtocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cấu trúc phiên làm việc của Client */
typedef struct {
    int fd;
    bool is_connected;
    bool is_logged_in;
    char token[64];
    char username[64];
    char current_dir[512];
    char last_error[256];
} fsp_client_t;

/** Callback tiến độ truyền tải file (chunk 64 KB) */
typedef void (*fsp_progress_cb)(size_t bytes_transferred, size_t total_bytes, void *userdata);

/* ---- 1. Khởi tạo & Kết nối mạng ------------------------------------------ */

/** Khởi tạo cấu trúc client ở trạng thái rỗng */
void fsp_client_init(fsp_client_t *cli);

/**
 * Kết nối tới máy chủ FSP qua TCP socket.
 * @return 0 = thành công, -1 = lỗi kết nối
 */
int fsp_client_connect(fsp_client_t *cli, const char *host, uint16_t port);

/** Ngắt kết nối và đóng socket */
void fsp_client_disconnect(fsp_client_t *cli);

/** Gửi gói PING kiểm tra kết nối, chờ PONG */
int fsp_client_ping(fsp_client_t *cli);

/* ---- 2. Xác thực & Phiên (AUTH) ------------------------------------------ */

/**
 * Đăng nhập tài khoản. Nếu thành công, lưu token vào cli->token.
 * @return 200 = OK, mã lỗi HTTP-style (401, 404,...) hoặc -1 nếu lỗi mạng
 */
int fsp_client_login(fsp_client_t *cli, const char *username, const char *password);

/** Đăng ký tài khoản mới */
int fsp_client_register(fsp_client_t *cli, const char *username, const char *password);

/** Đăng xuất phiên hiện tại */
int fsp_client_logout(fsp_client_t *cli);

/* ---- 3. Quản lý Thư mục & Tệp (FTP-like) ---------------------------------- */

/** Lấy đường dẫn thư mục làm việc hiện tại trên server */
int fsp_client_pwd(fsp_client_t *cli, char *out_path, size_t out_size);

/** Đổi thư mục làm việc trên server */
int fsp_client_cd(fsp_client_t *cli, const char *path);

/** Tạo thư mục mới trên server */
int fsp_client_mkdir(fsp_client_t *cli, const char *path);

/** Xóa tệp hoặc thư mục trên server */
int fsp_client_delete(fsp_client_t *cli, const char *path);

/* ---- 4. Truyền Tệp & Phân Quyền ------------------------------------------ */

/**
 * Tải một tệp từ client lên server (cắt khối 64 KB)
 * @return 200/201 = thành công, mã lỗi hoặc -1 nếu lỗi mạng/IO
 */
int fsp_client_upload_file(fsp_client_t *cli, const char *local_path, const char *remote_path,
                           fsp_progress_cb cb, void *userdata);

/**
 * Tải một tệp từ server về máy cục bộ
 * @return 200 = thành công, mã lỗi hoặc -1 nếu lỗi mạng/IO
 */
int fsp_client_download_file(fsp_client_t *cli, const char *remote_path, const char *local_path,
                             fsp_progress_cb cb, void *userdata);

/** Đổi quyền chia sẻ (chmod / share_list) của file/thư mục */
int fsp_client_set_perm(fsp_client_t *cli, const char *path, const char *mode, const char *level,
                        bool recursive);

#ifdef __cplusplus
}
#endif

#endif /* FSP_CLIENT_API_H */
