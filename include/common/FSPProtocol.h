#ifndef FSP_PROTOCOL_H
#define FSP_PROTOCOL_H

/*
 * ============================================================================
 * FSP (File Sharing Protocol) Specification v2.0 - C11
 * Hợp đồng chung giữa 4 thành viên (SDD §3). Header này biên dịch được bằng
 * C11 (gcc -std=c11) và vẫn dùng được từ C++ trong thời gian chuyển đổi.
 * KHÔNG thay đổi bố cục FSPHeader (42 byte) / FileChunkHeader (88 byte).
 * ============================================================================
 */

#include <stdint.h>

/* ---- Hằng số giao thức ---------------------------------------------------- */
#define FSP_MAGIC_BYTE 0xAAu
#define FSP_HEADER_SIZE 42u
#define FSP_CHUNK_HEADER_SIZE 88u
#define FSP_DEFAULT_CHUNK_SIZE (64u * 1024u) /* 64 KB mỗi chunk */
#define FSP_DEFAULT_SERVER_PORT 8088u
#define FSP_EVENT_NAME_MAX_LEN 32u /* gồm cả '\0' -> tên tối đa 31 ký tự */
#define FSP_UUID_MAX_LEN 64u

/* ---- 1. Loại thông điệp (trường type) ------------------------------------- */
typedef enum {
    FSP_MSG_EVENT_REQ = 0x01,  /* Client gửi yêu cầu (payload JSON) */
    FSP_MSG_EVENT_RESP = 0x02, /* Server phản hồi thành công */
    FSP_MSG_FILE_CHUNK = 0x03, /* Chunk dữ liệu file nhị phân */
    FSP_MSG_ACK = 0x04,        /* Xác nhận */
    FSP_MSG_ERROR_RESP = 0x05, /* Phản hồi lỗi */
    FSP_MSG_PING = 0x06,       /* Heartbeat */
    FSP_MSG_PONG = 0x07
} fsp_msg_type_t;

/* ---- 2. Mã trạng thái (trường "status" trong JSON phản hồi) ------------- */
typedef enum {
    FSP_STATUS_OK = 200,
    FSP_STATUS_CREATED = 201,
    FSP_STATUS_ACCEPTED = 202,
    FSP_STATUS_BAD_REQUEST = 400,
    FSP_STATUS_UNAUTHORIZED = 401,
    FSP_STATUS_FORBIDDEN = 403,
    FSP_STATUS_NOT_FOUND = 404,
    FSP_STATUS_CONFLICT = 409,
    FSP_STATUS_PAYLOAD_TOO_LARGE = 413,
    FSP_STATUS_INTERNAL_SERVER_ERROR = 500
} fsp_status_t;

/* ---- 3. Quyền kiểu Linux: r = 4, w = 2, s(share) = 1 ---------------------- */
#define FSP_PERM_OWNER_READ 0400u
#define FSP_PERM_OWNER_WRITE 0200u
#define FSP_PERM_OWNER_SHARE 0100u
#define FSP_PERM_GROUP_READ 0040u
#define FSP_PERM_GROUP_WRITE 0020u
#define FSP_PERM_GROUP_SHARE 0010u
#define FSP_PERM_OTHERS_READ 0004u
#define FSP_PERM_OTHERS_WRITE 0002u
#define FSP_PERM_OTHERS_SHARE 0001u

#define FSP_DEFAULT_FILE_PERM 0644u /* rw-r--r-- */
#define FSP_DEFAULT_DIR_PERM 0755u  /* rwxr-xr-x (xem SDD §9 vấn đề mở #1) */

typedef enum {
    FSP_LEVEL_PRIVATE = 0, /* Chỉ Owner */
    FSP_LEVEL_SHARED = 1,  /* Nhóm hoặc danh sách user (ACL) */
    FSP_LEVEL_PUBLIC = 2   /* Mọi user đã đăng nhập được đọc */
} fsp_sharing_level_t;

/* Mã hành động / mask ACL: READ = 1, WRITE = 2, SHARE = 4 (khác bảng bit Linux!) */
typedef enum {
    FSP_ACCESS_READ = 0x01,
    FSP_ACCESS_WRITE = 0x02,
    FSP_ACCESS_SHARE = 0x04
} fsp_access_t;

/* ---- 4. Tên sự kiện (SDD §3.4) -------------------------------------------- */
#define FSP_EV_AUTH_REGISTER "AUTH_REGISTER"
#define FSP_EV_AUTH_LOGIN "AUTH_LOGIN"
#define FSP_EV_AUTH_LOGOUT "AUTH_LOGOUT"

#define FSP_EV_DIR_LIST "DIR_LIST"
#define FSP_EV_DIR_MKDIR "DIR_MKDIR"
#define FSP_EV_DIR_CHANGE "DIR_CHANGE"
#define FSP_EV_DIR_PWD "DIR_PWD"
#define FSP_EV_FS_DELETE "FS_DELETE"
#define FSP_EV_FS_RENAME "FS_RENAME"

#define FSP_EV_FILE_UPLOAD_REQ "FILE_UPLOAD_REQ"
#define FSP_EV_FILE_UPLOAD_FINISH "FILE_UPLOAD_FINISH"
#define FSP_EV_FILE_DOWNLOAD_REQ "FILE_DOWNLOAD_REQ"

#define FSP_EV_FOLDER_UPLOAD_REQ "FOLDER_UPLOAD_REQ"
#define FSP_EV_FOLDER_DOWNLOAD_REQ "FOLDER_DOWNLOAD_REQ"

#define FSP_EV_SEARCH_QUERY "SEARCH_QUERY"
#define FSP_EV_PERM_GET "PERM_GET"
#define FSP_EV_PERM_SET "PERM_SET"

/* ============================================================================
 * Header nhị phân trên đường truyền (căn lề 1 byte, số nguyên big-endian)
 * ============================================================================ */
#pragma pack(push, 1)

/** Header cố định của mọi frame (42 byte) - SDD §3.1 */
typedef struct FSPHeader {
    uint8_t magic;                      /* 0xAA */
    uint8_t type;                       /* fsp_msg_type_t */
    uint32_t payload_len;               /* big-endian */
    char event[FSP_EVENT_NAME_MAX_LEN]; /* kết thúc '\0', phần dư đệm 0 */
    uint32_t checksum;                  /* CRC32 của payload, big-endian; 0 nếu rỗng */
} FSPHeader;

/** Header đặt đầu payload của frame FILE_CHUNK (88 byte) - SDD §3.2 */
typedef struct FileChunkHeader {
    char transfer_id[FSP_UUID_MAX_LEN];
    uint64_t chunk_index;  /* big-endian */
    uint64_t total_chunks; /* big-endian */
    uint32_t chunk_size;   /* big-endian, <= 65536 */
    uint32_t chunk_crc;    /* big-endian */
} FileChunkHeader;

#pragma pack(pop)

typedef FSPHeader fsp_header_t;
typedef FileChunkHeader fsp_chunk_header_t;

#ifdef __cplusplus
static_assert(sizeof(FSPHeader) == 42, "FSPHeader must be exactly 42 bytes");
static_assert(sizeof(FileChunkHeader) == 88, "FileChunkHeader must be exactly 88 bytes");
#else
_Static_assert(sizeof(FSPHeader) == 42, "FSPHeader must be exactly 42 bytes");
_Static_assert(sizeof(FileChunkHeader) == 88, "FileChunkHeader must be exactly 88 bytes");
#endif

#endif /* FSP_PROTOCOL_H */
