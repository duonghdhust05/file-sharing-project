/*
 * Kiểm thử tích hợp M1 (SDD §8 - mức "Tích hợp"): server thật chạy trên cổng ngẫu nhiên,
 * client là socket TCP thật. Bao gồm Probe 1 qua mạng thật và NFR-3 (>= 5 client đồng thời).
 */
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common/fsp_json.h"
#include "common/fsp_packet.h"
#include "common/fsp_socket.h"
#include "server/fsp_dispatcher.h"
#include "server/fsp_server.h"
#include "test_util.h"

/* ---------------------------------------------------------------------------
 * Handler mẫu (đóng vai M2/M3) dùng trong test
 * ------------------------------------------------------------------------- */
static atomic_int g_cleanups;
static atomic_int g_chunks;

/* ECHO: trả lại trường "msg" -> {"status":200,"msg":"..."} */
static int h_echo(client_ctx_t *ctx, const fsp_frame_t *req) {
    char msg[512];
    if (fsp_json_get_str((const char *)req->payload, "msg", msg, sizeof(msg)) != 0)
        return fsp_send_error(ctx->fd, req->event, FSP_STATUS_BAD_REQUEST, "missing msg");
    char fields[600];
    fsp_json_fields_t f;
    fsp_jf_init(&f, fields, sizeof(fields));
    fsp_jf_str(&f, "msg", msg);
    return fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, fields, NULL);
}

/* WHOAMI: trả conn_id và cwd mặc định của client_ctx_t */
static int h_whoami(client_ctx_t *ctx, const fsp_frame_t *req) {
    char fields[128];
    fsp_json_fields_t f;
    fsp_jf_init(&f, fields, sizeof(fields));
    fsp_jf_int(&f, "conn", (long long)ctx->conn_id);
    fsp_jf_str(&f, "cwd", ctx->cwd);
    return fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, fields, NULL);
}

/* BYE: handler yêu cầu đóng kết nối */
static int h_bye(client_ctx_t *ctx, const fsp_frame_t *req) {
    fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, NULL, NULL);
    return -1;
}

static int h_chunk(client_ctx_t *ctx, const fsp_frame_t *req) {
    atomic_fetch_add(&g_chunks, 1);
    char fields[32];
    snprintf(fields, sizeof(fields), "\"bytes\":%u", (unsigned)req->payload_len);
    return fsp_send_response(ctx->fd, req->event, FSP_STATUS_OK, fields, NULL);
}

static void on_disconnect(client_ctx_t *ctx) {
    (void)ctx;
    atomic_fetch_add(&g_cleanups, 1);
}

/* ---------------------------------------------------------------------------
 * Tiện ích chạy server trên một thread riêng
 * ------------------------------------------------------------------------- */
typedef struct {
    fsp_server_t *server;
    pthread_t thread;
} running_server_t;

static void *run_server(void *arg) {
    fsp_server_run((fsp_server_t *)arg);
    return NULL;
}

static int start_server(running_server_t *rs, int with_chunk_handler) {
    dispatcher_reset();
    if (dispatcher_register("ECHO", h_echo) != 0 || dispatcher_register("WHOAMI", h_whoami) != 0 ||
        dispatcher_register("BYE", h_bye) != 0 || dispatcher_register_cleanup(on_disconnect) != 0)
        return -1;
    if (with_chunk_handler && dispatcher_register_chunk(h_chunk) != 0)
        return -1;
    atomic_store(&g_cleanups, 0);
    atomic_store(&g_chunks, 0);
    rs->server = fsp_server_create(0, 32);
    if (!rs->server)
        return -1;
    fsp_server_set_verbose(rs->server, 0);
    return pthread_create(&rs->thread, NULL, run_server, rs->server) == 0 ? 0 : -1;
}

static void stop_server(running_server_t *rs) {
    fsp_server_request_stop(rs->server);
    pthread_join(rs->thread, NULL);
    fsp_server_destroy(rs->server);
    rs->server = NULL;
}

