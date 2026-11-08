#include "server/fsp_session.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/fsp_sha256.h"

static pthread_mutex_t g_session_lock = PTHREAD_MUTEX_INITIALIZER;
static fsp_session_t *g_sessions = NULL;
static size_t g_session_count = 0;
static size_t g_session_cap = 0;

/* Sinh UUID v4 theo chuẩn RFC 4122 (36 ký tự) */
static int generate_uuid_v4(char *out, size_t out_size) {
    if (!out || out_size < 37)
        return -1;

    char hex[33];
    if (fsp_random_hex(16, hex, sizeof(hex)) != 0)
        return -1;

    hex[12] = '4';
    char v = hex[16];
    int v_val = (v >= 'a' && v <= 'f') ? (v - 'a' + 10) : (v - '0');
    static const char variant_digits[] = "89ab";
    hex[16] = variant_digits[v_val & 3];

    snprintf(out, out_size, "%.8s-%.4s-%.4s-%.4s-%.12s", hex, hex + 8, hex + 12, hex + 16,
             hex + 20);
    return 0;
}

int fsp_session_init(void) {
    pthread_mutex_lock(&g_session_lock);
    fsp_session_destroy_all();
    pthread_mutex_unlock(&g_session_lock);
    return 0;
}

void fsp_session_destroy_all(void) {
    if (g_sessions) {
        free(g_sessions);
        g_sessions = NULL;
    }
    g_session_count = 0;
    g_session_cap = 0;
}

int fsp_session_create(uint32_t user_id, const char *username, char *out_token, size_t out_size) {
    if (!out_token || out_size < 37)
        return -1;

    char token[65];
    if (generate_uuid_v4(token, sizeof(token)) != 0)
        return -1;

    pthread_mutex_lock(&g_session_lock);

    /* Cấp phát thêm bộ nhớ nếu đầy */
    if (g_session_count >= g_session_cap) {
        size_t new_cap = g_session_cap == 0 ? 32 : g_session_cap * 2;
        fsp_session_t *new_arr =
            (fsp_session_t *)realloc(g_sessions, new_cap * sizeof(fsp_session_t));
        if (!new_arr) {
            pthread_mutex_unlock(&g_session_lock);
            return -1;
        }
        g_sessions = new_arr;
        g_session_cap = new_cap;
    }

    fsp_session_t *s = &g_sessions[g_session_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->token, sizeof(s->token), "%s", token);
    s->user_id = user_id;
    if (username) {
        snprintf(s->username, sizeof(s->username), "%s", username);
    }
    snprintf(s->cwd, sizeof(s->cwd), "/");
    s->last_active = time(NULL);
    s->is_valid = true;

    snprintf(out_token, out_size, "%s", token);
    pthread_mutex_unlock(&g_session_lock);
    return 0;
}

int fsp_session_validate(const char *token, fsp_session_t *out_session) {
    if (!token || token[0] == '\0')
        return -1;

    pthread_mutex_lock(&g_session_lock);
    time_t now = time(NULL);

    for (size_t i = 0; i < g_session_count; ++i) {
        fsp_session_t *s = &g_sessions[i];
        if (s->is_valid && strcmp(s->token, token) == 0) {
            /* Kiểm tra quá hạn timeout */
            if (now - s->last_active > FSP_SESSION_TIMEOUT_S) {
                s->is_valid = false;
                /* Xóa session quá hạn */
                g_sessions[i] = g_sessions[--g_session_count];
                pthread_mutex_unlock(&g_session_lock);
                return -1;
            }

            s->last_active = now;
            if (out_session)
                *out_session = *s;
            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_session_lock);
    return -1;
}

int fsp_session_destroy(const char *token) {
    if (!token)
        return -1;

    pthread_mutex_lock(&g_session_lock);
    for (size_t i = 0; i < g_session_count; ++i) {
        if (strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i] = g_sessions[--g_session_count];
            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_session_lock);
    return -1;
}

int fsp_session_set_cwd(const char *token, const char *cwd) {
    if (!token || !cwd)
        return -1;

    pthread_mutex_lock(&g_session_lock);
    time_t now = time(NULL);

    for (size_t i = 0; i < g_session_count; ++i) {
        fsp_session_t *s = &g_sessions[i];
        if (s->is_valid && strcmp(s->token, token) == 0) {
            if (now - s->last_active > FSP_SESSION_TIMEOUT_S) {
                s->is_valid = false;
                g_sessions[i] = g_sessions[--g_session_count];
                pthread_mutex_unlock(&g_session_lock);
                return -1;
            }
            snprintf(s->cwd, sizeof(s->cwd), "%s", cwd);
            s->last_active = now;
            pthread_mutex_unlock(&g_session_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_session_lock);
    return -1;
}
