#include "common/PacketHandler.h"

#include <chrono>
#include <cstring>
#include <iostream>

#include "common/Config.h"

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

namespace FSP {

namespace {
bool isKnownMessageType(uint8_t type) {
    return type >= static_cast<uint8_t>(MessageType::EVENT_REQ) &&
           type <= static_cast<uint8_t>(MessageType::PONG);
}

bool hasNullTerminator(const char (&event)[EVENT_NAME_MAX_LEN]) {
    return std::memchr(event, '\0', EVENT_NAME_MAX_LEN) != nullptr;
}
}  // namespace

uint32_t PacketHandler::maxPayloadSize() {
    return static_cast<uint32_t>(Config::BUFFER_SIZE);
}

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

bool PacketHandler::readExact(Socket& socket, void* buffer, size_t totalBytes,
                              const RecvControl& control) {
    if (!control.abortFlag && control.idleTimeoutMs <= 0)
        return readExact(socket, buffer, totalBytes);
    if (!socket.isValid() || !buffer || totalBytes == 0)
        return false;

    using Clock = std::chrono::steady_clock;
    size_t received = 0;
    char* ptr = static_cast<char*>(buffer);
    auto lastActivity = Clock::now();

    while (received < totalBytes) {
        // Chờ có dữ liệu theo từng lát ABORT_POLL_MS để kịp phản ứng với yêu cầu dừng.
        for (;;) {
            if (control.abortFlag && control.abortFlag->load())
                return false;
            int ready = socket.waitReadable(ABORT_POLL_MS);
            if (ready < 0)
                return false;
            if (ready > 0)
                break;
            if (control.idleTimeoutMs > 0 &&
                Clock::now() - lastActivity >= std::chrono::milliseconds(control.idleTimeoutMs))
                return false;
        }
        int bytes = socket.recv(ptr + received, totalBytes - received);
        if (bytes <= 0) {
            return false;  // Peer đóng (0) hoặc lỗi -> Fail-Closed
        }
        received += static_cast<size_t>(bytes);
        lastActivity = Clock::now();
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
    if (!payloadData)
        payloadLen = 0;
    std::vector<uint8_t> frame(HEADER_SIZE + payloadLen);

    // Dựng header trên stack rồi memcpy vào buffer: tránh truy cập không căn lề qua con trỏ ép
    // kiểu.
    FSPHeader header{};
    header.magic = MAGIC_BYTE;
    header.type = static_cast<uint8_t>(type);
    header.payload_len = htonl(static_cast<uint32_t>(payloadLen));

    // Điền event name (tối đa 31 ký tự + null-terminator)
    std::strncpy(header.event, eventName.c_str(), EVENT_NAME_MAX_LEN - 1);
    header.event[EVENT_NAME_MAX_LEN - 1] = '\0';

    // Tính checksum CRC32 của payload
    uint32_t crc = 0;
    if (payloadLen > 0) {
        crc = CRC32::calculate(payloadData, payloadLen);
        std::memcpy(frame.data() + HEADER_SIZE, payloadData, payloadLen);
    }
    header.checksum = htonl(crc);

    std::memcpy(frame.data(), &header, HEADER_SIZE);
    return frame;
}

std::vector<uint8_t> PacketHandler::packFrame(MessageType type, const std::string& eventName,
                                              const std::string& jsonPayload) {
    return packFrame(type, eventName, jsonPayload.data(), jsonPayload.size());
}

bool PacketHandler::validateHeader(const FSPHeader& header, uint32_t& outPayloadLen) {
    if (header.magic != MAGIC_BYTE) {
        std::cerr << "[PacketHandler] Malformed frame: invalid magic byte 0x" << std::hex
                  << static_cast<int>(header.magic) << std::dec << "\n";
        return false;
    }
    if (!isKnownMessageType(header.type)) {
        std::cerr << "[PacketHandler] Malformed frame: unknown message type "
                  << static_cast<int>(header.type) << "\n";
        return false;
    }
    if (!hasNullTerminator(header.event)) {
        // Nếu không chặn, std::string(header.event) sẽ đọc tràn ra ngoài 32 bytes.
        std::cerr << "[PacketHandler] Malformed frame: event name is not null-terminated\n";
        return false;
    }
    uint32_t payloadLen = ntohl(header.payload_len);
    if (payloadLen > maxPayloadSize()) {
        std::cerr << "[PacketHandler] Payload size exceeds buffer limits: " << payloadLen << "\n";
        return false;
    }
    outPayloadLen = payloadLen;
    return true;
}

bool PacketHandler::unpackFrame(Socket& socket, FSPHeader& outHeader,
                                std::vector<uint8_t>& outPayload, const RecvControl& control) {
    // Bước 1: Đọc đủ chính xác 42 bytes header
    if (!readExact(socket, &outHeader, HEADER_SIZE, control)) {
        return false;
    }

    // Bước 2 + 3: Kiểm tra magic byte, type, event name, giới hạn kích thước payload
    uint32_t payloadLen = 0;
    if (!validateHeader(outHeader, payloadLen)) {
        return false;
    }
    uint32_t expectedChecksum = ntohl(outHeader.checksum);

    outPayload.clear();
    if (payloadLen > 0) {
        outPayload.resize(payloadLen);
        // Bước 4: Đọc đủ chính xác payloadLen bytes
        if (!readExact(socket, outPayload.data(), payloadLen, control)) {
            return false;
        }

        // Bước 5: Kiểm tra tính toàn vẹn qua CRC32
        uint32_t computedChecksum = CRC32::calculate(outPayload.data(), payloadLen);
        if (computedChecksum != expectedChecksum) {
            std::cerr << "[PacketHandler] CRC32 mismatch! Expected: " << expectedChecksum
                      << ", Actual: " << computedChecksum << "\n";
            return false;
        }
    } else if (expectedChecksum != 0) {
        std::cerr << "[PacketHandler] Empty payload must carry checksum 0\n";
        return false;
    }

    return true;
}

bool PacketHandler::unpackFromBuffer(const std::vector<uint8_t>& buffer, size_t& offset,
                                     FSPHeader& outHeader, std::vector<uint8_t>& outPayload) {
    if (offset > buffer.size() || buffer.size() - offset < HEADER_SIZE) {
        return false;
    }

    // Đọc header
    std::memcpy(&outHeader, buffer.data() + offset, HEADER_SIZE);
    uint32_t payloadLen = 0;
    if (!validateHeader(outHeader, payloadLen)) {
        return false;
    }
    uint32_t expectedChecksum = ntohl(outHeader.checksum);

    if (buffer.size() - offset - HEADER_SIZE < payloadLen) {
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
    } else if (expectedChecksum != 0) {
        return false;
    }

    // Tịnh tiến offset qua hết frame này để sẵn sàng bóc frame kế tiếp
    offset += (HEADER_SIZE + payloadLen);
    return true;
}

bool PacketHandler::sendPacket(Socket& socket, MessageType type, const std::string& eventName,
                               const void* payloadData, size_t payloadLen) {
    if (eventName.size() >= EVENT_NAME_MAX_LEN) {
        std::cerr << "[PacketHandler] Event name too long (max 31 chars): " << eventName << "\n";
        return false;
    }
    if (payloadLen > maxPayloadSize() || (payloadLen > 0 && !payloadData)) {
        std::cerr << "[PacketHandler] Refusing to send invalid payload of " << payloadLen
                  << " bytes\n";
        return false;
    }
    std::vector<uint8_t> frame = packFrame(type, eventName, payloadData, payloadLen);
    return sendExact(socket, frame.data(), frame.size());
}

bool PacketHandler::sendPacket(Socket& socket, MessageType type, const std::string& eventName,
                               const std::string& jsonPayload) {
    return sendPacket(socket, type, eventName, jsonPayload.data(), jsonPayload.size());
}

bool PacketHandler::recvPacket(Socket& socket, Packet& outPacket, const RecvControl& control) {
    FSPHeader header{};
    if (!unpackFrame(socket, header, outPacket.payload, control)) {
        return false;
    }
    outPacket.type = static_cast<MessageType>(header.type);
    outPacket.event = std::string(header.event);  // An toàn: validateHeader đã kiểm tra '\0'
    return true;
}

}  // namespace FSP
