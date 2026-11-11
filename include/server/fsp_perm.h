#ifndef FSP_PERM_H
#define FSP_PERM_H

/*
 * ============================================================================
 * FSP Linux-like Permission Engine & Access Control (C11 - SDD §5.3)
 * ============================================================================
 * Thuật toán quyết định can_access 8 bước (0..7) chuẩn SRS FR-6.
 * Quản lý bitmask 9-bit, 3 mức chia sẻ (PRIVATE, SHARED, PUBLIC), danh sách
 * ACL (share_list), cờ đệ quy recursive, và lưu trữ bền vững JSON-lines.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/FSPProtocol.h"

#define FSP_MAX_ACL_ENTRIES 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t user_id;
    uint8_t mask; /* FSP_ACCESS_READ = 1, FSP_ACCESS_WRITE = 2, FSP_ACCESS_SHARE = 4 */
} fsp_acl_entry_t;

typedef struct {
    char path[512];
    uint32_t owner_id;
    uint32_t group_id;
    uint16_t perm_bits;                /* 0644, 0755,... */
    fsp_sharing_level_t sharing_level; /* PRIVATE=0, SHARED=1, PUBLIC=2 */
    fsp_acl_entry_t acl[FSP_MAX_ACL_ENTRIES];
    size_t acl_count;
} fsp_path_meta_t;

/** Khởi tạo CSDL phân quyền từ file JSON-lines */
int fsp_perm_db_init(const char *db_path);

/** Giải phóng bộ nhớ CSDL phân quyền */
void fsp_perm_db_destroy(void);

/**
 * Thuật toán quyết định truy cập can_access (8 bước 0–7) theo SRS FR-6:
 * @return true = CHO PHÉP (ALLOW), false = TỪ CHỐI (DENY 403)
 */
bool fsp_can_access(uint32_t user_id, const char *path, fsp_access_t access);

/** Đăng ký quyền cho một đường dẫn mới (khi tạo file/thư mục) */
int fsp_perm_register(const char *path, uint32_t owner_id, uint16_t perm_bits,
                      fsp_sharing_level_t level);

/** Xóa thông tin phân quyền của đường dẫn */
int fsp_perm_remove(const char *path);

/** Đổi tên đường dẫn (đồng thời di chuyển metadata của toàn bộ con bên dưới) */
int fsp_perm_rename(uint32_t actor_id, const char *old_path, const char *new_path);

/** Thay đổi bitmask quyền (yêu cầu quyền SHARE) */
int fsp_perm_chmod(uint32_t actor_id, const char *path, uint16_t perm_bits);

/** Đổi chủ sở hữu (chỉ Admin mới có quyền) */
int fsp_perm_chown(uint32_t actor_id, const char *path, uint32_t new_owner_id,
                   uint32_t new_group_id);

/** Đổi mức chia sẻ (PRIVATE, SHARED, PUBLIC - yêu cầu quyền SHARE) */
int fsp_perm_set_level(uint32_t actor_id, const char *path, fsp_sharing_level_t level);

/** Cấp quyền riêng cho một user trong danh sách ACL (yêu cầu quyền SHARE) */
int fsp_perm_grant(uint32_t actor_id, const char *path, uint32_t target_user, uint8_t mask);

/** Thu hồi quyền của một user trong danh sách ACL (yêu cầu quyền SHARE) */
int fsp_perm_revoke(uint32_t actor_id, const char *path, uint32_t target_user);

/** Cập nhật quyền có hỗ trợ cờ đệ quy (recursive=true) */
int fsp_perm_set_recursive(uint32_t actor_id, const char *path, uint16_t perm_bits,
                           fsp_sharing_level_t level, bool recursive);

/** Lấy thông tin metadata phân quyền của đường dẫn */
int fsp_perm_get_meta(const char *path, fsp_path_meta_t *out_meta);

#ifdef __cplusplus
}
#endif

#endif /* FSP_PERM_H */
