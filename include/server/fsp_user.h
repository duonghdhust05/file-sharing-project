#ifndef FSP_USER_H
#define FSP_USER_H

/*
 * ============================================================================
 * FSP User Database & Authentication Manager (C11 - SDD §5.1)
 * ============================================================================
 * Quản lý tài khoản người dùng, băm mật khẩu Salted SHA-256, kiểm soát đăng nhập
 * thất bại (khóa 5 phút sau 5 lần sai), lưu trữ JSON-lines nguyên tử (.tmp + rename).
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "common/FSPProtocol.h"

#define FSP_ADMIN_USER_ID 1
#define FSP_MAX_FAILED_LOGINS 5
#define FSP_LOCKOUT_SECONDS 300

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;
    char username[64];
    char full_name[128];
    char salt_hex[33];      /* 16 bytes = 32 hex chars */
    char password_hash[65]; /* SHA-256 = 64 hex chars */
    uint32_t group_id;
    time_t created_at;
} fsp_user_t;

/** Khởi tạo CSDL user từ file JSON-lines (nếu chưa có file thì tạo mới) */
int fsp_user_db_init(const char *db_path);

/** Giải phóng bộ nhớ và dọn dẹp CSDL user */
void fsp_user_db_destroy(void);

/**
 * Đăng ký tài khoản mới:
 * User đầu tiên có id = 1 (Admin).
 * @return 201 CREATED, 400 BAD_REQUEST, 409 CONFLICT, 500 INTERNAL_ERROR
 */
int fsp_user_register(const char *username, const char *password, const char *full_name,
                      uint32_t *out_id);

/**
 * Xác thực thông tin đăng nhập:
 * So sánh hash bằng thời gian hằng; nếu sai 5 lần liên tiếp sẽ bị khóa 5 phút.
 * @return 200 OK (ghi kết quả vào out_user), 401 UNAUTHORIZED
 */
int fsp_user_verify_login(const char *username, const char *password, fsp_user_t *out_user);

/** Kiểm tra tài khoản có đang bị tạm khóa do nhập sai nhiều lần hay không */
bool fsp_user_is_locked_out(const char *username);

/** Tìm người dùng theo ID */
int fsp_user_find_by_id(uint32_t id, fsp_user_t *out_user);

/** Tìm người dùng theo username */
int fsp_user_find_by_name(const char *username, fsp_user_t *out_user);

/** Đổi group_id của user (chỉ Admin actor_id == 1 mới có quyền) */
int fsp_user_set_group(uint32_t actor_id, uint32_t user_id, uint32_t group_id);

#ifdef __cplusplus
}
#endif

#endif /* FSP_USER_H */
