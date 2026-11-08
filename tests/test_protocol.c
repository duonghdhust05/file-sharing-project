/* Kiểm thử hợp đồng giao thức (port từ test_protocol.cpp): kích thước header, hằng số,
 * CRC32 IEEE 802.3, bitmask quyền Linux. */
#include <stddef.h>
#include <string.h>

#include "common/Config.h"
#include "common/FSPProtocol.h"
#include "common/fsp_crc32.h"
#include "test_util.h"

static int test_header_sizes(void) {
    CHECK(sizeof(FSPHeader) == 42, "FSPHeader must be exactly 42 bytes");
    CHECK(sizeof(FileChunkHeader) == 88, "FileChunkHeader must be exactly 88 bytes");
    /* Offset từng trường đúng bảng SDD §3.1 */
    CHECK(offsetof(FSPHeader, magic) == 0, "magic at offset 0");
    CHECK(offsetof(FSPHeader, type) == 1, "type at offset 1");
    CHECK(offsetof(FSPHeader, payload_len) == 2, "payload_len at offset 2");
    CHECK(offsetof(FSPHeader, event) == 6, "event at offset 6");
    CHECK(offsetof(FSPHeader, checksum) == 38, "checksum at offset 38");
    CHECK(offsetof(FileChunkHeader, chunk_index) == 64, "chunk_index at offset 64");
    CHECK(offsetof(FileChunkHeader, chunk_crc) == 84, "chunk_crc at offset 84");
    return 0;
}

static int test_constants(void) {
    CHECK(FSP_MAGIC_BYTE == 0xAA, "magic byte must be 0xAA");
    CHECK(FSP_SERVER_PORT == 8088, "default port must be 8088");
    CHECK(FSP_DEFAULT_CHUNK_SIZE == 65536, "chunk size must be 64 KB");
    CHECK(FSP_BUFFER_SIZE == 131072, "payload limit must be 128 KB");
    CHECK(FSP_BUFFER_SIZE >= FSP_CHUNK_HEADER_SIZE + FSP_DEFAULT_CHUNK_SIZE,
          "a full FILE_CHUNK frame must fit in FSP_BUFFER_SIZE");
    CHECK(FSP_MSG_EVENT_REQ == 1 && FSP_MSG_PONG == 7, "message type values per SDD §3.1");
    CHECK(FSP_ACCESS_READ == 1 && FSP_ACCESS_WRITE == 2 && FSP_ACCESS_SHARE == 4,
          "AccessType values");
    CHECK(strlen(FSP_EV_FOLDER_DOWNLOAD_REQ) < FSP_EVENT_NAME_MAX_LEN, "event names fit 31 chars");
    return 0;
}

static int test_crc32(void) {
    /* Vector chuẩn: CRC-32("123456789") = 0xCBF43926 */
    CHECK(fsp_crc32("123456789", 9) == 0xCBF43926u, "CRC32 check value");
    CHECK(fsp_crc32("", 0) == 0u, "CRC32 of empty data is 0");
    CHECK(fsp_crc32("The quick brown fox jumps over the lazy dog", 43) == 0x414FA339u,
          "CRC32 of pangram");
    /* Tính nối tiếp nhiều đoạn phải bằng tính một lần */
    uint32_t part = fsp_crc32_update(0, "12345", 5);
    CHECK(fsp_crc32_update(part, "6789", 4) == 0xCBF43926u, "incremental CRC32");
    return 0;
}

static int test_permission_bits(void) {
    unsigned dir = FSP_DEFAULT_DIR_PERM;   /* 0755 */
    unsigned file = FSP_DEFAULT_FILE_PERM; /* 0644 */
    CHECK(dir & FSP_PERM_OWNER_READ, "0755 has owner read");
    CHECK(dir & FSP_PERM_OWNER_WRITE, "0755 has owner write");
    CHECK(dir & FSP_PERM_OWNER_SHARE, "0755 has owner share");
    CHECK(dir & FSP_PERM_GROUP_READ, "0755 has group read");
    CHECK(!(dir & FSP_PERM_GROUP_WRITE), "0755 has no group write");
    CHECK(dir & FSP_PERM_GROUP_SHARE, "0755 has group share");
    CHECK(file & FSP_PERM_OWNER_READ, "0644 has owner read");
    CHECK(file & FSP_PERM_OWNER_WRITE, "0644 has owner write");
    CHECK(!(file & FSP_PERM_OWNER_SHARE), "0644 has no owner share");
    CHECK(file & FSP_PERM_GROUP_READ, "0644 has group read");
    CHECK(!(file & FSP_PERM_GROUP_WRITE), "0644 has no group write");
    CHECK(file & FSP_PERM_OTHERS_READ, "0644 has others read");
    return 0;
}

int main(void) {
    int passed = 0;
    printf("=== FSP PROTOCOL CONTRACT TESTS (C11) ===\n");
    RUN_TEST(test_header_sizes, passed);
    RUN_TEST(test_constants, passed);
    RUN_TEST(test_crc32, passed);
    RUN_TEST(test_permission_bits, passed);
    printf("ALL PROTOCOL TESTS PASSED (%d/%d)\n", passed, passed);
    return 0;
}
