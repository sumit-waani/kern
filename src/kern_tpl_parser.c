/*
 * kern_tpl_parser.c - .khtml template parser
 *
 * Parses Pug-style indentation-based template files into an AST.
 * Handles elements, attributes, text interpolation, C statements,
 * includes, extends/blocks, and doctype directives.
 */

#include "kern.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Internal structures
 * ============================================================ */

/* Maximum children per node (growable) */
#define INITIAL_CHILDREN_CAP 8
#define MAX_ATTRS 32
#define MAX_LINE_LEN 2048

/* Void (self-closing) HTML tags */
static const char *void_tags[] = {
    "img", "br", "hr", "input", "link", "meta",
    "area", "base", "col", "embed", "source", "track", "wbr",
    NULL
};

static bool is_void_tag(const char *tag) {
    for (int i = 0; void_tags[i]; i++) {
        if (strcmp(tag, void_tags[i]) == 0) return true;
    }
    return false;
}

/* ============================================================
 * Node allocation helpers
 * ============================================================ */

static kern_tpl_node_t *node_new(kern_tpl_node_type_t type) {
    kern_tpl_node_t *node = calloc(1, sizeof(kern_tpl_node_t));
    if (!node) return NULL;
    node->type = type;
    node->children_cap = INITIAL_CHILDREN_CAP;
    node->children = calloc(node->children_cap, sizeof(kern_tpl_node_t *));
    if (!node->children) {
        free(node);
        return NULL;
    }
    return node;
}

static int node_add_child(kern_tpl_node_t *parent, kern_tpl_node_t *child) {
    if (!parent || !child) return -1;
    if (parent->children_count >= parent->children_cap) {
        size_t new_cap = parent->children_cap * 2;
        kern_tpl_node_t **new_arr = realloc(parent->children,
                                             new_cap * sizeof(kern_tpl_node_t *));
        if (!new_arr) return -1;
        parent->children = new_arr;
        parent->children_cap = new_cap;
    }
    parent->children[parent->children_count++] = child;
    return 0;
}

static char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static char *str_ndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

/* ============================================================
 * Inline content parsing (text with interpolation)
 * ============================================================ */

static int parse_inline_content(kern_tpl_node_t *parent, const char *text) {
    if (!text || !*text) return 0;

    const char *p = text;
    const char *start = p;

    while (*p) {
        /* Check for #{expr} or !{expr} */
        if ((p[0] == '#' || p[0] == '!') && p[1] == '{') {
            /* Flush preceding text */
            if (p > start) {
                kern_tpl_node_t *txt = node_new(KERN_TPL_TEXT);
                if (!txt) return -1;
                txt->text = str_ndup(start, (size_t)(p - start));
                node_add_child(parent, txt);
            }

            bool escaped = (p[0] == '#');
            p += 2; /* skip #{ or !{ */

            /* Find matching close brace, handling nested braces */
            int depth = 1;
            const char *expr_start = p;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                if (depth > 0) p++;
            }

            kern_tpl_node_t *interp = node_new(KERN_TPL_INTERP);
            if (!interp) return -1;
            interp->expr = str_ndup(expr_start, (size_t)(p - expr_start));
            interp->escaped = escaped;
            node_add_child(parent, interp);

            if (*p == '}') p++; /* skip closing brace */
            start = p;
        } else {
            p++;
        }
    }

    /* Flush remaining text */
    if (p > start) {
        kern_tpl_node_t *txt = node_new(KERN_TPL_TEXT);
        if (!txt) return -1;
        txt->text = str_ndup(start, (size_t)(p - start));
        node_add_child(parent, txt);
    }

    return 0;
}

/* ============================================================
 * Attribute parsing: tag(attr="val" attr2={expr})
 * ============================================================ */

