#ifndef FSP_TEST_UTIL_H
#define FSP_TEST_UTIL_H

/* Khung test tối giản cho CTest: mỗi hàm test trả 0 (pass) hoặc 1 (fail). */

#include <dirent.h>
#include <stdio.h>
#include <string.h>

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

/* Đếm số file descriptor đang mở qua /proc/self/fd (trả -1 nếu không hỗ trợ). */
static inline int fsp_test_count_open_fds(void) {
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    int self_fd = dirfd(d);
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (self_fd >= 0) {
            char self_buf[32];
            snprintf(self_buf, sizeof(self_buf), "%d", self_fd);
            if (strcmp(ent->d_name, self_buf) == 0)
                continue;
        }
        ++count;
    }
    closedir(d);
    return count;
}

/* Kiểm tra thư mục có còn sót file tạm *.part hay không (1 = còn, 0 = sạch, -1 = lỗi). */
static inline int fsp_test_has_part_files(const char *dir_path) {
    if (!dir_path)
        return -1;
    DIR *d = opendir(dir_path);
    if (!d)
        return -1;
    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 5 && strcmp(ent->d_name + len - 5, ".part") == 0) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

#define CHECK_NO_FD_LEAK(fds_before)                                     \
    do {                                                                 \
        int fsp_fds_now_ = fsp_test_count_open_fds();                    \
        if ((fds_before) >= 0 && fsp_fds_now_ >= 0) {                    \
            CHECK(fsp_fds_now_ == (fds_before), "File descriptor leak"); \
        }                                                                \
    } while (0)

#define CHECK_NO_PART_FILES(dir_path) \
    CHECK(fsp_test_has_part_files((dir_path)) == 0, "Leftover .part file found")

#endif /* FSP_TEST_UTIL_H */
