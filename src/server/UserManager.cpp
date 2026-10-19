#include "server/UserManager.h"

#include <sstream>

#include "server/Sha256.h"

namespace FSP::Auth {

namespace {
bool validUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 32)
        return false;
    for (char c : u) {
        bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

bool constantTimeEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
}  // namespace

UserManager::UserManager(std::string dbPath, Clock clock)
    : dbPath_(std::move(dbPath)), clock_(std::move(clock)) {
    load();
}

void UserManager::load() {
    if (!std::filesystem::exists(dbPath_))
        return;
    std::ifstream f(dbPath_);
    if (!f)
        throw std::runtime_error("cannot open " + dbPath_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty())
            continue;
        auto m = jsonParseFlat(line);
        if (!m)
            throw std::runtime_error("corrupt user DB line: " + line);
        User u;
        u.id = static_cast<uint32_t>(jsonNeedU64(*m, "id"));
        u.username = jsonNeed(*m, "username");
        u.salt = jsonNeed(*m, "salt");
        u.password_hash = jsonNeed(*m, "password_hash");
        u.full_name = jsonNeed(*m, "full_name");
        u.group_id = static_cast<uint32_t>(jsonNeedU64(*m, "group_id"));
        u.created_at = static_cast<std::time_t>(jsonNeedU64(*m, "created_at"));
        users_.push_back(std::move(u));
    }
}

bool UserManager::save() const {
    std::ostringstream out;
    for (const auto& u : users_) {
        out << "{\"id\":" << u.id << ",\"username\":\"" << jsonEscape(u.username)
            << "\",\"salt\":\"" << u.salt << "\",\"password_hash\":\"" << u.password_hash
            << "\",\"full_name\":\"" << jsonEscape(u.full_name) << "\",\"group_id\":" << u.group_id
            << ",\"created_at\":" << u.created_at << "}\n";
    }
    return writeFileAtomic(dbPath_, out.str());
}

StatusCode UserManager::registerUser(const std::string& username, const std::string& password,
                                     const std::string& fullName, uint32_t* outId) {
    if (!validUsername(username) || password.size() < 6 || password.size() > 128 ||
        fullName.size() > 100)
        return StatusCode::BAD_REQUEST;

    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& existing : users_)
        if (existing.username == username)
            return StatusCode::CONFLICT;

    User u;
    u.id = users_.empty() ? 1 : users_.back().id + 1;  // first registered user (id 1) is admin
    u.username = username;
    u.full_name = fullName;
    u.salt = randomHex(16);
    u.password_hash = sha256Hex(u.salt + password);
    u.group_id = u.id;  // every user starts in a private group named after their id
    u.created_at = clock_();
    users_.push_back(u);
    if (!save()) {
        users_.pop_back();  // do not keep a user that failed to persist
        return StatusCode::INTERNAL_SERVER_ERROR;
    }
    if (outId)
        *outId = u.id;
    return StatusCode::CREATED;
}

std::optional<User> UserManager::verifyLogin(const std::string& username,
                                             const std::string& password) const {
    std::lock_guard<std::mutex> lock(mu_);
    const std::time_t now = clock_();
    if (lockedOut(username, now))
        return std::nullopt;

    const User* match = nullptr;
    for (const auto& u : users_) {
        if (u.username == username) {
            match = &u;
            break;
        }
    }
    bool ok = false;
    if (match)
        ok = constantTimeEq(sha256Hex(match->salt + password), match->password_hash);
    else
        sha256Hex(password);  // unknown user: burn similar time so response time doesn't reveal it

    if (ok) {
        failures_.erase(username);
        return *match;
    }
    recordFailure(username, now);
    return std::nullopt;
}

bool UserManager::lockedOut(const std::string& username, std::time_t now) const {
    auto it = failures_.find(username);
    return it != failures_.end() && it->second.locked_until > now;
}

void UserManager::recordFailure(const std::string& username, std::time_t now) const {
    auto it = failures_.find(username);
    if (it == failures_.end()) {
        if (failures_.size() >= MAX_TRACKED_NAMES) {
            /*
             * Table full: drop unlocked entries. If still full, skip tracking the new name, so
             * a flood of fake names can never release an active lockout.
             */
            for (auto e = failures_.begin(); e != failures_.end();)
                e = e->second.locked_until > now ? std::next(e) : failures_.erase(e);
            if (failures_.size() >= MAX_TRACKED_NAMES)
                return;
        }
        it = failures_.emplace(username, Failures{}).first;
    }
    if (++it->second.count >= MAX_FAILED_LOGINS) {
        it->second.count = 0;
        it->second.locked_until = now + LOCKOUT_SECONDS;
    }
}

bool UserManager::isLockedOut(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mu_);
    return lockedOut(username, clock_());
}

std::optional<User> UserManager::findById(uint32_t id) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& u : users_)
        if (u.id == id)
            return u;
    return std::nullopt;
}

StatusCode UserManager::setGroup(uint32_t actorId, uint32_t userId, uint32_t groupId) {
    if (actorId != ADMIN_USER_ID)
        return StatusCode::FORBIDDEN;
    if (groupId == 0)
        return StatusCode::BAD_REQUEST;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& u : users_) {
        if (u.id != userId)
            continue;
        uint32_t old = u.group_id;
        u.group_id = groupId;
        if (!save()) {
            u.group_id = old;
            return StatusCode::INTERNAL_SERVER_ERROR;
        }
        return StatusCode::OK;
    }
    return StatusCode::NOT_FOUND;
}

}  // namespace FSP::Auth
