#ifndef FSP_SESSION_H
#define FSP_SESSION_H

/*
 * ============================================================================
 * FSP Session Manager (C11 In-Memory RAM Table - SDD §5.2)
 * ============================================================================
 * Quản lý phiên làm việc trong RAM, cấp token định dạng UUID v4 (36 ký tự)
 * hoặc 64 hex characters, bảo vệ bằng pthread_mutex_t, tự động hết hạn sau 30 phút.
 */

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define FSP_SESSION_TIMEOUT_S 1800 /* 30 phút */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char token[65];
    uint32_t user_id;
    char username[64];
    char cwd[512];
    time_t last_active;
    bool is_valid;
} fsp_session_t;

/** Khởi tạo bảng quản lý phiên trong RAM */
int fsp_session_init(void);

/** Giải phóng toàn bộ phiên */
void fsp_session_destroy_all(void);

/**
 * Tạo phiên mới cho user đã xác thực thành công.
 * @return 0 = thành công (ghi token vào out_token), -1 = lỗi
 */
int fsp_session_create(uint32_t user_id, const char *username, char *out_token, size_t out_size);

/**
 * Kiểm tra tính hợp lệ của token (chưa hết hạn và đang hoạt động).
 * Tự động cập nhật thời gian last_active nếu hợp lệ.
 * @return 0 = hợp lệ (ghi thông tin vào out_session), -1 = không tồn tại hoặc hết hạn
 */
int fsp_session_validate(const char *token, fsp_session_t *out_session);

/** Hủy phiên (đăng xuất hoặc client ngắt kết nối) */
int fsp_session_destroy(const char *token);

/** Cập nhật thư mục làm việc hiện tại (cwd) của phiên */
int fsp_session_set_cwd(const char *token, const char *cwd);

#ifdef __cplusplus
}
#endif

#endif /* FSP_SESSION_H */
