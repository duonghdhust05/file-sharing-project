#ifndef FSP_SOCKET_H
#define FSP_SOCKET_H

#include <cstdint>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle_t = SOCKET;
constexpr socket_handle_t INVALID_SOCKET_FD = INVALID_SOCKET;
constexpr int SOCKET_ERROR_VAL = SOCKET_ERROR;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_handle_t = int;
constexpr socket_handle_t INVALID_SOCKET_FD = -1;
constexpr int SOCKET_ERROR_VAL = -1;
#endif

namespace FSP {

/**
 * @brief Cross-platform RAII TCP Socket Wrapper (M1.1)
 *
 * - 100% Raw Sockets: Winsock2 (Windows) / POSIX sockets (Linux).
 * - RAII: handle được đóng tự động trong destructor; chỉ cho phép move, cấm copy.
 * - send()/recv() không bao giờ phát SIGPIPE (MSG_NOSIGNAL) và tự retry khi bị EINTR.
 */
class Socket {
public:
    /// Tạo ngay một TCP socket (AF_INET, SOCK_STREAM).
    Socket();
    /// Nhận quyền sở hữu một handle đã có (vd: kết quả của ::accept()).
    explicit Socket(socket_handle_t handle);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    /// Bind IPv4. host rỗng hoặc "0.0.0.0" = mọi interface. port = 0 để OS tự chọn cổng trống.
    bool bind(uint16_t port, const std::string& host = "0.0.0.0");
    bool listen(int backlog = 64);
    /// Trả về Socket không hợp lệ (isValid() == false) nếu accept thất bại.
    Socket accept(std::string* clientIp = nullptr, uint16_t* clientPort = nullptr);

    /// host có thể là IPv4 dạng số ("127.0.0.1") hoặc hostname ("localhost").
    bool connect(const std::string& host, uint16_t port);

    /// Gửi tối đa length bytes. Trả về số byte đã gửi, hoặc -1 khi lỗi.
    int send(const void* data, size_t length);
    /// Nhận tối đa length bytes. Trả về số byte đã nhận, 0 khi peer đóng, -1 khi lỗi/timeout.
    int recv(void* buffer, size_t length);

    /**
     * @brief Chờ tối đa timeoutMs cho tới khi socket có dữ liệu để đọc (hoặc có kết nối mới
     * với listening socket).
     * @return 1 = sẵn sàng, 0 = hết thời gian chờ, -1 = lỗi
     */
    int waitReadable(int timeoutMs) const;

    /// Đóng chiều đọc/ghi (SHUT_RDWR / SD_BOTH) nhưng vẫn giữ handle; báo cho peer biết kết nối
    /// kết thúc. LƯU Ý: trên Winsock lệnh này KHÔNG đánh thức recv() đang bị chặn ở luồng khác
    /// -> muốn dừng luồng nhận hãy dùng RecvControl::abortFlag (xem PacketHandler.h).
    void shutdownBoth() noexcept;
    void close() noexcept;
    bool isValid() const noexcept;
    socket_handle_t getHandle() const noexcept;
    /// Cổng local thực tế sau bind() (hữu ích khi bind cổng 0). Trả về 0 nếu lỗi.
    uint16_t localPort() const;

    bool setNonBlocking(bool nonBlocking);
    bool setReuseAddr(bool reuse);
    bool setTimeout(int seconds);
    bool setNoDelay(bool enable);

    /// Mã lỗi socket cuối cùng (errno / WSAGetLastError()).
    static int lastError() noexcept;

private:
    socket_handle_t m_handle{INVALID_SOCKET_FD};
};

}  // namespace FSP

#endif  // FSP_SOCKET_H
