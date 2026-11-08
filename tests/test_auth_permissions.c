#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/FSPProtocol.h"
#include "common/fsp_sha256.h"
#include "server/fsp_perm.h"
#include "server/fsp_session.h"
#include "server/fsp_user.h"
#include "test_util.h"

static int g_checks = 0;

/* 1. Kiểm thử SHA-256 theo NIST Test Vectors */
static int test_sha256(void) {
    char hex[65];

    /* Test 1: Chuỗi rỗng */
    fsp_sha256_hex("", 0, hex);
    CHECK(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
          "SHA256 rỗng sai");

    /* Test 2: "abc" */
    fsp_sha256_hex("abc", 3, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
          "SHA256 'abc' sai");

    /* Test 3: "123456789" */
    fsp_sha256_hex("123456789", 9, hex);
    CHECK(strcmp(hex, "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225") == 0,
          "SHA256 '123456789' sai");

    /* Test 4: So sánh constant-time */
    CHECK(fsp_constant_time_memcmp("secret123", "secret123", 9) == 0, "constant_time_memcmp bang");
    CHECK(fsp_constant_time_memcmp("secret123", "secret456", 9) != 0, "constant_time_memcmp khac");

    return 0;
}

/* 2. Kiểm thử Quản lý Tài khoản (UserManager) */
static int test_users(void) {
    char db_file[] = "/tmp/fsp_test_users_XXXXXX";
    int fd = mkstemp(db_file);
    close(fd);
    unlink(db_file);

    CHECK(fsp_user_db_init(db_file) == 0, "Init user db");

    uint32_t admin_id = 0, bob_id = 0;
    /* Người đầu tiên đăng ký phải là Admin (id = 1) */
    CHECK(fsp_user_register("admin", "adminpass", "Administrator", &admin_id) ==
              (int)FSP_STATUS_CREATED,
          "Register admin");
    CHECK(admin_id == FSP_ADMIN_USER_ID, "Admin ID phai la 1");

    /* Đăng ký Bob */
    CHECK(fsp_user_register("bob_smith", "password123", "Bob", &bob_id) == (int)FSP_STATUS_CREATED,
          "Register bob");
    CHECK(bob_id == 2, "Bob ID phai la 2");

    /* Đăng ký trùng tên -> 409 CONFLICT */
    CHECK(fsp_user_register("admin", "otherpass", "Fake Admin", NULL) == (int)FSP_STATUS_CONFLICT,
          "Reject duplicate username");

    /* Username không hợp lệ -> 400 BAD_REQUEST */
    CHECK(fsp_user_register("a", "short", NULL, NULL) == (int)FSP_STATUS_BAD_REQUEST,
          "Reject short username");

    /* Xác thực đăng nhập */
    fsp_user_t logged;
    CHECK(fsp_user_verify_login("admin", "adminpass", &logged) == (int)FSP_STATUS_OK,
          "Verify login admin");
    CHECK(logged.id == admin_id, "Logged id matches");
    CHECK(fsp_user_verify_login("admin", "wrongpass", NULL) == (int)FSP_STATUS_UNAUTHORIZED,
          "Reject wrong pass");
    CHECK(fsp_user_verify_login("nonexistent", "pass123", NULL) == (int)FSP_STATUS_UNAUTHORIZED,
          "Reject nonexistent user");

    /* Đọc lại từ đĩa */
    fsp_user_db_init(db_file);
    fsp_user_t bob_found;
    CHECK(fsp_user_find_by_name("bob_smith", &bob_found) == 0, "Find bob after reload");
    CHECK(bob_found.id == bob_id, "Bob ID preserved after reload");

    fsp_user_db_destroy();
    unlink(db_file);
    return 0;
}

/* 3. Kiểm thử Quản lý Phiên (SessionManager - Probe 2) */
static int test_sessions(void) {
    fsp_session_init();

    char tok[65];
    CHECK(fsp_session_create(2, "bob", tok, sizeof(tok)) == 0, "Create session");
    CHECK(strlen(tok) >= 32, "Token length >= 32");

    fsp_session_t sess;
    CHECK(fsp_session_validate(tok, &sess) == 0, "Validate valid token");
    CHECK(sess.user_id == 2, "Session user_id matches");
    CHECK(strcmp(sess.username, "bob") == 0, "Session username matches");

    /* Token giả mạo -> từ chối (Probe 2) */
    CHECK(fsp_session_validate("tok_forged_hacker_99", NULL) != 0,
          "Reject forged token (Probe 2)");

    /* Cập nhật thư mục CWD */
    CHECK(fsp_session_set_cwd(tok, "/home/bob/docs") == 0, "Set CWD");
    fsp_session_validate(tok, &sess);
    CHECK(strcmp(sess.cwd, "/home/bob/docs") == 0, "CWD updated");

    /* Hủy phiên (Logout) -> validate lại phải thất bại */
    CHECK(fsp_session_destroy(tok) == 0, "Destroy session");
    CHECK(fsp_session_validate(tok, NULL) != 0, "Validate destroyed session must fail");

    fsp_session_destroy_all();
    return 0;
}