static int parse_attributes(const char *attr_str, kern_tpl_attr_t *attrs, int *attr_count) {
    *attr_count = 0;
    if (!attr_str || !*attr_str) return 0;

    const char *p = attr_str;

    while (*p && *attr_count < MAX_ATTRS) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Parse attribute name */
        const char *name_start = p;
        while (*p && *p != '=' && !isspace((unsigned char)*p) && *p != ')') p++;
        if (p == name_start) break;

        kern_tpl_attr_t *attr = &attrs[*attr_count];
        attr->name = str_ndup(name_start, (size_t)(p - name_start));
        attr->value = NULL;
        attr->dynamic = false;

        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;

        if (*p == '=') {
            p++; /* skip = */
            while (*p && isspace((unsigned char)*p)) p++;

            if (*p == '"') {
                /* Static string value */
                p++; /* skip opening quote */
                const char *val_start = p;
                while (*p && *p != '"') p++;
                attr->value = str_ndup(val_start, (size_t)(p - val_start));
                attr->dynamic = false;
                if (*p == '"') p++;
            } else if (*p == '\'') {
                /* Static string value with single quotes */
                p++;
                const char *val_start = p;
                while (*p && *p != '\'') p++;
                attr->value = str_ndup(val_start, (size_t)(p - val_start));
                attr->dynamic = false;
                if (*p == '\'') p++;
            } else if (*p == '{') {
                /* Dynamic expression value */
                p++; /* skip { */
                int depth = 1;
                const char *val_start = p;
                while (*p && depth > 0) {
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    if (depth > 0) p++;
                }
                attr->value = str_ndup(val_start, (size_t)(p - val_start));
                attr->dynamic = true;
                if (*p == '}') p++;
            } else {
                /* Bare value (no quotes) */
                const char *val_start = p;
                while (*p && !isspace((unsigned char)*p) && *p != ')') p++;
                attr->value = str_ndup(val_start, (size_t)(p - val_start));
                attr->dynamic = false;
            }
        }
        /* Boolean attribute (no value) - name is set, value is NULL */

        (*attr_count)++;
    }

    return 0;
}

/* ============================================================
 * Line parsing
 * ============================================================ */

static int get_indent_level(const char *line) {
    int spaces = 0;
    while (line[spaces] == ' ') spaces++;
    return spaces / 2;
}

static kern_tpl_node_t *parse_line(const char *line) {
    /* Skip leading whitespace */
    while (*line && isspace((unsigned char)*line)) line++;
    if (!*line) return NULL;

    /* Check for special line types */

    /* Doctype */
    if (strncmp(line, "doctype ", 8) == 0) {
        kern_tpl_node_t *node = node_new(KERN_TPL_DOCTYPE);
        if (!node) return NULL;
        node->text = str_dup(line + 8);
        return node;
    }

    /* C statement */
    if (line[0] == '-' && line[1] == ' ') {
        kern_tpl_node_t *node = node_new(KERN_TPL_STATEMENT);
        if (!node) return NULL;
        node->text = str_dup(line + 2);
        return node;
    }

    /* Literal text */
    if (line[0] == '|') {
        kern_tpl_node_t *node = node_new(KERN_TPL_TEXT);
        if (!node) return NULL;
        /* Skip '|' and optional space */
        const char *text = line + 1;
        if (*text == ' ') text++;

        /* Check for interpolation in literal text */
        /* We'll make this a container node if it has interpolation */
        if (strstr(text, "#{") || strstr(text, "!{")) {
            node->type = KERN_TPL_TEXT;
            /* Store raw text; the parent will handle interpolation */
            node->text = str_dup(text);
            /* Re-parse as inline content */
            /* Actually, use a wrapper node */
            free(node->text);
            node->text = NULL;
            parse_inline_content(node, text);
            /* If only one child that's plain text, simplify */
            if (node->children_count == 1 && node->children[0]->type == KERN_TPL_TEXT) {
                char *t = node->children[0]->text;
                node->children[0]->text = NULL;
                kern_tpl_node_free(node->children[0]);
                node->children_count = 0;
                node->text = t;
            }
        } else {
            node->text = str_dup(text);
        }
        return node;
    }

    /* Include directive */
    if (strncmp(line, "include ", 8) == 0) {
        kern_tpl_node_t *node = node_new(KERN_TPL_INCLUDE);
        if (!node) return NULL;
        node->text = str_dup(line + 8);
        return node;
    }

    /* Extend directive */
    if (strncmp(line, "extend ", 7) == 0) {
        kern_tpl_node_t *node = node_new(KERN_TPL_EXTEND);
        if (!node) return NULL;
        node->text = str_dup(line + 7);
        return node;
    }
    if (strncmp(line, "extends ", 8) == 0) {
        kern_tpl_node_t *node = node_new(KERN_TPL_EXTEND);
        if (!node) return NULL;
        node->text = str_dup(line + 8);
        return node;
    }

    /* Block directive */
    if (strncmp(line, "block ", 6) == 0) {
        kern_tpl_node_t *node = node_new(KERN_TPL_BLOCK);
        if (!node) return NULL;
        node->text = str_dup(line + 6);
        return node;
    }

    /* Element: tag(attrs) inline-content */
    kern_tpl_node_t *node = node_new(KERN_TPL_ELEMENT);
    if (!node) return NULL;

    /* Parse tag name */
    const char *p = line;
    const char *tag_start = p;
    while (*p && *p != '(' && *p != ' ' && *p != '\t') p++;

    node->tag = str_ndup(tag_start, (size_t)(p - tag_start));
    node->is_void = is_void_tag(node->tag);

    /* Parse attributes if present */
    if (*p == '(') {
        p++; /* skip ( */
        const char *attr_start = p;
        /* Find matching close paren */
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            if (depth > 0) p++;
        }
        char *attr_str = str_ndup(attr_start, (size_t)(p - attr_start));
        node->attrs = calloc(MAX_ATTRS, sizeof(kern_tpl_attr_t));
        parse_attributes(attr_str, node->attrs, &node->attr_count);
        free(attr_str);
        if (*p == ')') p++;
    }

    /* Parse inline content */
    if (*p == ' ' || *p == '\t') {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p) {
            parse_inline_content(node, p);
        }
    }

    return node;
}

