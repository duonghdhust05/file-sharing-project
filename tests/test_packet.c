/*
 * Probe 1 (SDD §8): đóng/mở frame 42 byte, dính gói, phân mảnh, frame dị dạng.
 * Mọi kiểm tra chạy trên byte thật: buffer trong bộ nhớ hoặc socketpair thật.
 */
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common/fsp_crc32.h"
#include "common/fsp_json.h"
#include "common/fsp_packet.h"
#include "common/fsp_socket.h"
#include "test_util.h"

static uint8_t g_buf[3 * (FSP_HEADER_SIZE + FSP_BUFFER_SIZE)];

static int pack(uint8_t type, const char *event, const char *json, uint8_t *out, size_t cap) {
    return fsp_pack_frame(type, event, json, (uint32_t)strlen(json), out, cap);
}

static void sleep_ms(long ms) {
    struct timespec ts = {0, ms * 1000000L};
    nanosleep(&ts, NULL);
}

static int test_single_frame_roundtrip(void) {
    const char *json = "{\"username\":\"nguyen\"}";
    int n = pack(FSP_MSG_EVENT_REQ, FSP_EV_AUTH_LOGIN, json, g_buf, sizeof(g_buf));
    CHECK(n == (int)(FSP_HEADER_SIZE + strlen(json)), "frame size = 42 + payload");
    CHECK(g_buf[0] == 0xAA && g_buf[1] == FSP_MSG_EVENT_REQ, "magic and type bytes");
    uint32_t be_len;
    memcpy(&be_len, g_buf + 2, 4);
    CHECK(ntohl(be_len) == strlen(json), "payload_len is big-endian");
    CHECK(memcmp(g_buf + 6, "AUTH_LOGIN\0\0", 12) == 0, "event name zero-padded");

    fsp_frame_t f[1];
    size_t used = 0;
    CHECK(fsp_parse_frames(g_buf, (size_t)n, f, 1, &used) == 1, "parse one frame");
    CHECK(used == (size_t)n, "consumed whole frame");
    CHECK(f[0].type == FSP_MSG_EVENT_REQ, "type preserved");
    CHECK(strcmp(f[0].event, FSP_EV_AUTH_LOGIN) == 0, "event preserved");
    CHECK(f[0].payload_len == strlen(json), "payload length preserved");
    CHECK(strcmp((const char *)f[0].payload, json) == 0, "payload is NUL-terminated JSON");
    fsp_frame_free(&f[0]);
    CHECK(f[0].payload == NULL, "fsp_frame_free clears pointer");
    fsp_frame_free(&f[0]); /* gọi lại không được crash */

    /* Frame rỗng: đúng 42 byte, checksum 0, payload vẫn là chuỗi "" hợp lệ */
    n = fsp_pack_frame(FSP_MSG_PING, "PING", NULL, 0, g_buf, sizeof(g_buf));
    CHECK(n == 42, "empty frame is exactly 42 bytes");
    CHECK(fsp_parse_frames(g_buf, 42, f, 1, &used) == 1, "parse empty frame");
    CHECK(f[0].payload != NULL && f[0].payload[0] == '\0', "empty payload is \"\"");
    fsp_frame_free(&f[0]);
    return 0;
}

/* PROBE 1a: 2 frame dính nhau trong 1 buffer -> tách đúng 2 frame, không mất byte */
static int test_sticky_two_frames(void) {
    const char *p1 = "{\"id\":1}";
    const char *p2 = "{\"id\":2,\"msg\":\"second frame right behind\"}";
    int n1 = pack(FSP_MSG_EVENT_REQ, "FIRST", p1, g_buf, sizeof(g_buf));
    int n2 = pack(FSP_MSG_EVENT_REQ, "SECOND", p2, g_buf + n1, sizeof(g_buf) - (size_t)n1);
    CHECK(n1 > 0 && n2 > 0, "pack both");

    fsp_frame_t f[4];
    size_t used = 0;
    int got = fsp_parse_frames(g_buf, (size_t)(n1 + n2), f, 4, &used);
    CHECK(got == 2, "exactly 2 frames separated");
    CHECK(used == (size_t)(n1 + n2), "all bytes consumed");
    CHECK(strcmp(f[0].event, "FIRST") == 0 && strcmp((char *)f[0].payload, p1) == 0, "frame 1");
    CHECK(strcmp(f[1].event, "SECOND") == 0 && strcmp((char *)f[1].payload, p2) == 0, "frame 2");
    fsp_frame_free(&f[0]);
    fsp_frame_free(&f[1]);

    /* max_out giới hạn số frame tách ra, phần còn lại không bị tính là đã dùng */
    got = fsp_parse_frames(g_buf, (size_t)(n1 + n2), f, 1, &used);
    CHECK(got == 1 && used == (size_t)n1, "max_out = 1 stops after first frame");
    fsp_frame_free(&f[0]);
    return 0;
}

