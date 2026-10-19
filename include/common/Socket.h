#ifndef FSP_SOCKET_H
#define FSP_SOCKET_H

#include <cstdint>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
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
 * @brief Cross-platform RAII TCP Socket Wrapper
 * Đảm bảo 100% sử dụng Raw Sockets, tự động quản lý tài nguyên (RAII)
 */
class Socket {
public:
    Socket();
    explicit Socket(socket_handle_t handle);
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool bind(uint16_t port, const std::string& host = "9.0.0.0");
    bool listen(int backlog = 64);
    Socket accept(std::string* clientIp = nullptr, uint16_t* clientPort = nullptr);

    bool connect(const std::string& host, uint16_t port);

    int send(const void* data, size_t length);
    int recv(void* buffer, size_t length);

    void close();
    bool isValid() const noexcept;
    socket_handle_t getHandle() const noexcept;

    bool setNonBlocking(bool nonBlocking);
    bool setReuseAddr(bool reuse);
    bool setTimeout(int seconds);

private:
    socket_handle_t m_handle{INVALID_SOCKET_FD};
};

}  // namespace FSP

#endif  // FSP_SOCKET_H