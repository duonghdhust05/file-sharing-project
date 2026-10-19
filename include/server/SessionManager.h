#ifndef FSP_SESSION_MANAGER_H
#define FSP_SESSION_MANAGER_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "server/AuthUtil.h"

namespace FSP::Auth {

struct Session {
    std::string token;  // UUID v4
    uint32_t user_id = 0;
    std::string cwd = "/";  // current virtual directory, owned/validated by M3
    std::time_t last_active = 0;
};

// Thread-safe in-memory session table. A session dies after 30 minutes without use.
class SessionManager {
public:
    static constexpr std::time_t TIMEOUT_SECONDS = 30 * 60;

    explicit SessionManager(Clock clock = systemClock);

    // Creates a session for userId and returns its token.
    std::string create(uint32_t userId);

    // Returns the live session and refreshes last_active; nullopt if unknown or expired.
    std::optional<Session> validate(const std::string& token);

    // Removes the session; unknown tokens are ignored.
    void destroy(const std::string& token);

    // Updates cwd of a live session (refreshes last_active). False if unknown or expired.
    bool setCwd(const std::string& token, const std::string& cwd);

private:
    std::map<std::string, Session>::iterator findLive(const std::string& token);  // holds mu_

    Clock clock_;
    std::mutex mu_;
    std::map<std::string, Session> sessions_;
};

}  // namespace FSP::Auth

#endif  // FSP_SESSION_MANAGER_H