/* 4. Kiểm thử Thuật toán Phân quyền 8 bước (PermissionEngine - Probe 3) */
static int test_permissions(void) {
    char user_db[] = "/tmp/fsp_test_perm_u_XXXXXX";
    int ufd = mkstemp(user_db);
    close(ufd);
    unlink(user_db);

    char perm_db[] = "/tmp/fsp_test_perm_p_XXXXXX";
    int pfd = mkstemp(perm_db);
    close(pfd);
    unlink(perm_db);

    fsp_user_db_init(user_db);
    fsp_perm_db_init(perm_db);

    uint32_t admin_id = 0, an_id = 0, binh_id = 0;
    fsp_user_register("admin", "password123", "Admin", &admin_id);
    fsp_user_register("an", "password123", "An", &an_id);
    fsp_user_register("binh", "password123", "Binh", &binh_id);

    /* Bước 0: Đường dẫn chưa đăng ký -> fail-closed DENY */
    CHECK(fsp_can_access(an_id, "/untracked/file.txt", FSP_ACCESS_READ) == false,
          "Untracked path denied (Step 0)");

    /* Đăng ký /home/an/secret.zip: level PRIVATE, perm 0666 */
    CHECK(fsp_perm_register("/home/an/secret.zip", an_id, 0666, FSP_LEVEL_PRIVATE) ==
              (int)FSP_STATUS_CREATED,
          "Register secret.zip");

    /* Bước 1: Admin toàn quyền */
    CHECK(fsp_can_access(admin_id, "/home/an/secret.zip", FSP_ACCESS_READ) == true,
          "Admin allowed READ (Step 1)");
    CHECK(fsp_can_access(admin_id, "/home/an/secret.zip", FSP_ACCESS_WRITE) == true,
          "Admin allowed WRITE (Step 1)");

    /* Bước 2: Chủ sở hữu An có quyền READ và SHARE */
    CHECK(fsp_can_access(an_id, "/home/an/secret.zip", FSP_ACCESS_READ) == true,
          "Owner allowed READ (Step 2)");
    CHECK(fsp_can_access(an_id, "/home/an/secret.zip", FSP_ACCESS_SHARE) == true,
          "Owner allowed SHARE (Step 2)");
    /* Vì bit 0666 có bit 0200 nên An có quyền WRITE */
    CHECK(fsp_can_access(an_id, "/home/an/secret.zip", FSP_ACCESS_WRITE) == true,
          "Owner allowed WRITE with bit 0200");

    /* Bước 3 (PROBE 3): Mức PRIVATE từ chối BÌNH mặc dù perm là 0666! */
    CHECK(fsp_can_access(binh_id, "/home/an/secret.zip", FSP_ACCESS_READ) == false,
          "PROBE 3: PRIVATE blocks outsider even with 0666 bit");
    CHECK(fsp_can_access(binh_id, "/home/an/secret.zip", FSP_ACCESS_WRITE) == false,
          "PROBE 3: PRIVATE blocks outsider write");

    /* Bước 6: PUBLIC level cho phép người khác ĐỌC */
    CHECK(fsp_perm_register("/public/readme.txt", an_id, 0644, FSP_LEVEL_PUBLIC) ==
              (int)FSP_STATUS_CREATED,
          "Register public file");
    CHECK(fsp_can_access(binh_id, "/public/readme.txt", FSP_ACCESS_READ) == true,
          "PUBLIC file allows outsider READ (Step 6)");
    CHECK(fsp_can_access(binh_id, "/public/readme.txt", FSP_ACCESS_WRITE) == false,
          "PUBLIC file denies outsider WRITE (Step 6)");

    /* Bước 7: SHARED level kèm danh sách ACL */
    CHECK(fsp_perm_register("/home/an/project.doc", an_id, 0644, FSP_LEVEL_SHARED) ==
              (int)FSP_STATUS_CREATED,
          "Register shared file");
    CHECK(fsp_can_access(binh_id, "/home/an/project.doc", FSP_ACCESS_READ) == false,
          "SHARED file denies before ACL grant");

    /* Cấp quyền READ cho Bình */
    CHECK(fsp_perm_grant(an_id, "/home/an/project.doc", binh_id, FSP_ACCESS_READ) ==
              (int)FSP_STATUS_OK,
          "Grant read to Binh");
    CHECK(fsp_can_access(binh_id, "/home/an/project.doc", FSP_ACCESS_READ) == true,
          "SHARED file allows Binh after grant (Step 7)");
    CHECK(fsp_can_access(binh_id, "/home/an/project.doc", FSP_ACCESS_WRITE) == false,
          "SHARED file denies ungranted write");

    /* Thu hồi quyền */
    CHECK(fsp_perm_revoke(an_id, "/home/an/project.doc", binh_id) == (int)FSP_STATUS_OK,
          "Revoke Binh");
    CHECK(fsp_can_access(binh_id, "/home/an/project.doc", FSP_ACCESS_READ) == false,
          "SHARED file denies Binh after revoke");

    fsp_perm_db_destroy();
    fsp_user_db_destroy();
    unlink(perm_db);
    unlink(user_db);
    return 0;
}

