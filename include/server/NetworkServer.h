#ifndef FSP_NETWORK_SERVER_H
#define FSP_NETWORK_SERVER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "common/Config.h"
#include "common/Connection.h"
#include "common/EventDispatcher.h"
#include "common/Socket.h"
#include "server/ServerWorkerThreadPool.h"

namespace FSP {

struct ServerOptions {
    std::string host = "0.0.0.0";
    uint16_t port = Config::SERVER_PORT;  // 0 = để OS chọn cổng trống (dùng trong test)
    size_t workerCount = static_cast<size_t>(Config::THREAD_POOL_SIZE);
    size_t maxPendingConnections = static_cast<size_t>(Config::MAX_PENDING_CONNECTIONS);
    int backlog = Config::BACKLOG;
    /// > 0: tự ngắt client không gửi gì trong N giây. 0 = tắt (M1.5 Heartbeat
    /// sẽ thay thế bằng cơ chế Ping/Pong).
    int idleTimeoutSeconds = 0;
    /// Chu kỳ vòng accept kiểm tra cờ dừng (ms).
    int acceptPollIntervalMs = 200;
};

/**
 * @brief FSP Core Network Server (M1.4).
 *
 *   [Listener thread]  waitReadable + accept()  ──push──>  ThreadSafeQueue  ──pop──>  [Worker N]
 *                                                                                   recvPacket
 *                                                                                   -> dispatch
 *
 * - Luồng listener CHỈ làm accept() và giao kết nối cho ServerWorkerThreadPool.
 * - Mỗi worker phục vụ trọn vẹn một kết nối: vòng lặp recvPacket -> EventDispatcher::dispatch.
 * - Hàng đợi đầy => trả ERROR_RESP "SERVER_BUSY" rồi đóng kết nối (Fail-Closed).
 * - Frame dị dạng / mất kết nối => đóng kết nối, giải phóng tài nguyên, gọi onDisconnect.
 */
class NetworkServer {
public:
    using ConnectionHook = std::function<void(Connection&)>;

    static constexpr const char* SERVER_BUSY_EVENT = "SERVER_BUSY";

    explicit NetworkServer(EventDispatcher& dispatcher, ServerOptions options = ServerOptions{});
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    /// Hook gọi trên worker thread khi kết nối bắt đầu / kết thúc (vd: M2 hủy session).
    /// Phải được đặt trước start()/run().
    void setOnConnect(ConnectionHook hook) { m_onConnect = std::move(hook); }
    void setOnDisconnect(ConnectionHook hook) { m_onDisconnect = std::move(hook); }

    /// bind + listen + tạo pool + chạy listener ở luồng nền. false nếu bind/listen thất bại.
    bool start();

    /**
     * @brief bind + listen rồi chạy vòng accept NGAY TRÊN luồng gọi (Main Listener Thread),
     * chặn cho tới khi requestStop()/stop() được gọi; sau đó tự dọn dẹp như stop().
     * @return false nếu không khởi động được.
     */
    bool run();

    /// Yêu cầu dừng bất đồng bộ. Chỉ ghi một std::atomic<bool> lock-free nên gọi được từ
    /// signal handler (SIGINT). Vòng accept thoát trong <= acceptPollIntervalMs.
    void requestStop() noexcept;

    /// Dừng hẳn: ngừng accept, ngắt mọi kết nối, join toàn bộ thread. Idempotent.
    void stop();

    bool isRunning() const noexcept { return m_running.load(); }
    /// Cổng thực tế đang lắng nghe (khác options.port khi port = 0).
    uint16_t port() const noexcept { return m_boundPort.load(); }

    size_t activeConnections() const;
    uint64_t totalAccepted() const noexcept { return m_totalAccepted.load(); }
    uint64_t totalRejected() const noexcept { return m_totalRejected.load(); }

private:
    bool openListener();
    void acceptLoop();
    void serveConnection(const std::shared_ptr<Connection>& connection);
    void cleanup();

    EventDispatcher& m_dispatcher;
    const ServerOptions m_options;
    ConnectionHook m_onConnect;
    ConnectionHook m_onDisconnect;

    Socket m_listener{INVALID_SOCKET_FD};
    std::unique_ptr<ServerWorkerThreadPool> m_pool;
    std::thread m_acceptThread;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<uint16_t> m_boundPort{0};
    std::atomic<uint64_t> m_nextConnectionId{1};
    std::atomic<uint64_t> m_totalAccepted{0};
    std::atomic<uint64_t> m_totalRejected{0};

    std::mutex m_lifecycleMutex;  // tuần tự hóa start/run/stop
    mutable std::mutex m_connectionsMutex;
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> m_connections;
};

}  // namespace FSP

#endif  // FSP_NETWORK_SERVER_H
