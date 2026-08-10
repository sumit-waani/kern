/*
 * kern_tailwind.c - Tailwind CSS JIT compiler
 *
 * Scans files for Tailwind class names and emits corresponding CSS.
 * No Node.js required - pure C implementation covering the most
 * commonly used Tailwind utility classes.
 */

#include "kern.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ============================================================
 * Color palette - Tailwind v4 colors
 * ============================================================ */

typedef struct {
    const char *name;
    const char *shades[11]; /* 50,100,200,300,400,500,600,700,800,900,950 */
} tw_palette_t;

static const tw_palette_t tw_palettes[] = {
    {"zinc", {"#fafafa", "#f4f4f5", "#e4e4e7", "#d4d4d8", "#a1a1aa", "#71717a", "#52525b", "#3f3f46", "#27272a", "#18181b", "#09090b"}},
    {"red", {"#fef2f2", "#fee2e2", "#fecaca", "#fca5a5", "#f87171", "#ef4444", "#dc2626", "#b91c1c", "#991b1b", "#7f1d1d", "#450a0a"}},
    {"green", {"#f0fdf4", "#dcfce7", "#bbf7d0", "#86efac", "#4ade80", "#22c55e", "#16a34a", "#15803d", "#166534", "#14532d", "#052e16"}},
    {"blue", {"#eff6ff", "#dbeafe", "#bfdbfe", "#93c5fd", "#60a5fa", "#3b82f6", "#2563eb", "#1d4ed8", "#1e40af", "#1e3a8a", "#172554"}},
    {"yellow", {"#fefce8", "#fef9c3", "#fef08a", "#fde047", "#facc15", "#eab308", "#ca8a04", "#a16207", "#854d0e", "#713f12", "#422006"}},
    {"purple", {"#faf5ff", "#f3e8ff", "#e9d5ff", "#d8b4fe", "#c084fc", "#a855f7", "#9333ea", "#7e22ce", "#6b21a8", "#581c87", "#3b0764"}},
    {"pink", {"#fdf2f8", "#fce7f3", "#fbcfe8", "#f9a8d4", "#f472b6", "#ec4899", "#db2777", "#be185d", "#9d174d", "#831843", "#500724"}},
    {"gray", {"#f9fafb", "#f3f4f6", "#e5e7eb", "#d1d5db", "#9ca3af", "#6b7280", "#4b5563", "#374151", "#1f2937", "#111827", "#030712"}},
};

#define TW_PALETTE_COUNT 8

static const int tw_shade_values[] = {50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950};
#define TW_SHADE_COUNT 11

/* Spacing scale: value * 0.25rem */
static const int tw_spacing_scale[] = {0,1,2,3,4,5,6,8,10,12,16,20,24,32,40,48,56,64};
#define TW_SPACING_COUNT 18

/* ============================================================
 * Class name scanner
 * ============================================================ */

static int is_class_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' ||
           c == ':' || c == '/' || c == '[' || c == ']' || c == '.';
}

static int is_valid_tw_token(const char *tok, size_t len) {
    if (len == 0 || len > 80) return 0;
    int has_alpha = 0;
    for (size_t i = 0; i < len; i++) {
        if (isalpha((unsigned char)tok[i])) { has_alpha = 1; break; }
    }
    return has_alpha;
}

int kern_tw_scan(const char *content, size_t len, kern_buf_t *classes) {
    if (!content || !classes) return -1;

    kern_dict_t *seen = kern_dict_new();
    if (!seen) return -1;

    size_t i = 0;
    while (i < len) {
        /* Skip non-class characters */
        while (i < len && !is_class_char(content[i])) i++;
        if (i >= len) break;

        /* Collect token */
        size_t start = i;
        while (i < len && is_class_char(content[i])) i++;
        size_t tok_len = i - start;

        if (is_valid_tw_token(content + start, tok_len)) {
            char tmp[128];
            if (tok_len < sizeof(tmp)) {
                memcpy(tmp, content + start, tok_len);
                tmp[tok_len] = '\0';
                if (!kern_dict_has(seen, tmp)) {
                    kern_dict_set(seen, tmp, (void*)1);
                    kern_buf_write(classes, content + start, tok_len);
                    kern_buf_writes(classes, "\n");
                }
            }
        }
    }

    kern_dict_free(seen);
    return 0;
}

