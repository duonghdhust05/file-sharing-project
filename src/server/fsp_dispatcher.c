#include "server/fsp_dispatcher.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[FSP_EVENT_NAME_MAX_LEN];
    fsp_handler_fn fn;
} dispatcher_entry_t;

/* Bảng tĩnh (~20 sự kiện) tìm tuyến tính bằng strcmp - đủ nhanh và dễ hiểu (SDD §4.3).
 * Mutex bảo vệ trường hợp đăng ký muộn khi đã có client thread đang đọc bảng. */
static dispatcher_entry_t g_handlers[FSP_DISPATCHER_MAX_HANDLERS];
static size_t g_handler_count = 0;
static fsp_handler_fn g_chunk_handler = NULL;
static fsp_cleanup_fn g_cleanups[FSP_DISPATCHER_MAX_CLEANUPS];
static size_t g_cleanup_count = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int valid_event_name(const char *event) {
    if (!event)
        return 0;
    size_t n = strnlen(event, FSP_EVENT_NAME_MAX_LEN);
    return n > 0 && n < FSP_EVENT_NAME_MAX_LEN;
}

/* Gọi khi đã giữ g_lock */
static fsp_handler_fn find_locked(const char *event) {
    for (size_t i = 0; i < g_handler_count; ++i) {
        if (strcmp(g_handlers[i].name, event) == 0)
            return g_handlers[i].fn;
    }
    return NULL;
}

int dispatcher_register(const char *event, fsp_handler_fn fn) {
    if (!valid_event_name(event) || !fn)
        return -1;
    int rc = -1;
    pthread_mutex_lock(&g_lock);
    if (!find_locked(event) && g_handler_count < FSP_DISPATCHER_MAX_HANDLERS) {
        dispatcher_entry_t *e = &g_handlers[g_handler_count++];
        memset(e->name, 0, sizeof(e->name));
        memcpy(e->name, event, strlen(event));
        e->fn = fn;
        rc = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int dispatcher_register_chunk(fsp_handler_fn fn) {
    int rc = -1;
    if (!fn)
        return -1;
    pthread_mutex_lock(&g_lock);
    if (!g_chunk_handler) {
        g_chunk_handler = fn;
        rc = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int dispatcher_register_cleanup(fsp_cleanup_fn fn) {
    int rc = -1;
    if (!fn)
        return -1;
    pthread_mutex_lock(&g_lock);
    if (g_cleanup_count < FSP_DISPATCHER_MAX_CLEANUPS) {
        g_cleanups[g_cleanup_count++] = fn;
        rc = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int dispatcher_has(const char *event) {
    if (!valid_event_name(event))
        return 0;
    pthread_mutex_lock(&g_lock);
    int found = find_locked(event) != NULL;
    pthread_mutex_unlock(&g_lock);
    return found;
}

int dispatcher_handle(client_ctx_t *ctx, const fsp_frame_t *req) {
    if (!ctx || !req)
        return -1;

    /* Lấy con trỏ hàm dưới lock rồi gọi NGOÀI lock: handler chạy lâu (truyền file) không
     * chặn các client khác. */
    fsp_handler_fn fn = NULL;
    pthread_mutex_lock(&g_lock);
    if (req->type == FSP_MSG_FILE_CHUNK)
        fn = g_chunk_handler;
    else if (req->type == FSP_MSG_EVENT_REQ)
        fn = find_locked(req->event);
    pthread_mutex_unlock(&g_lock);

    if (fn)
        return fn(ctx, req);

    /* Không có handler: báo lỗi 400 nhưng vẫn giữ kết nối */
    char msg[96];
    if (req->type == FSP_MSG_EVENT_REQ)
        snprintf(msg, sizeof(msg), "unknown event: %s", req->event);
    else
        snprintf(msg, sizeof(msg), "unexpected message type %u", (unsigned)req->type);
    const char *reply_event = valid_event_name(req->event) ? req->event : "UNKNOWN";
    return fsp_send_error(ctx->fd, reply_event, FSP_STATUS_BAD_REQUEST, msg) == 0 ? 0 : -1;
}

void dispatcher_run_cleanups(client_ctx_t *ctx) {
    fsp_cleanup_fn fns[FSP_DISPATCHER_MAX_CLEANUPS];
    size_t n;
    pthread_mutex_lock(&g_lock);
    n = g_cleanup_count;
    memcpy(fns, g_cleanups, n * sizeof(fns[0]));
    pthread_mutex_unlock(&g_lock);
    while (n > 0)
        fns[--n](ctx);
}

void dispatcher_reset(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_handlers, 0, sizeof(g_handlers));
    g_handler_count = 0;
    g_chunk_handler = NULL;
    g_cleanup_count = 0;
    pthread_mutex_unlock(&g_lock);
}
