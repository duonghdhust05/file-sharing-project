# M2 Function Reference: Auth, Session & Permission

Module owner: Bùi Đức Hiếu (`feat/m2-auth-permissions`). All code lives in namespace `FSP::Auth`.
Headers are in `include/server/`, sources in `src/server/`, tests in `tests/test_auth_permissions.cpp`.

## How the pieces fit

```
AUTH_* request (JSON) ─► AuthHandler ─► UserManager (users.json)
                                    └─► SessionManager (in memory, token -> user_id)

Every other request:  token ─► SessionManager::validate ─► user_id
                      user_id + virtual path ─► PermissionEngine::canAccess (permissions.json)
```

Status codes come from `StatusCode` in `FSPProtocol.h`. Persisted files are **JSON lines**: one flat JSON
object per line, written atomically (`.tmp` file then rename). A corrupt file makes the constructor throw
instead of silently starting empty (fail-closed).

---

## `Sha256.h` / `Sha256.cpp`

| Function | What it does |
|---|---|
| `std::string sha256Hex(const std::string& msg)` | Computes SHA-256 (FIPS 180-4) of the bytes in `msg` and returns 64 lowercase hex characters. Pads the message (0x80, zeros, 64-bit bit length), then runs the 64-round compression over each 64-byte block. No OpenSSL, so it behaves the same on Windows and Linux. |
| `rotr` (internal) | 32-bit rotate-right used by the compression rounds. |

## `AuthUtil.h` (shared helpers, header-only)

