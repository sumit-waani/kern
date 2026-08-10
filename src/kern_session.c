/*
 * kern_session.c - Cookie-based in-memory session store
 *
 * Provides session management with random 32-byte hex session IDs,
 * stored in HttpOnly cookies with SameSite=Lax and Secure flags.
 *
 * Features:
 * - TTL-based session expiration (default: 7 days = 604800 seconds)
 * - Maximum session cap (default: 10,000 sessions)
 * - Oldest-session eviction when cap is reached
 * - Secure cookie flag (always set for production safety)
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Configuration constants */
#define KERN_SESSION_TTL_DEFAULT     604800   /* 7 days in seconds */
#define KERN_SESSION_MAX_DEFAULT     10000    /* Maximum concurrent sessions */

/* Global session store */
static kern_dict_t *g_session_store = NULL;
static uint32_t g_session_ttl = KERN_SESSION_TTL_DEFAULT;
static uint32_t g_session_max = KERN_SESSION_MAX_DEFAULT;
static time_t g_next_eviction_time = 0;

struct kern_session {
    char *id;
    kern_dict_t *data;
    time_t created_at;
};

/* Generate a random hex string of the given byte length */
static char *generate_session_id(void) {
    unsigned char bytes[32];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return NULL;

    size_t read_count = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    if (read_count != sizeof(bytes)) return NULL;

    /* Hex encode: 32 bytes -> 64 hex chars */
    char *hex = malloc(65);
    if (!hex) return NULL;

    for (int i = 0; i < 32; i++) {
        snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    }
    hex[64] = '\0';
    return hex;
}

/* Extract cookie value from Cookie header */
static char *extract_cookie(const char *cookie_header, const char *name) {
    if (!cookie_header || !name) return NULL;

    size_t name_len = strlen(name);
    const char *p = cookie_header;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;

        /* Check if this cookie matches */
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            p += name_len + 1;
            const char *end = p;
            while (*end && *end != ';') end++;
            size_t val_len = (size_t)(end - p);
            char *val = malloc(val_len + 1);
            if (!val) return NULL;
            memcpy(val, p, val_len);
            val[val_len] = '\0';
            return val;
        }

        /* Skip to next cookie */
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
    return NULL;
}

/* Free a single session struct */
static void free_session(void *ptr) {
    kern_session_t *session = (kern_session_t *)ptr;
    if (!session) return;
    kern_dict_free(session->data);
    free(session->id);
    free(session);
}

/* Eviction context for finding expired/oldest sessions */
typedef struct {
    time_t now;
    uint32_t ttl;
    char **expired_ids;
    size_t expired_count;
    size_t expired_cap;
    /* For oldest-eviction */
    const char *oldest_id;
    time_t oldest_time;
} evict_ctx_t;

/* Iteration callback to find expired sessions */
static bool find_expired_cb(const char *key, void *value, void *userdata) {
    evict_ctx_t *ctx = (evict_ctx_t *)userdata;
    kern_session_t *session = (kern_session_t *)value;

    /* Check if expired */
    if ((ctx->now - session->created_at) > (time_t)ctx->ttl) {
        if (ctx->expired_count < ctx->expired_cap) {
            ctx->expired_ids[ctx->expired_count] = strdup(key);
            ctx->expired_count++;
        }
    }

    /* Track oldest session for eviction when at capacity */
    if (ctx->oldest_id == NULL || session->created_at < ctx->oldest_time) {
        ctx->oldest_id = key;
        ctx->oldest_time = session->created_at;
    }

    return true; /* continue iteration */
}

/* Evict expired sessions from the store */
static void evict_expired_sessions(void) {
    if (!g_session_store) return;

    /* Throttle: only run eviction scan at most once per minute */
    time_t now = time(NULL);
    if (now < g_next_eviction_time) return;
    g_next_eviction_time = now + 60;

    size_t count = kern_dict_count(g_session_store);
    if (count == 0) return;

    evict_ctx_t ctx;
    ctx.now = time(NULL);
    ctx.ttl = g_session_ttl;
    ctx.expired_cap = count;
    ctx.expired_ids = malloc(count * sizeof(char *));
    if (!ctx.expired_ids) return;
    ctx.expired_count = 0;
    ctx.oldest_id = NULL;
    ctx.oldest_time = 0;

    kern_dict_iter(g_session_store, find_expired_cb, &ctx);

    /* Remove expired sessions */
    for (size_t i = 0; i < ctx.expired_count; i++) {
        kern_session_t *session = kern_dict_get(g_session_store, ctx.expired_ids[i]);
        if (session) {
            free_session(session);
            kern_dict_del(g_session_store, ctx.expired_ids[i]);
        }
        free(ctx.expired_ids[i]);
    }
    free(ctx.expired_ids);
}