/* ============================================================
 * Public API
 * ============================================================ */

void kern_tpl_node_free(kern_tpl_node_t *node) {
    if (!node) return;

    free(node->tag);
    free(node->text);
    free(node->expr);

    if (node->attrs) {
        for (int i = 0; i < node->attr_count; i++) {
            free(node->attrs[i].name);
            free(node->attrs[i].value);
        }
        free(node->attrs);
    }

    for (size_t i = 0; i < node->children_count; i++) {
        kern_tpl_node_free(node->children[i]);
    }
    free(node->children);
    free(node);
}

kern_tpl_node_t *kern_tpl_parse(const char *source) {
    if (!source) return NULL;

    /* Root node acts as document container */
    kern_tpl_node_t *root = node_new(KERN_TPL_ELEMENT);
    if (!root) return NULL;
    root->tag = str_dup("__root__");

    /* Stack for tracking parent nodes at each indent level */
    kern_tpl_node_t *stack[256];
    int stack_levels[256];
    int stack_depth = 0;

    stack[0] = root;
    stack_levels[0] = -1;
    stack_depth = 1;

    /* Parse line by line */
    const char *line_start = source;

    while (*line_start) {
        /* Find end of line */
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;

        size_t line_len = (size_t)(line_end - line_start);

        /* Skip empty lines */
        bool all_space = true;
        for (size_t i = 0; i < line_len; i++) {
            if (!isspace((unsigned char)line_start[i])) {
                all_space = false;
                break;
            }
        }

        if (!all_space && line_len > 0) {
            char line_buf[MAX_LINE_LEN];
            if (line_len >= MAX_LINE_LEN) line_len = MAX_LINE_LEN - 1;
            memcpy(line_buf, line_start, line_len);
            line_buf[line_len] = '\0';

            int indent = get_indent_level(line_buf);
            kern_tpl_node_t *node = parse_line(line_buf);

            if (node) {
                /* Find the correct parent based on indentation */
                while (stack_depth > 1 && stack_levels[stack_depth - 1] >= indent) {
                    stack_depth--;
                }

                kern_tpl_node_t *parent = stack[stack_depth - 1];
                node_add_child(parent, node);

                /* Push this node onto stack as potential parent */
                stack[stack_depth] = node;
                stack_levels[stack_depth] = indent;
                stack_depth++;
            }
        }

        /* Advance to next line */
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    return root;
}

kern_tpl_node_t *kern_tpl_parse_file(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *source = malloc((size_t)size + 1);
    if (!source) {
        fclose(f);
        return NULL;
    }

    size_t read_len = fread(source, 1, (size_t)size, f);
    source[read_len] = '\0';
    fclose(f);

    kern_tpl_node_t *ast = kern_tpl_parse(source);
    free(source);
    return ast;
}
