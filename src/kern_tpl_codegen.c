/*
 * kern_tpl_codegen.c - C code generator for .khtml templates
 *
 * Walks the template AST and emits C source code that, when compiled
 * and called, writes HTML into a kern_buf_t buffer.
 *
 * =====================================================================
 * SECURITY NOTICE: TRUSTED INPUT ONLY
 * =====================================================================
 *
 * This code generator emits interpolation expressions (#{expr}) and
 * statement blocks (- if/for/etc) as raw C source code. The generated
 * output is compiled and linked into the application binary.
 *
 * .khtml template files are TRUSTED INPUT. They have the same privilege
 * level as application source code. Any C expression in a template will
 * be executed with full application permissions at request time.
 *
 * DO NOT allow user-uploadable .khtml templates. If the framework ever
 * supports dynamic template loading (CMS themes, plugin templates,
 * user-generated content), those templates MUST be sandboxed or use a
 * different, restricted template language. Allowing untrusted parties
 * to provide .khtml files is equivalent to allowing arbitrary code
 * execution within the application process.
 *
 * This is by design: compile-time templates give maximum performance
 * and full access to application types and functions. The security
 * boundary is at the template file level, not at the expression level.
 * =====================================================================
 */

#include "kern.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Internal helpers
 * ============================================================ */

static char *codegen_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static void emit_indent(kern_buf_t *out, int depth) {
    for (int i = 0; i < depth; i++) {
        kern_buf_writes(out, "    ");
    }
}

/* Escape a C string literal (handle backslashes, quotes, newlines) */
static void emit_c_string(kern_buf_t *out, const char *str) {
    kern_buf_writes(out, "\"");
    const char *p = str;
    while (*p) {
        switch (*p) {
        case '"':  kern_buf_writes(out, "\\\""); break;
        case '\\': kern_buf_writes(out, "\\\\"); break;
        case '\n': kern_buf_writes(out, "\\n");  break;
        case '\r': kern_buf_writes(out, "\\r");  break;
        case '\t': kern_buf_writes(out, "\\t");  break;
        default:   kern_buf_write(out, p, 1);    break;
        }
        p++;
    }
    kern_buf_writes(out, "\"");
}

/* Check if a statement line needs an opening brace (if, for, else, while) */
static bool stmt_needs_brace(const char *stmt) {
    /* Trim leading whitespace */
    while (*stmt && isspace((unsigned char)*stmt)) stmt++;

    if (strncmp(stmt, "if ", 3) == 0 || strncmp(stmt, "if(", 3) == 0) return true;
    if (strncmp(stmt, "for ", 4) == 0 || strncmp(stmt, "for(", 4) == 0) return true;
    if (strncmp(stmt, "while ", 6) == 0 || strncmp(stmt, "while(", 6) == 0) return true;
    if (strncmp(stmt, "else if ", 8) == 0 || strncmp(stmt, "else if(", 8) == 0) return true;
    if (strcmp(stmt, "else") == 0 || strncmp(stmt, "else ", 5) == 0) return true;
    return false;
}

/* Check if a statement is "else" or "else if" (needs to close prior brace first) */
static bool stmt_is_else(const char *stmt) {
    while (*stmt && isspace((unsigned char)*stmt)) stmt++;
    if (strncmp(stmt, "else", 4) == 0) return true;
    return false;
}

/* ============================================================
 * Code generation - recursive AST walk
 * ============================================================ */

static void codegen_node(kern_buf_t *out, kern_tpl_node_t *node, int indent,
                         bool *in_block);

static void codegen_children(kern_buf_t *out, kern_tpl_node_t *node, int indent,
                             bool *in_block) {
    for (size_t i = 0; i < node->children_count; i++) {
        codegen_node(out, node->children[i], indent, in_block);
    }
}

