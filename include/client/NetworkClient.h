#ifndef FSP_NETWORK_CLIENT_H
#define FSP_NETWORK_CLIENT_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "common/FSPProtocol.h"
#include "common/PacketHandler.h"
#include "common/Socket.h"

namespace FSP {

/**
 * @brief Phản hồi của Server cho một request (EVENT_RESP hoặc ERROR_RESP).
 */
struct Response {
    MessageType type{MessageType::EVENT_RESP};
    std::string event;
    int status{-1};  // Lấy từ envelope {"status":..,"data":..}; -1 nếu không parse được
    std::string body;  // Toàn bộ envelope JSON

    bool ok() const { return type == MessageType::EVENT_RESP && status >= 200 && status < 300; }
};

/**
 * @brief Client SDK đồng bộ cho FSP (dùng bởi CLI và Network Worker Thread của Qt GUI - M4).
 *
 * - send()/sendEvent() thread-safe (mutex gửi riêng): nhiều luồng gửi không bị trộn frame.
 * - connect()/disconnect()/receive()/request() phải được gọi từ CÙNG MỘT luồng sở hữu client
 *   (luồng network worker). Luồng khác muốn ngắt kết nối thì dùng interrupt().
 * - Mọi lỗi I/O hoặc frame dị dạng => tự disconnect() (Fail-Closed).
 */
class NetworkClient {
public:
    enum class RecvStatus { OK, TIMEOUT, DISCONNECTED };

    NetworkClient() = default;
    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool connect(const std::string& host, uint16_t port = DEFAULT_SERVER_PORT);
    void disconnect();
    bool isConnected() const;
    /// Thread-safe: đánh thức luồng đang chờ trong receive()/request() (nó sẽ trả về
    /// DISCONNECTED). Socket chỉ thực sự được đóng bởi luồng sở hữu qua disconnect().
    void interrupt() noexcept;

    bool send(MessageType type, const std::string& event, const void* data, size_t length);
    /// Gửi EVENT_REQ với JSON payload.
    bool sendEvent(const std::string& event, const std::string& jsonPayload);

    /**
     * @brief Nhận 1 frame. timeoutMs < 0: chờ vô hạn.
     * Các frame đã nhận trước đó trong lúc request() chờ phản hồi khác sẽ được trả về trước.
     */
    RecvStatus receive(Packet& outPacket, int timeoutMs = -1);

    /**
     * @brief Gửi EVENT_REQ rồi chờ EVENT_RESP/ERROR_RESP có cùng tên sự kiện.
     * Frame không liên quan (server push) được giữ lại cho receive() sau.
     * @return std::nullopt nếu mất kết nối hoặc quá timeoutMs.
     */
    std::optional<Response> request(const std::string& event, const std::string& jsonPayload,
                                    int timeoutMs = 10000);

private:
    RecvStatus receiveFromSocket(Packet& outPacket, int timeoutMs);

    std::mutex m_sendMutex;
    Socket m_socket{INVALID_SOCKET_FD};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_interrupted{false};
    std::deque<Packet> m_backlog;
};

}  // namespace FSP

#endif  // FSP_NETWORK_CLIENT_H