static int connect_to(running_server_t *rs) {
    return fsp_connect("127.0.0.1", fsp_server_port(rs->server));
}

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static int wait_until_cleanups(int expected, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 5) {
        if (atomic_load(&g_cleanups) == expected)
            return 1;
        sleep_ms(5);
    }
    return atomic_load(&g_cleanups) == expected;
}

/* Gửi ECHO {"msg":m} và kiểm tra phản hồi đúng từng byte */
static int echo_roundtrip(int fd, const char *m) {
    char req[600], expect[700], esc[540];
    fsp_json_escape(m, esc, sizeof(esc));
    int n = snprintf(req, sizeof(req), "{\"msg\":\"%s\"}", esc);
    snprintf(expect, sizeof(expect), "{\"status\":200,\"msg\":\"%s\"}", esc);
    if (fsp_send_frame(fd, FSP_MSG_EVENT_REQ, "ECHO", req, (uint32_t)n) != 0)
        return -1;
    fsp_frame_t r;
    if (fsp_recv_frame(fd, &r) != 0)
        return -1;
    int ok = r.type == FSP_MSG_EVENT_RESP && strcmp(r.event, "ECHO") == 0 &&
             strcmp((char *)r.payload, expect) == 0;
    fsp_frame_free(&r);
    return ok ? 0 : -1;
}

/* Đợi đối phương đóng kết nối (recv trả 0 / lỗi) trong tối đa timeout_ms */
static int peer_closed(int fd, int timeout_ms) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, timeout_ms) != 1)
        return 0;
    char b;
    return recv(fd, &b, 1, 0) <= 0;
}

/* ---------------------------------------------------------------------------
 * Test
 * ------------------------------------------------------------------------- */
static int test_request_response_and_unknown_event(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    CHECK(fsp_server_port(rs.server) != 0, "random port assigned");

    int fd = fsp_connect("localhost", fsp_server_port(rs.server));
    CHECK(fd >= 0, "connect via hostname");
    CHECK(echo_roundtrip(fd, "xin chào") == 0, "ECHO round-trip");

    fsp_frame_t r;
    CHECK(fsp_send_frame(fd, FSP_MSG_EVENT_REQ, "WHOAMI", "{}", 2) == 0, "send WHOAMI");
    CHECK(fsp_recv_frame(fd, &r) == 0, "recv WHOAMI");
    char cwd[16];
    CHECK(
        fsp_json_get_str((char *)r.payload, "cwd", cwd, sizeof(cwd)) == 0 && strcmp(cwd, "/") == 0,
        "client_ctx_t.cwd starts at \"/\"");
    fsp_frame_free(&r);

    CHECK(fsp_send_frame(fd, FSP_MSG_EVENT_REQ, FSP_EV_DIR_LIST, "{}", 2) == 0, "send DIR_LIST");
    CHECK(fsp_recv_frame(fd, &r) == 0, "recv reply");
    CHECK(r.type == FSP_MSG_ERROR_RESP, "unknown event -> ERROR_RESP");
    CHECK(strcmp(r.event, FSP_EV_DIR_LIST) == 0, "reply repeats request event");
    long long st = 0;
    CHECK(fsp_json_get_int((char *)r.payload, "status", &st) == 0 && st == 400, "status 400");
    fsp_frame_free(&r);
    CHECK(echo_roundtrip(fd, "still alive") == 0, "connection kept after unknown event");

    fsp_close(fd);
    CHECK(wait_until_cleanups(1, 2000), "cleanup hook ran once on disconnect");
    stop_server(&rs);
    return 0;
}

static int test_ping_pong(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    int fd = connect_to(&rs);
    CHECK(fd >= 0, "connect");
    CHECK(fsp_send_frame(fd, FSP_MSG_PING, "PING", NULL, 0) == 0, "send PING");
    fsp_frame_t r;
    CHECK(fsp_recv_frame(fd, &r) == 0 && r.type == FSP_MSG_PONG && r.payload_len == 0, "PONG");
    fsp_frame_free(&r);
    fsp_close(fd);
    stop_server(&rs);
    return 0;
}

