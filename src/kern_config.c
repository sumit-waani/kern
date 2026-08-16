/*
 * kern_config.c - Minimal TOML configuration parser
 *
 * Supports: sections ([section]), strings (quoted and bare),
 * integers, booleans, dot-notation access, and ${env:VAR} expansion.
 */

#include "kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Config entry value types */
typedef enum {
    CFG_STR,
    CFG_INT,
    CFG_BOOL
} kern_config_val_type_t;

typedef struct {
    char *key;      /* Full dotted key: "section.name" */
    kern_config_val_type_t type;
    union {
        char *s;
        int64_t i;
        bool b;
    } val;
} kern_config_entry_t;

struct kern_config {
    kern_config_entry_t *entries;
    int count;
    int cap;
};

static void config_add(kern_config_t *cfg, const char *key,
                       kern_config_val_type_t type, const void *val) {
    if (cfg->count >= cfg->cap) {
        int new_cap = cfg->cap == 0 ? 32 : cfg->cap * 2;
        kern_config_entry_t *new_entries = realloc(cfg->entries,
            (size_t)new_cap * sizeof(kern_config_entry_t));
        if (!new_entries) return;
        cfg->entries = new_entries;
        cfg->cap = new_cap;
    }

    kern_config_entry_t *e = &cfg->entries[cfg->count];
    e->key = strdup(key);
    e->type = type;

    switch (type) {
        case CFG_STR:
            e->val.s = strdup((const char *)val);
            break;
        case CFG_INT:
            e->val.i = *(const int64_t *)val;
            break;
        case CFG_BOOL:
            e->val.b = *(const bool *)val;
            break;
    }
    cfg->count++;
}

/* Expand ${env:VAR_NAME} in a string */
static char *expand_env(const char *str) {
    kern_buf_t *buf = kern_buf_new(128);
    if (!buf) return strdup(str);

    const char *p = str;
    while (*p) {
        if (p[0] == '$' && p[1] == '{' && strncmp(p + 2, "env:", 4) == 0) {
            /* Find the closing } */
            const char *start = p + 6;  /* After "${env:" */
            const char *end = strchr(start, '}');
            if (end) {
                size_t var_len = (size_t)(end - start);
                char *var_name = malloc(var_len + 1);
                if (var_name) {
                    memcpy(var_name, start, var_len);
                    var_name[var_len] = '\0';

                    const char *env_val = getenv(var_name);
                    if (env_val) {
                        kern_buf_writes(buf, env_val);
                    }
                    free(var_name);
                }
                p = end + 1;
                continue;
            }
        }
        kern_buf_write(buf, p, 1);
        p++;
    }

    char *result = strdup(kern_buf_data(buf));
    kern_buf_free(buf);
    return result;
}

/* Skip whitespace */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

kern_config_t *kern_config_load(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    kern_config_t *cfg = calloc(1, sizeof(kern_config_t));
    if (!cfg) {
        fclose(f);
        return NULL;
    }

    char section[256] = "";
    char line[4096];

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        const char *p = skip_ws(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#') continue;

        /* Section header: [section] */
        if (*p == '[') {
            p++;
            const char *end = strchr(p, ']');
            if (end) {
                size_t slen = (size_t)(end - p);
                if (slen < sizeof(section)) {
                    memcpy(section, p, slen);
                    section[slen] = '\0';
                }
            }
            continue;
        }

        /* Key = value */
        const char *eq = strchr(p, '=');
        if (!eq) continue;

        /* Extract key (trim whitespace) */
        size_t key_len = (size_t)(eq - p);
        while (key_len > 0 && (p[key_len - 1] == ' ' || p[key_len - 1] == '\t')) {
            key_len--;
        }

        char key_buf[512];
        if (section[0]) {
            snprintf(key_buf, sizeof(key_buf), "%s.%.*s", section, (int)key_len, p);
        } else {
            snprintf(key_buf, sizeof(key_buf), "%.*s", (int)key_len, p);
        }

        /* Extract value */
        const char *val_start = skip_ws(eq + 1);

        /* Determine value type */
        if (*val_start == '"') {
            /* Quoted string */
            val_start++;
            const char *val_end = strrchr(val_start, '"');
            size_t vlen;
            if (val_end && val_end >= val_start) {
                vlen = (size_t)(val_end - val_start);
            } else {
                vlen = strlen(val_start);
            }
            char *raw = malloc(vlen + 1);
            if (raw) {
                memcpy(raw, val_start, vlen);
                raw[vlen] = '\0';
                char *expanded = expand_env(raw);
                config_add(cfg, key_buf, CFG_STR, expanded);
                free(expanded);
                free(raw);
            }
        } else if (strcmp(val_start, "true") == 0) {
            bool b = true;
            config_add(cfg, key_buf, CFG_BOOL, &b);
        } else if (strcmp(val_start, "false") == 0) {
            bool b = false;
            config_add(cfg, key_buf, CFG_BOOL, &b);
        } else if (*val_start == '-' || (*val_start >= '0' && *val_start <= '9')) {
            /* Integer */
            int64_t ival = strtoll(val_start, NULL, 10);
            config_add(cfg, key_buf, CFG_INT, &ival);
        } else {
            /* Bare string (unquoted) */
            /* Trim trailing whitespace and inline comments */
            char *bare = strdup(val_start);
            if (bare) {
                size_t blen = strlen(bare);
                while (blen > 0 && (bare[blen - 1] == ' ' || bare[blen - 1] == '\t' ||
                                    bare[blen - 1] == '\n' || bare[blen - 1] == '\r')) {
                    bare[--blen] = '\0';
                }
                char *expanded = expand_env(bare);
                config_add(cfg, key_buf, CFG_STR, expanded);
                free(expanded);
                free(bare);
            }
        }
    }

    fclose(f);
    return cfg;
}

const char *kern_config_get_str(const kern_config_t *cfg, const char *key) {
    if (!cfg || !key) return NULL;
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            if (cfg->entries[i].type == CFG_STR) {
                return cfg->entries[i].val.s;
            }
            return NULL;
        }
    }
    return NULL;
}

int64_t kern_config_get_int(const kern_config_t *cfg, const char *key) {
    if (!cfg || !key) return 0;
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            if (cfg->entries[i].type == CFG_INT) {
                return cfg->entries[i].val.i;
            }
            return 0;
        }
    }
    return 0;
}

bool kern_config_get_bool(const kern_config_t *cfg, const char *key) {
    if (!cfg || !key) return false;
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            if (cfg->entries[i].type == CFG_BOOL) {
                return cfg->entries[i].val.b;
            }
            return false;
        }
    }
    return false;
}

void kern_config_free(kern_config_t *cfg) {
    if (!cfg) return;
    for (int i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].key);
        if (cfg->entries[i].type == CFG_STR) {
            free(cfg->entries[i].val.s);
        }
    }
    free(cfg->entries);
    free(cfg);
}
