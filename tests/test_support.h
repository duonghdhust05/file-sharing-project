#ifndef FSP_TEST_SUPPORT_H
#define FSP_TEST_SUPPORT_H

#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <utility>

#include "common/Socket.h"

#define TEST_ASSERT(cond, msg)                                                           \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at " << __FILE__ << ":" \
                      << __LINE__ << "\n";                                               \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

namespace TestSupport {

/// Chờ tối đa timeoutMs cho tới khi predicate() == true.
inline bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

/**
 * @brief Tạo một cặp socket TCP thật đã kết nối với nhau qua 127.0.0.1 (cổng do OS chọn).
 * @return true nếu thành công. first = phía "server" (accept), second = phía "client".
 */
inline bool makeLoopbackPair(FSP::Socket& serverSide, FSP::Socket& clientSide) {
    FSP::Socket listener;
    if (!listener.bind(0, "127.0.0.1") || !listener.listen(1))
        return false;
    uint16_t port = listener.localPort();
    FSP::Socket client;
    if (!client.connect("127.0.0.1", port))
        return false;
    if (listener.waitReadable(2000) != 1)
        return false;
    FSP::Socket accepted = listener.accept();
    if (!accepted.isValid())
        return false;
    serverSide = std::move(accepted);
    clientSide = std::move(client);
    return true;
}

}  // namespace TestSupport

#endif  // FSP_TEST_SUPPORT_H
