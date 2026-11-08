#ifndef FSP_CONNECTION_H
#define FSP_CONNECTION_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "common/FSPProtocol.h"
#include "common/PacketHandler.h"
#include "common/Socket.h"

namespace FSP {

namespace Json {
/// Escape chuỗi để nhúng an toàn vào JSON string literal.
std::string escape(const std::string& raw);

/**
 * @brief Envelope chuẩn cho mọi EVENT_RESP / ERROR_RESP của FSP:
 *        {"status":<code>,"data":<dataJson>}
 * dataJson phải là một JSON value hợp lệ; chuỗi rỗng được thay bằng {}.
 */
std::string envelope(StatusCode status, const std::string& dataJson);

/// {"status":<code>,"data":{"error":"<message>"}}
std::string errorEnvelope(StatusCode status, const std::string& message);

/// Đọc trường "status" từ một envelope. Trả về -1 nếu payload không đúng định dạng.
int parseStatus(const std::string& envelopeJson);
}  // namespace Json

/**
 * @brief Một kết nối TCP đang hoạt động giữa Server và một Client.
 *
 * - Chỉ MỘT luồng (worker sở hữu kết nối) được gọi receive().
 * - send()/respond()/sendError() thread-safe: nhiều luồng có thể gửi đồng thời mà các frame
 *   không bị trộn byte vào nhau.
 * - requestClose() thread-safe: đánh thức luồng đang bị chặn trong receive().
 * - Attribute map cho phép module tầng trên lưu trạng thái theo kết nối
 *   (vd: M2 lưu "session_token", M3 lưu "cwd").
 */
class Connection {
public:
    Connection(Socket socket, uint64_t id, std::string peerIp = "", uint16_t peerPort = 0);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    uint64_t id() const noexcept { return m_id; }
    const std::string& peerIp() const noexcept { return m_peerIp; }
    uint16_t peerPort() const noexcept { return m_peerPort; }
    std::string peerAddress() const;

    bool send(MessageType type, const std::string& event, const void* data, size_t length);
    bool send(MessageType type, const std::string& event, const std::string& payload);

    /// Gửi EVENT_RESP {"status":..,"data":..} cho sự kiện event.
    bool respond(const std::string& event, StatusCode status, const std::string& dataJson = "{}");
    /// Gửi ERROR_RESP {"status":..,"data":{"error":..}} cho sự kiện event.
    bool sendError(const std::string& event, StatusCode status, const std::string& message);

    /// Nhận 1 frame hợp lệ. false = mất kết nối / frame dị dạng -> caller phải đóng kết nối.
    bool receive(Packet& outPacket);

    /// Yêu cầu đóng kết nối (thread-safe, idempotent).
    void requestClose() noexcept;
    bool isClosing() const noexcept { return m_closing.load(); }

    /// > 0: receive() trả về false nếu client im lặng quá N ms (0 = tắt). Gọi trước receive().
    void setIdleTimeoutMs(int ms) noexcept { m_idleTimeoutMs = ms; }

    /// Socket gốc (dùng cho cấu hình như setTimeout). Không dùng để send trực tiếp.
    Socket& socket() noexcept { return m_socket; }

    void setAttribute(const std::string& key, const std::string& value);
    std::optional<std::string> attribute(const std::string& key) const;
    void eraseAttribute(const std::string& key);

private:
    Socket m_socket;
    const uint64_t m_id;
    const std::string m_peerIp;
    const uint16_t m_peerPort;

    std::mutex m_sendMutex;
    std::atomic<bool> m_closing{false};
    int m_idleTimeoutMs{0};

    mutable std::mutex m_attrMutex;
    std::map<std::string, std::string> m_attributes;
};

}  // namespace FSP

#endif  // FSP_CONNECTION_H