/* PROBE 1b: frame đến từng mảnh -> chỉ tách khi đủ byte; frame sau dở dang được giữ lại */
static int test_fragmented_buffer(void) {
    char json[1200];
    memset(json, 'x', sizeof(json) - 1);
    json[0] = '"';
    json[sizeof(json) - 2] = '"';
    json[sizeof(json) - 1] = '\0';
    int n = pack(FSP_MSG_EVENT_REQ, "BIG", json, g_buf, sizeof(g_buf));
    CHECK(n > 0, "pack");

    fsp_frame_t f[1];
    for (int have = 0; have <= n; have += 7) {
        size_t used = 99;
        int got = fsp_parse_frames(g_buf, (size_t)have, f, 1, &used);
        CHECK(got == 0 && used == 0, "incomplete frame is not extracted");
    }
    size_t used = 0;
    CHECK(fsp_parse_frames(g_buf, (size_t)n, f, 1, &used) == 1 && used == (size_t)n,
          "extracted once complete");
    CHECK(strcmp((char *)f[0].payload, json) == 0, "payload intact");
    fsp_frame_free(&f[0]);

    /* frame đầy đủ + nửa frame sau */
    int n2 = pack(FSP_MSG_EVENT_REQ, "NEXT", "{}", g_buf + n, sizeof(g_buf) - (size_t)n);
    fsp_frame_t g[2];
    int got = fsp_parse_frames(g_buf, (size_t)n + (size_t)n2 / 2, g, 2, &used);
    CHECK(got == 1 && used == (size_t)n, "only the complete frame is consumed");
    fsp_frame_free(&g[0]);
    return 0;
}

/* Fail-Closed: header/payload bị làm giả phải bị từ chối, không rò rỉ bộ nhớ */
static int test_malformed_frames(void) {
    const char *json = "{\"secret\":\"x\"}";
    uint8_t good[128];
    int n = pack(FSP_MSG_EVENT_REQ, "TEST", json, good, sizeof(good));
    fsp_frame_t f[2];
    size_t used = 1;
    uint8_t bad[256];

    memcpy(bad, good, (size_t)n);
    bad[0] = 0x55;
    CHECK(fsp_parse_frames(bad, (size_t)n, f, 2, &used) == -1 && used == 0, "bad magic");

    memcpy(bad, good, (size_t)n);
    bad[1] = 0x7F;
    CHECK(fsp_parse_frames(bad, (size_t)n, f, 2, &used) == -1, "unknown type");

    memcpy(bad, good, (size_t)n);
    memset(bad + 6, 'A', FSP_EVENT_NAME_MAX_LEN); /* event không có '\0' */
    CHECK(fsp_parse_frames(bad, (size_t)n, f, 2, &used) == -1, "event without terminator");

    memcpy(bad, good, (size_t)n);
    uint32_t huge = htonl(0x7FFFFFFFu);
    memcpy(bad + 2, &huge, 4);
    CHECK(fsp_parse_frames(bad, (size_t)n, f, 2, &used) == -1, "payload_len above 128 KB");

    memcpy(bad, good, (size_t)n);
    bad[n - 1] ^= 0x01; /* lật 1 bit payload */
    CHECK(fsp_parse_frames(bad, (size_t)n, f, 2, &used) == -1, "CRC32 mismatch");

    int e = fsp_pack_frame(FSP_MSG_PING, "PING", NULL, 0, bad, sizeof(bad));
    bad[e - 1] = 0x01; /* payload rỗng nhưng checksum != 0 */
    CHECK(fsp_parse_frames(bad, (size_t)e, f, 2, &used) == -1, "empty payload, non-zero CRC");

    /* frame tốt rồi tới frame hỏng: trả -1 và giải phóng luôn frame tốt (ASan bắt rò rỉ) */
    memcpy(bad, good, (size_t)n);
    memcpy(bad + n, good, (size_t)n);
    bad[n] = 0x00;
    CHECK(fsp_parse_frames(bad, (size_t)(2 * n), f, 2, &used) == -1 && used == 0,
          "good frame followed by bad frame");
    return 0;
}

typedef struct {
    int fd;
    const uint8_t *data;
    uint32_t len;
    int rc;
} chunk_job_t;

