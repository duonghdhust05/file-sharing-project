#include "server/fsp_perm.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/fsp_json.h"
#include "server/fsp_user.h"

static pthread_mutex_t g_perm_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_perm_db_path[512] = {0};
static fsp_path_meta_t *g_metas = NULL;
static size_t g_meta_count = 0;
static size_t g_meta_cap = 0;

static void encode_acl(const fsp_acl_entry_t *acl, size_t count, char *out, size_t out_size) {
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    size_t written = 0;

    for (size_t i = 0; i < count; ++i) {
        char item[32];
        int n = snprintf(item, sizeof(item), "%s%u:%u", (i == 0 ? "" : ","), acl[i].user_id,
                         acl[i].mask);
        if (n > 0 && written + n < out_size) {
            strcat(out, item);
            written += n;
        }
    }
}

static size_t decode_acl(const char *str, fsp_acl_entry_t *acl, size_t max_acl) {
    if (!str || str[0] == '\0')
        return 0;

    size_t count = 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", str);

    char *token = strtok(buf, ",");
    while (token && count < max_acl) {
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            uint32_t uid = (uint32_t)strtoul(token, NULL, 10);
            uint8_t mask = (uint8_t)strtoul(colon + 1, NULL, 10);
            if (mask > 0 && mask <= 7) {
                acl[count].user_id = uid;
                acl[count].mask = mask;
                count++;
            }
        }
        token = strtok(NULL, ",");
    }
    return count;
}

static int append_meta_memory(const fsp_path_meta_t *m) {
    if (g_meta_count >= g_meta_cap) {
        size_t new_cap = g_meta_cap == 0 ? 32 : g_meta_cap * 2;
        fsp_path_meta_t *new_arr =
            (fsp_path_meta_t *)realloc(g_metas, new_cap * sizeof(fsp_path_meta_t));
        if (!new_arr)
            return -1;
        g_metas = new_arr;
        g_meta_cap = new_cap;
    }
    g_metas[g_meta_count++] = *m;
    return 0;
}

