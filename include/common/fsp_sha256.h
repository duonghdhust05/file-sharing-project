#ifndef FSP_SHA256_H
#define FSP_SHA256_H

/*
 * ============================================================================
 * FSP SHA-256 & CSPRNG Utilities (C11 - FIPS 180-4)
 * ============================================================================
 * Dùng chung cho:
 *  - M2: Băm mật khẩu (Salted SHA-256) và sinh salt/token an toàn.
 *  - M3: Băm toàn bộ nội dung file để đối soát sau khi tải (End-to-End Integrity).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Tính SHA-256 trả về 32 byte nhị phân */
void fsp_sha256(const void *data, size_t len, uint8_t hash[32]);

/** Tính SHA-256 trả về 64 ký tự hex chữ thường (kết thúc '\0', buffer cần >= 65 bytes) */
void fsp_sha256_hex(const void *data, size_t len, char hex[65]);

/** So sánh hai vùng nhớ với thời gian hằng (chống tấn công timing attack) */
int fsp_constant_time_memcmp(const void *a, const void *b, size_t n);

/** Sinh n byte ngẫu nhiên an toàn từ hệ điều hành (/dev/urandom) */
int fsp_random_bytes(void *buf, size_t n);

/** Sinh chuỗi hex ngẫu nhiên gồm (2 * n_bytes) ký tự (kết thúc '\0') */
int fsp_random_hex(size_t n_bytes, char *out_hex, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* FSP_SHA256_H */
