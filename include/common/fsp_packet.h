#ifndef FSP_PACKET_H
#define FSP_PACKET_H

/*
 * Đóng / mở frame FSP 42 byte (M1.2 - SDD §4.1, §4.2).
 * Quy ước trả về: 0 = OK, -1 = phải đóng kết nối (mất kết nối hoặc frame dị dạng).
 */

#include <stddef.h>
#include <stdint.h>

#include "common/Config.h"
#include "common/FSPProtocol.h"

typedef struct {
    uint8_t type;                       /* fsp_msg_type_t */
    char event[FSP_EVENT_NAME_MAX_LEN]; /* luôn kết thúc '\0' */
    uint32_t payload_len;
    /* malloc bởi fsp_recv_frame / fsp_parse_frames với payload_len + 1 byte và luôn kết thúc
     * bằng '\0' (kể cả khi payload rỗng) -> dùng trực tiếp như chuỗi JSON được.
     * Người gọi giải phóng bằng fsp_frame_free(). */
    uint8_t *payload;
} fsp_frame_t;

/**
 * Gửi một frame: header 42 byte + payload trong MỘT lần sendmsg (tránh trễ do Nagle).
 * Từ chối (trả -1, không gửi gì) nếu event rỗng/dài >= 32 ký tự, len > FSP_BUFFER_SIZE
 * hoặc payload == NULL mà len > 0.
 */
int fsp_send_frame(int fd, uint8_t type, const char *event, const void *payload, uint32_t len);

/**
 * Nhận đúng 1 frame: đọc đủ 42 byte -> kiểm tra header -> đọc đủ payload_len byte ->
 * kiểm tra CRC32. Mọi sai lệch đều trả -1 (Fail-Closed) và không để lại bộ nhớ cần giải phóng.
 */
int fsp_recv_frame(int fd, fsp_frame_t *out);

/** Giải phóng payload và đưa frame về trạng thái rỗng. An toàn khi gọi nhiều lần. */
void fsp_frame_free(fsp_frame_t *f);

/**
 * Tách các frame HOÀN CHỈNH từ một buffer (Probe 1: dính gói / phân mảnh).
 * Frame cuối chưa đủ byte được giữ lại: *consumed chỉ tính các frame đã tách xong.
 * @return số frame đã tách (0..max_out), hoặc -1 nếu gặp frame dị dạng (khi đó không còn
 *         frame nào trong out cần giải phóng và *consumed = 0).
 */
int fsp_parse_frames(const uint8_t *buf, size_t len, fsp_frame_t *out, size_t max_out,
                     size_t *consumed);

/**
 * Kiểm tra header vừa nhận: magic == 0xAA, type thuộc 1..7, event có '\0' trong 32 byte,
 * payload_len <= FSP_BUFFER_SIZE. Ghi payload_len (đã ntohl) vào *out_len.
 */
int fsp_validate_header(const FSPHeader *hdr, uint32_t *out_len);

/**
 * Đóng gói frame vào bộ đệm có sẵn (dùng cho test và gửi gộp).
 * @return tổng số byte (42 + len), hoặc -1 nếu tham số sai / out_size không đủ.
 */
int fsp_pack_frame(uint8_t type, const char *event, const void *payload, uint32_t len, uint8_t *out,
                   size_t out_size);

/*
 * ----------------------------------------------------------------------------
 * Phản hồi theo quy ước SDD §3.3 - trường event lặp lại tên sự kiện của yêu cầu.
 * ----------------------------------------------------------------------------
 */

/**
 * EVENT_RESP: dòng đầu {"status":<status>[,<fields>]}, sau đó (nếu lines != NULL) là
 * '\n' + lines (các dòng TSV). fields là danh sách "k":v đã escape (xem fsp_json_fields_t).
 * Ví dụ: fsp_send_response(fd, "AUTH_LOGIN", 200, "\"token\":\"ab12\"", NULL)
 *        -> {"status":200,"token":"ab12"}
 */
int fsp_send_response(int fd, const char *event, int status, const char *fields, const char *lines);

/** ERROR_RESP: {"status":<status>,"error":"<message đã escape>"} */
int fsp_send_error(int fd, const char *event, int status, const char *message);

#endif /* FSP_PACKET_H */
