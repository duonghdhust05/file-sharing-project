#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "common/Config.h"
#include "common/FSPProtocol.h"

// Simple Lightweight Test Framework for CI
#define ASSERT_TRUE(condition, message)                                       \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::cerr << "[FAIL] " << message << " (" << #condition << ")\n"; \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define ASSERT_EQ(val1, val2, message)                                                             \
    do {                                                                                           \
        if ((val1) != (val2)) {                                                                    \
            std::cerr << "[FAIL] " << message << " Expected: " << (val2) << ", Actual: " << (val1) \
                      << "\n";                                                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int testHeaderSizes() {
    std::cout << "[TEST] Running testHeaderSizes...\n";
    ASSERT_EQ(sizeof(FSP::FSPHeader), 42, "FSPHeader size must be exactly 42 bytes");
    ASSERT_EQ(sizeof(FSP::FileChunkHeader), 88, "FileChunkHeader size must be exactly 88 bytes");
    std::cout << "  -> PASSED: All header sizes match specification.\n";
    return 0;
}

int testMagicByteAndDefaults() {
    std::cout << "[TEST] Running testMagicByteAndDefaults...\n";
    FSP::FSPHeader hdr;
    hdr.magic = FSP::MAGIC_BYTE;
    ASSERT_EQ(hdr.magic, 0xAA, "Default magic byte must be 0xAA");
    ASSERT_EQ(FSP::Config::SERVER_PORT, 8088, "Default port must be 8088");
    ASSERT_EQ(FSP::DEFAULT_CHUNK_SIZE, 64 * 1024, "Default chunk size must be 64KB");
    std::cout << "  -> PASSED: Constants and defaults are correct.\n";
    return 0;
}

int testCRC32Calculation() {
    std::cout << "[TEST] Running testCRC32Calculation...\n";
    // Standard test vector: "123456789" -> CRC-32 = 0xCBF43926
    const char* testStr = "123456789";
    uint32_t crc = FSP::CRC32::calculate(testStr, std::strlen(testStr));
    ASSERT_EQ(crc, 0xCBF43926, "CRC32 of '123456789' must equal 0xCBF43926");

    // Empty string CRC32 = 0x00000000
    uint32_t emptyCrc = FSP::CRC32::calculate("", 0);
    ASSERT_EQ(emptyCrc, 0x00000000, "CRC32 of empty string must equal 0");

    std::cout << "  -> PASSED: CRC32 calculations match IEEE standards.\n";
    return 0;
}

int testLinuxPermissionsBitmask() {
    std::cout << "[TEST] Running testLinuxPermissionsBitmask...\n";
    using namespace FSP::Permission;

    // Test default 0755: rwxr-xr-x
    uint16_t dirPerm = DEFAULT_DIR_PERM;
    ASSERT_TRUE(dirPerm & OWNER_READ, "0755 must have Owner Read");
    ASSERT_TRUE(dirPerm & OWNER_WRITE, "0755 must have Owner Write");
    ASSERT_TRUE(dirPerm & OWNER_SHARE, "0755 must have Owner Share");
    ASSERT_TRUE(dirPerm & GROUP_READ, "0755 must have Group Read");
    ASSERT_TRUE(!(dirPerm & GROUP_WRITE), "0755 must NOT have Group Write");
    ASSERT_TRUE(dirPerm & GROUP_SHARE, "0755 must have Group Share");

    // Test default 0644: rw-r--r--
    uint16_t filePerm = DEFAULT_FILE_PERM;
    ASSERT_TRUE(filePerm & OWNER_READ, "0644 must have Owner Read");
    ASSERT_TRUE(filePerm & OWNER_WRITE, "0644 must have Owner Write");
    ASSERT_TRUE(!(filePerm & OWNER_SHARE), "0644 must NOT have Owner Share");
    ASSERT_TRUE(filePerm & GROUP_READ, "0644 must have Group Read");
    ASSERT_TRUE(!(filePerm & GROUP_WRITE), "0644 must NOT have Group Write");

    std::cout << "  -> PASSED: Linux permission bitmasks validated.\n";
    return 0;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "       FSP UNIT TEST SUITE (CI/CD Automated)        \n";
    std::cout << "====================================================\n";

    if (testHeaderSizes() != 0)
        return 1;
    if (testMagicByteAndDefaults() != 0)
        return 1;
    if (testCRC32Calculation() != 0)
        return 1;
    if (testLinuxPermissionsBitmask() != 0)
        return 1;

    std::cout << "====================================================\n";
    std::cout << "  ALL PROTOCOL UNIT TESTS PASSED SUCCESSFULLY! (4/4)\n";
    std::cout << "====================================================\n";
    return 0;
}
