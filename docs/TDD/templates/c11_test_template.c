/**
 * @file c11_test_template.c
 * @brief Mẫu kiểm thử C11 4 tầng cho CTest trong dự án FSP (dùng tests/test_util.h).
 * @note TUYỆT ĐỐI KHÔNG dùng fake assertion kiểu CHECK(1, "...").
 *       Mọi kiểm tra phải đối chiếu dữ liệu thật trên buffer, socket hoặc đĩa,
 *       và gắn thẻ @covers tới UC / FR / NFR tương ứng.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/Config.h"
#include "common/FSPProtocol.h"
#include "common/fsp_crc32.h"
#include "common/fsp_packet.h"
#include "common/fsp_socket.h"
#include "server/fsp_dispatcher.h"
#include "server/fsp_server.h"
#include "test_util.h"

/* ---------------------------------------------------------------------------
 * Tầng 1: Pure Unit Test (Zero-IO / Sans-IO)
 * @covers: UC08, FR-3.1, NFR-A2, NFR-A4
 * ------------------------------------------------------------------------- */
static int test_tier1_sans_io_contract(void) {
    CHECK(sizeof(FSPHeader) == FSP_HEADER_SIZE, "FSPHeader phai dung 42 byte");
    CHECK(sizeof(FileChunkHeader) == FSP_CHUNK_HEADER_SIZE, "FileChunkHeader phai dung 88 byte");

    uint8_t buf[128];
    memset(buf, 0x5A, sizeof(buf));
    uint32_t orig_crc = fsp_crc32(buf, sizeof(buf));
    buf[64] ^= 0x01; /* Lật đúng 1 bit */
    CHECK(fsp_crc32(buf, sizeof(buf)) != orig_crc, "Lat 1 bit phai phat hien sai CRC32");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Tầng 2: Component Socket/FS Test (socketpair + mkdtemp + .part Hygiene)
 * @covers: UC08, FR-3.4, NFR-A3, NFR-D1
 * ------------------------------------------------------------------------- */
static int test_tier2_socketpair_and_sandbox(void) {
    char sandbox_dir[] = "/tmp/fsp_tpl_XXXXXX";
    CHECK(mkdtemp(sandbox_dir) != NULL, "Tao thu muc sandbox tam");

    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "Tao socketpair");

    /* Mô phỏng gửi nửa header rồi ngắt kết nối đột ngột (Fail-Closed) */
    uint8_t partial[16];
    memset(partial, 0xAA, sizeof(partial));
    CHECK(send_exact(sv[0], partial, sizeof(partial)) == 0, "Gui 16 byte dau");
    close(sv[0]);

    fsp_frame_t frame;
    CHECK(fsp_recv_frame(sv[1], &frame) == -1, "Ngat giua header phai that bai");
    close(sv[1]);

    CHECK_NO_PART_FILES(sandbox_dir);
    rmdir(sandbox_dir);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Tầng 3: In-Process Loopback Server (port = 0)
 * @covers: UC02, NFR-C1, NFR-D2
 * ------------------------------------------------------------------------- */
static int test_tier3_loopback_server(void) {
    dispatcher_reset();
    fsp_server_t *srv = fsp_server_create(0, 16);
    CHECK(srv != NULL, "Khoi tao server tren port dong");
    fsp_server_set_verbose(srv, 0);

    uint16_t port = fsp_server_port(srv);
    CHECK(port != 0, "Port dong khac 0");

    fsp_server_destroy(srv);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Tầng 4: Resource & FD Hygiene
 * @covers: NFR-G1, NFR-D1
 * ------------------------------------------------------------------------- */
static int test_tier4_fd_hygiene(void) {
    int fds_before = fsp_test_count_open_fds();
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "Mo socketpair");
    close(sv[0]);
    close(sv[1]);
    CHECK_NO_FD_LEAK(fds_before);
    return 0;
}

int main(void) {
    int passed = 0;
    RUN_TEST(test_tier1_sans_io_contract, passed);
    RUN_TEST(test_tier2_socketpair_and_sandbox, passed);
    RUN_TEST(test_tier3_loopback_server, passed);
    RUN_TEST(test_tier4_fd_hygiene, passed);
    printf("All %d template checks passed.\n", passed);
    return 0;
}
