/*
 * kern_session.c - Cookie-based in-memory session store
 *
 * Provides session management with random 32-byte hex session IDs,
 * stored in HttpOnly cookies with SameSite=Lax.
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global session store */
static kern_dict_t *g_session_store = NULL;

struct kern_session {
    char *id;
    kern_dict_t *data;
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

void kern_session_init(void) {
    if (g_session_store) {
        kern_dict_free(g_session_store);
    }
    g_session_store = kern_dict_new();
}

kern_session_t *kern_session_start(kern_req_t *req) {
    if (!req) return NULL;

    if (!g_session_store) {
        kern_session_init();
    }

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
                free(sid);
                return session;
            }
            /* Invalid session ID - will create a new one */
            free(sid);
            sid = NULL;
        }
    }

    /* Create new session */
    session = calloc(1, sizeof(kern_session_t));
    if (!session) return NULL;

    session->id = generate_session_id();
    if (!session->id) {
        free(session);
        return NULL;
    }
    session->data = kern_dict_new();

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
    kern_dict_free(session->data);
    free(session->id);
    free(session);

    kern_dict_del(g_session_store, session_id);
}

const char *kern_session_id(const kern_session_t *session) {
    if (!session) return NULL;
    return session->id;
}

const char *kern_session_cookie(const kern_session_t *session) {
    if (!session || !session->id) return NULL;

    /* Build the Set-Cookie header value */
    static char cookie_buf[256];
    snprintf(cookie_buf, sizeof(cookie_buf),
             "kern_session=%s; Path=/; HttpOnly; SameSite=Lax",
             session->id);
    return cookie_buf;
}
