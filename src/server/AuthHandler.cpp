#include "server/AuthHandler.h"

namespace FSP::Auth {

namespace {
AuthResult error(StatusCode code, const std::string& msg) {
    return {code, "{\"error\":\"" + jsonEscape(msg) + "\"}"};
}

const std::string* field(const JsonMap& m, const std::string& key) {
    auto it = m.find(key);
    return it == m.end() ? nullptr : &it->second;
}
}  // namespace

AuthHandler::AuthHandler(UserManager& users, SessionManager& sessions, PermissionEngine* perms)
    : users_(users), sessions_(sessions), perms_(perms) {}

AuthResult AuthHandler::handle(const std::string& event, const std::string& jsonPayload) {
    try {
        return handleImpl(event, jsonPayload);
    } catch (const std::exception&) {
        // e.g. the OS random source failed: report it, never leave the request half-handled
        return error(StatusCode::INTERNAL_SERVER_ERROR, "internal error");
    }
}

AuthResult AuthHandler::handleImpl(const std::string& event, const std::string& jsonPayload) {
    auto m = jsonParseFlat(jsonPayload);
    if (!m)
        return error(StatusCode::BAD_REQUEST, "malformed JSON");

    if (event == Events::AUTH_LOGOUT) {
        const std::string* token = field(*m, "token");
        if (!token)
            return error(StatusCode::BAD_REQUEST, "token required");
        if (!sessions_.validate(*token))
            return error(StatusCode::UNAUTHORIZED, "invalid session");
        sessions_.destroy(*token);
        return {StatusCode::OK, "{}"};
    }

    if (event != Events::AUTH_REGISTER && event != Events::AUTH_LOGIN)
        return error(StatusCode::BAD_REQUEST, "unknown auth event");

    const std::string* username = field(*m, "username");
    const std::string* password = field(*m, "password");
    if (!username || !password)
        return error(StatusCode::BAD_REQUEST, "username/password required");

    if (event == Events::AUTH_REGISTER) {
        uint32_t id = 0;
        const std::string* fullName = field(*m, "full_name");
        StatusCode code = users_.registerUser(*username, *password, fullName ? *fullName : "", &id);
        if (code != StatusCode::CREATED)
            return error(code, "register failed");
        if (perms_ &&
            perms_->registerPath("/home/" + *username, id, Permission::DEFAULT_DIR_PERM,
                                 Permission::SharingLevel::PRIVATE) != StatusCode::CREATED)
            return error(StatusCode::INTERNAL_SERVER_ERROR, "home directory setup failed");
        return {StatusCode::CREATED, "{\"user_id\":" + std::to_string(id) + "}"};
    }

    if (users_.isLockedOut(*username))
        return error(StatusCode::UNAUTHORIZED, "too many failed attempts, try again later");
    auto user = users_.verifyLogin(*username, *password);
    if (!user)
        return error(StatusCode::UNAUTHORIZED, "invalid credentials");
    return {StatusCode::OK, "{\"token\":\"" + sessions_.create(user->id) + "\"}"};
}

}  // namespace FSP::Auth
