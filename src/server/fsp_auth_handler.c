#include "server/fsp_auth_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/FSPProtocol.h"
#include "common/fsp_json.h"
#include "common/fsp_packet.h"
#include "server/fsp_perm.h"
#include "server/fsp_session.h"
#include "server/fsp_user.h"

static int handle_auth_register(client_ctx_t *ctx, const fsp_frame_t *req) {
    if (!req->payload) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Payload JSON khong hop le");
    }

    char username[64] = {0}, password[128] = {0}, full_name[128] = {0};
    if (fsp_json_get_str((const char *)req->payload, "username", username, sizeof(username)) !=
            0 ||
        fsp_json_get_str((const char *)req->payload, "password", password, sizeof(password)) !=
            0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Thieu username hoac password");
    }

    fsp_json_get_str((const char *)req->payload, "full_name", full_name, sizeof(full_name));

    uint32_t user_id = 0;
    int rc = fsp_user_register(username, password, full_name[0] ? full_name : NULL, &user_id);
    if (rc != (int)FSP_STATUS_CREATED) {
        const char *msg = (rc == (int)FSP_STATUS_CONFLICT) ? "Ten nguoi dung da ton tai"
                                                           : "Dang ky that bai";
        return fsp_send_error(ctx->fd, req->event, rc, msg);
    }

    /* Tạo thư mục home cá nhân mặc định /home/<username> */
    char home_path[128];
    snprintf(home_path, sizeof(home_path), "/home/%s", username);
    fsp_perm_register(home_path, user_id, FSP_DEFAULT_DIR_PERM, FSP_LEVEL_PRIVATE);

    char fields[128];
    fsp_json_fields_t jf;
    fsp_jf_init(&jf, fields, sizeof(fields));
    fsp_jf_int(&jf, "user_id", user_id);

    return fsp_send_response(ctx->fd, req->event, (int)FSP_STATUS_CREATED, fields, NULL);
}

static int handle_auth_login(client_ctx_t *ctx, const fsp_frame_t *req) {
    if (!req->payload) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Payload JSON khong hop le");
    }

    char username[64] = {0}, password[128] = {0};
    if (fsp_json_get_str((const char *)req->payload, "username", username, sizeof(username)) !=
            0 ||
        fsp_json_get_str((const char *)req->payload, "password", password, sizeof(password)) !=
            0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Thieu username hoac password");
    }

    if (fsp_user_is_locked_out(username)) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_UNAUTHORIZED,
                              "Tai khoan bi tam khoa do dang nhap sai qua 5 lan. Thu lai sau 5 phut.");
    }

    fsp_user_t user;
    int rc = fsp_user_verify_login(username, password, &user);
    if (rc != (int)FSP_STATUS_OK) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_UNAUTHORIZED,
                              "Sai ten dang nhap hoac mat khau");
    }

    char token[65];
    if (fsp_session_create(user.id, user.username, token, sizeof(token)) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_INTERNAL_SERVER_ERROR,
                              "Khong the khoi tao phien lam viec");
    }

    snprintf(ctx->token, sizeof(ctx->token), "%s", token);
    ctx->user_id = user.id;

    char fields[128];
    fsp_json_fields_t jf;
    fsp_jf_init(&jf, fields, sizeof(fields));
    fsp_jf_str(&jf, "token", token);

    return fsp_send_response(ctx->fd, req->event, (int)FSP_STATUS_OK, fields, NULL);
}

static int handle_auth_logout(client_ctx_t *ctx, const fsp_frame_t *req) {
    char token[65] = {0};
    if (req->payload) {
        fsp_json_get_str((const char *)req->payload, "token", token, sizeof(token));
    }
    if (token[0] == '\0' && ctx->token[0] != '\0') {
        snprintf(token, sizeof(token), "%s", ctx->token);
    }

    if (token[0] == '\0') {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Thieu session token");
    }

    fsp_session_t sess;
    if (fsp_session_validate(token, &sess) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_UNAUTHORIZED,
                              "Phien khong ton tai hoac da het han");
    }

    fsp_session_destroy(token);
    ctx->token[0] = '\0';
    ctx->user_id = 0;

    return fsp_send_response(ctx->fd, req->event, (int)FSP_STATUS_OK, NULL, NULL);
}

