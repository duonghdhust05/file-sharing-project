#include "common/fsp_packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "common/fsp_crc32.h"
#include "common/fsp_json.h"
#include "common/fsp_socket.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Tên sự kiện hợp lệ để GỬI: không rỗng, tối đa 31 ký tự (còn chỗ cho '\0'). */
static int event_name_ok(const char *event) {
    if (!event)
        return 0;
    size_t n = strnlen(event, FSP_EVENT_NAME_MAX_LEN);
    return n > 0 && n < FSP_EVENT_NAME_MAX_LEN;
}

static void build_header(FSPHeader *hdr, uint8_t type, const char *event, const void *payload,
                         uint32_t len) {
    memset(hdr, 0, sizeof(*hdr)); /* phần dư của event được đệm 0 */
    hdr->magic = FSP_MAGIC_BYTE;
    hdr->type = type;
    hdr->payload_len = htonl(len);
    memcpy(hdr->event, event, strlen(event));
    hdr->checksum = htonl(len > 0 ? fsp_crc32(payload, len) : 0u);
}

static int send_params_ok(uint8_t type, const char *event, const void *payload, uint32_t len) {
    if (!event_name_ok(event) || len > FSP_BUFFER_SIZE || (len > 0 && !payload))
        return 0;
    return type >= FSP_MSG_EVENT_REQ && type <= FSP_MSG_PONG;
}