/* PROBE 1 qua TCP thật: 500 frame dồn trong MỘT lần send -> 500 phản hồi độc lập, đúng thứ tự */
static int test_sticky_500_frames_over_tcp(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    int fd = connect_to(&rs);
    CHECK(fd >= 0, "connect");

    enum { N = 500 };
    size_t cap = (size_t)N * 64, off = 0;
    uint8_t *blob = malloc(cap);
    CHECK(blob != NULL, "alloc");
    for (int i = 0; i < N; ++i) {
        char json[32];
        int n = snprintf(json, sizeof(json), "{\"msg\":\"%d\"}", i);
        int w = fsp_pack_frame(FSP_MSG_EVENT_REQ, "ECHO", json, (uint32_t)n, blob + off, cap - off);
        CHECK(w > 0, "pack");
        off += (size_t)w;
    }
    CHECK(send_exact(fd, blob, off) == 0, "send all 500 frames at once");
    free(blob);

    for (int i = 0; i < N; ++i) {
        fsp_frame_t r;
        char expect[48];
        snprintf(expect, sizeof(expect), "{\"status\":200,\"msg\":\"%d\"}", i);
        CHECK(fsp_recv_frame(fd, &r) == 0, "receive reply");
        int ok = strcmp((char *)r.payload, expect) == 0;
        fsp_frame_free(&r);
        CHECK(ok, "replies are independent and in order");
    }
    fsp_close(fd);
    stop_server(&rs);
    return 0;
}

/* Phân mảnh qua TCP thật: 1 frame gửi từng byte -> server vẫn ghép đúng */
static int test_byte_by_byte_over_tcp(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    int fd = connect_to(&rs);
    CHECK(fd >= 0, "connect");

    char msg[201];
    memset(msg, 'z', 200);
    msg[200] = '\0';
    char json[256];
    int n = snprintf(json, sizeof(json), "{\"msg\":\"%s\"}", msg);
    uint8_t frame[512];
    int len = fsp_pack_frame(FSP_MSG_EVENT_REQ, "ECHO", json, (uint32_t)n, frame, sizeof(frame));
    for (int i = 0; i < len; ++i) {
        CHECK(send_exact(fd, frame + i, 1) == 0, "send 1 byte");
        if (i % 20 == 0)
            sleep_ms(1);
    }
    fsp_frame_t r;
    char expect[300];
    snprintf(expect, sizeof(expect), "{\"status\":200,\"msg\":\"%s\"}", msg);
    CHECK(fsp_recv_frame(fd, &r) == 0 && strcmp((char *)r.payload, expect) == 0,
          "frame reassembled from single bytes");
    fsp_frame_free(&r);
    fsp_close(fd);
    stop_server(&rs);
    return 0;
}

typedef struct {
    uint16_t port;
    int id;
    int ok;
} client_job_t;

static void *client_worker(void *arg) {
    client_job_t *job = (client_job_t *)arg;
    int fd = fsp_connect("127.0.0.1", job->port);
    if (fd < 0)
        return NULL;
    int good = 0;
    for (int i = 0; i < 50; ++i) {
        char m[48];
        snprintf(m, sizeof(m), "client %d request %d", job->id, i);
        if (echo_roundtrip(fd, m) == 0)
            ++good;
    }
    fsp_close(fd);
    job->ok = good;
    return NULL;
}