static int save_db_locked(void) {
    if (g_perm_db_path[0] == '\0')
        return -1;

    char tmp_path[550];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_perm_db_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f)
        return -1;

    for (size_t i = 0; i < g_meta_count; ++i) {
        const fsp_path_meta_t *m = &g_metas[i];
        char p_esc[512];
        fsp_json_escape(m->path, p_esc, sizeof(p_esc));

        char acl_str[256];
        encode_acl(m->acl, m->acl_count, acl_str, sizeof(acl_str));

        fprintf(f,
                "{\"path\":\"%s\",\"owner_id\":%u,\"group_id\":%u,\"perm_bits\":%u,\"sharing_"
                "level\":%d,\"acl\":\"%s\"}\n",
                p_esc, m->owner_id, m->group_id, (unsigned int)m->perm_bits,
                (int)m->sharing_level, acl_str);
    }

    if (fflush(f) != 0) {
        fclose(f);
        unlink(tmp_path);
        return -1;
    }
    fclose(f);

    if (rename(tmp_path, g_perm_db_path) != 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

static int load_db_locked(void) {
    FILE *f = fopen(g_perm_db_path, "r");
    if (!f)
        return 0;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n')) {
            line[--l] = '\0';
        }
        if (l == 0)
            continue;

        fsp_path_meta_t m;
        memset(&m, 0, sizeof(m));
        long long owner_id = 0, group_id = 0, perm_bits = 0, sharing_level = 0;
        char acl_str[256] = {0};

        if (fsp_json_get_str(line, "path", m.path, sizeof(m.path)) != 0 ||
            fsp_json_get_int(line, "owner_id", &owner_id) != 0 ||
            fsp_json_get_int(line, "group_id", &group_id) != 0 ||
            fsp_json_get_int(line, "perm_bits", &perm_bits) != 0 ||
            fsp_json_get_int(line, "sharing_level", &sharing_level) != 0) {
            fclose(f);
            return -1;
        }

        fsp_json_get_str(line, "acl", acl_str, sizeof(acl_str));

        m.owner_id = (uint32_t)owner_id;
        m.group_id = (uint32_t)group_id;
        m.perm_bits = (uint16_t)perm_bits;
        m.sharing_level = (fsp_sharing_level_t)sharing_level;
        m.acl_count = decode_acl(acl_str, m.acl, FSP_MAX_ACL_ENTRIES);

        if (append_meta_memory(&m) != 0) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

int fsp_perm_db_init(const char *db_path) {
    if (!db_path)
        return -1;

    pthread_mutex_lock(&g_perm_lock);
    fsp_perm_db_destroy();
    snprintf(g_perm_db_path, sizeof(g_perm_db_path), "%s", db_path);
    int rc = load_db_locked();
    pthread_mutex_unlock(&g_perm_lock);
    return rc;
}

void fsp_perm_db_destroy(void) {
    if (g_metas) {
        free(g_metas);
        g_metas = NULL;
    }
    g_meta_count = 0;
    g_meta_cap = 0;
    g_perm_db_path[0] = '\0';
}

static fsp_path_meta_t *find_meta_locked(const char *path) {
    for (size_t i = 0; i < g_meta_count; ++i) {
        if (strcmp(g_metas[i].path, path) == 0) {
            return &g_metas[i];
        }
    }
    return NULL;
}

static bool allowed_locked(uint32_t user_id, const fsp_path_meta_t *m, fsp_access_t access) {
    /* Bước 1: Admin toàn quyền */
    if (user_id == FSP_ADMIN_USER_ID)
        return true;

    /* Bước 2: Owner */
    if (user_id == m->owner_id) {
        /* Đọc (READ) và Đổi quyền (SHARE) luôn được phép đối với chủ sở hữu */
        if (access != FSP_ACCESS_WRITE)
            return true;
        /* Ghi (WRITE) tuân theo bit 0200 (OWNER_WRITE) */
        return (m->perm_bits & FSP_PERM_OWNER_WRITE) != 0;
    }

    /* Bước 3: Mức PRIVATE từ chối người ngoài, bất kể bit nhóm hay others */
    if (m->sharing_level == FSP_LEVEL_PRIVATE)
        return false;

    /* Bước 4: Kiểm tra sự tồn tại của người dùng */
    fsp_user_t u;
    if (fsp_user_find_by_id(user_id, &u) != 0)
        return false;

    /* Bước 5: Cùng nhóm */
    if (u.group_id == m->group_id) {
        if (access == FSP_ACCESS_READ && (m->perm_bits & FSP_PERM_GROUP_READ))
            return true;
        if (access == FSP_ACCESS_WRITE && (m->perm_bits & FSP_PERM_GROUP_WRITE))
            return true;
        if (access == FSP_ACCESS_SHARE && (m->perm_bits & FSP_PERM_GROUP_SHARE))
            return true;
    }

    /* Bước 6: Mức PUBLIC cho phép quyền ĐỌC (READ) */
    if (access == FSP_ACCESS_READ && m->sharing_level == FSP_LEVEL_PUBLIC)
        return true;

    /* Bước 7: Mức SHARED kiểm tra danh sách ACL */
    if (m->sharing_level == FSP_LEVEL_SHARED) {
        for (size_t i = 0; i < m->acl_count; ++i) {
            if (m->acl[i].user_id == user_id && (m->acl[i].mask & (uint8_t)access)) {
                return true;
            }
        }
    }

    /* Bước 8: Mặc định từ chối */
    return false;
}

bool fsp_can_access(uint32_t user_id, const char *path, fsp_access_t access) {
    if (!path)
        return false;

    pthread_mutex_lock(&g_perm_lock);
    const fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return false; /* Bước 0: Không tồn tại đường dẫn -> từ chối (fail-closed) */
    }

    bool res = allowed_locked(user_id, m, access);
    pthread_mutex_unlock(&g_perm_lock);
    return res;
}

