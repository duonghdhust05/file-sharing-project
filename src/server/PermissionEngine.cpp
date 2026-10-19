#include "server/PermissionEngine.h"

#include <sstream>
#include <vector>

namespace FSP::Auth {

namespace {
// rwx-like triplet for an access type (r=4, w=2, share=1) moved to the owner (6) or group (3) slot.
uint16_t classBits(AccessType t, int shift) {
    uint16_t base = t == AccessType::READ ? 4 : t == AccessType::WRITE ? 2 : 1;
    return static_cast<uint16_t>(base << shift);
}

std::string encodeAcl(const std::map<uint32_t, uint8_t>& acl) {
    std::string s;
    for (const auto& [uid, mask] : acl) {
        if (!s.empty())
            s += ',';
        s += std::to_string(uid) + ":" + std::to_string(mask);
    }
    return s;
}

std::map<uint32_t, uint8_t> decodeAcl(const std::string& s) {
    std::map<uint32_t, uint8_t> acl;
    std::istringstream in(s);
    std::string item;
    while (std::getline(in, item, ',')) {
        auto colon = item.find(':');
        uint64_t uid = 0, mask = 0;
        if (colon == std::string::npos || !parseU64(item.substr(0, colon), uid) ||
            !parseU64(item.substr(colon + 1), mask) || mask == 0 || mask > 7)
            throw std::runtime_error("corrupt ACL entry: " + item);
        acl[static_cast<uint32_t>(uid)] = static_cast<uint8_t>(mask);
    }
    return acl;
}
}  // namespace

PermissionEngine::PermissionEngine(std::string dbPath, const UserManager& users)
    : dbPath_(std::move(dbPath)), users_(users) {
    load();
}

void PermissionEngine::load() {
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
            throw std::runtime_error("corrupt permission DB line: " + line);
        PathMeta pm;
        pm.owner_id = static_cast<uint32_t>(jsonNeedU64(*m, "owner_id"));
        pm.group_id = static_cast<uint32_t>(jsonNeedU64(*m, "group_id"));
        pm.perm_bits = static_cast<uint16_t>(jsonNeedU64(*m, "perm_bits"));
        uint64_t level = jsonNeedU64(*m, "sharing_level");
        if (pm.perm_bits > 0777 || level > 2)
            throw std::runtime_error("corrupt permission DB");
        pm.sharing_level = static_cast<SharingLevel>(level);
        pm.acl = decodeAcl(jsonNeed(*m, "acl"));
        meta_[jsonNeed(*m, "path")] = std::move(pm);
    }
}

bool PermissionEngine::save() const {
    std::ostringstream out;
    for (const auto& [path, pm] : meta_) {
        out << "{\"path\":\"" << jsonEscape(path) << "\",\"owner_id\":" << pm.owner_id
            << ",\"group_id\":" << pm.group_id << ",\"perm_bits\":" << pm.perm_bits
            << ",\"sharing_level\":" << static_cast<int>(pm.sharing_level) << ",\"acl\":\""
            << encodeAcl(pm.acl) << "\"}\n";
    }
    return writeFileAtomic(dbPath_, out.str());
}

bool PermissionEngine::allowed(uint32_t userId, const PathMeta& m, AccessType t) const {
    if (userId == ADMIN_USER_ID)
        return true;
    if (userId == m.owner_id) {
        // SRS section 5: the owner can always read and chmod/share; writes obey the owner bit.
        if (t != AccessType::WRITE)
            return true;
        return (m.perm_bits & classBits(t, 6)) != 0;
    }
    if (m.sharing_level == SharingLevel::PRIVATE)
        return false;  // overrides group/other bits

    auto user = users_.findById(userId);
    if (!user)
        return false;
    if (user->group_id == m.group_id && (m.perm_bits & classBits(t, 3)))
        return true;
    if (t == AccessType::READ && m.sharing_level == SharingLevel::PUBLIC)
        return true;
    if (m.sharing_level == SharingLevel::SHARED) {
        auto it = m.acl.find(userId);
        return it != m.acl.end() && (it->second & static_cast<uint8_t>(t)) != 0;
    }
    return false;
}

StatusCode PermissionEngine::registerPath(const std::string& path, uint32_t ownerId, uint16_t perm,
                                          SharingLevel level) {
    auto owner = users_.findById(ownerId);
    if (!owner || perm > 0777)
        return StatusCode::BAD_REQUEST;
    std::lock_guard<std::mutex> lock(mu_);
    if (meta_.count(path))
        return StatusCode::CONFLICT;
    PathMeta pm;
    pm.owner_id = ownerId;
    pm.group_id = owner->group_id;
    pm.perm_bits = perm;
    pm.sharing_level = level;
    meta_[path] = pm;
    if (!save()) {
        meta_.erase(path);
        return StatusCode::INTERNAL_SERVER_ERROR;
    }
    return StatusCode::CREATED;
}

