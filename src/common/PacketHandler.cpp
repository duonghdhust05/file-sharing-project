#include "common/PacketHandler.h"

#include <cstring>
#include <iostream>

#include "common/Config.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace FSP {

bool PacketHandler::readExact(Socket& socket, void* buffer, size_t totalBytes) {
    if (!socket.isValid() || !buffer || totalBytes == 0)
        return false;

    size_t received = 0;
    char* ptr = static_cast<char*>(buffer);

    while (received < totalBytes) {
        int bytes = socket.recv(ptr + received, totalBytes - received);
        if (bytes <= 0) {
            // Ngắt kết nối (0) hoặc lỗi âm tính -> Fail-Closed dứt khoát
            return false;
        }
        received += static_cast<size_t>(bytes);
    }
    return true;
}

bool PacketHandler::sendExact(Socket& socket, const void* buffer, size_t totalBytes) {
    if (!socket.isValid() || !buffer || totalBytes == 0)
        return false;

    size_t sent = 0;
    const char* ptr = static_cast<const char*>(buffer);

    while (sent < totalBytes) {
        int bytes = socket.send(ptr + sent, totalBytes - sent);
        if (bytes <= 0) {
            return false;
        }
        sent += static_cast<size_t>(bytes);
    }
    return true;
}

std::vector<uint8_t> PacketHandler::packFrame(MessageType type, const std::string& eventName,
                                              const void* payloadData, size_t payloadLen) {
    std::vector<uint8_t> frame(HEADER_SIZE + payloadLen);

    FSPHeader* header = reinterpret_cast<FSPHeader*>(frame.data());
    header->magic = MAGIC_BYTE;
    header->type = static_cast<uint8_t>(type);
    header->payload_len = htonl(static_cast<uint32_t>(payloadLen));

    // Điền event name (tối đa 32 bytes kèm null-terminator)
    std::memset(header->event, 0, EVENT_NAME_MAX_LEN);
    std::strncpy(header->event, eventName.c_str(), EVENT_NAME_MAX_LEN - 1);

    // Tính checksum CRC32 của payload
    uint32_t crc = 0;
    if (payloadData && payloadLen > 0) {
        crc = CRC32::calculate(payloadData, payloadLen);
        std::memcpy(frame.data() + HEADER_SIZE, payloadData, payloadLen);
    }
    header->checksum = htonl(crc);

    return frame;
}

std::vector<uint8_t> PacketHandler::packFrame(MessageType type, const std::string& eventName,
                                              const std::string& jsonPayload) {
    return packFrame(type, eventName, jsonPayload.data(), jsonPayload.size());
}

bool PacketHandler::unpackFrame(Socket& socket, FSPHeader& outHeader,
                                std::vector<uint8_t>& outPayload) {
    // Bước 1: Đọc đủ chính xác 42 bytes header
    if (!readExact(socket, &outHeader, HEADER_SIZE)) {
        return false;
    }

    // Bước 2: Kiểm tra magic byte
    if (outHeader.magic != MAGIC_BYTE) {
        std::cerr << "[PacketHandler] Malformed frame: invalid magic byte 0x" << std::hex
                  << static_cast<int>(outHeader.magic) << std::dec << "\n";
        return false;
    }

    // Bước 3: Chuyển đổi Endianness và kiểm tra kích thước payload
    uint32_t payloadLen = ntohl(outHeader.payload_len);
    uint32_t expectedChecksum = ntohl(outHeader.checksum);

    if (payloadLen > static_cast<uint32_t>(Config::BUFFER_SIZE)) {
        std::cerr << "[PacketHandler] Payload size exceeds buffer limits: " << payloadLen << "\n";
        return false;
    }

    outPayload.clear();
    if (payloadLen > 0) {
        outPayload.resize(payloadLen);
        // Bước 4: Đọc đủ chính xác payloadLen bytes
        if (!readExact(socket, outPayload.data(), payloadLen)) {
            return false;
        }

        // Bước 5: Kiểm tra tính toàn vẹn qua CRC32
        uint32_t computedChecksum = CRC32::calculate(outPayload.data(), payloadLen);
        if (computedChecksum != expectedChecksum) {
            std::cerr << "[PacketHandler] CRC32 mismatch! Expected: " << expectedChecksum
                      << ", Actual: " << computedChecksum << "\n";
            return false;
        }
    }

    return true;
}

bool PacketHandler::unpackFromBuffer(const std::vector<uint8_t>& buffer, size_t& offset,
                                     FSPHeader& outHeader, std::vector<uint8_t>& outPayload) {
    if (offset + HEADER_SIZE > buffer.size()) {
        return false;
    }

    // Đọc header
    std::memcpy(&outHeader, buffer.data() + offset, HEADER_SIZE);
    if (outHeader.magic != MAGIC_BYTE) {
        return false;
    }

    uint32_t payloadLen = ntohl(outHeader.payload_len);
    uint32_t expectedChecksum = ntohl(outHeader.checksum);

    if (offset + HEADER_SIZE + payloadLen > buffer.size()) {
        return false;  // Buffer chưa đủ payload (phân mảnh)
    }

    outPayload.clear();
    if (payloadLen > 0) {
        outPayload.resize(payloadLen);
        std::memcpy(outPayload.data(), buffer.data() + offset + HEADER_SIZE, payloadLen);

        uint32_t computed = CRC32::calculate(outPayload.data(), payloadLen);
        if (computed != expectedChecksum) {
            return false;
        }
    }

    // Tịnh tiến offset qua hết frame này để sẵn sàng bóc frame kế tiếp
    offset += (HEADER_SIZE + payloadLen);
    return true;
}

}  // namespace FSP