int kern_tw_scan_file(const char *path, kern_buf_t *classes) {
    if (!path || !classes) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) { fclose(f); return 0; }

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return -1; }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);

    int rc = kern_tw_scan(buf, nread, classes);
    free(buf);
    return rc;
}

/* ============================================================
 * CSS compiler - helper functions
 * ============================================================ */

static int find_shade_index(int shade) {
    for (int i = 0; i < TW_SHADE_COUNT; i++) {
        if (tw_shade_values[i] == shade) return i;
    }
    return -1;
}

static const char *find_color(const char *palette_name, int shade) {
    int si = find_shade_index(shade);
    if (si < 0) return NULL;
    for (int i = 0; i < TW_PALETTE_COUNT; i++) {
        if (strcmp(tw_palettes[i].name, palette_name) == 0) {
            return tw_palettes[i].shades[si];
        }
    }
    return NULL;
}

static int is_spacing_value(int n) {
    for (int i = 0; i < TW_SPACING_COUNT; i++) {
        if (tw_spacing_scale[i] == n) return 1;
    }
    return 0;
}

/* Escape class name for CSS selector */
static void emit_selector(kern_buf_t *css, const char *class_name,
                           const char *pseudo, const char *media_prefix) {
    if (media_prefix) kern_buf_writef(css, "@media (min-width: %s) {\n", media_prefix);
    kern_buf_writes(css, ".");
    for (const char *p = class_name; *p; p++) {
        if (*p == ':' || *p == '/' || *p == '[' || *p == ']' || *p == '.') {
            kern_buf_writef(css, "\\%c", *p);
        } else {
            kern_buf_write(css, p, 1);
        }
    }
    if (pseudo) kern_buf_writes(css, pseudo);
    kern_buf_writes(css, " {\n");
}

static void emit_close(kern_buf_t *css, const char *media_prefix) {
    kern_buf_writes(css, "}\n");
    if (media_prefix) kern_buf_writes(css, "}\n");
}

static void emit_prop(kern_buf_t *css, const char *prop, const char *val) {
    kern_buf_writef(css, "  %s: %s;\n", prop, val);
}

/* Parse responsive/state prefix from class name. */
static const char *parse_prefix(const char *cls, const char **media_prefix,
                                 const char **pseudo) {
    *media_prefix = NULL;
    *pseudo = NULL;

    static const struct { const char *prefix; const char *mq; } responsive[] = {
        {"sm:", "640px"},
        {"md:", "768px"},
        {"lg:", "1024px"},
        {"xl:", "1280px"},
        {"2xl:", "1536px"},
    };

    const char *utility = cls;
    for (int i = 0; i < 5; i++) {
        size_t plen = strlen(responsive[i].prefix);
        if (strncmp(utility, responsive[i].prefix, plen) == 0) {
            *media_prefix = responsive[i].mq;
            utility += plen;
            break;
        }
    }

    static const struct { const char *prefix; const char *pseudo_sel; } states[] = {
        {"hover:", ":hover"},
        {"focus:", ":focus"},
        {"active:", ":active"},
        {"disabled:", ":disabled"},
        {"first:", ":first-child"},
        {"last:", ":last-child"},
    };

    for (int i = 0; i < 6; i++) {
        size_t plen = strlen(states[i].prefix);
        if (strncmp(utility, states[i].prefix, plen) == 0) {
            *pseudo = states[i].pseudo_sel;
            utility += plen;
            break;
        }
    }

    return utility;
}

