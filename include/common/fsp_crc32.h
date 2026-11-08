#ifndef FSP_CRC32_H
#define FSP_CRC32_H

#include <stddef.h>
#include <stdint.h>

/** CRC-32 IEEE 802.3 của một vùng nhớ. Dữ liệu rỗng (len == 0) cho kết quả 0. */
uint32_t fsp_crc32(const void *data, size_t len);

/**
 * Tính CRC-32 nối tiếp nhiều đoạn: crc = fsp_crc32_update(0, a, la);
 * crc = fsp_crc32_update(crc, b, lb); -> bằng fsp_crc32(a ‖ b).
 */
uint32_t fsp_crc32_update(uint32_t crc, const void *data, size_t len);

#endif /* FSP_CRC32_H */
