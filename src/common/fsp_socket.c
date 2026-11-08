#include "common/fsp_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* Hệ không có MSG_NOSIGNAL: server đã bỏ qua SIGPIPE */
#endif

int fsp_listen(uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    /* SO_REUSEADDR: khởi động lại server ngay khi cổng cũ còn ở trạng thái TIME_WAIT */
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0 ||
        bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, backlog) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int fsp_connect(const char *host, uint16_t port) {
    if (!host)
        return -1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        int rc;
        do {
            rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        } while (rc != 0 && errno == EINTR);
        if (rc == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int send_exact(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    if (fd < 0 || (!p && len > 0))
        return -1;

    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1; /* Lỗi thật (EPIPE, ECONNRESET...) -> Fail-Closed */
        }
    }
    return 0;
}

int recv_exact(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    if (fd < 0 || (!p && len > 0))
        return -1;

    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n > 0) {
            got += (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1; /* n == 0: đối phương đóng; n < 0: lỗi thật */
        }
    }
    return 0;
}

uint16_t fsp_local_port(int fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (fd < 0 || getsockname(fd, (struct sockaddr *)&addr, &len) != 0)
        return 0;
    return ntohs(addr.sin_port);
}

int fsp_peer_address(int fd, char *out, size_t out_size) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char ip[INET_ADDRSTRLEN];
    if (!out || out_size == 0 || fd < 0)
        return -1;
    if (getpeername(fd, (struct sockaddr *)&addr, &len) != 0 ||
        !inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
        snprintf(out, out_size, "?");
        return -1;
    }
    snprintf(out, out_size, "%s:%u", ip, (unsigned)ntohs(addr.sin_port));
    return 0;
}

void fsp_close(int fd) {
    if (fd >= 0)
        close(fd);
}