int fsp_perm_register(const char *path, uint32_t owner_id, uint16_t perm_bits,
                      fsp_sharing_level_t level) {
    if (!path || perm_bits > 0777 || level > 2)
        return (int)FSP_STATUS_BAD_REQUEST;

    fsp_user_t owner;
    if (fsp_user_find_by_id(owner_id, &owner) != 0)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    if (find_meta_locked(path) != NULL) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_CONFLICT;
    }

    fsp_path_meta_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.path, sizeof(m.path), "%s", path);
    m.owner_id = owner_id;
    m.group_id = owner.group_id;
    m.perm_bits = perm_bits;
    m.sharing_level = level;
    m.acl_count = 0;

    if (append_meta_memory(&m) != 0) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    if (save_db_locked() != 0) {
        --g_meta_count;
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_CREATED;
}

int fsp_perm_remove(const char *path) {
    if (!path)
        return -1;

    pthread_mutex_lock(&g_perm_lock);
    for (size_t i = 0; i < g_meta_count; ++i) {
        if (strcmp(g_metas[i].path, path) == 0) {
            fsp_path_meta_t backup = g_metas[i];
            g_metas[i] = g_metas[--g_meta_count];
            if (save_db_locked() != 0) {
                g_metas[g_meta_count++] = backup;
                pthread_mutex_unlock(&g_perm_lock);
                return -1;
            }
            pthread_mutex_unlock(&g_perm_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_perm_lock);
    return -1;
}

int fsp_perm_rename(uint32_t actor_id, const char *old_path, const char *new_path) {
    if (!old_path || !new_path)
        return (int)FSP_STATUS_BAD_REQUEST;

    size_t old_len = strlen(old_path);
    char prefix[520];
    snprintf(prefix, sizeof(prefix), "%s/", old_path);
    size_t prefix_len = strlen(prefix);

    if (strcmp(old_path, new_path) == 0 || strncmp(new_path, prefix, prefix_len) == 0) {
        return (int)FSP_STATUS_BAD_REQUEST;
    }

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *root = find_meta_locked(old_path);
    if (!root) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, root, FSP_ACCESS_WRITE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    /* Kiểm tra xung đột ở đích */
    if (find_meta_locked(new_path) != NULL) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_CONFLICT;
    }

    /* Thực hiện di chuyển đường dẫn */
    for (size_t i = 0; i < g_meta_count; ++i) {
        if (strcmp(g_metas[i].path, old_path) == 0) {
            snprintf(g_metas[i].path, sizeof(g_metas[i].path), "%s", new_path);
        } else if (strncmp(g_metas[i].path, prefix, prefix_len) == 0) {
            char target[512];
            snprintf(target, sizeof(target), "%s%s", new_path, g_metas[i].path + old_len);
            snprintf(g_metas[i].path, sizeof(g_metas[i].path), "%s", target);
        }
    }

    if (save_db_locked() != 0) {
        load_db_locked(); /* Rollback từ đĩa */
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_chmod(uint32_t actor_id, const char *path, uint16_t perm_bits) {
    if (!path || perm_bits > 0777)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, m, FSP_ACCESS_SHARE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    uint16_t old = m->perm_bits;
    m->perm_bits = perm_bits;
    if (save_db_locked() != 0) {
        m->perm_bits = old;
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_chown(uint32_t actor_id, const char *path, uint32_t new_owner_id,
                   uint32_t new_group_id) {
    if (actor_id != FSP_ADMIN_USER_ID)
        return (int)FSP_STATUS_FORBIDDEN;
    if (new_group_id == 0)
        return (int)FSP_STATUS_BAD_REQUEST;

    fsp_user_t new_owner;
    if (fsp_user_find_by_id(new_owner_id, &new_owner) != 0)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }

    uint32_t old_o = m->owner_id, old_g = m->group_id;
    m->owner_id = new_owner_id;
    m->group_id = new_group_id;
    if (save_db_locked() != 0) {
        m->owner_id = old_o;
        m->group_id = old_g;
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_set_level(uint32_t actor_id, const char *path, fsp_sharing_level_t level) {
    if (!path || level > 2)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, m, FSP_ACCESS_SHARE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    fsp_sharing_level_t old = m->sharing_level;
    m->sharing_level = level;
    if (save_db_locked() != 0) {
        m->sharing_level = old;
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_grant(uint32_t actor_id, const char *path, uint32_t target_user, uint8_t mask) {
    if (!path || mask == 0 || mask > 7)
        return (int)FSP_STATUS_BAD_REQUEST;

    fsp_user_t target;
    if (fsp_user_find_by_id(target_user, &target) != 0)
        return (int)FSP_STATUS_NOT_FOUND;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, m, FSP_ACCESS_SHARE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    for (size_t i = 0; i < m->acl_count; ++i) {
        if (m->acl[i].user_id == target_user) {
            m->acl[i].mask = mask;
            save_db_locked();
            pthread_mutex_unlock(&g_perm_lock);
            return (int)FSP_STATUS_OK;
        }
    }

    if (m->acl_count < FSP_MAX_ACL_ENTRIES) {
        m->acl[m->acl_count].user_id = target_user;
        m->acl[m->acl_count].mask = mask;
        m->acl_count++;
        save_db_locked();
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_OK;
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_INTERNAL_SERVER_ERROR;
}

int fsp_perm_revoke(uint32_t actor_id, const char *path, uint32_t target_user) {
    if (!path)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, m, FSP_ACCESS_SHARE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    for (size_t i = 0; i < m->acl_count; ++i) {
        if (m->acl[i].user_id == target_user) {
            m->acl[i] = m->acl[--m->acl_count];
            save_db_locked();
            pthread_mutex_unlock(&g_perm_lock);
            return (int)FSP_STATUS_OK;
        }
    }

    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_set_recursive(uint32_t actor_id, const char *path, uint16_t perm_bits,
                           fsp_sharing_level_t level, bool recursive) {
    if (!path || perm_bits > 0777 || level > 2)
        return (int)FSP_STATUS_BAD_REQUEST;

    pthread_mutex_lock(&g_perm_lock);
    fsp_path_meta_t *root = find_meta_locked(path);
    if (!root) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_NOT_FOUND;
    }
    if (!allowed_locked(actor_id, root, FSP_ACCESS_SHARE)) {
        pthread_mutex_unlock(&g_perm_lock);
        return (int)FSP_STATUS_FORBIDDEN;
    }

    root->perm_bits = perm_bits;
    root->sharing_level = level;

    if (recursive) {
        char prefix[520];
        snprintf(prefix, sizeof(prefix), "%s/", path);
        size_t prefix_len = strlen(prefix);

        for (size_t i = 0; i < g_meta_count; ++i) {
            if (strncmp(g_metas[i].path, prefix, prefix_len) == 0) {
                g_metas[i].perm_bits = perm_bits;
                g_metas[i].sharing_level = level;
                /* Sao chép danh sách ACL nếu có */
                g_metas[i].acl_count = root->acl_count;
                memcpy(g_metas[i].acl, root->acl, sizeof(root->acl));
            }
        }
    }

    save_db_locked();
    pthread_mutex_unlock(&g_perm_lock);
    return (int)FSP_STATUS_OK;
}

int fsp_perm_get_meta(const char *path, fsp_path_meta_t *out_meta) {
    if (!path || !out_meta)
        return -1;

    pthread_mutex_lock(&g_perm_lock);
    const fsp_path_meta_t *m = find_meta_locked(path);
    if (!m) {
        pthread_mutex_unlock(&g_perm_lock);
        return -1;
    }
    *out_meta = *m;
    pthread_mutex_unlock(&g_perm_lock);
    return 0;
}
