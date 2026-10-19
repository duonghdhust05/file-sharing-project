#ifndef FSP_PERMISSION_ENGINE_H
#define FSP_PERMISSION_ENGINE_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "common/FSPProtocol.h"
#include "server/UserManager.h"

namespace FSP::Auth {

using Permission::AccessType;
using Permission::SharingLevel;

struct PathMeta {
    uint32_t owner_id = 0;
    uint32_t group_id = 0;
    uint16_t perm_bits = Permission::DEFAULT_FILE_PERM;
    SharingLevel sharing_level = SharingLevel::PRIVATE;
    std::map<uint32_t, uint8_t> acl;  // user_id -> OR of AccessType values
};

// Thread-safe permission table keyed by sandboxed virtual path; saved as JSON lines.
class PermissionEngine {
public:
    // Loads dbPath if it exists. Throws std::runtime_error if the file is corrupt.
    PermissionEngine(std::string dbPath, const UserManager& users);

    // Tracks path for ownerId. CREATED, BAD_REQUEST, or CONFLICT if already tracked.
    StatusCode registerPath(const std::string& path, uint32_t ownerId,
                            uint16_t perm = Permission::DEFAULT_FILE_PERM,
                            SharingLevel level = SharingLevel::PRIVATE);

    // Stops tracking `path` (call when the file is deleted). Returns whether it was tracked.
    bool removePath(const std::string& path);

    /*
     * Moves metadata of `oldPath` and its children to `newPath`. Needs WRITE on oldPath; M3 checks
     * the destination folder. OK, NOT_FOUND, FORBIDDEN, BAD_REQUEST (same path or into itself),
     * CONFLICT (destination tracked), INTERNAL_SERVER_ERROR.
     */
    StatusCode renamePath(uint32_t actorId, const std::string& oldPath, const std::string& newPath);

    std::optional<PathMeta> get(const std::string& path) const;

    // The access decision. Untracked paths and unknown users are denied (fail-closed).
    bool canAccess(uint32_t userId, const std::string& path, AccessType type) const;

    // Mutators below return OK, NOT_FOUND, FORBIDDEN, BAD_REQUEST or INTERNAL_SERVER_ERROR.

    // Needs SHARE access on the path. bits must fit in 0777.
    StatusCode chmod(uint32_t actorId, const std::string& path, uint16_t bits);

    // Admin only (like Linux chown). newOwner must exist, newGroup must be non-zero.
    StatusCode chown(uint32_t actorId, const std::string& path, uint32_t newOwner,
                     uint32_t newGroup);

    // Needs SHARE access on the path.
    StatusCode setSharingLevel(uint32_t actorId, const std::string& path, SharingLevel level);

    // Needs SHARE access. mask is an OR of AccessType values (1..7); target must exist.
    StatusCode grantAcl(uint32_t actorId, const std::string& path, uint32_t targetUser,
                        uint8_t mask);

    // Needs SHARE access. Removing a non-existent entry is OK.
    StatusCode revokeAcl(uint32_t actorId, const std::string& path, uint32_t targetUser);

private:
    void load();
    bool save() const;  // caller holds mu_
    bool allowed(uint32_t userId, const PathMeta& meta, AccessType type) const;
    // Looks up `path`, checks the actor has SHARE access, then runs `edit` and persists.
    template <typename Edit>
    StatusCode editAsSharer(uint32_t actorId, const std::string& path, Edit edit);

    std::string dbPath_;
    const UserManager& users_;
    mutable std::mutex mu_;
    std::map<std::string, PathMeta> meta_;
};

}  // namespace FSP::Auth

#endif  // FSP_PERMISSION_ENGINE_H
