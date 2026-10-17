#ifndef ADVERSARIAL_SOCKET_PROBES_HPP
#define ADVERSARIAL_SOCKET_PROBES_HPP

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "common/Config.h"
#include "common/FSPProtocol.h"

namespace FSP::Testing {

/**
 * @brief PROBE 1: Kiểm thử phát hiện và tách gói khi bị Dính Gói (Sticky Packets)
 */
inline bool probeStickyPacketsBuffer(const std::vector<uint8_t>& buffer, size_t& packet1Size, size_t& packet2Size) {
    if (buffer.size() < FSP::HEADER_SIZE * 2) return false;

    // Đọc header 1
    const FSPHeader* h1 = reinterpret_cast<const FSPHeader*>(buffer.data());
    if (h1->magic != FSP::MAGIC_BYTE) return false;
    packet1Size = FSP::HEADER_SIZE + h1->payload_len;

    // Đọc header 2 sau offset của packet 1
    if (buffer.size() < packet1Size + FSP::HEADER_SIZE) return false;
    const FSPHeader* h2 = reinterpret_cast<const FSPHeader*>(buffer.data() + packet1Size);
    if (h2->magic != FSP::MAGIC_BYTE) return false;
    packet2Size = FSP::HEADER_SIZE + h2->payload_len;

    return (packet1Size + packet2Size) == buffer.size();
}

/**
 * @brief PROBE 2: Kiểm thử tính toàn vẹn CRC32 khi bị sửa đổi 1 bit dữ liệu
 */
inline bool probeCRC32Tampering(const void* data, size_t size, uint32_t expectedCrc) {
    uint32_t computed = CRC32::calculate(data, size);
    return computed == expectedCrc;
}

/**
 * @brief PROBE 3: Kiểm thử ngăn chặn tấn công Path Traversal ngoài sandbox
 */
inline bool probePathTraversalVulnerability(const std::string& requestedPath, const std::string& sandboxRoot) {
    std::filesystem::path root = std::filesystem::weakly_canonical(sandboxRoot);
    std::filesystem::path target = std::filesystem::weakly_canonical(root / requestedPath);
    
    // Nếu target không bắt đầu bằng root -> Tấn công Path Traversal bị chặn
    std::string rootStr = root.lexically_normal().string();
    std::string targetStr = target.lexically_normal().string();

    return targetStr.rfind(rootStr, 0) == 0; // Trả về true nếu an toàn, false nếu vi phạm sandbox
}

/**
 * @brief PROBE 4: Kiểm thử ma trận quyền hạn POSIX (Linux Permission Bits)
 */
inline bool probeLinuxPermissionAccess(uint16_t permBits, bool isOwner, bool isGroupMember, FSP::Permission::AccessType req) {
    using namespace FSP::Permission;

    if (isOwner) {
        if (req == AccessType::READ && (permBits & OWNER_READ)) return true;
        if (req == AccessType::WRITE && (permBits & OWNER_WRITE)) return true;
        if (req == AccessType::SHARE && (permBits & OWNER_SHARE)) return true;
        return false;
    }
    if (isGroupMember) {
        if (req == AccessType::READ && (permBits & GROUP_READ)) return true;
        if (req == AccessType::WRITE && (permBits & GROUP_WRITE)) return true;
        if (req == AccessType::SHARE && (permBits & GROUP_SHARE)) return true;
        return false;
    }
    // Others
    if (req == AccessType::READ && (permBits & OTHERS_READ)) return true;
    if (req == AccessType::WRITE && (permBits & OTHERS_WRITE)) return true;
    if (req == AccessType::SHARE && (permBits & OTHERS_SHARE)) return true;
    return false;
}

} // namespace FSP::Testing

#endif // ADVERSARIAL_SOCKET_PROBES_HPP