/* 5. Kiểm thử Đổi quyền Đệ quy (Recursive PERM_SET) */
static int test_recursive_perms(void) {
    char user_db[] = "/tmp/fsp_test_rec_u_XXXXXX";
    int ufd = mkstemp(user_db);
    close(ufd);
    unlink(user_db);

    char perm_db[] = "/tmp/fsp_test_rec_p_XXXXXX";
    int pfd = mkstemp(perm_db);
    close(pfd);
    unlink(perm_db);

    fsp_user_db_init(user_db);
    fsp_perm_db_init(perm_db);

    uint32_t an_id = 0, binh_id = 0;
    fsp_user_register("an", "pass123", "An", &an_id);
    fsp_user_register("binh", "pass123", "Binh", &binh_id);

    fsp_perm_register("/home/an/folder", an_id, 0755, FSP_LEVEL_PRIVATE);
    fsp_perm_register("/home/an/folder/sub1.txt", an_id, 0644, FSP_LEVEL_PRIVATE);
    fsp_perm_register("/home/an/folder/sub2.txt", an_id, 0644, FSP_LEVEL_PRIVATE);
    fsp_perm_register("/home/an/folder-other.txt", an_id, 0644, FSP_LEVEL_PRIVATE);

    /* Đổi quyền đệ quy /home/an/folder sang PUBLIC */
    CHECK(fsp_perm_set_recursive(an_id, "/home/an/folder", 0755, FSP_LEVEL_PUBLIC, true) ==
              (int)FSP_STATUS_OK,
          "Recursive perm_set to PUBLIC");

    /* Thư mục gốc và các con bên dưới phải thành PUBLIC */
    CHECK(fsp_can_access(binh_id, "/home/an/folder", FSP_ACCESS_READ) == true,
          "Folder is now public");
    CHECK(fsp_can_access(binh_id, "/home/an/folder/sub1.txt", FSP_ACCESS_READ) == true,
          "Child 1 is now public");
    CHECK(fsp_can_access(binh_id, "/home/an/folder/sub2.txt", FSP_ACCESS_READ) == true,
          "Child 2 is now public");

    /* Tệp cùng tiền tố text nhưng KHÔNG PHẢI con (/home/an/folder-other.txt) không bị ảnh hưởng */
    CHECK(fsp_can_access(binh_id, "/home/an/folder-other.txt", FSP_ACCESS_READ) == false,
          "Sibling prefix unaffected");

    fsp_perm_db_destroy();
    fsp_user_db_destroy();
    unlink(perm_db);
    unlink(user_db);
    return 0;
}

int main(void) {
    printf("====================================================\n");
    printf("  FSP M2 UNIT TESTS (C11 Auth, Session & Perms)     \n");
    printf("====================================================\n");

    RUN_TEST(test_sha256, g_checks);
    RUN_TEST(test_users, g_checks);
    RUN_TEST(test_sessions, g_checks);
    RUN_TEST(test_permissions, g_checks);
    RUN_TEST(test_recursive_perms, g_checks);

    printf("====================================================\n");
    printf("  ALL M2 UNIT TESTS PASSED SUCCESSFULLY! (%d/5)   \n", g_checks);
    printf("====================================================\n");
    return 0;
}
