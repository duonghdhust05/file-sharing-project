#include "common/Socket.h"

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
#endif

namespace FSP {

Socket::Socket() {
    m_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

void Socket::close() {
    if (isValid()) {
#if defined(_WIN32) || defined(_WIN64)
        ::closesocket(m_handle);
#else
        ::close(m_handle);
#endif
        m_handle = INVALID_SOCKET_FD;
    }
}

bool Socket::isValid() const noexcept {
    return m_handle != INVALID_SOCKET_FD;
}

socket_handle_t Socket::getHandle() const noexcept {
    return m_handle;
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
    if (!isValid())
        return false;
#if defined(_WIN32) || defined(_WIN64)
    DWORD timeout = seconds * 1000;
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
    setReuseAddr(true);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            return false;
        }
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
    socket_handle_t clientHandle =
        ::accept(m_handle, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    if (clientHandle == INVALID_SOCKET_FD) {
        return Socket(INVALID_SOCKET_FD);
    }
    if (clientIp) {
        char ipStr[INET_ADDRSTRLEN];
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
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        return false;
    }
    return ::connect(m_handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}
int Socket::send(const void* data, size_t length) {
    if (!isValid() || !data)
        return -1;
#if defined(_WIN32) || defined(_WIN64)
    return ::send(m_handle, static_cast<const char*>(data), static_cast<int>(length), 0);
#else
    return static_cast<int>(::send(m_handle, data, length, 0));
#endif
}
int Socket::recv(void* buffer, size_t length) {
    if (!isValid() || !buffer)
        return -1;
#if defined(_WIN32) || defined(_WIN64)
    return ::recv(m_handle, static_cast<char*>(buffer), static_cast<int>(length), 0);
#else
    return static_cast<int>(::recv(m_handle, buffer, length, 0));
#endif
}

}  // namespace FSP