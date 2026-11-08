#include "common/Socket.h"

#include <cerrno>
#include <cstring>
#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")

namespace {
class WinsockInitializer {
public:
    WinsockInitializer() {
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0) {
            std::cerr << "[Socket] WSAStartup failed with error: " << res << "\n";
        }
    }
    ~WinsockInitializer() { WSACleanup(); }
};

static WinsockInitializer g_winsockInit;
}  // namespace
#else
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#endif

namespace {
#if defined(_WIN32) || defined(_WIN64)
constexpr int SEND_FLAGS = 0;
#elif defined(MSG_NOSIGNAL)
// Ghi vào socket mà peer đã đóng sẽ trả về EPIPE thay vì giết cả tiến trình bằng SIGPIPE.
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

// Giới hạn mỗi lần gọi send/recv trong phạm vi int để tránh tràn số khi ép kiểu.
constexpr size_t MAX_IO_CHUNK = static_cast<size_t>(1) << 30;

bool interruptedBySignal() {
#if defined(_WIN32) || defined(_WIN64)
    return false;
#else
    return errno == EINTR;
#endif
}

void disableSigPipe(socket_handle_t handle) {
#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int on = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    (void)handle;
#endif
}
}  // namespace

namespace FSP {

Socket::Socket() {
    m_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (isValid())
        disableSigPipe(m_handle);
}

Socket::Socket(socket_handle_t handle) : m_handle(handle) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : m_handle(other.m_handle) {
    other.m_handle = INVALID_SOCKET_FD;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        m_handle = other.m_handle;
        other.m_handle = INVALID_SOCKET_FD;
    }
    return *this;
}

void Socket::close() noexcept {
    if (isValid()) {
#if defined(_WIN32) || defined(_WIN64)
        ::closesocket(m_handle);
#else
        ::close(m_handle);
#endif
        m_handle = INVALID_SOCKET_FD;
    }
}

void Socket::shutdownBoth() noexcept {
    if (isValid()) {
#if defined(_WIN32) || defined(_WIN64)
        ::shutdown(m_handle, SD_BOTH);
#else
        ::shutdown(m_handle, SHUT_RDWR);
#endif
    }
}

bool Socket::isValid() const noexcept {
    return m_handle != INVALID_SOCKET_FD;
}

socket_handle_t Socket::getHandle() const noexcept {
    return m_handle;
}

int Socket::lastError() noexcept {
#if defined(_WIN32) || defined(_WIN64)
    return WSAGetLastError();
#else
    return errno;
#endif
}

uint16_t Socket::localPort() const {
    if (!isValid())
        return 0;
    sockaddr_in addr{};
#if defined(_WIN32) || defined(_WIN64)
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::getsockname(m_handle, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
        return 0;
    return ntohs(addr.sin_port);
}

bool Socket::setReuseAddr(bool reuse) {
    if (!isValid())
        return false;
    int optval = reuse ? 1 : 0;
#if defined(_WIN32) || defined(_WIN64)
    return ::setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval),
                        sizeof(optval)) == 0;
#else
    return ::setsockopt(m_handle, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == 0;
#endif
}

bool Socket::setNoDelay(bool enable) {
    if (!isValid())
        return false;
    int optval = enable ? 1 : 0;
#if defined(_WIN32) || defined(_WIN64)
    return ::setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&optval),
                        sizeof(optval)) == 0;
#else
    return ::setsockopt(m_handle, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) == 0;
#endif
}

bool Socket::setNonBlocking(bool nonBlocking) {
    if (!isValid())
        return false;
#if defined(_WIN32) || defined(_WIN64)
    u_long mode = nonBlocking ? 1 : 0;
    return ::ioctlsocket(m_handle, FIONBIO, &mode) == 0;
#else
    int flags = ::fcntl(m_handle, F_GETFL, 0);
    if (flags == -1)
        return false;
    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(m_handle, F_SETFL, flags) == 0;
#endif
}