/* Try to compile a single utility class. Returns 1 if handled, 0 if not. */
static int compile_utility(kern_buf_t *css, const char *full_class,
                            const char *utility, const char *media_prefix,
                            const char *pseudo) {
    char val_buf[64];
    int n;
    char palette[32];
    int shade;

    /* === Display === */
    if (strcmp(utility, "block") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "block"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "inline-block") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "inline-block"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "inline") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "inline"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "flex"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "inline-flex") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "inline-flex"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "grid") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "grid"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "table") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "table"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "hidden") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "display", "none"); emit_close(css, media_prefix); return 1; }

    /* === Position === */
    if (strcmp(utility, "relative") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "position", "relative"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "absolute") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "position", "absolute"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "fixed") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "position", "fixed"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "sticky") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "position", "sticky"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "static") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "position", "static"); emit_close(css, media_prefix); return 1; }

    /* Position offsets */
    if (strcmp(utility, "inset-0") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "top", "0px");
        emit_prop(css, "right", "0px");
        emit_prop(css, "bottom", "0px");
        emit_prop(css, "left", "0px");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "top-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "top", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "right-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "right", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "bottom-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "bottom", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "left-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "left", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "top-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "top", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "right-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "right", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "bottom-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "bottom", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "left-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "left", "auto"); emit_close(css, media_prefix); return 1; }

    /* Z-index */
    if (strcmp(utility, "z-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "0"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-10") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "10"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-20") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "20"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-30") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "30"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-40") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "40"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-50") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "50"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "z-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "z-index", "auto"); emit_close(css, media_prefix); return 1; }

    /* === Flexbox === */
    if (strcmp(utility, "flex-row") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-direction", "row"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-col") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-direction", "column"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-row-reverse") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-direction", "row-reverse"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-col-reverse") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-direction", "column-reverse"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-wrap") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-wrap", "wrap"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-nowrap") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-wrap", "nowrap"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-1") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex", "1 1 0%"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex", "1 1 auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex", "none"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "flex-initial") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex", "0 1 auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "grow") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-grow", "1"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "grow-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-grow", "0"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shrink") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-shrink", "1"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shrink-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "flex-shrink", "0"); emit_close(css, media_prefix); return 1; }

    /* Align items */
    if (strcmp(utility, "items-start") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "align-items", "flex-start"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "items-center") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "align-items", "center"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "items-end") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "align-items", "flex-end"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "items-stretch") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "align-items", "stretch"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "items-baseline") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "align-items", "baseline"); emit_close(css, media_prefix); return 1; }

    /* Justify content */
    if (strcmp(utility, "justify-start") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "flex-start"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "justify-center") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "center"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "justify-end") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "flex-end"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "justify-between") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "space-between"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "justify-around") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "space-around"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "justify-evenly") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "justify-content", "space-evenly"); emit_close(css, media_prefix); return 1; }

    /* === Spacing (padding) === */
    if (strncmp(utility, "p-", 2) == 0 && sscanf(utility + 2, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "padding", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "px-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "padding-left", val_buf); emit_prop(css, "padding-right", val_buf);
        emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "py-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "padding-top", val_buf); emit_prop(css, "padding-bottom", val_buf);
        emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "pt-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "padding-top", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "pr-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "padding-right", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "pb-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "padding-bottom", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "pl-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "padding-left", val_buf); emit_close(css, media_prefix); return 1;
    }

    /* === Spacing (margin) === */
    if (strcmp(utility, "m-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "mx-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-left", "auto"); emit_prop(css, "margin-right", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "my-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-top", "auto"); emit_prop(css, "margin-bottom", "auto"); emit_close(css, media_prefix); return 1; }
    if (strncmp(utility, "m-", 2) == 0 && sscanf(utility + 2, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "mx-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "margin-left", val_buf); emit_prop(css, "margin-right", val_buf);
        emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "my-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "margin-top", val_buf); emit_prop(css, "margin-bottom", val_buf);
        emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "mt-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-top", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "mr-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-right", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "mb-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-bottom", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strncmp(utility, "ml-", 3) == 0 && sscanf(utility + 3, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "margin-left", val_buf); emit_close(css, media_prefix); return 1;
    }

    /* Gap */
    if (strncmp(utility, "gap-", 4) == 0 && sscanf(utility + 4, "%d", &n) == 1 && is_spacing_value(n)) {
        snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "gap", val_buf); emit_close(css, media_prefix); return 1;
    }

    /* === Sizing === */
    if (strncmp(utility, "w-", 2) == 0) {
        const char *v = utility + 2;
        if (sscanf(v, "%d", &n) == 1 && is_spacing_value(n)) {
            snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
            emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", val_buf); emit_close(css, media_prefix); return 1;
        }
        if (strcmp(v, "full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "100%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "screen") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "100vw"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "auto"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/2") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "50%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/3") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "33.333333%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "2/3") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "66.666667%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/4") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "25%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "3/4") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "width", "75%"); emit_close(css, media_prefix); return 1; }
    }
    if (strncmp(utility, "h-", 2) == 0) {
        const char *v = utility + 2;
        if (sscanf(v, "%d", &n) == 1 && is_spacing_value(n)) {
            snprintf(val_buf, sizeof(val_buf), "%.2grem", n * 0.25);
            emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", val_buf); emit_close(css, media_prefix); return 1;
        }
        if (strcmp(v, "full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "100%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "screen") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "100vh"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "auto"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/2") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "50%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/3") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "33.333333%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "2/3") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "66.666667%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "1/4") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "25%"); emit_close(css, media_prefix); return 1; }
        if (strcmp(v, "3/4") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "height", "75%"); emit_close(css, media_prefix); return 1; }
    }

    /* Min/max width */
    if (strcmp(utility, "min-w-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "min-width", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "min-w-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "min-width", "100%"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-sm") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "24rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-md") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "28rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-lg") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "32rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "36rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-2xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "42rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-4xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "56rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "100%"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-screen-sm") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "640px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-screen-md") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "768px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-w-screen-lg") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-width", "1024px"); emit_close(css, media_prefix); return 1; }

    /* Min/max height */
    if (strcmp(utility, "min-h-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "min-height", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "min-h-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "min-height", "100%"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "min-h-screen") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "min-height", "100vh"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-h-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-height", "100%"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "max-h-screen") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "max-height", "100vh"); emit_close(css, media_prefix); return 1; }

    /* === Typography === */
    if (strcmp(utility, "text-xs") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "0.75rem");
        emit_prop(css, "line-height", "1rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-sm") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "0.875rem");
        emit_prop(css, "line-height", "1.25rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-base") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "1rem");
        emit_prop(css, "line-height", "1.5rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-lg") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "1.125rem");
        emit_prop(css, "line-height", "1.75rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-xl") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "1.25rem");
        emit_prop(css, "line-height", "1.75rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-2xl") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "1.5rem");
        emit_prop(css, "line-height", "2rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-3xl") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "1.875rem");
        emit_prop(css, "line-height", "2.25rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-4xl") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "2.25rem");
        emit_prop(css, "line-height", "2.5rem");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "text-5xl") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "font-size", "3rem");
        emit_prop(css, "line-height", "1");
        emit_close(css, media_prefix); return 1;
    }

    /* Font weight */
    if (strcmp(utility, "font-thin") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "100"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-light") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "300"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-normal") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "400"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-medium") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "500"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-semibold") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "600"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-bold") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "700"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "font-extrabold") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "font-weight", "800"); emit_close(css, media_prefix); return 1; }

    /* Line height */
    if (strcmp(utility, "leading-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "line-height", "1"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "leading-tight") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "line-height", "1.25"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "leading-normal") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "line-height", "1.5"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "leading-relaxed") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "line-height", "1.625"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "leading-loose") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "line-height", "2"); emit_close(css, media_prefix); return 1; }

    /* Letter spacing */
    if (strcmp(utility, "tracking-tight") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "letter-spacing", "-0.025em"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "tracking-normal") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "letter-spacing", "0em"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "tracking-wide") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "letter-spacing", "0.025em"); emit_close(css, media_prefix); return 1; }

    /* Text alignment */
    if (strcmp(utility, "text-left") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-align", "left"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "text-center") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-align", "center"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "text-right") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-align", "right"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "text-justify") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-align", "justify"); emit_close(css, media_prefix); return 1; }

    /* Text transform */
    if (strcmp(utility, "uppercase") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-transform", "uppercase"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "lowercase") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-transform", "lowercase"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "capitalize") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-transform", "capitalize"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "normal-case") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "text-transform", "none"); emit_close(css, media_prefix); return 1; }

    /* Whitespace / truncate */
    if (strcmp(utility, "truncate") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "overflow", "hidden");
        emit_prop(css, "text-overflow", "ellipsis");
        emit_prop(css, "white-space", "nowrap");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "whitespace-nowrap") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "white-space", "nowrap"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "whitespace-normal") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "white-space", "normal"); emit_close(css, media_prefix); return 1; }

    /* === Colors === */
    if (strncmp(utility, "bg-", 3) == 0) {
        const char *rest = utility + 3;
        if (strcmp(rest, "white") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "background-color", "#ffffff"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "black") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "background-color", "#000000"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "transparent") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "background-color", "transparent"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "current") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "background-color", "currentColor"); emit_close(css, media_prefix); return 1; }
        if (sscanf(rest, "%31[a-z]-%d", palette, &shade) == 2) {
            const char *hex = find_color(palette, shade);
            if (hex) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "background-color", hex); emit_close(css, media_prefix); return 1; }
        }
    }

    /* text-{color}-{shade} */
    if (strncmp(utility, "text-", 5) == 0) {
        const char *rest = utility + 5;
        if (strcmp(rest, "white") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "color", "#ffffff"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "black") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "color", "#000000"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "transparent") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "color", "transparent"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "current") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "color", "currentColor"); emit_close(css, media_prefix); return 1; }
        if (sscanf(rest, "%31[a-z]-%d", palette, &shade) == 2) {
            const char *hex = find_color(palette, shade);
            if (hex) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "color", hex); emit_close(css, media_prefix); return 1; }
        }
    }

    /* border-{color}-{shade} */
    if (strncmp(utility, "border-", 7) == 0) {
        const char *rest = utility + 7;
        if (strcmp(rest, "white") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-color", "#ffffff"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "black") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-color", "#000000"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "transparent") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-color", "transparent"); emit_close(css, media_prefix); return 1; }
        if (strcmp(rest, "current") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-color", "currentColor"); emit_close(css, media_prefix); return 1; }
        if (sscanf(rest, "%31[a-z]-%d", palette, &shade) == 2) {
            const char *hex = find_color(palette, shade);
            if (hex) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-color", hex); emit_close(css, media_prefix); return 1; }
        }
    }

    /* === Borders === */
    if (strcmp(utility, "border") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-width", "1px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-width", "0px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-2") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-width", "2px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-4") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-width", "4px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-8") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-width", "8px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-t") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-top-width", "1px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-r") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-right-width", "1px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-b") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-bottom-width", "1px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "border-l") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-left-width", "1px"); emit_close(css, media_prefix); return 1; }

    /* Border radius */
    if (strcmp(utility, "rounded") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0.25rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-sm") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0.125rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-md") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0.375rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-lg") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0.5rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0.75rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-2xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "1rem"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "9999px"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "rounded-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "border-radius", "0px"); emit_close(css, media_prefix); return 1; }

    /* === Grid === */
    if (strncmp(utility, "grid-cols-", 10) == 0 && sscanf(utility + 10, "%d", &n) == 1 && n >= 1 && n <= 12) {
        snprintf(val_buf, sizeof(val_buf), "repeat(%d, minmax(0, 1fr))", n);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "grid-template-columns", val_buf); emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "col-span-full") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "grid-column", "1 / -1"); emit_close(css, media_prefix); return 1; }
    if (strncmp(utility, "col-span-", 9) == 0 && sscanf(utility + 9, "%d", &n) == 1 && n >= 1 && n <= 12) {
        snprintf(val_buf, sizeof(val_buf), "span %d / span %d", n, n);
        emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "grid-column", val_buf); emit_close(css, media_prefix); return 1;
    }

    /* === Overflow === */
    if (strcmp(utility, "overflow-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-hidden") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow", "hidden"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-visible") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow", "visible"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-scroll") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow", "scroll"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-x-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow-x", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-y-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow-y", "auto"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-x-hidden") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow-x", "hidden"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "overflow-y-hidden") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "overflow-y", "hidden"); emit_close(css, media_prefix); return 1; }

    /* === Opacity === */
    if (strcmp(utility, "opacity-0") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "opacity", "0"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "opacity-25") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "opacity", "0.25"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "opacity-50") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "opacity", "0.5"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "opacity-75") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "opacity", "0.75"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "opacity-100") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "opacity", "1"); emit_close(css, media_prefix); return 1; }

    /* === Transitions === */
    if (strcmp(utility, "transition") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "transition-property", "color, background-color, border-color, text-decoration-color, fill, stroke, opacity, box-shadow, transform, filter, backdrop-filter");
        emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)");
        emit_prop(css, "transition-duration", "150ms");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "transition-all") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "transition-property", "all");
        emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)");
        emit_prop(css, "transition-duration", "150ms");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "transition-colors") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "transition-property", "color, background-color, border-color, text-decoration-color, fill, stroke");
        emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)");
        emit_prop(css, "transition-duration", "150ms");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "transition-opacity") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "transition-property", "opacity");
        emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)");
        emit_prop(css, "transition-duration", "150ms");
        emit_close(css, media_prefix); return 1;
    }
    if (strcmp(utility, "transition-transform") == 0) {
        emit_selector(css, full_class, pseudo, media_prefix);
        emit_prop(css, "transition-property", "transform");
        emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)");
        emit_prop(css, "transition-duration", "150ms");
        emit_close(css, media_prefix); return 1;
    }

    /* Duration */
    if (strcmp(utility, "duration-75") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "75ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-100") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "100ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-150") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "150ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-200") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "200ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-300") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "300ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-500") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "500ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-700") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "700ms"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "duration-1000") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-duration", "1000ms"); emit_close(css, media_prefix); return 1; }

    /* Easing */
    if (strcmp(utility, "ease-linear") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-timing-function", "linear"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "ease-in") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 1, 1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "ease-out") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-timing-function", "cubic-bezier(0, 0, 0.2, 1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "ease-in-out") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "transition-timing-function", "cubic-bezier(0.4, 0, 0.2, 1)"); emit_close(css, media_prefix); return 1; }

    /* === Shadows === */
    if (strcmp(utility, "shadow") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 1px 3px 0 rgb(0 0 0 / 0.1), 0 1px 2px -1px rgb(0 0 0 / 0.1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-sm") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 1px 2px 0 rgb(0 0 0 / 0.05)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-md") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 4px 6px -1px rgb(0 0 0 / 0.1), 0 2px 4px -2px rgb(0 0 0 / 0.1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-lg") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 10px 15px -3px rgb(0 0 0 / 0.1), 0 4px 6px -4px rgb(0 0 0 / 0.1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 20px 25px -5px rgb(0 0 0 / 0.1), 0 8px 10px -6px rgb(0 0 0 / 0.1)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-2xl") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 25px 50px -12px rgb(0 0 0 / 0.25)"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "shadow-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "box-shadow", "0 0 #0000"); emit_close(css, media_prefix); return 1; }

    /* === Cursor === */
    if (strcmp(utility, "cursor-pointer") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "cursor", "pointer"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "cursor-default") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "cursor", "default"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "cursor-not-allowed") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "cursor", "not-allowed"); emit_close(css, media_prefix); return 1; }

    /* === Pointer events === */
    if (strcmp(utility, "pointer-events-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "pointer-events", "none"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "pointer-events-auto") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "pointer-events", "auto"); emit_close(css, media_prefix); return 1; }

    /* === User select === */
    if (strcmp(utility, "select-none") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "user-select", "none"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "select-text") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "user-select", "text"); emit_close(css, media_prefix); return 1; }
    if (strcmp(utility, "select-all") == 0) { emit_selector(css, full_class, pseudo, media_prefix); emit_prop(css, "user-select", "all"); emit_close(css, media_prefix); return 1; }

    /* Not recognized */
    (void)n; (void)shade; (void)val_buf;
    (void)palette;
    return 0;
}