bool PermissionEngine::removePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = meta_.find(path);
    if (it == meta_.end())
        return false;
    PathMeta backup = it->second;
    meta_.erase(it);
    if (!save()) {
        meta_[path] = backup;  // keep memory and disk consistent
        return false;
    }
    return true;
}

StatusCode PermissionEngine::renamePath(uint32_t actorId, const std::string& oldPath,
                                        const std::string& newPath) {
    const std::string prefix = oldPath + "/";
    if (oldPath == newPath || newPath.compare(0, prefix.size(), prefix) == 0)
        return StatusCode::BAD_REQUEST;

    std::lock_guard<std::mutex> lock(mu_);
    auto root = meta_.find(oldPath);
    if (root == meta_.end())
        return StatusCode::NOT_FOUND;
    if (!allowed(actorId, root->second, AccessType::WRITE))
        return StatusCode::FORBIDDEN;

    // Keys sharing the prefix `oldPath` are contiguous in the ordered map.
    std::map<std::string, PathMeta> moved;
    std::vector<std::string> oldKeys;
    for (auto it = meta_.lower_bound(oldPath);
         it != meta_.end() && it->first.compare(0, oldPath.size(), oldPath) == 0; ++it) {
        if (it->first == oldPath) {
            moved[newPath] = it->second;
        } else if (it->first.compare(0, prefix.size(), prefix) == 0) {
            moved[newPath + it->first.substr(oldPath.size())] = it->second;
        } else {
            continue;  // e.g. "/a-b" when renaming "/a": same prefix text, not a child
        }
        oldKeys.push_back(it->first);
    }
    for (const auto& kv : moved)
        if (meta_.count(kv.first))
            return StatusCode::CONFLICT;

    auto backup = meta_;
    for (const auto& k : oldKeys)
        meta_.erase(k);
    meta_.insert(moved.begin(), moved.end());
    if (!save()) {
        meta_ = std::move(backup);
        return StatusCode::INTERNAL_SERVER_ERROR;
    }
    return StatusCode::OK;
}

std::optional<PathMeta> PermissionEngine::get(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = meta_.find(path);
    if (it == meta_.end())
        return std::nullopt;
    return it->second;
}

bool PermissionEngine::canAccess(uint32_t userId, const std::string& path, AccessType type) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = meta_.find(path);
    if (it == meta_.end())
        return false;
    return allowed(userId, it->second, type);
}

template <typename Edit>
StatusCode PermissionEngine::editAsSharer(uint32_t actorId, const std::string& path, Edit edit) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = meta_.find(path);
    if (it == meta_.end())
        return StatusCode::NOT_FOUND;
    if (!allowed(actorId, it->second, AccessType::SHARE))
        return StatusCode::FORBIDDEN;
    PathMeta backup = it->second;
    edit(it->second);
    if (!save()) {
        it->second = backup;
        return StatusCode::INTERNAL_SERVER_ERROR;
    }
    return StatusCode::OK;
}

StatusCode PermissionEngine::chmod(uint32_t actorId, const std::string& path, uint16_t bits) {
    if (bits > 0777)
        return StatusCode::BAD_REQUEST;
    return editAsSharer(actorId, path, [&](PathMeta& m) { m.perm_bits = bits; });
}

StatusCode PermissionEngine::chown(uint32_t actorId, const std::string& path, uint32_t newOwner,
                                   uint32_t newGroup) {
    if (!users_.findById(newOwner) || newGroup == 0)
        return StatusCode::BAD_REQUEST;
    std::lock_guard<std::mutex> lock(mu_);
    auto it = meta_.find(path);
    if (it == meta_.end())
        return StatusCode::NOT_FOUND;
    if (actorId != ADMIN_USER_ID)
        return StatusCode::FORBIDDEN;
    PathMeta backup = it->second;
    it->second.owner_id = newOwner;
    it->second.group_id = newGroup;
    if (!save()) {
        it->second = backup;
        return StatusCode::INTERNAL_SERVER_ERROR;
    }
    return StatusCode::OK;
}

StatusCode PermissionEngine::setSharingLevel(uint32_t actorId, const std::string& path,
                                             SharingLevel level) {
    return editAsSharer(actorId, path, [&](PathMeta& m) { m.sharing_level = level; });
}

StatusCode PermissionEngine::grantAcl(uint32_t actorId, const std::string& path,
                                      uint32_t targetUser, uint8_t mask) {
    if (mask == 0 || mask > 7 || !users_.findById(targetUser))
        return StatusCode::BAD_REQUEST;
    return editAsSharer(actorId, path, [&](PathMeta& m) { m.acl[targetUser] = mask; });
}

StatusCode PermissionEngine::revokeAcl(uint32_t actorId, const std::string& path,
                                       uint32_t targetUser) {
    return editAsSharer(actorId, path, [&](PathMeta& m) { m.acl.erase(targetUser); });
}

}  // namespace FSP::Auth
