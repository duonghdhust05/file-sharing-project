#include "common/fsp_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static inline uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

void fsp_sha256(const void *data, size_t len, uint8_t hash[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t total_bits = (uint64_t)len * 8;

    /* Tính toán kích thước đệm sau khi padding */
    size_t rem = len % 64;
    size_t pad_len = (rem < 56) ? (56 - rem) : (120 - rem);
    size_t total_len = len + pad_len + 8;

    uint8_t block[64];
    uint32_t w[64];
    size_t offset = 0;

    while (offset < total_len) {
        /* Nạp dữ liệu vào block 64 byte */
        for (size_t i = 0; i < 64; ++i) {
            size_t curr = offset + i;
            if (curr < len) {
                block[i] = bytes[curr];
            } else if (curr == len) {
                block[i] = 0x80;
            } else if (curr < total_len - 8) {
                block[i] = 0x00;
            } else {
                int shift = (7 - (int)(curr - (total_len - 8))) * 8;
                block[i] = (uint8_t)((total_bits >> shift) & 0xFF);
            }
        }

        /* 16 từ đầu tiên */
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | ((uint32_t)block[i * 4 + 3]);
        }

        /* Mở rộng thành 64 từ */
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        /* Vòng nén chính */
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;

        offset += 64;
    }

    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = (uint8_t)(h[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

void fsp_sha256_hex(const void *data, size_t len, char hex[65]) {
    uint8_t hash[32];
    fsp_sha256(data, len, hash);
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex[i * 2] = digits[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = digits[hash[i] & 0x0F];
    }
    hex[64] = '\0';
}

int fsp_constant_time_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        diff |= (p1[i] ^ p2[i]);
    }
    return diff == 0 ? 0 : -1;
}

int fsp_random_bytes(void *buf, size_t n) {
    if (!buf || n == 0)
        return 0;

    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;

#if defined(__linux__) && defined(SYS_getrandom)
    while (got < n) {
        ssize_t r = getrandom(p + got, n - got, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r > 0) {
            got += (size_t)r;
        } else {
            break;
        }
    }
    if (got == n)
        return 0;
#endif

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;

    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

int fsp_random_hex(size_t n_bytes, char *out_hex, size_t out_size) {
    if (!out_hex || out_size < (n_bytes * 2 + 1))
        return -1;

    uint8_t *raw = (uint8_t *)malloc(n_bytes);
    if (!raw)
        return -1;

    if (fsp_random_bytes(raw, n_bytes) != 0) {
        free(raw);
        return -1;
    }

    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n_bytes; ++i) {
        out_hex[i * 2] = digits[(raw[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = digits[raw[i] & 0x0F];
    }
    out_hex[n_bytes * 2] = '\0';
    free(raw);
    return 0;
}
