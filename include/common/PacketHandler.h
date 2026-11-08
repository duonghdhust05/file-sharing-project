#ifndef FSP_PACKETHANDLER_H
#define FSP_PACKETHANDLER_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "common/FSPProtocol.h"
#include "common/Socket.h"

namespace FSP {

/**
 * @brief Một frame FSP đã được bóc tách và xác thực (magic, type, event, CRC32).
 * Đây là đơn vị dữ liệu mà EventDispatcher và các module nghiệp vụ (M2, M3) làm việc.
 */
/**
 * @brief Điều khiển việc nhận dữ liệu để không bao giờ bị chặn vô thời hạn trong recv().
 *
 * Trên Winsock, shutdown() từ một luồng khác KHÔNG đánh thức recv() đang bị chặn (khác Linux).
 * Vì vậy khi có abortFlag, readExact() chỉ gọi recv() sau khi poll báo có dữ liệu, và cứ mỗi
 * ABORT_POLL_MS lại kiểm tra abortFlag -> luồng khác luôn dừng được luồng nhận trên mọi nền tảng.
 */
struct RecvControl {
    /// true => dừng nhận ngay, trả về false
    const std::atomic<bool>* abortFlag = nullptr;
    /// > 0: quá N ms không nhận thêm byte nào => trả về false
    int idleTimeoutMs = -1;
};

struct Packet {
    MessageType type{MessageType::EVENT_REQ};
    /// Tên sự kiện (đã đảm bảo null-terminated, <= 31 ký tự)
    std::string event;
    /// Payload thô (JSON hoặc nhị phân FILE_CHUNK)
    std::vector<uint8_t> payload;

    /// Payload dạng chuỗi (dùng cho JSON payload).
    std::string text() const { return std::string(payload.begin(), payload.end()); }
};

class PacketHandler {
public:
    /// Giới hạn kích thước payload chấp nhận được (bảo vệ khỏi cấp phát bộ nhớ khổng lồ).
    static uint32_t maxPayloadSize();

    /**
     * @brief Vòng lặp đọc chính xác N bytes từ Socket (Chống dính gói & phân mảnh TCP)
     * @return true nếu đọc đủ N bytes; false nếu đối phương đóng kết nối hoặc gặp lỗi I/O
     */
    static bool readExact(Socket& socket, void* buffer, size_t totalBytes);
    static bool readExact(Socket& socket, void* buffer, size_t totalBytes,
                          const RecvControl& control);

    /// Chu kỳ kiểm tra abortFlag khi đang chờ dữ liệu (ms).
    static constexpr int ABORT_POLL_MS = 100;

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
     * @brief Kiểm tra header vừa nhận (Fail-Closed):
     * magic == 0xAA, type thuộc MessageType, event null-terminated trong 32 bytes,
     * payload_len <= maxPayloadSize(). Giá trị payload_len trả về đã được ntohl().
     */
    static bool validateHeader(const FSPHeader& header, uint32_t& outPayloadLen);

    /**
     * @brief Nhận và bóc tách một Frame hoàn chỉnh trực tiếp từ Socket
     * Đọc đúng 42B Header -> validateHeader -> cấp phát vector -> đọc payload -> verify CRC32
     */
    static bool unpackFrame(Socket& socket, FSPHeader& outHeader, std::vector<uint8_t>& outPayload,
                            const RecvControl& control = RecvControl{});

    /**
     * @brief Bóc tách Frame từ bộ đệm bộ nhớ (Dùng cho Unit Test Probe 1 không cần mạng thật)
     * @param buffer Mảng byte nhận được từ socket buffer
     * @param offset Vị trí con trỏ đọc (tự động tịnh tiến sau khi bóc thành công 1 frame)
     */
    static bool unpackFromBuffer(const std::vector<uint8_t>& buffer, size_t& offset,
                                 FSPHeader& outHeader, std::vector<uint8_t>& outPayload);

    // ------------------------------------------------------------------
    // API mức cao (M1.2: sendPacket / recvPacket)
    // ------------------------------------------------------------------

    /// Đóng gói + gửi trọn vẹn 1 frame. false nếu gửi thất bại (caller phải ngắt kết nối).
    static bool sendPacket(Socket& socket, MessageType type, const std::string& eventName,
                           const void* payloadData, size_t payloadLen);
    static bool sendPacket(Socket& socket, MessageType type, const std::string& eventName,
                           const std::string& jsonPayload);

    /// Nhận trọn vẹn 1 frame hợp lệ. false nếu mất kết nối hoặc frame dị dạng (Fail-Closed).
    static bool recvPacket(Socket& socket, Packet& outPacket,
                           const RecvControl& control = RecvControl{});
};

}  // namespace FSP

#endif  // FSP_PACKETHANDLER_H