/* Evict oldest session to make room */
static void evict_oldest_session(void) {
    if (!g_session_store) return;

    size_t count = kern_dict_count(g_session_store);
    if (count == 0) return;

    evict_ctx_t ctx;
    ctx.now = time(NULL);
    ctx.ttl = g_session_ttl;
    ctx.expired_cap = 0;
    ctx.expired_ids = NULL;
    ctx.expired_count = 0;
    ctx.oldest_id = NULL;
    ctx.oldest_time = 0;

    kern_dict_iter(g_session_store, find_expired_cb, &ctx);

    if (ctx.oldest_id) {
        /* Copy the ID before deletion since it points into the dict */
        char *id_copy = strdup(ctx.oldest_id);
        if (id_copy) {
            kern_session_t *session = kern_dict_get(g_session_store, id_copy);
            if (session) {
                free_session(session);
                kern_dict_del(g_session_store, id_copy);
            }
            free(id_copy);
        }
    }
}

/* Iteration callback to free all sessions during re-init */
static bool free_all_sessions_cb(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    free_session(value);
    return true; /* continue iteration */
}

void kern_session_init(void) {
    if (g_session_store) {
        /* Free all session structs before destroying the dict */
        kern_dict_iter(g_session_store, free_all_sessions_cb, NULL);
        kern_dict_free(g_session_store);
    }
    g_session_store = kern_dict_new();
    g_session_ttl = KERN_SESSION_TTL_DEFAULT;
    g_session_max = KERN_SESSION_MAX_DEFAULT;
}

void kern_session_set_ttl(uint32_t ttl_seconds) {
    g_session_ttl = ttl_seconds;
}

void kern_session_set_max(uint32_t max_sessions) {
    g_session_max = max_sessions;
}

kern_session_t *kern_session_start(kern_req_t *req) {
    if (!req) return NULL;

    if (!g_session_store) {
        kern_session_init();
    }

    /* Evict expired sessions on each start call */
    evict_expired_sessions();

    /* Check for existing session cookie */
    const char *cookie_header = kern_header(req, "cookie");
    char *sid = NULL;
    kern_session_t *session = NULL;

    if (cookie_header) {
        sid = extract_cookie(cookie_header, "kern_session");
        if (sid) {
            /* Look up existing session */
            session = kern_dict_get(g_session_store, sid);
            if (session) {
                /* Check if this session is expired */
                time_t now = time(NULL);
                if ((now - session->created_at) > (time_t)g_session_ttl) {
                    /* Session expired, destroy it */
                    free_session(session);
                    kern_dict_del(g_session_store, sid);
                    session = NULL;
                } else {
                    free(sid);
                    return session;
                }
            }
            /* Invalid or expired session ID - will create a new one */
            free(sid);
            sid = NULL;
        }
    }

    /* Enforce max sessions cap */
    while (kern_dict_count(g_session_store) >= g_session_max) {
        evict_oldest_session();
    }

    /* Create new session */
    session = calloc(1, sizeof(kern_session_t));
    if (!session) return NULL;

    session->id = generate_session_id();
    if (!session->id) {
        free(session);
        return NULL;
    }
    session->data = kern_dict_new_with_free(free);
    session->created_at = time(NULL);

    /* Store in global store */
    kern_dict_set(g_session_store, session->id, session);

    return session;
}

const char *kern_session_get(const kern_session_t *session, const char *key) {
    if (!session || !key || !session->data) return NULL;
    return (const char *)kern_dict_get(session->data, key);
}

void kern_session_set(kern_session_t *session, const char *key, const char *value) {
    if (!session || !key || !session->data) return;
    /* Store a copy of the value */
    char *copy = value ? strdup(value) : NULL;
    kern_dict_set(session->data, key, copy);
}

void kern_session_destroy(const char *session_id) {
    if (!session_id || !g_session_store) return;

    kern_session_t *session = kern_dict_get(g_session_store, session_id);
    if (!session) return;

    /* Free session data */
    free_session(session);
    kern_dict_del(g_session_store, session_id);
}

const char *kern_session_id(const kern_session_t *session) {
    if (!session) return NULL;
    return session->id;
}

const char *kern_session_cookie(const kern_session_t *session) {
    if (!session || !session->id) return NULL;

    /* Build the Set-Cookie header value with Secure flag */
    static char cookie_buf[256];
    snprintf(cookie_buf, sizeof(cookie_buf),
             "kern_session=%s; Path=/; HttpOnly; SameSite=Lax; Secure",
             session->id);
    return cookie_buf;
}

time_t kern_session_created_at(const kern_session_t *session) {
    if (!session) return 0;
    return session->created_at;
}
