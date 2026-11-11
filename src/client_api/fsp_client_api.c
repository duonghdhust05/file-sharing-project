#include "client/fsp_client_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/fsp_json.h"
#include "common/fsp_packet.h"
#include "common/fsp_socket.h"

void fsp_client_init(fsp_client_t *cli) {
    if (!cli)
        return;
    memset(cli, 0, sizeof(*cli));
    cli->fd = -1;
    cli->is_connected = false;
    cli->is_logged_in = false;
    snprintf(cli->current_dir, sizeof(cli->current_dir), "/");
}

int fsp_client_connect(fsp_client_t *cli, const char *host, uint16_t port) {
    if (!cli || !host)
        return -1;
    fsp_client_disconnect(cli);

    int fd = fsp_connect(host, port);
    if (fd < 0) {
        snprintf(cli->last_error, sizeof(cli->last_error), "Không thể kết nối tới %s:%u", host,
                 port);
        return -1;
    }

    cli->fd = fd;
    cli->is_connected = true;
    return 0;
}

void fsp_client_disconnect(fsp_client_t *cli) {
    if (!cli)
        return;
    if (cli->fd >= 0) {
        fsp_close(cli->fd);
        cli->fd = -1;
    }
    cli->is_connected = false;
    cli->is_logged_in = false;
    cli->token[0] = '\0';
}

int fsp_client_ping(fsp_client_t *cli) {
    if (!cli || !cli->is_connected)
        return -1;

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_PING, "PING", NULL, 0) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int ok = (resp.type == (uint8_t)FSP_MSG_PONG) ? 0 : -1;
    fsp_frame_free(&resp);
    return ok;
}

static int parse_status_and_error(fsp_client_t *cli, const fsp_frame_t *resp) {
    long long status = 500;
    if (resp->payload) {
        fsp_json_get_int((const char *)resp->payload, "status", &status);
        if (status != 200 && status != 201) {
            char err[256] = {0};
            if (fsp_json_get_str((const char *)resp->payload, "error", err, sizeof(err)) == 0) {
                snprintf(cli->last_error, sizeof(cli->last_error), "%s", err);
            } else {
                snprintf(cli->last_error, sizeof(cli->last_error), "Mã lỗi %lld", status);
            }
        }
    }
    return (int)status;
}

int fsp_client_login(fsp_client_t *cli, const char *username, const char *password) {
    if (!cli || !cli->is_connected || !username || !password)
        return -1;

    char payload[256];
    char u_esc[64], p_esc[64];
    fsp_json_escape(username, u_esc, sizeof(u_esc));
    fsp_json_escape(password, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload), "{\"username\":\"%s\",\"password\":\"%s\"}", u_esc, p_esc);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_AUTH_LOGIN, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    if (status == 200 && resp.payload) {
        char tok[64] = {0};
        if (fsp_json_get_str((const char *)resp.payload, "token", tok, sizeof(tok)) == 0) {
            snprintf(cli->token, sizeof(cli->token), "%s", tok);
            snprintf(cli->username, sizeof(cli->username), "%s", username);
            cli->is_logged_in = true;
        }
    }

    fsp_frame_free(&resp);
    return status;
}

int fsp_client_register(fsp_client_t *cli, const char *username, const char *password) {
    if (!cli || !cli->is_connected || !username || !password)
        return -1;

    char payload[256];
    char u_esc[64], p_esc[64];
    fsp_json_escape(username, u_esc, sizeof(u_esc));
    fsp_json_escape(password, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload), "{\"username\":\"%s\",\"password\":\"%s\"}", u_esc, p_esc);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_AUTH_REGISTER, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    fsp_frame_free(&resp);
    return status;
}

int fsp_client_logout(fsp_client_t *cli) {
    if (!cli || !cli->is_connected)
        return -1;

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"token\":\"%s\"}", cli->token);

    fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_AUTH_LOGOUT, payload,
                   (uint32_t)strlen(payload));

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) == 0) {
        fsp_frame_free(&resp);
    }

    cli->is_logged_in = false;
    cli->token[0] = '\0';
    return 0;
}