| Function | What it does |
|---|---|
| `systemClock()` | Returns the current time (`std::time(nullptr)`). The default `Clock`. Managers accept any `std::function<time_t()>`, so tests can move time without sleeping. |
| `randomBytes(buf, n)` | Fills `buf` with OS random bytes: `getrandom(2)` on Linux, `std::random_device` elsewhere. Throws on failure and never falls back to a weak source. |
| `randomHex(nBytes)` | Returns `nBytes` OS-random bytes as `2*nBytes` hex chars. Used for salts and session tokens. |
| `jsonEscape(s)` | Escapes `"`, `\` and control characters so `s` can be placed inside a JSON string. |
| `parseU64(s, out)` | Parses an unsigned decimal. True only if the whole string is a valid number. |
| `jsonParseFlat(s)` | Parses one flat JSON object (string, number, bool or null values, no nesting or arrays). Decodes escapes including `\uXXXX`. Returns `nullopt` on any malformed input or trailing garbage. |
| `jsonNeed(map, key)` | Returns a field's text, or throws `runtime_error` if missing. Used when loading DB files. |
| `jsonNeedU64(map, key)` | Same, but also requires the value to be a valid number. |
| `writeFileAtomic(path, content)` | Creates parent directories, writes `<path>.tmp`, then renames it over `path`. Returns false on any I/O error, so a crash never leaves a half-written DB. |

## `UserManager` (users.json)

A `User` has `id`, `username`, `salt` (32 hex chars), `password_hash` (64 hex chars), `group_id`, `created_at`.
All methods are guarded by a mutex.

| Function | What it does |
|---|---|
| `UserManager(dbPath, clock)` | Loads the user file if it exists. Throws `runtime_error` if a line is corrupt. |
| `registerUser(username, password, fullName, outId)` | Validates input (username `[A-Za-z0-9_]{3,32}`, password 6 to 128 chars, optional `fullName` up to 100 bytes), rejects duplicate names, generates a 16-byte random salt, stores `sha256Hex(salt + password)`, and gives the user a private group equal to their id. The first user gets id 1 and is the admin. Returns `CREATED`, `BAD_REQUEST`, `CONFLICT` or `INTERNAL_SERVER_ERROR` (the user is rolled back if saving fails). |
| `verifyLogin(username, password)` | Returns the `User` only if the name exists and the salted hash matches. The hash comparison is constant-time, and an unknown name still burns a hash so timing does not reveal whether it exists. After `MAX_FAILED_LOGINS` (5) failures in a row the name is locked for `LOCKOUT_SECONDS` (5 min), even for the right password; a success clears the count. Unknown names are throttled the same way. |
| `isLockedOut(username)` | True while the name is locked. `AuthHandler` uses it to give a clear message. |
| `lockedOut`, `recordFailure` (private) | Failure bookkeeping. The table holds at most `MAX_TRACKED_NAMES` (10000): when full it drops unlocked entries, and if still full it skips tracking new names so a flood can't release an active lock. |
| `findById(id)` | Returns a copy of the user, or `nullopt`. |
| `setGroup(actorId, userId, groupId)` | Admin only: moves a user into a group. `FORBIDDEN` if the actor is not the admin, `BAD_REQUEST` if `groupId == 0`, `NOT_FOUND` if no such user, `OK` otherwise. Rolled back if saving fails. |
| `load()` / `save()` (private) | Read and write the JSON-lines file. |
| `validUsername`, `constantTimeEq` (internal) | Username format check, and a timing-safe string compare. |

## `SessionManager` (in memory)

A `Session` has `token` (UUID v4), `user_id`, `cwd` (default `/`) and `last_active`.
A session expires after `TIMEOUT_SECONDS` (30 minutes) without use. All methods are guarded by a mutex.

| Function | What it does |
|---|---|
| `SessionManager(clock)` | Creates an empty table. |
| `create(userId)` | Makes a new session with a random UUID v4 token and returns the token. |
| `validate(token)` | If the session exists and is not expired, refreshes `last_active` and returns a copy. Expired sessions are deleted. Unknown or forged tokens give `nullopt`. |
| `destroy(token)` | Deletes the session (logout). Unknown tokens are ignored. |
| `setCwd(token, cwd)` | Stores the session's current virtual directory (refreshes the timeout). False if the session is unknown or expired. M3 validates the path itself. |
| `findLive(token)` (private) | Shared lookup: finds the session, removes it if expired, otherwise refreshes it. |
| `newUuid()` (internal) | Formats 16 random bytes as an RFC 4122 v4 UUID. |

## `PermissionEngine` (permissions.json)

Keeps a `PathMeta` per virtual path: `owner_id`, `group_id`, `perm_bits` (Linux style `rwx` x 3 where `x`
means SHARE), `sharing_level` (`PRIVATE`/`SHARED`/`PUBLIC`) and `acl` (user id -> OR of `AccessType`).
Paths must already be sandboxed by M3. All methods are guarded by a mutex.

| Function | What it does |
|---|---|
| `PermissionEngine(dbPath, users)` | Loads the permission file if it exists; throws on corruption. Needs the `UserManager` to look up groups. |
| `registerPath(path, ownerId, perm, level)` | Starts tracking a new file or folder (M3 calls this on create or upload). Group is copied from the owner. `CREATED`, `BAD_REQUEST` (unknown owner or bits above 0777), `CONFLICT` (already tracked). |
| `removePath(path)` | Stops tracking a deleted path. False if it was not tracked or the save failed. |
| `renamePath(actorId, oldPath, newPath)` | Moves the metadata of `oldPath` and everything under `oldPath/` to `newPath` (call after `FS_RENAME` succeeds). Needs WRITE on `oldPath`. `BAD_REQUEST` if the paths are equal or `newPath` is inside `oldPath`, `CONFLICT` if any destination is already tracked, `NOT_FOUND`, `FORBIDDEN`, or `INTERNAL_SERVER_ERROR` (state restored). Siblings sharing only a text prefix (`/a-b` vs `/a`) are not moved. M3 checks the destination folder. |
| `get(path)` | Returns a copy of the path's metadata, or `nullopt`. |
| `canAccess(userId, path, type)` | The access decision, below. |
| `chmod(actorId, path, bits)` | Replaces `perm_bits`. The actor needs SHARE access. `BAD_REQUEST` if bits exceed 0777. |
| `chown(actorId, path, newOwner, newGroup)` | Changes owner and group. Admin only (as in Linux). The new owner must exist and the group must be non-zero. |
| `setSharingLevel(actorId, path, level)` | Sets `PRIVATE`/`SHARED`/`PUBLIC`. Needs SHARE access. |
| `grantAcl(actorId, path, targetUser, mask)` | Gives a user `mask` (OR of READ=1, WRITE=2, SHARE=4) on the path. Only takes effect at `SHARED` level. Needs SHARE access. |
| `revokeAcl(actorId, path, targetUser)` | Removes a user's ACL entry. Needs SHARE access. |
| `allowed(userId, meta, type)` (private) | The pure decision function used by `canAccess` and the mutators. |
| `editAsSharer(...)` (private) | Common path for the mutators: look up the path (`NOT_FOUND`), check SHARE access (`FORBIDDEN`), apply the edit, save, and undo the edit if saving fails. |
| `classBits`, `encodeAcl`, `decodeAcl` (internal) | Map an access type to its owner/group bit, and convert the ACL to and from the `"uid:mask,uid:mask"` file format. |

### `canAccess` rules, in order

1. Path not tracked: **deny** (fail-closed).
2. User is the admin (id 1): **allow**.
3. User is the owner: `READ` and `SHARE` (chmod, sharing, ACL) are **always** allowed (SRS section 5);
   `WRITE` is allowed only if the `OWNER_WRITE` bit is set. Because there is a single `WRITE` access type,
   owner delete follows the write bit too. Split it into its own type if the SRS "always delete" must hold.
4. Level is `PRIVATE`: **deny** everyone else, even if group or other bits are set.
5. User does not exist: deny.
6. Same group and matching `GROUP_*` bit set: allow.
7. Level `PUBLIC` and the request is READ: allow.
8. Level `SHARED` and the user's ACL entry contains the access type: allow.
9. Otherwise deny.

The `OTHERS_*` bits are intentionally not used: outsiders are governed only by sharing level and ACL.

## `AuthHandler`

| Function | What it does |
|---|---|
| `AuthHandler(users, sessions, perms)` | Holds references to the managers. `perms` is optional; when given, a successful register also calls `registerPath("/home/<username>", id, 0755, PRIVATE)` so each user owns a private home directory. Creating the folder on disk is M3's job. If tracking fails the response is 500. |
| `handle(event, jsonPayload)` | Parses the payload with `jsonParseFlat` (`BAD_REQUEST` if malformed) and routes by event name. Returns `AuthResult{status, body}` where `body` is the JSON for the `EVENT_RESP` frame. |

Event behavior:

| Event | Request | Success | Failures |
|---|---|---|---|
| `AUTH_REGISTER` | `{"username","password","full_name"?}` | 201 `{"user_id":N}` | 400 invalid or missing fields, 409 name taken, 500 home setup failed |
| `AUTH_LOGIN` | `{"username","password"}` | 200 `{"token":"..."}` | 400 missing fields, 401 wrong credentials or locked out |
| `AUTH_LOGOUT` | `{"token"}` | 200 `{}` | 400 missing token, 401 unknown, expired or already logged-out token |
| anything else | n/a | n/a | 400 |

Any internal exception (for example the OS random source failing) becomes 500. Error bodies are `{"error":"..."}`. Messages are generic on purpose (login never says whether the user exists).

## Tests (`tests/test_auth_permissions.cpp`)

Real-data checks only: NIST SHA-256 vectors; salts and hashes differ per user; plain passwords never appear
in `users.json`; reload-from-disk gives identical results; corrupt DB throws; forged, logged-out and
expired tokens are rejected (clock moved 31 minutes); a full owner/group/other/ACL permission matrix
including a `PRIVATE` file with 0666 bits; `chmod`, `chown` and ACL changes alter results; and the handler
status codes for every event; lockout and unlock after 5 minutes; admin-only `setGroup`; `renamePath`
(children move, text-prefix siblings stay, conflicts leave state untouched); unique random values; and 8
threads racing to register, log in and check access (exactly one wins a shared name, ids stay unique).
190 checks pass under `-fsanitize=address,undefined` (Probe 6) and under `-fsanitize=thread`.

Not covered here (other members): wiring into the ThreadPool and dispatcher (M1), sandbox path
normalization and `DIR_LIST`/`SEARCH_QUERY` filtering (M3), Permission Dialog (M4).
