#ifndef FSP_AUTH_HANDLER_H
#define FSP_AUTH_HANDLER_H

#include <string>

#include "common/FSPProtocol.h"
#include "server/PermissionEngine.h"
#include "server/SessionManager.h"
#include "server/UserManager.h"

namespace FSP::Auth {

struct AuthResult {
    StatusCode status;
    std::string body;  // JSON payload for the EVENT_RESP frame
};

// Turns AUTH_* event payloads (JSON) into responses. M1's dispatcher calls handle().
class AuthHandler {
public:
    // With perms, register also tracks /home/<user> (PRIVATE, 0755). M3 creates the folder.
    AuthHandler(UserManager& users, SessionManager& sessions, PermissionEngine* perms = nullptr);

    /*
     * Routes AUTH_REGISTER / AUTH_LOGIN / AUTH_LOGOUT. Other events or bad JSON give 400.
     *   REGISTER {username, password, full_name?} -> 201 {user_id} | 400 | 409 | 500
     *   LOGIN    {username, password}             -> 200 {token}   | 400 | 401 (also if locked out)
     *   LOGOUT   {token}                          -> 200 {}        | 400 | 401
     * Any internal exception becomes 500.
     */
    AuthResult handle(const std::string& event, const std::string& jsonPayload);

private:
    AuthResult handleImpl(const std::string& event, const std::string& jsonPayload);

    UserManager& users_;
    SessionManager& sessions_;
    PermissionEngine* perms_;
};

}  // namespace FSP::Auth

#endif  // FSP_AUTH_HANDLER_H
