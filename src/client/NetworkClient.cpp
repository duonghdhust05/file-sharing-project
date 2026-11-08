#include "client/NetworkClient.h"

#include <chrono>
#include <utility>

#include "common/Connection.h"

namespace FSP {

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t port) {
    disconnect();
    Socket socket;
    if (!socket.isValid() || !socket.connect(host, port)) {
        return false;
    }
    socket.setNoDelay(true);
    m_socket = std::move(socket);
    m_interrupted = false;
    m_connected = true;
    return true;
}

void NetworkClient::disconnect() {
    m_connected = false;
    std::lock_guard<std::mutex> lock(m_sendMutex);  // không đóng socket giữa lúc luồng khác gửi
    m_socket.shutdownBoth();
    m_socket.close();
    m_backlog.clear();
}

bool NetworkClient::isConnected() const {
    return m_connected.load();
}

void NetworkClient::interrupt() noexcept {
    m_interrupted = true;
    m_connected = false;
    m_socket.shutdownBoth();
}

bool NetworkClient::send(MessageType type, const std::string& event, const void* data,
                         size_t length) {
    if (!m_connected.load())
        return false;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        ok = PacketHandler::sendPacket(m_socket, type, event, data, length);
    }
    if (!ok)
        interrupt();
    return ok;
}

bool NetworkClient::sendEvent(const std::string& event, const std::string& jsonPayload) {
    return send(MessageType::EVENT_REQ, event, jsonPayload.data(), jsonPayload.size());
}

NetworkClient::RecvStatus NetworkClient::receiveFromSocket(Packet& outPacket, int timeoutMs) {
    if (!m_connected.load())
        return RecvStatus::DISCONNECTED;
    if (timeoutMs >= 0) {
        int ready = m_socket.waitReadable(timeoutMs);
        if (ready == 0)
            return RecvStatus::TIMEOUT;
        if (ready < 0) {
            disconnect();
            return RecvStatus::DISCONNECTED;
        }
    }
    RecvControl control;
    control.abortFlag = &m_interrupted;  // interrupt() dừng được receive() trên cả Windows
    if (!PacketHandler::recvPacket(m_socket, outPacket, control)) {
        disconnect();
        return RecvStatus::DISCONNECTED;
    }
    return RecvStatus::OK;
}

NetworkClient::RecvStatus NetworkClient::receive(Packet& outPacket, int timeoutMs) {
    if (!m_backlog.empty()) {
        outPacket = std::move(m_backlog.front());
        m_backlog.pop_front();
        return RecvStatus::OK;
    }
    return receiveFromSocket(outPacket, timeoutMs);
}

std::optional<Response> NetworkClient::request(const std::string& event,
                                               const std::string& jsonPayload, int timeoutMs) {
    if (!sendEvent(event, jsonPayload))
        return std::nullopt;

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);

    for (;;) {
        int remaining = -1;
        if (timeoutMs >= 0) {
            auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            if (left.count() <= 0)
                return std::nullopt;
            remaining = static_cast<int>(left.count());
        }

        Packet packet;
        RecvStatus status = receiveFromSocket(packet, remaining);
        if (status != RecvStatus::OK)
            return std::nullopt;

        bool isReply =
            (packet.type == MessageType::EVENT_RESP || packet.type == MessageType::ERROR_RESP) &&
            packet.event == event;
        if (!isReply) {
            constexpr size_t MAX_BACKLOG = 1024;
            if (m_backlog.size() >= MAX_BACKLOG) {
                disconnect();  // Server đang spam frame không mong đợi -> Fail-Closed
                return std::nullopt;
            }
            m_backlog.push_back(std::move(packet));
            continue;
        }

        Response response;
        response.type = packet.type;
        response.event = packet.event;
        response.body = packet.text();
        response.status = Json::parseStatus(response.body);
        return response;
    }
}

}  // namespace FSP