/* NFR-3 / Probe 6 (phần M1): 10 client đồng thời x 50 request, không lẫn dữ liệu */
static int test_concurrent_clients(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    enum { CLIENTS = 10 };
    pthread_t t[CLIENTS];
    client_job_t jobs[CLIENTS];
    for (int c = 0; c < CLIENTS; ++c) {
        jobs[c].port = fsp_server_port(rs.server);
        jobs[c].id = c;
        jobs[c].ok = 0;
        CHECK(pthread_create(&t[c], NULL, client_worker, &jobs[c]) == 0, "spawn client");
    }
    for (int c = 0; c < CLIENTS; ++c)
        pthread_join(t[c], NULL);
    for (int c = 0; c < CLIENTS; ++c)
        CHECK(jobs[c].ok == 50, "every client got 50 correct replies");

    CHECK(wait_until_cleanups(CLIENTS, 3000), "one cleanup per client");
    CHECK(fsp_server_active_clients(rs.server) == 0, "all client threads exited");
    CHECK(fsp_server_total_accepted(rs.server) == CLIENTS, "accept counter");
    stop_server(&rs);
    return 0;
}

/* Fail-Closed: frame độc hại -> server đóng ĐÚNG kết nối đó và vẫn phục vụ client khác */
static int test_malformed_frame_closes_connection(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    uint8_t good[64];
    int n = fsp_pack_frame(FSP_MSG_EVENT_REQ, "ECHO", "{\"msg\":\"a\"}", 11, good, sizeof(good));
    uint8_t bad[64];

    int victim = connect_to(&rs);
    CHECK(victim >= 0, "connect");
    memcpy(bad, good, (size_t)n);
    bad[0] = 0x55;
    CHECK(send_exact(victim, bad, (size_t)n) == 0 && peer_closed(victim, 2000), "bad magic");
    fsp_close(victim);

    victim = connect_to(&rs);
    memcpy(bad, good, (size_t)n);
    bad[n - 1] ^= 0x01;
    CHECK(send_exact(victim, bad, (size_t)n) == 0 && peer_closed(victim, 2000), "bad CRC");
    fsp_close(victim);

    victim = connect_to(&rs);
    memcpy(bad, good, (size_t)n);
    uint32_t huge = htonl(0xFFFFFFF0u);
    memcpy(bad + 2, &huge, 4);
    CHECK(send_exact(victim, bad, 42) == 0 && peer_closed(victim, 2000), "4 GB payload_len");
    fsp_close(victim);

    CHECK(wait_until_cleanups(3, 2000), "3 bad connections cleaned up");
    int fd = connect_to(&rs);
    CHECK(fd >= 0 && echo_roundtrip(fd, "server survived") == 0, "server still serves");
    fsp_close(fd);
    stop_server(&rs);
    return 0;
}

