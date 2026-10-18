#ifndef FSP_PACKETHANDLER_H
#define FSP_PACKETHANDLER_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/FSPProtocol.h"
#include "common/Socket.h"

namespace FSP {

class PacketHandler {
public:
    /**
     * @brief Vòng lặp đọc chính xác N bytes từ Socket (Chống dính gói & phân mảnh TCP)
     * @return true nếu đọc đủ N bytes; false nếu đối phương đóng kết nối hoặc gặp lỗi I/O
     */
    static bool readExact(Socket& socket, void* buffer, size_t totalBytes);

    /**
     * @brief Vòng lặp gửi đủ chính xác N bytes qua Socket
     */
    static bool sendExact(Socket& socket, const void* buffer, size_t totalBytes);

    /**
     * @brief Đóng gói Frame FSP 42 bytes Header + Payload nhị phân
     */
    static std::vector<uint8_t> packFrame(MessageType type, const std::string& eventName,
                                          const void* payloadData, size_t payloadLen);

    /**
     * @brief Đóng gói Frame tiện lợi với Payload dạng chuỗi std::string (JSON)
     */
    static std::vector<uint8_t> packFrame(MessageType type, const std::string& eventName,
                                          const std::string& jsonPayload);

    /**
     * @brief Nhận và bóc tách một Frame hoàn chỉnh trực tiếp từ Socket
     * Đọc đúng 42B Header -> kiểm tra Magic 0xAA -> cấp phát vector -> đọc payload -> verify CRC32
     */
    static bool unpackFrame(Socket& socket, FSPHeader& outHeader, std::vector<uint8_t>& outPayload);

    /**
     * @brief Bóc tách Frame từ bộ đệm bộ nhớ (Dùng cho Unit Test Probe 1 không cần mạng thật)
     * @param buffer Mảng byte nhận được từ socket buffer
     * @param offset Vị trí con trỏ đọc (tự động tịnh tiến sau khi bóc thành công 1 frame)
     */
    static bool unpackFromBuffer(const std::vector<uint8_t>& buffer, size_t& offset,
                                 FSPHeader& outHeader, std::vector<uint8_t>& outPayload);
};

}  // namespace FSP

#endif  // FSP_PACKETHANDLER_H