int fsp_client_pwd(fsp_client_t *cli, char *out_path, size_t out_size) {
    if (!cli || !cli->is_connected || !out_path)
        return -1;

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"token\":\"%s\"}", cli->token);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_DIR_PWD, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    if (status == 200 && resp.payload) {
        char p[512] = {0};
        if (fsp_json_get_str((const char *)resp.payload, "path", p, sizeof(p)) == 0) {
            snprintf(out_path, out_size, "%s", p);
            snprintf(cli->current_dir, sizeof(cli->current_dir), "%s", p);
        }
    }
    fsp_frame_free(&resp);
    return status;
}

int fsp_client_cd(fsp_client_t *cli, const char *path) {
    if (!cli || !cli->is_connected || !path)
        return -1;

    char payload[512];
    char p_esc[256];
    fsp_json_escape(path, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload), "{\"token\":\"%s\",\"path\":\"%s\"}", cli->token, p_esc);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_DIR_CHANGE, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    if (status == 200) {
        snprintf(cli->current_dir, sizeof(cli->current_dir), "%s", path);
    }
    fsp_frame_free(&resp);
    return status;
}

int fsp_client_mkdir(fsp_client_t *cli, const char *path) {
    if (!cli || !cli->is_connected || !path)
        return -1;

    char payload[512];
    char p_esc[256];
    fsp_json_escape(path, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload), "{\"token\":\"%s\",\"path\":\"%s\"}", cli->token, p_esc);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_DIR_MKDIR, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    fsp_frame_free(&resp);
    return status;
}

int fsp_client_delete(fsp_client_t *cli, const char *path) {
    if (!cli || !cli->is_connected || !path)
        return -1;

    char payload[512];
    char p_esc[256];
    fsp_json_escape(path, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload), "{\"token\":\"%s\",\"path\":\"%s\"}", cli->token, p_esc);

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_FS_DELETE, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    fsp_frame_free(&resp);
    return status;
}

int fsp_client_upload_file(fsp_client_t *cli, const char *local_path, const char *remote_path,
                           fsp_progress_cb cb, void *userdata) {
    (void)cb;
    (void)userdata;
    if (!cli || !cli->is_connected || !local_path || !remote_path)
        return -1;
    /* M4 và M3 phối hợp hoàn thiện chi tiết streaming 64 KB */
    return 0;
}

int fsp_client_download_file(fsp_client_t *cli, const char *remote_path, const char *local_path,
                             fsp_progress_cb cb, void *userdata) {
    (void)cb;
    (void)userdata;
    if (!cli || !cli->is_connected || !remote_path || !local_path)
        return -1;
    /* M4 và M3 phối hợp hoàn thiện chi tiết streaming 64 KB */
    return 0;
}

int fsp_client_set_perm(fsp_client_t *cli, const char *path, const char *mode, const char *level,
                        bool recursive) {
    if (!cli || !cli->is_connected || !path)
        return -1;

    char payload[512];
    char p_esc[256];
    fsp_json_escape(path, p_esc, sizeof(p_esc));
    snprintf(payload, sizeof(payload),
             "{\"token\":\"%s\",\"path\":\"%s\",\"mode\":\"%s\",\"level\":\"%s\",\"recursive\":%s}",
             cli->token, p_esc, mode ? mode : "0644", level ? level : "PRIVATE",
             recursive ? "true" : "false");

    if (fsp_send_frame(cli->fd, (uint8_t)FSP_MSG_EVENT_REQ, FSP_EV_PERM_SET, payload,
                       (uint32_t)strlen(payload)) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    fsp_frame_t resp;
    if (fsp_recv_frame(cli->fd, &resp) != 0) {
        fsp_client_disconnect(cli);
        return -1;
    }

    int status = parse_status_and_error(cli, &resp);
    fsp_frame_free(&resp);
    return status;
}
