#ifndef FSP_PROTOCOL_H
#define FSP_PROTOCOL_H

#include <cstdint>
#include <string>

// ============================================================================
// FSP (File Sharing Protocol) Specification v1.0
// Designed for University Networking Program - Topic 6
// ============================================================================

namespace FSP {

// Protocol constants
constexpr uint8_t  MAGIC_BYTE            = 0xAA;
constexpr uint32_t HEADER_SIZE           = 42;
constexpr uint32_t DEFAULT_CHUNK_SIZE    = 64 * 1024; // 64 KB per chunk
constexpr uint16_t DEFAULT_SERVER_PORT   = 8088;
constexpr uint32_t EVENT_NAME_MAX_LEN    = 32;
constexpr uint32_t UUID_MAX_LEN          = 64;

// 1. Message Types
enum class MessageType : uint8_t {
    EVENT_REQ   = 0x01, // Client gửi yêu cầu sự kiện (JSON payload)
    EVENT_RESP  = 0x02, // Server phản hồi sự kiện (JSON payload)
    FILE_CHUNK  = 0x03, // Gói dữ liệu truyền tải chunk file nhị phân
    ACK         = 0x04, // Xác nhận nhận thành công
    ERROR_RESP  = 0x05, // Thông báo lỗi hệ thống/giao thức
    PING        = 0x06, // Heartbeat Ping
    PONG        = 0x07  // Heartbeat Pong
};

// 2. Standard Status Codes
enum class StatusCode : uint16_t {
    OK                    = 200,
    CREATED               = 201,
    ACCEPTED              = 202,
    BAD_REQUEST           = 400,
    UNAUTHORIZED          = 401,
    FORBIDDEN             = 403, // Không đủ quyền truy cập file/folder
    NOT_FOUND             = 404, // File/Folder không tồn tại
    CONFLICT              = 409, // Tên file/folder hoặc username đã tồn tại
    PAYLOAD_TOO_LARGE     = 413,
    INTERNAL_SERVER_ERROR = 500
};

// 3. Linux-like Permission Constants
namespace Permission {
    // Permission bits (Read, Write, Share)
    constexpr uint16_t OWNER_READ   = 0400; // 0b100 000 000
    constexpr uint16_t OWNER_WRITE  = 0200; // 0b010 000 000
    constexpr uint16_t OWNER_SHARE  = 0100; // 0b001 000 000

    constexpr uint16_t GROUP_READ   = 0040; // 0b000 100 000
    constexpr uint16_t GROUP_WRITE  = 0020; // 0b000 010 000
    constexpr uint16_t GROUP_SHARE  = 0010; // 0b000 001 000

    constexpr uint16_t OTHERS_READ  = 0004; // 0b000 000 100
    constexpr uint16_t OTHERS_WRITE = 0002; // 0b000 000 010
    constexpr uint16_t OTHERS_SHARE = 0001; // 0b000 000 001

    // Default file: 0644 (rw-r--r--), Default dir: 0755 (rwxr-xr-x)
    constexpr uint16_t DEFAULT_FILE_PERM = 0644;
    constexpr uint16_t DEFAULT_DIR_PERM  = 0755;

    // Sharing Levels
    enum class SharingLevel : uint8_t {
        PRIVATE = 0, // Chỉ Owner truy cập
        SHARED  = 1, // Chia sẻ cho nhóm hoặc danh sách user cụ thể
        PUBLIC  = 2  // Tất cả user đăng nhập đều có quyền đọc
    };

    enum class AccessType : uint8_t {
        READ   = 0x01, // Xem nội dung / Download
        WRITE  = 0x02, // Sửa / Upload đè / Xóa / Đổi tên
        SHARE  = 0x04  // Đổi quyền (chmod), cấp quyền cho người khác
    };
}

// 4. Standard Event Names
namespace Events {
    // Auth Events
    inline constexpr const char* AUTH_REGISTER       = "AUTH_REGISTER";
    inline constexpr const char* AUTH_LOGIN          = "AUTH_LOGIN";
    inline constexpr const char* AUTH_LOGOUT         = "AUTH_LOGOUT";

    // Directory & File Management (FTP-like)
    inline constexpr const char* DIR_LIST            = "DIR_LIST";
    inline constexpr const char* DIR_MKDIR           = "DIR_MKDIR";
    inline constexpr const char* DIR_CHANGE          = "DIR_CHANGE"; // cd
    inline constexpr const char* DIR_PWD             = "DIR_PWD";    // pwd
    inline constexpr const char* FS_DELETE           = "FS_DELETE";  // rm / rmdir
    inline constexpr const char* FS_RENAME           = "FS_RENAME";

    // Single File Transfer
    inline constexpr const char* FILE_UPLOAD_REQ     = "FILE_UPLOAD_REQ";
    inline constexpr const char* FILE_UPLOAD_FINISH  = "FILE_UPLOAD_FINISH";
    inline constexpr const char* FILE_DOWNLOAD_REQ   = "FILE_DOWNLOAD_REQ";

    // Recursive Folder Transfer
    inline constexpr const char* FOLDER_UPLOAD_REQ   = "FOLDER_UPLOAD_REQ";
    inline constexpr const char* FOLDER_DOWNLOAD_REQ = "FOLDER_DOWNLOAD_REQ";

    // Search & Permissions
    inline constexpr const char* SEARCH_QUERY        = "SEARCH_QUERY";
    inline constexpr const char* PERM_GET            = "PERM_GET";
    inline constexpr const char* PERM_SET            = "PERM_SET";
}

// ============================================================================
// Network Binary Headers (Byte-aligned)
// ============================================================================
#pragma pack(push, 1)

/**
 * @brief Fixed Header cho mọi frame truyền qua TCP socket (42 Bytes)
 */
struct FSPHeader {
    uint8_t  magic;                         // 1 byte: 0xAA
    uint8_t  type;                          // 1 byte: MessageType
    uint32_t payload_len;                   // 4 bytes: Độ dài payload tiếp theo (Big-Endian)
    char     event[EVENT_NAME_MAX_LEN];     // 32 bytes: Tên sự kiện (null-terminated string)
    uint32_t checksum;                      // 4 bytes: CRC32 của payload (0 nếu không payload)
};

/**
 * @brief Header đi kèm trước mỗi chunk dữ liệu nhị phân khi gửi FILE_CHUNK
 */
struct FileChunkHeader {
    char     transfer_id[UUID_MAX_LEN];     // 64 bytes: UUID định danh phiên truyền file
    uint64_t chunk_index;                   // 8 bytes: Thứ tự chunk hiện tại (0-indexed)
    uint64_t total_chunks;                  // 8 bytes: Tổng số chunks
    uint32_t chunk_size;                    // 4 bytes: Kích thước dữ liệu chunk đi kèm
    uint32_t chunk_crc;                     // 4 bytes: CRC32 riêng cho chunk này
};

#pragma pack(pop)

// Compile-time assertion để đảm bảo kích thước header đúng 42 bytes
static_assert(sizeof(FSPHeader) == 42, "FSPHeader must be exactly 42 bytes! Check padding.");
static_assert(sizeof(FileChunkHeader) == 88, "FileChunkHeader must be exactly 88 bytes! Check padding.");

} // namespace FSP

#endif // FSP_PROTOCOL_H