static int handle_perm_get(client_ctx_t *ctx, const fsp_frame_t *req) {
    if (!req->payload) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Payload JSON khong hop le");
    }

    char path[512] = {0};
    if (fsp_json_get_str((const char *)req->payload, "path", path, sizeof(path)) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST, "Thieu path");
    }

    fsp_path_meta_t meta;
    if (fsp_perm_get_meta(path, &meta) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_NOT_FOUND,
                              "Khong tim thay duong dan");
    }

    char fields[256];
    fsp_json_fields_t jf;
    fsp_jf_init(&jf, fields, sizeof(fields));
    fsp_jf_int(&jf, "owner_id", meta.owner_id);
    fsp_jf_int(&jf, "group_id", meta.group_id);
    fsp_jf_int(&jf, "perm_bits", meta.perm_bits);
    fsp_jf_int(&jf, "sharing_level", meta.sharing_level);

    return fsp_send_response(ctx->fd, req->event, (int)FSP_STATUS_OK, fields, NULL);
}

static int handle_perm_set(client_ctx_t *ctx, const fsp_frame_t *req) {
    if (!req->payload) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST,
                              "Payload JSON khong hop le");
    }

    char token[65] = {0}, path[512] = {0}, level_str[32] = {0};
    long long perm_bits = 0;
    bool recursive = false;

    fsp_json_get_str((const char *)req->payload, "token", token, sizeof(token));
    if (token[0] == '\0' && ctx->token[0] != '\0') {
        snprintf(token, sizeof(token), "%s", ctx->token);
    }

    fsp_session_t sess;
    if (fsp_session_validate(token, &sess) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_UNAUTHORIZED,
                              "Token khong hop le");
    }

    if (fsp_json_get_str((const char *)req->payload, "path", path, sizeof(path)) != 0) {
        return fsp_send_error(ctx->fd, req->event, (int)FSP_STATUS_BAD_REQUEST, "Thieu path");
    }

    fsp_json_get_int((const char *)req->payload, "perm_bits", &perm_bits);
    fsp_json_get_str((const char *)req->payload, "level", level_str, sizeof(level_str));
    fsp_json_get_bool((const char *)req->payload, "recursive", &recursive);

    fsp_sharing_level_t level = FSP_LEVEL_PRIVATE;
    if (strcmp(level_str, "SHARED") == 0)
        level = FSP_LEVEL_SHARED;
    else if (strcmp(level_str, "PUBLIC") == 0)
        level = FSP_LEVEL_PUBLIC;

    uint16_t bits = (perm_bits > 0) ? (uint16_t)perm_bits : FSP_DEFAULT_FILE_PERM;

    int rc = fsp_perm_set_recursive(sess.user_id, path, bits, level, recursive);
    if (rc != (int)FSP_STATUS_OK) {
        const char *msg = (rc == (int)FSP_STATUS_FORBIDDEN) ? "Tu choi truy cap (Khong co quyen SHARE)"
                                                            : "Khong tim thay duong dan";
        return fsp_send_error(ctx->fd, req->event, rc, msg);
    }

    return fsp_send_response(ctx->fd, req->event, (int)FSP_STATUS_OK, NULL, NULL);
}

void fsp_auth_register_handlers(void) {
    dispatcher_register(FSP_EV_AUTH_REGISTER, handle_auth_register);
    dispatcher_register(FSP_EV_AUTH_LOGIN, handle_auth_login);
    dispatcher_register(FSP_EV_AUTH_LOGOUT, handle_auth_logout);
    dispatcher_register(FSP_EV_PERM_GET, handle_perm_get);
    dispatcher_register(FSP_EV_PERM_SET, handle_perm_set);
}
