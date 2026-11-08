#ifndef FSP_JSON_H
#define FSP_JSON_H

/*
 * JSON PHẲNG cho payload FSP (SDD §3.3): một object không lồng, không mảng; giá trị là
 * chuỗi, số nguyên, true/false hoặc null. Chỉ đọc object ở DÒNG ĐẦU của payload, các dòng
 * TSV phía sau (nếu có) được bỏ qua - người gọi tự tách bằng strchr(payload, '\n').
 *
 * Quy ước trả về chung: 0 = OK, -1 = không có key / sai kiểu / JSON dị dạng,
 *                       -2 = bộ đệm out quá nhỏ (out vẫn kết thúc '\0' nhưng không dùng được).
 */

#include <stdbool.h>
#include <stddef.h>

int fsp_json_get_str(const char *json, const char *key, char *out, size_t out_size);
int fsp_json_get_int(const char *json, const char *key, long long *out);
int fsp_json_get_bool(const char *json, const char *key, bool *out);
/** 1 nếu key tồn tại (kể cả giá trị null), 0 nếu không, -1 nếu JSON dị dạng. */
int fsp_json_has(const char *json, const char *key);
/** 0 nếu dòng đầu là một object JSON phẳng hợp lệ, -1 nếu không. */
int fsp_json_validate(const char *json);

/**
 * Escape chuỗi UTF-8 để nhúng vào giá trị chuỗi JSON (", \, ký tự điều khiển).
 * Giống snprintf: luôn kết thúc '\0' khi out_size > 0, trả về độ dài CẦN THIẾT (không kể '\0').
 */
size_t fsp_json_escape(const char *in, char *out, size_t out_size);

/*
 * Bộ ghi danh sách trường "k":v,"k2":v2 (KHÔNG có ngoặc nhọn) để truyền vào
 * fsp_send_response(). Tràn bộ đệm được ghi nhận trong overflow, không ghi quá cap.
 *
 *   char buf[256]; fsp_json_fields_t f; fsp_jf_init(&f, buf, sizeof(buf));
 *   fsp_jf_str(&f, "token", tok); fsp_jf_str(&f, "home", "/home/an");
 *   fsp_send_response(fd, req->event, FSP_STATUS_OK, f.overflow ? NULL : buf, NULL);
 */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} fsp_json_fields_t;

void fsp_jf_init(fsp_json_fields_t *f, char *buf, size_t cap);
void fsp_jf_str(fsp_json_fields_t *f, const char *key, const char *value);
void fsp_jf_int(fsp_json_fields_t *f, const char *key, long long value);
void fsp_jf_bool(fsp_json_fields_t *f, const char *key, bool value);

#endif /* FSP_JSON_H */
