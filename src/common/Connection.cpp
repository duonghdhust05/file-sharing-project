#include "common/Connection.h"

#include <cctype>
#include <cstdio>
#include <utility>

namespace FSP {

namespace Json {

std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (unsigned char c : raw) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string envelope(StatusCode status, const std::string& dataJson) {
    return "{\"status\":" + std::to_string(static_cast<int>(status)) +
           ",\"data\":" + (dataJson.empty() ? std::string("{}") : dataJson) + "}";
}

std::string errorEnvelope(StatusCode status, const std::string& message) {
    return envelope(status, "{\"error\":\"" + escape(message) + "\"}");
}

int parseStatus(const std::string& envelopeJson) {
    static const std::string prefix = "{\"status\":";
    if (envelopeJson.compare(0, prefix.size(), prefix) != 0)
        return -1;
    size_t pos = prefix.size();
    int value = 0;
    size_t digits = 0;
    while (pos < envelopeJson.size() && digits < 4 &&
           std::isdigit(static_cast<unsigned char>(envelopeJson[pos]))) {
        value = value * 10 + (envelopeJson[pos] - '0');
        ++pos;
        ++digits;
    }
    if (digits == 0 || pos >= envelopeJson.size() || envelopeJson[pos] != ',')
        return -1;
    return value;
}

}  // namespace Json

Connection::Connection(Socket socket, uint64_t id, std::string peerIp, uint16_t peerPort)
    : m_socket(std::move(socket)), m_id(id), m_peerIp(std::move(peerIp)), m_peerPort(peerPort) {}

std::string Connection::peerAddress() const {
    return m_peerIp + ":" + std::to_string(m_peerPort);
}

bool Connection::send(MessageType type, const std::string& event, const void* data, size_t length) {
    if (m_closing.load())
        return false;
    std::lock_guard<std::mutex> lock(m_sendMutex);
    bool ok = PacketHandler::sendPacket(m_socket, type, event, data, length);
    if (!ok) {
        // Fail-Closed: lỗi gửi => kết nối không còn tin cậy được nữa.
        requestClose();
    }
    return ok;
}

bool Connection::send(MessageType type, const std::string& event, const std::string& payload) {
    return send(type, event, payload.data(), payload.size());
}

bool Connection::respond(const std::string& event, StatusCode status, const std::string& dataJson) {
    return send(MessageType::EVENT_RESP, event, Json::envelope(status, dataJson));
}

bool Connection::sendError(const std::string& event, StatusCode status,
                           const std::string& message) {
    return send(MessageType::ERROR_RESP, event, Json::errorEnvelope(status, message));
}

bool Connection::receive(Packet& outPacket) {
    if (m_closing.load())
        return false;
    RecvControl control;
    control.abortFlag = &m_closing;  // requestClose() dừng được receive() trên cả Windows
    control.idleTimeoutMs = m_idleTimeoutMs;
    return PacketHandler::recvPacket(m_socket, outPacket, control);
}

void Connection::requestClose() noexcept {
    bool expected = false;
    if (m_closing.compare_exchange_strong(expected, true)) {
        // Chỉ shutdown, KHÔNG close: handle vẫn thuộc sở hữu của Connection cho tới destructor,
        // nên không có nguy cơ luồng khác dùng nhầm một file descriptor đã được OS cấp lại.
        m_socket.shutdownBoth();
    }
}

void Connection::setAttribute(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_attrMutex);
    m_attributes[key] = value;
}

std::optional<std::string> Connection::attribute(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_attrMutex);
    auto it = m_attributes.find(key);
    if (it == m_attributes.end())
        return std::nullopt;
    return it->second;
}

void Connection::eraseAttribute(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_attrMutex);
    m_attributes.erase(key);
}

}  // namespace FSP
