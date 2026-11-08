#include "common/EventDispatcher.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <mutex>
#include <utility>

namespace FSP {

bool EventDispatcher::isValidEventName(const std::string& eventName) {
    return !eventName.empty() && eventName.size() < EVENT_NAME_MAX_LEN;
}

bool EventDispatcher::registerHandler(const std::string& eventName, Handler handler) {
    if (!isValidEventName(eventName) || !handler)
        return false;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return m_handlers.emplace(eventName, std::move(handler)).second;
}

bool EventDispatcher::replaceHandler(const std::string& eventName, Handler handler) {
    if (!isValidEventName(eventName) || !handler)
        return false;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_handlers[eventName] = std::move(handler);
    return true;
}

bool EventDispatcher::unregisterHandler(const std::string& eventName) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return m_handlers.erase(eventName) > 0;
}

bool EventDispatcher::hasHandler(const std::string& eventName) const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_handlers.find(eventName) != m_handlers.end();
}

std::vector<std::string> EventDispatcher::registeredEvents() const {
    std::vector<std::string> names;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        names.reserve(m_handlers.size());
        for (const auto& entry : m_handlers)
            names.push_back(entry.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void EventDispatcher::setFallbackHandler(Handler handler) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_fallback = std::move(handler);
}

EventDispatcher::Result EventDispatcher::dispatch(Connection& connection,
                                                  const Packet& packet) const {
    if (packet.event.empty()) {
        connection.sendError(packet.event, StatusCode::BAD_REQUEST, "missing event name");
        return Result::REJECTED;
    }

    // Sao chép handler ra ngoài rồi mới gọi -> không giữ lock trong lúc chạy logic nghiệp vụ.
    Handler handler;
    bool isFallback = false;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_handlers.find(packet.event);
        if (it != m_handlers.end()) {
            handler = it->second;
        } else if (m_fallback) {
            handler = m_fallback;
            isFallback = true;
        }
    }

    if (!handler) {
        connection.sendError(packet.event, StatusCode::BAD_REQUEST,
                             "unknown event: " + packet.event);
        return Result::UNKNOWN_EVENT;
    }

    try {
        handler(connection, packet);
    } catch (const std::exception& ex) {
        std::cerr << "[EventDispatcher] Handler for '" << packet.event
                  << "' threw exception: " << ex.what() << "\n";
        connection.sendError(packet.event, StatusCode::INTERNAL_SERVER_ERROR, "internal error");
        return Result::HANDLER_FAILED;
    } catch (...) {
        std::cerr << "[EventDispatcher] Handler for '" << packet.event
                  << "' threw unknown exception\n";
        connection.sendError(packet.event, StatusCode::INTERNAL_SERVER_ERROR, "internal error");
        return Result::HANDLER_FAILED;
    }
    return isFallback ? Result::FALLBACK : Result::HANDLED;
}

}  // namespace FSP
