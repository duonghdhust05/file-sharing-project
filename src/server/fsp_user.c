#include "server/fsp_user.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/fsp_json.h"
#include "common/fsp_sha256.h"

typedef struct {
    char username[64];
    int count;
    time_t locked_until;
} failure_entry_t;

static pthread_mutex_t g_user_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_db_path[512] = {0};
static fsp_user_t *g_users = NULL;
static size_t g_user_count = 0;
static size_t g_user_cap = 0;

#define MAX_FAILURES 1000
static failure_entry_t g_failures[MAX_FAILURES];
static size_t g_failure_count = 0;

static bool valid_username(const char *u) {
    if (!u)
        return false;
    size_t len = strlen(u);
    if (len < 3 || len > 32)
        return false;
    for (size_t i = 0; i < len; ++i) {
        char c = u[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  (c == '_');
        if (!ok)
            return false;
    }
    return true;
}

static int append_user_memory(const fsp_user_t *u) {
    if (g_user_count >= g_user_cap) {
        size_t new_cap = g_user_cap == 0 ? 16 : g_user_cap * 2;
        fsp_user_t *new_arr = (fsp_user_t *)realloc(g_users, new_cap * sizeof(fsp_user_t));
        if (!new_arr)
            return -1;
        g_users = new_arr;
        g_user_cap = new_cap;
    }
    g_users[g_user_count++] = *u;
    return 0;
}

static int save_db_locked(void) {
    if (g_db_path[0] == '\0')
        return -1;

    char tmp_path[550];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_db_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f)
        return -1;

    for (size_t i = 0; i < g_user_count; ++i) {
        const fsp_user_t *u = &g_users[i];
        char u_esc[128], fn_esc[256];
        fsp_json_escape(u->username, u_esc, sizeof(u_esc));
        fsp_json_escape(u->full_name, fn_esc, sizeof(fn_esc));

        fprintf(f,
                "{\"id\":%u,\"username\":\"%s\",\"salt\":\"%s\",\"password_hash\":\"%s\",\"full_"
                "name\":\"%s\",\"group_id\":%u,\"created_at\":%lld}\n",
                u->id, u_esc, u->salt_hex, u->password_hash, fn_esc, u->group_id,
                (long long)u->created_at);
    }

    if (fflush(f) != 0) {
        fclose(f);
        unlink(tmp_path);
        return -1;
    }
    fclose(f);

    if (rename(tmp_path, g_db_path) != 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

static int load_db_locked(void) {
    FILE *f = fopen(g_db_path, "r");
    if (!f)
        return 0; /* File chưa tồn tại, bắt đầu rỗng */

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) {
            line[--l] = '\0';
        }
        if (l == 0)
            continue;

        fsp_user_t u;
        memset(&u, 0, sizeof(u));
        long long id = 0, group_id = 0, created_at = 0;

        if (fsp_json_get_int(line, "id", &id) != 0 ||
            fsp_json_get_str(line, "username", u.username, sizeof(u.username)) != 0 ||
            fsp_json_get_str(line, "salt", u.salt_hex, sizeof(u.salt_hex)) != 0 ||
            fsp_json_get_str(line, "password_hash", u.password_hash, sizeof(u.password_hash)) !=
                0 ||
            fsp_json_get_int(line, "group_id", &group_id) != 0 ||
            fsp_json_get_int(line, "created_at", &created_at) != 0) {
            fclose(f);
            return -1; /* Lỗi cú pháp dòng: fail-closed */
        }

        fsp_json_get_str(line, "full_name", u.full_name, sizeof(u.full_name));
        u.id = (uint32_t)id;
        u.group_id = (uint32_t)group_id;
        u.created_at = (time_t)created_at;

        if (append_user_memory(&u) != 0) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

int fsp_user_db_init(const char *db_path) {
    if (!db_path)
        return -1;

    pthread_mutex_lock(&g_user_lock);
    fsp_user_db_destroy();
    snprintf(g_db_path, sizeof(g_db_path), "%s", db_path);
    int rc = load_db_locked();
    pthread_mutex_unlock(&g_user_lock);
    return rc;
}

void fsp_user_db_destroy(void) {
    if (g_users) {
        free(g_users);
        g_users = NULL;
    }
    g_user_count = 0;
    g_user_cap = 0;
    g_failure_count = 0;
    g_db_path[0] = '\0';
}

int fsp_user_register(const char *username, const char *password, const char *full_name,
                      uint32_t *out_id) {
    if (!valid_username(username))
        return (int)FSP_STATUS_BAD_REQUEST;
    size_t pass_len = password ? strlen(password) : 0;
    if (pass_len < 6 || pass_len > 128)
        return (int)FSP_STATUS_BAD_REQUEST;
    if (full_name && strlen(full_name) > 100)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_user_lock);

    /* Kiểm tra trùng tên */
    for (size_t i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].username, username) == 0) {
            pthread_mutex_unlock(&g_user_lock);
            return (int)FSP_STATUS_CONFLICT;
        }
    }

    fsp_user_t u;
    memset(&u, 0, sizeof(u));

    /* User đầu tiên là Admin (id = 1) */
    u.id = (g_user_count == 0) ? FSP_ADMIN_USER_ID : (g_users[g_user_count - 1].id + 1);
    snprintf(u.username, sizeof(u.username), "%s", username);
    if (full_name) {
        snprintf(u.full_name, sizeof(u.full_name), "%s", full_name);
    }
    u.group_id = u.id; /* Nhóm mặc định bằng id của user */
    u.created_at = time(NULL);

    /* Sinh 16 byte salt ngẫu nhiên = 32 hex chars */
    if (fsp_random_hex(16, u.salt_hex, sizeof(u.salt_hex)) != 0) {
        pthread_mutex_unlock(&g_user_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    /* Băm salt + password */
    char salt_pass[256];
    snprintf(salt_pass, sizeof(salt_pass), "%s%s", u.salt_hex, password);
    fsp_sha256_hex(salt_pass, strlen(salt_pass), u.password_hash);

    if (append_user_memory(&u) != 0) {
        pthread_mutex_unlock(&g_user_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if (save_db_locked() != 0) {
        --g_user_count; /* Rollback */
        pthread_mutex_unlock(&g_user_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if (out_id)
        *out_id = u.id;
    pthread_mutex_unlock(&g_user_lock);
    return (int)FSP_STATUS_CREATED;
}

static bool is_locked_out_internal(const char *username, time_t now) {
    for (size_t i = 0; i < g_failure_count; ++i) {
        if (strcmp(g_failures[i].username, username) == 0) {
            return g_failures[i].locked_until > now;
        }
    }
    return false;
}

static void record_failure_internal(const char *username, time_t now) {
    for (size_t i = 0; i < g_failure_count; ++i) {
        if (strcmp(g_failures[i].username, username) == 0) {
            if (++g_failures[i].count >= FSP_MAX_FAILED_LOGINS) {
                g_failures[i].count = 0;
                g_failures[i].locked_until = now + FSP_LOCKOUT_SECONDS;
            }
            return;
        }
    }
    if (g_failure_count < MAX_FAILURES) {
        failure_entry_t *e = &g_failures[g_failure_count++];
        snprintf(e->username, sizeof(e->username), "%s", username);
        e->count = 1;
        e->locked_until = 0;
    }
}

static void clear_failure_internal(const char *username) {
    for (size_t i = 0; i < g_failure_count; ++i) {
        if (strcmp(g_failures[i].username, username) == 0) {
            g_failures[i] = g_failures[--g_failure_count];
            return;
        }
    }
}

bool fsp_user_is_locked_out(const char *username) {
    if (!username)
        return false;
    pthread_mutex_lock(&g_user_lock);
    bool locked = is_locked_out_internal(username, time(NULL));
    pthread_mutex_unlock(&g_user_lock);
    return locked;
}

int fsp_user_verify_login(const char *username, const char *password, fsp_user_t *out_user) {
    if (!username || !password)
        return (int)FSP_STATUS_UNAUTHORIZED;

    pthread_mutex_lock(&g_user_lock);
    time_t now = time(NULL);

    if (is_locked_out_internal(username, now)) {
        pthread_mutex_unlock(&g_user_lock);
        return (int)FSP_STATUS_UNAUTHORIZED;
    }

    const fsp_user_t *match = NULL;
    for (size_t i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].username, username) == 0) {
            match = &g_users[i];
            break;
        }
    }

    bool ok = false;
    if (match) {
        char salt_pass[256];
        snprintf(salt_pass, sizeof(salt_pass), "%s%s", match->salt_hex, password);
        char computed_hash[65];
        fsp_sha256_hex(salt_pass, strlen(salt_pass), computed_hash);
        ok = (fsp_constant_time_memcmp(computed_hash, match->password_hash, 64) == 0);
    } else {
        /* Tiêu thụ cùng lượng thời gian tính toán băm SHA-256 để chống timing attack */
        char dummy[65];
        fsp_sha256_hex(password, strlen(password), dummy);
    }

    if (ok) {
        clear_failure_internal(username);
        if (out_user)
            *out_user = *match;
        pthread_mutex_unlock(&g_user_lock);
        return (int)FSP_STATUS_OK;
    }

    record_failure_internal(username, now);
    pthread_mutex_unlock(&g_user_lock);
    return (int)FSP_STATUS_UNAUTHORIZED;
}

int fsp_user_find_by_id(uint32_t id, fsp_user_t *out_user) {
    pthread_mutex_lock(&g_user_lock);
    for (size_t i = 0; i < g_user_count; ++i) {
        if (g_users[i].id == id) {
            if (out_user)
                *out_user = g_users[i];
            pthread_mutex_unlock(&g_user_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_user_lock);
    return -1;
}

int fsp_user_find_by_name(const char *username, fsp_user_t *out_user) {
    if (!username)
        return -1;
    pthread_mutex_lock(&g_user_lock);
    for (size_t i = 0; i < g_user_count; ++i) {
        if (strcmp(g_users[i].username, username) == 0) {
            if (out_user)
                *out_user = g_users[i];
            pthread_mutex_unlock(&g_user_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_user_lock);
    return -1;
}

int fsp_user_set_group(uint32_t actor_id, uint32_t user_id, uint32_t group_id) {
    if (actor_id != FSP_ADMIN_USER_ID)
        return (int)FSP_STATUS_FORBIDDEN;
    if (group_id == 0)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_user_lock);
    for (size_t i = 0; i < g_user_count; ++i) {
        if (g_users[i].id == user_id) {
            uint32_t old_grp = g_users[i].group_id;
            g_users[i].group_id = group_id;
            if (save_db_locked() != 0) {
                g_users[i].group_id = old_grp;
                pthread_mutex_unlock(&g_user_lock);
                return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
            }
            pthread_mutex_unlock(&g_user_lock);
            return (int)FSP_STATUS_OK;
        }
    }
    pthread_mutex_unlock(&g_user_lock);
    return (int)FSP_STATUS_NOT_FOUND;
}