/* Frame 64 KB lớn hơn bộ đệm socketpair -> phải gửi ở luồng khác trong khi luồng chính nhận */
static void *send_chunk_thread(void *arg) {
    chunk_job_t *j = (chunk_job_t *)arg;
    j->rc = fsp_send_frame(j->fd, FSP_MSG_FILE_CHUNK, "CHUNK", j->data, j->len);
    return NULL;
}

static int test_send_guards(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    char long_name[FSP_EVENT_NAME_MAX_LEN + 1];
    memset(long_name, 'E', FSP_EVENT_NAME_MAX_LEN);
    long_name[FSP_EVENT_NAME_MAX_LEN] = '\0'; /* 32 ký tự: không còn chỗ cho '\0' */
    uint8_t *big = calloc(1, FSP_BUFFER_SIZE + 1);
    CHECK(big != NULL, "alloc");

    CHECK(fsp_send_frame(sv[0], FSP_MSG_EVENT_REQ, "", "{}", 2) == -1, "empty event refused");
    CHECK(fsp_send_frame(sv[0], FSP_MSG_EVENT_REQ, long_name, "{}", 2) == -1, "32-char event");
    CHECK(fsp_send_frame(sv[0], FSP_MSG_EVENT_REQ, "X", NULL, 5) == -1, "NULL payload, len 5");
    CHECK(fsp_send_frame(sv[0], FSP_MSG_FILE_CHUNK, "X", big, FSP_BUFFER_SIZE + 1) == -1,
          "payload above limit");
    CHECK(fsp_send_frame(sv[0], 0x09, "X", "{}", 2) == -1, "unknown type refused");
    CHECK(fsp_send_frame(-1, FSP_MSG_EVENT_REQ, "X", "{}", 2) == -1, "invalid fd");

    struct pollfd p = {sv[1], POLLIN, 0};
    CHECK(poll(&p, 1, 50) == 0, "nothing was put on the wire");

    /* Frame FILE_CHUNK đầy đủ (88 + 64 KB) nằm trong giới hạn và đi qua socket nguyên vẹn */
    uint32_t full = FSP_CHUNK_HEADER_SIZE + FSP_DEFAULT_CHUNK_SIZE;
    for (uint32_t i = 0; i < full; ++i)
        big[i] = (uint8_t)(i * 31u);
    chunk_job_t job = {sv[0], big, full, -1};
    pthread_t t;
    CHECK(pthread_create(&t, NULL, send_chunk_thread, &job) == 0, "start sender");
    fsp_frame_t f;
    int rc = fsp_recv_frame(sv[1], &f);
    pthread_join(t, NULL);
    CHECK(job.rc == 0 && rc == 0, "send + receive full-size chunk frame");
    CHECK(f.payload_len == full && memcmp(f.payload, big, full) == 0, "chunk bytes intact");
    fsp_frame_free(&f);
    free(big);
    close(sv[0]);
    close(sv[1]);
    return 0;
}

typedef struct {
    int fd;
    const uint8_t *data;
    size_t len;
} pieces_job_t;

/* Gửi 1 frame thành 3 lần, nghỉ giữa các lần để bên nhận chắc chắn nhận thành nhiều mảnh */
static void *send_in_three_pieces(void *arg) {
    pieces_job_t *j = (pieces_job_t *)arg;
    size_t a = 5, b = j->len / 2;
    send_exact(j->fd, j->data, a);
    sleep_ms(30);
    send_exact(j->fd, j->data + a, b - a);
    sleep_ms(30);
    send_exact(j->fd, j->data + b, j->len - b);
    return NULL;
}

/* PROBE 1c: 1 frame gửi qua socketpair thành 3 lần -> fsp_recv_frame ghép đúng */
static int test_recv_frame_in_three_pieces(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    const char *json = "{\"path\":\"/home/an/Báo cáo.docx\",\"size\":123456}";
    int n = pack(FSP_MSG_EVENT_REQ, FSP_EV_FILE_UPLOAD_REQ, json, g_buf, sizeof(g_buf));

    pieces_job_t job = {sv[0], g_buf, (size_t)n};
    pthread_t t;
    CHECK(pthread_create(&t, NULL, send_in_three_pieces, &job) == 0, "start sender");
    fsp_frame_t f;
    int rc = fsp_recv_frame(sv[1], &f);
    pthread_join(t, NULL);
    CHECK(rc == 0, "frame reassembled from 3 pieces");
    CHECK(strcmp(f.event, FSP_EV_FILE_UPLOAD_REQ) == 0, "event intact");
    CHECK(strcmp((char *)f.payload, json) == 0, "UTF-8 payload intact");
    fsp_frame_free(&f);

    /* Đối phương đóng giữa chừng (mới gửi 20 byte header) -> -1, không rò rỉ */
    CHECK(send_exact(sv[0], g_buf, 20) == 0, "send partial header");
    close(sv[0]);
    CHECK(fsp_recv_frame(sv[1], &f) == -1, "peer closed mid-header");
    CHECK(fsp_recv_frame(sv[1], &f) == -1, "closed socket keeps failing");
    close(sv[1]);
    return 0;
}

