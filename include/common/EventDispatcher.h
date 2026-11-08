#ifndef FSP_EVENT_DISPATCHER_H
#define FSP_EVENT_DISPATCHER_H

#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/Connection.h"
#include "common/PacketHandler.h"

namespace FSP {

/**
 * @brief Event-driven dispatcher kiểu Socket.IO (M1.3).
 *
 * Module nghiệp vụ đăng ký callback theo tên sự kiện:
 * @code
 *   dispatcher.registerHandler(Events::AUTH_LOGIN, [&](Connection& conn, const Packet& pkt) {
 *       auto r = authHandler.handle(pkt.event, pkt.text());   // M2
 *       conn.respond(pkt.event, r.status, r.body);
 *   });
 * @endcode
 *
 * Khi một Packet tới, dispatch() tìm đúng callback theo pkt.event và gọi nó.
 * - Không có handler (và không có fallback) -> tự trả ERROR_RESP 400 "unknown event".
 * - Handler ném exception -> tự trả ERROR_RESP 500, kết nối vẫn được giữ.
 *
 * Thread-safety: register/unregister/dispatch có thể gọi đồng thời từ nhiều luồng
 * (shared_mutex). Callback được gọi NGOÀI lock nên có thể tự gọi lại dispatcher.
 */
class EventDispatcher {
public:
    using Handler = std::function<void(Connection&, const Packet&)>;

    enum class Result {
        HANDLED,         // Handler đã đăng ký được gọi và trả về bình thường
        FALLBACK,        // Không có handler riêng, fallback handler đã xử lý
        UNKNOWN_EVENT,   // Không có handler nào, đã trả ERROR_RESP 400
        HANDLER_FAILED,  // Handler ném exception, đã trả ERROR_RESP 500
        REJECTED  // Packet không hợp lệ để dispatch (event rỗng), đã trả ERROR_RESP 400
    };

    /**
     * @brief Đăng ký callback cho một sự kiện.
     * @return false nếu tên rỗng, dài hơn 31 ký tự, handler rỗng, hoặc sự kiện đã có handler
     *         (tránh việc 2 module vô tình ghi đè handler của nhau).
     */
    bool registerHandler(const std::string& eventName, Handler handler);

    /// Ghi đè handler đã có (hoặc đăng ký mới). false nếu tên/handler không hợp lệ.
    bool replaceHandler(const std::string& eventName, Handler handler);

    bool unregisterHandler(const std::string& eventName);
    bool hasHandler(const std::string& eventName) const;
    std::vector<std::string> registeredEvents() const;

    /// Handler dự phòng cho các sự kiện không có handler riêng (tùy chọn).
    void setFallbackHandler(Handler handler);

    Result dispatch(Connection& connection, const Packet& packet) const;

private:
    static bool isValidEventName(const std::string& eventName);

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, Handler> m_handlers;
    Handler m_fallback;
};

}  // namespace FSP

#endif  // FSP_EVENT_DISPATCHER_H