/* ============================================================
 * Main compile function
 * ============================================================ */

int kern_tw_compile(const char *classes, kern_buf_t *css) {
    if (!classes || !css) return -1;

    const char *p = classes;
    while (*p) {
        while (*p && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        size_t len = (size_t)(p - start);

        while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) len--;
        if (len == 0) continue;

        char cls[128];
        if (len >= sizeof(cls)) continue;
        memcpy(cls, start, len);
        cls[len] = '\0';

        const char *media_prefix = NULL;
        const char *pseudo = NULL;
        const char *utility = parse_prefix(cls, &media_prefix, &pseudo);

        compile_utility(css, cls, utility, media_prefix, pseudo);
    }

    return 0;
}

int kern_tw_compile_to_file(const char *classes, const char *output_path) {
    if (!classes || !output_path) return -1;

    kern_buf_t *css = kern_buf_new(4096);
    if (!css) return -1;

    int rc = kern_tw_compile(classes, css);
    if (rc != 0) {
        kern_buf_free(css);
        return rc;
    }

    FILE *f = fopen(output_path, "w");
    if (!f) {
        kern_buf_free(css);
        return -1;
    }

    const char *data = kern_buf_data(css);
    size_t len = kern_buf_len(css);
    if (len > 0) {
        fwrite(data, 1, len, f);
    }
    fclose(f);
    kern_buf_free(css);
    return 0;
}
