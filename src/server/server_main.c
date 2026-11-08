/*
 * FSP Server daemon (M1.4 - SDD §4.3).
 *   Cách dùng: fsp_server [port]      (mặc định 8088)
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/Config.h"
#include "common/FSPProtocol.h"
#include "server/fsp_auth_handler.h"
#include "server/fsp_dispatcher.h"
#include "server/fsp_perm.h"
#include "server/fsp_server.h"
#include "server/fsp_session.h"
#include "server/fsp_user.h"

static fsp_server_t *g_server = NULL;

static void on_stop_signal(int sig) {
    (void)sig;
    /* Chỉ ghi một atomic_int lock-free -> an toàn trong signal handler */
    fsp_server_request_stop(g_server);
}

static int parse_port(const char *text, uint16_t *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v < 1 || v > 65535)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

/*
 * Nơi các module nghiệp vụ đăng ký handler khi được tích hợp (Mốc 1, Ngày 4-5):
 *   M2: dispatcher_register(FSP_EV_AUTH_LOGIN, auth_handle_login); ...
 *       dispatcher_register_cleanup(auth_on_disconnect);       // session_destroy
 *   M3: dispatcher_register(FSP_EV_DIR_LIST, vfs_handle_list); ...
 *       dispatcher_register_chunk(transfer_handle_chunk);
 *       dispatcher_register_cleanup(transfer_on_disconnect);   // xóa file .part dở
 * Sự kiện chưa có handler sẽ tự nhận ERROR_RESP 400 "unknown event".
 */
static void register_handlers(void) {
    fsp_user_db_init("storage/users.json");
    fsp_session_init();
    fsp_perm_db_init("storage/permissions.json");
    fsp_auth_register_handlers();
}

int main(int argc, char **argv) {
    uint16_t port = (uint16_t)FSP_SERVER_PORT;
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: %s [port]   (default %u)\n", argv[0], (unsigned)FSP_SERVER_PORT);
            return 0;
        }
        if (parse_port(argv[1], &port) != 0) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    /* Ghi vào socket đã bị client đóng phải trả lỗi EPIPE, không được giết cả server */
    signal(SIGPIPE, SIG_IGN);

    register_handlers();

    g_server = fsp_server_create(port, FSP_BACKLOG);
    if (!g_server) {
        fprintf(stderr, "Cannot listen on port %u: %s\n", (unsigned)port, strerror(errno));
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("FSP Server v2.0 (C11) - header %u bytes, magic 0x%02X. Ctrl+C to stop.\n",
           (unsigned)FSP_HEADER_SIZE, (unsigned)FSP_MAGIC_BYTE);
    fflush(stdout);

    fsp_server_run(g_server);
    fsp_server_destroy(g_server);
    g_server = NULL;
    fsp_session_destroy_all();
    fsp_user_db_destroy();
    fsp_perm_db_destroy();
    printf("Server stopped.\n");
    return 0;
}
