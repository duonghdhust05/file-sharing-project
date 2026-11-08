#ifndef FSP_TEST_UTIL_H
#define FSP_TEST_UTIL_H

/* Khung test tối giản cho CTest: mỗi hàm test trả 0 (pass) hoặc 1 (fail). */

#include <stdio.h>

#define CHECK(cond, msg)                                                                    \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            fprintf(stderr, "[FAIL] %s (%s) at %s:%d\n", (msg), #cond, __FILE__, __LINE__); \
            return 1;                                                                       \
        }                                                                                   \
    } while (0)

#define RUN_TEST(fn, counter)       \
    do {                            \
        printf("[TEST] %s\n", #fn); \
        fflush(stdout);             \
        if ((fn)() != 0)            \
            return 1;               \
        printf("  -> PASSED\n");    \
        ++(counter);                \
    } while (0)

#endif /* FSP_TEST_UTIL_H */