static void codegen_node(kern_buf_t *out, kern_tpl_node_t *node, int indent,
                         bool *in_block) {
    if (!node) return;

    switch (node->type) {
    case KERN_TPL_DOCTYPE:
        emit_indent(out, indent);
        kern_buf_writes(out, "kern_buf_writes(buf, \"<!DOCTYPE ");
        if (node->text) {
            kern_buf_writes(out, node->text);
        } else {
            kern_buf_writes(out, "html");
        }
        kern_buf_writes(out, ">\\n\");\n");
        break;

    case KERN_TPL_TEXT:
        if (node->text) {
            emit_indent(out, indent);
            kern_buf_writes(out, "kern_buf_writes(buf, ");
            emit_c_string(out, node->text);
            kern_buf_writes(out, ");\n");
        }
        /* If the text node has children (from inline content with interpolation) */
        if (node->children_count > 0) {
            codegen_children(out, node, indent, in_block);
        }
        break;

    case KERN_TPL_INTERP:
        emit_indent(out, indent);
        if (node->escaped) {
            kern_buf_writes(out, "kern_html_escape(buf, (const char *)(");
            kern_buf_writes(out, node->expr);
            kern_buf_writes(out, "));\n");
        } else {
            kern_buf_writes(out, "kern_buf_writes(buf, (const char *)(");
            kern_buf_writes(out, node->expr);
            kern_buf_writes(out, "));\n");
        }
        break;

    case KERN_TPL_STATEMENT: {
        /* If this is "else" or "else if", close the previous block first */
        if (stmt_is_else(node->text) && *in_block) {
            emit_indent(out, indent);
            kern_buf_writes(out, "} ");
            kern_buf_writes(out, node->text);
            if (stmt_needs_brace(node->text)) {
                kern_buf_writes(out, " {\n");
                *in_block = true;
            } else {
                kern_buf_writes(out, ";\n");
                *in_block = false;
            }
        } else {
            /* Close any prior block that's still open */
            if (*in_block) {
                emit_indent(out, indent);
                kern_buf_writes(out, "}\n");
                *in_block = false;
            }
            emit_indent(out, indent);
            kern_buf_writes(out, node->text);
            if (stmt_needs_brace(node->text)) {
                kern_buf_writes(out, " {\n");
                *in_block = true;
            } else {
                kern_buf_writes(out, ";\n");
            }
        }
        /* Generate children (body of the control structure) */
        bool child_block = false;
        for (size_t i = 0; i < node->children_count; i++) {
            codegen_node(out, node->children[i], indent + 1, &child_block);
        }
        /* Close the child block if one was opened inside */
        if (child_block) {
            emit_indent(out, indent + 1);
            kern_buf_writes(out, "}\n");
        }
        break;
    }

    case KERN_TPL_INCLUDE:
        emit_indent(out, indent);
        /* Convert path like "partials/nav" to function name "kern_render_partials_nav" */
        kern_buf_writes(out, "kern_render_");
        if (node->text) {
            const char *p = node->text;
            while (*p) {
                if (*p == '/' || *p == '-') {
                    kern_buf_writes(out, "_");
                } else {
                    kern_buf_write(out, p, 1);
                }
                p++;
            }
        }
        kern_buf_writes(out, "(buf, vars);\n");
        break;

    case KERN_TPL_EXTEND:
        /* Extended template: generate a call to the parent layout */
        emit_indent(out, indent);
        kern_buf_writes(out, "/* extends: ");
        if (node->text) kern_buf_writes(out, node->text);
        kern_buf_writes(out, " */\n");
        emit_indent(out, indent);
        kern_buf_writes(out, "kern_render_");
        if (node->text) {
            const char *p = node->text;
            while (*p) {
                if (*p == '/' || *p == '-') {
                    kern_buf_writes(out, "_");
                } else {
                    kern_buf_write(out, p, 1);
                }
                p++;
            }
        }
        kern_buf_writes(out, "(buf, vars);\n");
        break;

    case KERN_TPL_BLOCK:
        emit_indent(out, indent);
        kern_buf_writes(out, "/* block: ");
        if (node->text) kern_buf_writes(out, node->text);
        kern_buf_writes(out, " */\n");
        {
            bool block_state = false;
            codegen_children(out, node, indent, &block_state);
            if (block_state) {
                emit_indent(out, indent);
                kern_buf_writes(out, "}\n");
            }
        }
        break;

    case KERN_TPL_ELEMENT: {
        /* Skip the root node tag output but still process children */
        if (node->tag && strcmp(node->tag, "__root__") == 0) {
            bool root_block = false;
            codegen_children(out, node, indent, &root_block);
            if (root_block) {
                emit_indent(out, indent);
                kern_buf_writes(out, "}\n");
            }
            break;
        }

        /* Open tag */
        emit_indent(out, indent);
        kern_buf_writes(out, "kern_buf_writes(buf, \"<");
        kern_buf_writes(out, node->tag);
        kern_buf_writes(out, "\");\n");

        /* Attributes */
        for (int i = 0; i < node->attr_count; i++) {
            kern_tpl_attr_t *attr = &node->attrs[i];
            if (attr->value) {
                if (attr->dynamic) {
                    /* Dynamic attribute: value is a C expression */
                    emit_indent(out, indent);
                    kern_buf_writes(out, "kern_buf_writes(buf, \" ");
                    kern_buf_writes(out, attr->name);
                    kern_buf_writes(out, "=\\\"\");\n");
                    emit_indent(out, indent);
                    kern_buf_writes(out, "kern_html_escape(buf, (const char *)(");
                    kern_buf_writes(out, attr->value);
                    kern_buf_writes(out, "));\n");
                    emit_indent(out, indent);
                    kern_buf_writes(out, "kern_buf_writes(buf, \"\\\"\");\n");
                } else {
                    /* Static attribute */
                    emit_indent(out, indent);
                    kern_buf_writes(out, "kern_buf_writes(buf, \" ");
                    kern_buf_writes(out, attr->name);
                    kern_buf_writes(out, "=\\\"");
                    kern_buf_writes(out, attr->value);
                    kern_buf_writes(out, "\\\"\");\n");
                }
            } else {
                /* Boolean attribute */
                emit_indent(out, indent);
                kern_buf_writes(out, "kern_buf_writes(buf, \" ");
                kern_buf_writes(out, attr->name);
                kern_buf_writes(out, "\");\n");
            }
        }

        /* Close opening tag */
        emit_indent(out, indent);
        kern_buf_writes(out, "kern_buf_writes(buf, \">\");\n");

        /* Children (inline content + nested elements) */
        if (!node->is_void) {
            bool child_block = false;
            codegen_children(out, node, indent, &child_block);
            /* Close any open block from children */
            if (child_block) {
                emit_indent(out, indent + 1);
                kern_buf_writes(out, "}\n");
            }

            /* Close tag */
            emit_indent(out, indent);
            kern_buf_writes(out, "kern_buf_writes(buf, \"</");
            kern_buf_writes(out, node->tag);
            kern_buf_writes(out, ">\");\n");
        }
        break;
    }
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

char *kern_tpl_codegen(kern_tpl_node_t *ast, const char *func_name) {
    if (!ast || !func_name) return NULL;

    kern_buf_t *out = kern_buf_new(4096);
    if (!out) return NULL;

    /* File header */
    kern_buf_writes(out, "/* Auto-generated by kern template compiler. Do not edit. */\n");
    kern_buf_writes(out, "#include \"kern.h\"\n\n");

    /* Function signature */
    kern_buf_writes(out, "void ");
    kern_buf_writes(out, func_name);
    kern_buf_writes(out, "(kern_buf_t *buf, kern_dict_t *vars) {\n");
    kern_buf_writes(out, "    (void)vars;\n");

    /* Generate body */
    bool in_block = false;
    codegen_children(out, ast, 1, &in_block);

    /* Close any remaining open block */
    if (in_block) {
        kern_buf_writes(out, "    }\n");
    }

    kern_buf_writes(out, "}\n");

    /* Extract result */
    const char *data = kern_buf_data(out);
    char *result = codegen_strdup(data);
    kern_buf_free(out);
    return result;
}