static int test_handler_can_close_and_type_routing(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server (no chunk handler)");
    int fd = connect_to(&rs);
    fsp_frame_t r;

    /* FILE_CHUNK khi M3 chưa đăng ký -> ERROR_RESP 400, kết nối vẫn giữ */
    CHECK(fsp_send_frame(fd, FSP_MSG_FILE_CHUNK, "CHUNK", "abc", 3) == 0, "send chunk");
    CHECK(fsp_recv_frame(fd, &r) == 0 && r.type == FSP_MSG_ERROR_RESP, "no chunk handler -> 400");
    fsp_frame_free(&r);

    /* Client không được gửi EVENT_RESP lên server */
    CHECK(fsp_send_frame(fd, FSP_MSG_EVENT_RESP, "ECHO", "{}", 2) == 0, "send EVENT_RESP");
    CHECK(fsp_recv_frame(fd, &r) == 0 && r.type == FSP_MSG_ERROR_RESP, "wrong type -> 400");
    fsp_frame_free(&r);

    /* Handler trả -1 -> server đóng kết nối sau khi gửi phản hồi */
    CHECK(fsp_send_frame(fd, FSP_MSG_EVENT_REQ, "BYE", "{}", 2) == 0, "send BYE");
    CHECK(fsp_recv_frame(fd, &r) == 0 && r.type == FSP_MSG_EVENT_RESP, "BYE answered");
    fsp_frame_free(&r);
    CHECK(peer_closed(fd, 2000), "connection closed by server");
    fsp_close(fd);
    stop_server(&rs);

    /* Có chunk handler (vai M3): frame FILE_CHUNK được chuyển đúng chỗ */
    CHECK(start_server(&rs, 1) == 0, "start server (with chunk handler)");
    fd = connect_to(&rs);
    uint8_t data[FSP_CHUNK_HEADER_SIZE + 1000];
    memset(data, 7, sizeof(data));
    CHECK(fsp_send_frame(fd, FSP_MSG_FILE_CHUNK, "CHUNK", data, sizeof(data)) == 0, "chunk");
    CHECK(fsp_recv_frame(fd, &r) == 0 && r.type == FSP_MSG_EVENT_RESP, "chunk handled");
    long long bytes = 0;
    CHECK(fsp_json_get_int((char *)r.payload, "bytes", &bytes) == 0 &&
              bytes == (long long)sizeof(data),
          "chunk handler saw whole payload");
    fsp_frame_free(&r);
    CHECK(atomic_load(&g_chunks) == 1, "chunk handler called once");
    fsp_close(fd);
    stop_server(&rs);

    /* dispatcher_register từ chối trùng tên, tên rỗng, tên 32 ký tự */
    CHECK(dispatcher_register("ECHO", h_echo) == -1, "duplicate refused");
    CHECK(dispatcher_register("", h_echo) == -1, "empty name refused");
    CHECK(dispatcher_register("ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", h_echo) == -1, "32 chars");
    CHECK(dispatcher_register("NEW_EVENT", NULL) == -1, "NULL handler refused");
    CHECK(dispatcher_register_chunk(h_chunk) == -1, "second chunk handler refused");
    return 0;
}

/* Dừng server khi còn client đang treo kết nối: phải kết thúc nhanh, client thấy EOF */
static int test_shutdown_with_idle_clients(void) {
    running_server_t rs;
    CHECK(start_server(&rs, 0) == 0, "start server");
    enum { IDLE = 6 };
    int fds[IDLE];
    for (int i = 0; i < IDLE; ++i) {
        fds[i] = connect_to(&rs);
        CHECK(fds[i] >= 0 && echo_roundtrip(fds[i], "hi") == 0, "connect + serve");
    }
    CHECK(fsp_server_active_clients(rs.server) == IDLE, "6 client threads alive");

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    stop_server(&rs);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    CHECK(ms < 2000, "shutdown completes quickly even with blocked client threads");
    CHECK(atomic_load(&g_cleanups) == IDLE, "every client cleaned up");
    for (int i = 0; i < IDLE; ++i) {
        CHECK(peer_closed(fds[i], 1000), "client observes disconnect");
        fsp_close(fds[i]);
    }

    /* Cổng đang bị chiếm -> fsp_server_create trả NULL */
    fsp_server_t *a = fsp_server_create(0, 4);
    CHECK(a != NULL, "first server");
    CHECK(fsp_server_create(fsp_server_port(a), 4) == NULL, "second bind on same port fails");
    fsp_server_destroy(a);
    return 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int passed = 0;
    printf("=== M1 NETWORK CORE INTEGRATION TESTS (C11, real TCP) ===\n");
    RUN_TEST(test_request_response_and_unknown_event, passed);
    RUN_TEST(test_ping_pong, passed);
    RUN_TEST(test_sticky_500_frames_over_tcp, passed);
    RUN_TEST(test_byte_by_byte_over_tcp, passed);
    RUN_TEST(test_concurrent_clients, passed);
    RUN_TEST(test_malformed_frame_closes_connection, passed);
    RUN_TEST(test_handler_can_close_and_type_routing, passed);
    RUN_TEST(test_shutdown_with_idle_clients, passed);
    printf("ALL NETWORK CORE TESTS PASSED (%d/%d)\n", passed, passed);
    dispatcher_reset();
    return 0;
}