static int test_recv_rejects_bad_frame_on_socket(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    int n = pack(FSP_MSG_EVENT_REQ, "X", "{\"a\":1}", g_buf, sizeof(g_buf));
    g_buf[n - 2] ^= 0x40; /* hỏng payload */
    CHECK(send_exact(sv[0], g_buf, (size_t)n) == 0, "send corrupted frame");
    fsp_frame_t f;
    CHECK(fsp_recv_frame(sv[1], &f) == -1, "CRC mismatch rejected");
    CHECK(f.payload == NULL, "no payload left to free");

    uint8_t hdr[42];
    fsp_pack_frame(FSP_MSG_EVENT_REQ, "X", NULL, 0, hdr, sizeof(hdr));
    uint32_t huge = htonl(FSP_BUFFER_SIZE + 1u);
    memcpy(hdr + 2, &huge, 4);
    CHECK(send_exact(sv[0], hdr, sizeof(hdr)) == 0, "send oversized header");
    CHECK(fsp_recv_frame(sv[1], &f) == -1, "oversized payload_len rejected before malloc");
    close(sv[0]);
    close(sv[1]);
    return 0;
}

static int test_response_helpers(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    fsp_frame_t f;

    CHECK(fsp_send_response(sv[0], "AUTH_LOGIN", 200, "\"token\":\"ab12\"", NULL) == 0, "send");
    CHECK(fsp_recv_frame(sv[1], &f) == 0, "recv");
    CHECK(f.type == FSP_MSG_EVENT_RESP && strcmp(f.event, "AUTH_LOGIN") == 0, "EVENT_RESP");
    CHECK(strcmp((char *)f.payload, "{\"status\":200,\"token\":\"ab12\"}") == 0, "flat JSON");
    fsp_frame_free(&f);

    CHECK(fsp_send_response(sv[0], "DIR_MKDIR", 201, NULL, NULL) == 0, "status only");
    CHECK(fsp_recv_frame(sv[1], &f) == 0 && strcmp((char *)f.payload, "{\"status\":201}") == 0,
          "{\"status\":201}");
    fsp_frame_free(&f);

    CHECK(fsp_send_response(sv[0], "DIR_LIST", 200, "\"count\":2", "a.txt\tf\nsub\td") == 0,
          "list response");
    CHECK(fsp_recv_frame(sv[1], &f) == 0, "recv list");
    CHECK(strcmp((char *)f.payload, "{\"status\":200,\"count\":2}\na.txt\tf\nsub\td") == 0,
          "JSON first line + TSV lines (SDD §3.3)");
    long long count = 0;
    CHECK(fsp_json_get_int((char *)f.payload, "count", &count) == 0 && count == 2,
          "first line still parses as JSON");
    fsp_frame_free(&f);

    CHECK(fsp_send_error(sv[0], "PERM_SET", 403, "Bạn không có quyền \"ghi\"") == 0, "error");
    CHECK(fsp_recv_frame(sv[1], &f) == 0 && f.type == FSP_MSG_ERROR_RESP, "ERROR_RESP");
    char msg[128];
    CHECK(fsp_json_get_str((char *)f.payload, "error", msg, sizeof(msg)) == 0, "error field");
    CHECK(strcmp(msg, "Bạn không có quyền \"ghi\"") == 0, "message escaped and round-trips");
    long long status = 0;
    CHECK(fsp_json_get_int((char *)f.payload, "status", &status) == 0 && status == 403, "403");
    fsp_frame_free(&f);
    close(sv[0]);
    close(sv[1]);
    return 0;
}

int main(void) {
    int passed = 0;
    printf("=== PROBE 1: FRAMING 42-BYTE / STICKY PACKETS (C11) ===\n");
    RUN_TEST(test_single_frame_roundtrip, passed);
    RUN_TEST(test_sticky_two_frames, passed);
    RUN_TEST(test_fragmented_buffer, passed);
    RUN_TEST(test_malformed_frames, passed);
    RUN_TEST(test_send_guards, passed);
    RUN_TEST(test_recv_frame_in_three_pieces, passed);
    RUN_TEST(test_recv_rejects_bad_frame_on_socket, passed);
    RUN_TEST(test_response_helpers, passed);
    printf("ALL PACKET TESTS PASSED (%d/%d)\n", passed, passed);
    return 0;
}
