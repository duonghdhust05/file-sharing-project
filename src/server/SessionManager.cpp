#include "server/SessionManager.h"

namespace FSP::Auth {

namespace {
// Formats 16 random bytes as an RFC 4122 version-4 UUID.
std::string newUuid() {
    std::string h = randomHex(16);
    h[12] = '4';
    h[16] = "89ab"[(h[16] >= 'a' ? h[16] - 'a' + 10 : h[16] - '0') & 3];
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) +
           "-" + h.substr(20, 12);
}
}  // namespace

SessionManager::SessionManager(Clock clock) : clock_(std::move(clock)) {}

std::string SessionManager::create(uint32_t userId) {
    std::lock_guard<std::mutex> lock(mu_);
    Session s;
    s.token = newUuid();
    s.user_id = userId;
    s.last_active = clock_();
    sessions_[s.token] = s;
    return s.token;
}

std::map<std::string, Session>::iterator SessionManager::findLive(const std::string& token) {
    auto it = sessions_.find(token);
    if (it == sessions_.end())
        return it;
    if (clock_() - it->second.last_active > TIMEOUT_SECONDS) {
        sessions_.erase(it);
        return sessions_.end();
    }
    it->second.last_active = clock_();
    return it;
}

std::optional<Session> SessionManager::validate(const std::string& token) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = findLive(token);
    if (it == sessions_.end())
        return std::nullopt;
    return it->second;
}

void SessionManager::destroy(const std::string& token) {
    std::lock_guard<std::mutex> lock(mu_);
    sessions_.erase(token);
}

bool SessionManager::setCwd(const std::string& token, const std::string& cwd) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = findLive(token);
    if (it == sessions_.end())
        return false;
    it->second.cwd = cwd;
    return true;
}

}  // namespace FSP::Auth