/* Gửi 2 vùng nhớ liên tiếp bằng sendmsg, lặp tới khi hết (xử lý gửi thiếu và EINTR). */
static int send_two(int fd, const void *a, size_t alen, const void *b, size_t blen) {
    struct iovec iov[2];
    iov[0].iov_base = (void *)a;
    iov[0].iov_len = alen;
    iov[1].iov_base = (void *)b;
    iov[1].iov_len = blen;
    int idx = 0;
    int cnt = blen > 0 ? 2 : 1;

    while (idx < cnt) {
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov[idx];
        msg.msg_iovlen = (size_t)(cnt - idx);
        ssize_t n = sendmsg(fd, &msg, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        size_t left = (size_t)n;
        while (idx < cnt && left >= iov[idx].iov_len) {
            left -= iov[idx].iov_len;
            ++idx;
        }
        if (idx < cnt) {
            iov[idx].iov_base = (uint8_t *)iov[idx].iov_base + left;
            iov[idx].iov_len -= left;
        }
    }
    return 0;
}

int fsp_send_frame(int fd, uint8_t type, const char *event, const void *payload, uint32_t len) {
    if (fd < 0 || !send_params_ok(type, event, payload, len))
        return -1;
    FSPHeader hdr;
    build_header(&hdr, type, event, payload, len);
    return send_two(fd, &hdr, sizeof(hdr), payload, len);
}

int fsp_pack_frame(uint8_t type, const char *event, const void *payload, uint32_t len, uint8_t *out,
                   size_t out_size) {
    if (!out || !send_params_ok(type, event, payload, len) ||
        out_size < (size_t)FSP_HEADER_SIZE + len)
        return -1;
    FSPHeader hdr;
    build_header(&hdr, type, event, payload, len);
    memcpy(out, &hdr, FSP_HEADER_SIZE);
    if (len > 0)
        memcpy(out + FSP_HEADER_SIZE, payload, len);
    return (int)(FSP_HEADER_SIZE + len);
}

int fsp_validate_header(const FSPHeader *hdr, uint32_t *out_len) {
    if (!hdr || hdr->magic != FSP_MAGIC_BYTE)
        return -1;
    if (hdr->type < FSP_MSG_EVENT_REQ || hdr->type > FSP_MSG_PONG)
        return -1;
    /* Thiếu '\0' trong 32 byte -> đọc event như chuỗi C sẽ tràn bộ đệm */
    if (memchr(hdr->event, '\0', FSP_EVENT_NAME_MAX_LEN) == NULL)
        return -1;
    uint32_t len = ntohl(hdr->payload_len);
    if (len > FSP_BUFFER_SIZE)
        return -1; /* chặn cấp phát bộ nhớ vô hạn */
    if (len == 0 && hdr->checksum != 0)
        return -1;
    if (out_len)
        *out_len = len;
    return 0;
}

/* Cấp phát payload (len + 1, kết thúc '\0'), sao chép, kiểm CRC rồi điền frame. */
static int fill_frame(fsp_frame_t *out, const FSPHeader *hdr, const uint8_t *data, uint32_t len) {
    uint8_t *payload = (uint8_t *)malloc((size_t)len + 1u);
    if (!payload)
        return -1;
    if (len > 0)
        memcpy(payload, data, len);
    payload[len] = '\0';
    if (fsp_crc32(payload, len) != ntohl(hdr->checksum)) {
        free(payload);
        return -1;
    }
    out->type = hdr->type;
    memcpy(out->event, hdr->event, FSP_EVENT_NAME_MAX_LEN);
    out->payload_len = len;
    out->payload = payload;
    return 0;
}

int fsp_recv_frame(int fd, fsp_frame_t *out) {
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    FSPHeader hdr;
    uint32_t len = 0;
    if (recv_exact(fd, &hdr, sizeof(hdr)) != 0)
        return -1;
    if (fsp_validate_header(&hdr, &len) != 0)
        return -1;

    uint8_t *payload = (uint8_t *)malloc((size_t)len + 1u);
    if (!payload)
        return -1;
    if (len > 0 && recv_exact(fd, payload, len) != 0) {
        free(payload);
        return -1;
    }
    payload[len] = '\0';
    if (fsp_crc32(payload, len) != ntohl(hdr.checksum)) {
        free(payload);
        return -1;
    }
    out->type = hdr.type;
    memcpy(out->event, hdr.event, FSP_EVENT_NAME_MAX_LEN);
    out->payload_len = len;
    out->payload = payload;
    return 0;
}

void fsp_frame_free(fsp_frame_t *f) {
    if (!f)
        return;
    free(f->payload);
    f->payload = NULL;
    f->payload_len = 0;
}

int fsp_parse_frames(const uint8_t *buf, size_t len, fsp_frame_t *out, size_t max_out,
                     size_t *consumed) {
    size_t off = 0;
    size_t count = 0;
    if (consumed)
        *consumed = 0;
    if ((!buf && len > 0) || (!out && max_out > 0))
        return -1;

    while (count < max_out && len - off >= FSP_HEADER_SIZE) {
        FSPHeader hdr;
        uint32_t plen = 0;
        memcpy(&hdr, buf + off, FSP_HEADER_SIZE); /* memcpy: tránh truy cập không căn lề */
        if (fsp_validate_header(&hdr, &plen) != 0)
            goto malformed;
        if (len - off - FSP_HEADER_SIZE < plen)
            break; /* frame chưa nhận đủ: chờ thêm byte */
        memset(&out[count], 0, sizeof(out[count]));
        if (fill_frame(&out[count], &hdr, buf + off + FSP_HEADER_SIZE, plen) != 0)
            goto malformed;
        ++count;
        off += FSP_HEADER_SIZE + plen;
    }
    if (consumed)
        *consumed = off;
    return (int)count;

malformed:
    for (size_t i = 0; i < count; ++i)
        fsp_frame_free(&out[i]);
    return -1;
}

int fsp_send_response(int fd, const char *event, int status, const char *fields,
                      const char *lines) {
    size_t flen = fields ? strlen(fields) : 0;
    size_t llen = lines ? strlen(lines) : 0;
    size_t cap = 32 + flen + 1 + llen + 1;
    if (cap > FSP_BUFFER_SIZE + 1u)
        return -1;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return -1;
    int n = snprintf(buf, cap, "{\"status\":%d%s%s}%s%s", status, flen ? "," : "",
                     flen ? fields : "", lines ? "\n" : "", lines ? lines : "");
    int rc = -1;
    if (n > 0 && (size_t)n < cap)
        rc = fsp_send_frame(fd, FSP_MSG_EVENT_RESP, event, buf, (uint32_t)n);
    free(buf);
    return rc;
}

int fsp_send_error(int fd, const char *event, int status, const char *message) {
    char escaped[512];
    char buf[640];
    fsp_json_escape(message ? message : "", escaped, sizeof(escaped));
    int n = snprintf(buf, sizeof(buf), "{\"status\":%d,\"error\":\"%s\"}", status, escaped);
    if (n < 0 || (size_t)n >= sizeof(buf))
        return -1;
    return fsp_send_frame(fd, FSP_MSG_ERROR_RESP, event, buf, (uint32_t)n);
}