bool Socket::setTimeout(int seconds) {
    if (!isValid() || seconds < 0)
        return false;
#if defined(_WIN32) || defined(_WIN64)
    DWORD timeout = static_cast<DWORD>(seconds) * 1000;
    bool s1 = (::setsockopt(m_handle, SOL_SOCKET, SO_RCVTIMEO,
                            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
    bool s2 = (::setsockopt(m_handle, SOL_SOCKET, SO_SNDTIMEO,
                            reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
    return s1 && s2;
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    bool s1 = (::setsockopt(m_handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);
    bool s2 = (::setsockopt(m_handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0);
    return s1 && s2;
#endif
}

bool Socket::bind(uint16_t port, const std::string& host) {
    if (!isValid()) {
        m_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!isValid())
            return false;
    }
#if !defined(_WIN32) && !defined(_WIN64)
    // POSIX: cho phép restart server ngay khi cổng cũ còn ở TIME_WAIT.
    // Windows: KHÔNG bật SO_REUSEADDR vì trên Winsock nó cho phép tiến trình khác bind trùng cổng.
    setReuseAddr(true);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        return false;
    }
    return ::bind(m_handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool Socket::listen(int backlog) {
    if (!isValid())
        return false;
    return ::listen(m_handle, backlog) == 0;
}

Socket Socket::accept(std::string* clientIp, uint16_t* clientPort) {
    if (!isValid())
        return Socket(INVALID_SOCKET_FD);
    sockaddr_in clientAddr{};
#if defined(_WIN32) || defined(_WIN64)
    int addrLen = sizeof(clientAddr);
#else
    socklen_t addrLen = sizeof(clientAddr);
#endif
    socket_handle_t clientHandle = INVALID_SOCKET_FD;
    do {
        clientHandle = ::accept(m_handle, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    } while (clientHandle == INVALID_SOCKET_FD && interruptedBySignal());

    if (clientHandle == INVALID_SOCKET_FD) {
        return Socket(INVALID_SOCKET_FD);
    }
    disableSigPipe(clientHandle);
    if (clientIp) {
        char ipStr[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr))) {
            *clientIp = ipStr;
        }
    }
    if (clientPort) {
        *clientPort = ntohs(clientAddr.sin_port);
    }
    return Socket(clientHandle);
}

bool Socket::connect(const std::string& host, uint16_t port) {
    if (!isValid()) {
        m_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (!isValid())
            return false;
        disableSigPipe(m_handle);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Không phải IPv4 dạng số -> phân giải hostname (vd: "localhost") bằng getaddrinfo.
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        ::freeaddrinfo(result);
    }

    int rc = ::connect(m_handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return rc == 0;
}

int Socket::send(const void* data, size_t length) {
    if (!isValid() || !data)
        return -1;
    if (length > MAX_IO_CHUNK)
        length = MAX_IO_CHUNK;
    for (;;) {
#if defined(_WIN32) || defined(_WIN64)
        int rc =
            ::send(m_handle, static_cast<const char*>(data), static_cast<int>(length), SEND_FLAGS);
#else
        int rc = static_cast<int>(::send(m_handle, data, length, SEND_FLAGS));
#endif
        if (rc < 0 && interruptedBySignal())
            continue;
        return rc;
    }
}

int Socket::recv(void* buffer, size_t length) {
    if (!isValid() || !buffer)
        return -1;
    if (length > MAX_IO_CHUNK)
        length = MAX_IO_CHUNK;
    for (;;) {
#if defined(_WIN32) || defined(_WIN64)
        int rc = ::recv(m_handle, static_cast<char*>(buffer), static_cast<int>(length), 0);
#else
        int rc = static_cast<int>(::recv(m_handle, buffer, length, 0));
#endif
        if (rc < 0 && interruptedBySignal())
            continue;
        return rc;
    }
}

int Socket::waitReadable(int timeoutMs) const {
    if (!isValid())
        return -1;
#if defined(_WIN32) || defined(_WIN64)
    WSAPOLLFD pfd{};
    pfd.fd = m_handle;
    pfd.events = POLLRDNORM;
    int rc = ::WSAPoll(&pfd, 1, timeoutMs);
#else
    pollfd pfd{};
    pfd.fd = m_handle;
    pfd.events = POLLIN;
    int rc = 0;
    do {
        rc = ::poll(&pfd, 1, timeoutMs);
    } while (rc < 0 && interruptedBySignal());
#endif
    if (rc < 0)
        return -1;
    return rc == 0 ? 0 : 1;
}

}  // namespace FSP
