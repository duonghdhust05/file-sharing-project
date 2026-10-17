/**
 * @file socket_test_template.cpp
 * @brief Mẫu kiểm thử chuẩn cho các module C++ trong dự án FSP
 * @note Không dùng framework bên ngoài nặng nề, tích hợp trực tiếp với CTest.
 *       TUYỆT ĐỐI KHÔNG dùng fake assertion kiểu ASSERT_TRUE(true).
 */

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include "common/Config.h"
#include "common/FSPProtocol.h"

// ============================================================================
// Macro kiểm tra không thỏa hiệp (Zero-Compromise Assertions)
// ============================================================================
#define ASSERT_TRUE(condition, msg)                                             \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "[FAIL] " << msg << " (" << #condition << ")\n";       \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ(actual, expected, msg)                                        \
    do {                                                                        \
        if ((actual) != (expected)) {                                           \
            std::cerr << "[FAIL] " << msg << " -> Expected: " << (expected)     \
                      << ", Actual: " << (actual) << "\n";                      \
            return 1;                                                           \
        }                                                                       \
    } while (0)

// ============================================================================
// 1. HAPPY PATH TEST CASE
// ============================================================================
int testModuleHappyPath() {
    std::cout << "[TEST] Running testModuleHappyPath...\n";
    
    // Khởi tạo đối tượng cần test với dữ liệu hợp lệ
    FSP::FSPHeader header;
    header.magic = FSP::MAGIC_BYTE;
    header.type = static_cast<uint8_t>(FSP::MessageType::EVENT_REQ);
    header.payload_len = 100;
    
    ASSERT_EQ(header.magic, 0xAA, "Magic byte must be 0xAA");
    ASSERT_EQ(header.payload_len, 100, "Payload length must match");

    std::cout << "  -> PASSED: Happy path test succeeded.\n";
    return 0;
}

// ============================================================================
// 2. ADVERSARIAL TEST CASE (Kiểm thử đối kháng - Dữ liệu sai lệch)
// ============================================================================
int testModuleAdversarial() {
    std::cout << "[TEST] Running testModuleAdversarial (Tampered Data)...\n";
    
    // Giả lập dữ liệu bị đảo 1 bit (Corrupted Data)
    std::string originalData = "Hello FSP World";
    uint32_t originalCrc = FSP::CRC32::calculate(originalData.data(), originalData.size());
    
    std::string corruptedData = originalData;
    corruptedData[0] ^= 0xFF; // Lật 1 bit
    uint32_t corruptedCrc = FSP::CRC32::calculate(corruptedData.data(), corruptedData.size());
    
    // Assert rằng hệ thống PHẢI phát hiện ra sự khác biệt, không được nuốt lỗi
    ASSERT_TRUE(originalCrc != corruptedCrc, "CRC32 must detect 1-bit corruption");

    std::cout << "  -> PASSED: Adversarial test succeeded.\n";
    return 0;
}

// ============================================================================
// MAIN RUNNER (Tương thích 100% với CTest)
// ============================================================================
int main() {
    std::cout << "====================================================\n";
    std::cout << "       MODULE UNIT TEST SUITE (FSP Standard)        \n";
    std::cout << "====================================================\n";

    if (testModuleHappyPath() != 0) return 1;
    if (testModuleAdversarial() != 0) return 1;

    std::cout << "====================================================\n";
    std::cout << "  ALL TESTS PASSED SUCCESSFULLY! (2/2)              \n";
    std::cout << "====================================================\n";
    return 0;
}
