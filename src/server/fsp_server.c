#include "server/fsp_server.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common/fsp_packet.h"
#include "common/fsp_socket.h"
#include "server/fsp_dispatcher.h"

#define ACCEPT_POLL_MS 200

typedef struct client_node {
    client_ctx_t ctx;
    struct fsp_server *server;
    struct client_node *prev;
    struct client_node *next;
} client_node_t;

struct fsp_server {
    int listen_fd;
    uint16_t port;
    atomic_int stop; /* lock-free: ghi được từ signal handler */
    int verbose;

    pthread_mutex_t lock; /* bảo vệ danh sách client + bộ đếm */
    pthread_cond_t all_gone;
    client_node_t *clients;
    int active;
    uint64_t next_conn_id;
    uint64_t accepted;
};

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static void unlink_node_locked(fsp_server_t *s, client_node_t *n) {
    if (n->prev)
        n->prev->next = n->next;
    else
        s->clients = n->next;
    if (n->next)
        n->next->prev = n->prev;
    n->prev = n->next = NULL;
}

static void *client_thread(void *arg) {
    client_node_t *node = (client_node_t *)arg;
    client_ctx_t *ctx = &node->ctx;
    fsp_server_t *s = node->server;

    if (s->verbose)
        printf("[server] client #%llu connected from %s\n", (unsigned long long)ctx->conn_id,
               ctx->peer);

    while (!atomic_load(&s->stop)) {
        fsp_frame_t req;
        if (fsp_recv_frame(ctx->fd, &req) != 0)
            break; /* mất kết nối hoặc frame dị dạng -> Fail-Closed */

        int rc;
        if (req.type == FSP_MSG_PING) {
            rc = fsp_send_frame(ctx->fd, FSP_MSG_PONG, "PONG", NULL, 0);
        } else if (req.type == FSP_MSG_EVENT_REQ || req.type == FSP_MSG_FILE_CHUNK) {
            rc = dispatcher_handle(ctx, &req);
        } else {
            /* Client không được gửi EVENT_RESP / ERROR_RESP / ACK / PONG lên server */
            rc = fsp_send_error(ctx->fd, req.event[0] ? req.event : "UNKNOWN",
                                FSP_STATUS_BAD_REQUEST, "unexpected message type");
        }
        fsp_frame_free(&req);
        if (rc != 0)
            break;
    }

    /* Dọn dẹp theo SDD §4.3: hủy upload dở (M3), hủy phiên (M2)... qua cleanup hook */
    dispatcher_run_cleanups(ctx);

    if (s->verbose)
        printf("[server] client #%llu disconnected\n", (unsigned long long)ctx->conn_id);

    pthread_mutex_lock(&s->lock);
    unlink_node_locked(s, node);
    /* Đóng fd khi còn giữ lock: fsp_server_destroy() chỉ shutdown() fd dưới cùng lock này,
     * nên không bao giờ shutdown nhầm một fd đã bị OS cấp lại cho kết nối khác. */
    fsp_close(ctx->fd);
    ctx->fd = -1;
    s->active--;
    if (s->active == 0)
        pthread_cond_broadcast(&s->all_gone);
    pthread_mutex_unlock(&s->lock);
    /* Từ đây không được chạm vào s nữa: destroy có thể đã giải phóng nó. */

    free(node);
    return NULL;
}

fsp_server_t *fsp_server_create(uint16_t port, int backlog) {
    fsp_server_t *s = (fsp_server_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->listen_fd = fsp_listen(port, backlog);
    if (s->listen_fd < 0) {
        int saved = errno;
        free(s);
        errno = saved;
        return NULL;
    }
    s->port = fsp_local_port(s->listen_fd);
    atomic_init(&s->stop, 0);
    s->verbose = 1;
    s->next_conn_id = 1;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->all_gone, NULL);
    return s;
}

uint16_t fsp_server_port(const fsp_server_t *server) {
    return server ? server->port : 0;
}

void fsp_server_set_verbose(fsp_server_t *server, int verbose) {
    if (server)
        server->verbose = verbose;
}

void fsp_server_request_stop(fsp_server_t *server) {
    if (server)
        atomic_store(&server->stop, 1);
}

int fsp_server_active_clients(fsp_server_t *server) {
    pthread_mutex_lock(&server->lock);
    int n = server->active;
    pthread_mutex_unlock(&server->lock);
    return n;
}

uint64_t fsp_server_total_accepted(fsp_server_t *server) {
    pthread_mutex_lock(&server->lock);
    uint64_t n = server->accepted;
    pthread_mutex_unlock(&server->lock);
    return n;
}

int fsp_server_run(fsp_server_t *s) {
    if (!s)
        return -1;
    if (s->verbose)
        printf("[server] listening on port %u\n", (unsigned)s->port);

    while (!atomic_load(&s->stop)) {
        struct pollfd pfd = {s->listen_fd, POLLIN, 0};
        int ready = poll(&pfd, 1, ACCEPT_POLL_MS);
        if (ready <= 0)
            continue; /* hết giờ (kiểm tra lại cờ dừng) hoặc EINTR */

        int cfd = accept(s->listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EMFILE || errno == ENFILE || errno == ENOBUFS || errno == ENOMEM)
                sleep_ms(100); /* hết tài nguyên: chờ client khác ngắt rồi thử lại */
            continue;
        }

        client_node_t *node = (client_node_t *)calloc(1, sizeof(*node));
        if (!node) {
            fsp_close(cfd);
            continue;
        }
        node->server = s;
        node->ctx.fd = cfd;
        node->ctx.upload = NULL;
        node->ctx.cwd[0] = '/';
        fsp_peer_address(cfd, node->ctx.peer, sizeof(node->ctx.peer));

        pthread_mutex_lock(&s->lock);
        node->ctx.conn_id = s->next_conn_id++;
        node->next = s->clients;
        if (s->clients)
            s->clients->prev = node;
        s->clients = node;
        s->active++;
        s->accepted++;
        pthread_mutex_unlock(&s->lock);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, node) != 0) {
            pthread_mutex_lock(&s->lock);
            unlink_node_locked(s, node);
            s->active--;
            if (s->active == 0)
                pthread_cond_broadcast(&s->all_gone);
            pthread_mutex_unlock(&s->lock);
            fsp_close(cfd);
            free(node);
            continue;
        }
        pthread_detach(tid);
    }
    return 0;
}

void fsp_server_destroy(fsp_server_t *s) {
    if (!s)
        return;
    atomic_store(&s->stop, 1);

    pthread_mutex_lock(&s->lock);
    /* Đánh thức mọi client thread đang chặn trong recv(): recv sẽ trả 0 -> thread thoát */
    for (client_node_t *n = s->clients; n; n = n->next) {
        if (n->ctx.fd >= 0)
            shutdown(n->ctx.fd, SHUT_RDWR);
    }
    while (s->active > 0)
        pthread_cond_wait(&s->all_gone, &s->lock);
    pthread_mutex_unlock(&s->lock);

    fsp_close(s->listen_fd);
    pthread_cond_destroy(&s->all_gone);
    pthread_mutex_destroy(&s->lock);
    free(s);
}
