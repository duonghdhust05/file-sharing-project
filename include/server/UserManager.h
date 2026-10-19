#ifndef FSP_USER_MANAGER_H
#define FSP_USER_MANAGER_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/FSPProtocol.h"
#include "server/AuthUtil.h"

namespace FSP::Auth {

constexpr uint32_t ADMIN_USER_ID = 1;  // first registered user

struct User {
    uint32_t id = 0;
    std::string username;
    std::string salt;           // 16 random bytes as 32 hex chars
    std::string password_hash;  // sha256Hex(salt + password), 64 hex chars
    std::string full_name;      // optional display name, up to 100 bytes
    uint32_t group_id = 0;
    std::time_t created_at = 0;
};

// Thread-safe user store persisted to a JSON-lines file (one flat JSON object per user).
class UserManager {
public:
    static constexpr int MAX_FAILED_LOGINS = 5;  // consecutive failures before lockout
    static constexpr std::time_t LOCKOUT_SECONDS = 5 * 60;
    static constexpr size_t MAX_TRACKED_NAMES = 10000;  // bound on the failure table

    // Loads dbPath if it exists. Throws std::runtime_error if the file is corrupt.
    explicit UserManager(std::string dbPath, Clock clock = systemClock);

    // CREATED, BAD_REQUEST (invalid input), CONFLICT (name taken) or INTERNAL_SERVER_ERROR.
    StatusCode registerUser(const std::string& username, const std::string& password,
                            const std::string& fullName = "", uint32_t* outId = nullptr);

    /*
     * Returns the user if the password matches. After MAX_FAILED_LOGINS failures in a row the name
     * is locked for LOCKOUT_SECONDS, even for the right password. Success resets the count.
     */
    std::optional<User> verifyLogin(const std::string& username, const std::string& password) const;

    /* True while `username` is locked out (unknown names are throttled too). */
    bool isLockedOut(const std::string& username) const;

    std::optional<User> findById(uint32_t id) const;

    /* Admin only. OK, FORBIDDEN, BAD_REQUEST (groupId 0), NOT_FOUND, INTERNAL_SERVER_ERROR. */
    StatusCode setGroup(uint32_t actorId, uint32_t userId, uint32_t groupId);

private:
    struct Failures {
        int count = 0;
        std::time_t locked_until = 0;
    };

    void load();
    bool save() const;                                                       // caller holds mu_
    bool lockedOut(const std::string& username, std::time_t now) const;      // caller holds mu_
    void recordFailure(const std::string& username, std::time_t now) const;  // caller holds mu_

    std::string dbPath_;
    Clock clock_;
    mutable std::mutex mu_;
    std::vector<User> users_;
    mutable std::map<std::string, Failures> failures_;
};

}  // namespace FSP::Auth

#endif  // FSP_USER_MANAGER_H
