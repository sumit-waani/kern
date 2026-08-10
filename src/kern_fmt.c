/*
 * kern_fmt.c - Code formatter for C source files and .khtml templates
 *
 * Provides in-memory formatting of C code (4-space indent, trailing whitespace
 * trimming, operator spacing) and .khtml templates (2-space indent normalization).
 */

#include "kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Internal helpers
 * ============================================================ */

/**
 * Trim trailing whitespace from a single line (does not remove newline).
 * Returns new length of the line data written into 'out'.
 */
static size_t trim_trailing(const char *line, size_t len)
{
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' ||
                       line[len - 1] == '\r'))
    {
        len--;
    }
    return len;
}

/**
 * Count leading whitespace characters (spaces and tabs).
 * Tabs count as 4 spaces for measurement purposes.
 */
static int measure_indent(const char *line, size_t len)
{
    int indent = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (line[i] == ' ')
        {
            indent++;
        }
        else if (line[i] == '\t')
        {
            indent += 4;
        }
        else
        {
            break;
        }
    }
    return indent;
}

/**
 * Skip leading whitespace characters and return pointer to first
 * non-whitespace character.
 */
static const char *skip_whitespace(const char *line, size_t len, size_t *content_start)
{
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
    {
        i++;
    }
    *content_start = i;
    return line + i;
}

/**
 * Write N spaces of indentation to the buffer.
 */
static void write_indent(kern_buf_t *out, int spaces)
{
    for (int i = 0; i < spaces; i++)
    {
        kern_buf_write(out, " ", 1);
    }
}

/* ============================================================
 * C Formatter
 * ============================================================ */

int kern_fmt_c(const char *source, kern_buf_t *out)
{
    if (!source || !out)
    {
        return -1;
    }

    const char *p = source;
    int indent_level = 0;
    int in_block_comment = 0;
    int in_string = 0;
    int in_char_literal = 0;

    while (*p)
    {
        /* Find end of current line */
        const char *line_start = p;
        const char *line_end = p;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        size_t line_len = (size_t)(line_end - line_start);

        /* Trim trailing whitespace */
        size_t trimmed_len = trim_trailing(line_start, line_len);

        /* Skip leading whitespace to get content */
        size_t content_start = 0;
        skip_whitespace(line_start, trimmed_len, &content_start);
        const char *content = line_start + content_start;
        size_t content_len = trimmed_len - content_start;

        /* Handle empty lines */
        if (content_len == 0)
        {
            kern_buf_write(out, "\n", 1);
            p = line_end;
            if (*p == '\n')
            {
                p++;
            }
            continue;
        }

        /* Check if line starts with closing brace - dedent before writing */
        int pre_dedent = 0;
        if (!in_block_comment && content[0] == '}')
        {
            pre_dedent = 1;
            if (indent_level > 0)
            {
                indent_level--;
            }
        }

        /* Write indentation (4 spaces per level) */
        write_indent(out, indent_level * 4);

        /* Write the content */
        kern_buf_write(out, content, content_len);
        kern_buf_write(out, "\n", 1);

        /* Scan content for brace counting (skip comments and strings) */
        int line_in_string = in_string;
        int line_in_char = in_char_literal;
        int line_in_comment = in_block_comment;

        for (size_t i = 0; i < content_len; i++)
        {
            char c = content[i];
            char next = (i + 1 < content_len) ? content[i + 1] : '\0';

            /* Handle block comments */
            if (line_in_comment)
            {
                if (c == '*' && next == '/')
                {
                    line_in_comment = 0;
                    i++;
                }
                continue;
            }

            /* Handle string literals */
            if (line_in_string)
            {
                if (c == '\\')
                {
                    i++; /* skip escaped char */
                }
                else if (c == '"')
                {
                    line_in_string = 0;
                }
                continue;
            }

            /* Handle char literals */
            if (line_in_char)
            {
                if (c == '\\')
                {
                    i++; /* skip escaped char */
                }
                else if (c == '\'')
                {
                    line_in_char = 0;
                }
                continue;
            }

            /* Start of line comment - rest of line is comment */
            if (c == '/' && next == '/')
            {
                break;
            }

            /* Start of block comment */
            if (c == '/' && next == '*')
            {
                line_in_comment = 1;
                i++;
                continue;
            }

            /* String literal start */
            if (c == '"')
            {
                line_in_string = 1;
                continue;
            }

            /* Char literal start */
            if (c == '\'')
            {
                line_in_char = 1;
                continue;
            }

            /* Count braces for indentation (only outside of pre-dedented line) */
            if (c == '{' && !pre_dedent)
            {
                indent_level++;
            }
            else if (c == '{' && pre_dedent)
            {
                /* Opening brace on a line that started with } - net effect */
                indent_level++;
            }
            else if (c == '}' && i > 0)
            {
                /* Additional closing braces on same line */
                if (indent_level > 0)
                {
                    indent_level--;
                }
            }
        }

        in_block_comment = line_in_comment;
        in_string = line_in_string;
        in_char_literal = line_in_char;

        /* Move past newline */
        p = line_end;
        if (*p == '\n')
        {
            p++;
        }
    }

    return 0;
}

/* ============================================================
 * KHTML Formatter
 * ============================================================ */

int kern_fmt_khtml(const char *source, kern_buf_t *out)
{
    if (!source || !out)
    {
        return -1;
    }

    /* First pass: detect the base indentation unit */
    int min_indent = 0;
    const char *scan = source;
    while (*scan)
    {
        const char *line_start = scan;
        const char *line_end = scan;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        size_t line_len = (size_t)(line_end - line_start);
        size_t trimmed_len = trim_trailing(line_start, line_len);

        if (trimmed_len > 0)
        {
            int indent = measure_indent(line_start, trimmed_len);
            if (indent > 0 && (min_indent == 0 || indent < min_indent))
            {
                min_indent = indent;
            }
        }

        scan = line_end;
        if (*scan == '\n')
        {
            scan++;
        }
    }

    /* If no indentation found, default to 2 */
    if (min_indent == 0)
    {
        min_indent = 2;
    }

    /* Second pass: normalize indentation */
    const char *p = source;

    while (*p)
    {
        /* Find end of current line */
        const char *line_start = p;
        const char *line_end = p;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        size_t line_len = (size_t)(line_end - line_start);

        /* Trim trailing whitespace */
        size_t trimmed_len = trim_trailing(line_start, line_len);

        /* Empty line: just write newline */
        if (trimmed_len == 0)
        {
            kern_buf_write(out, "\n", 1);
            p = line_end;
            if (*p == '\n')
            {
                p++;
            }
            continue;
        }

        /* Measure original indent and calculate logical level */
        int orig_indent = measure_indent(line_start, trimmed_len);
        int indent_level = orig_indent / min_indent;
        int normalized_indent = indent_level * 2;

        /* Get content after whitespace */
        size_t content_start = 0;
        skip_whitespace(line_start, trimmed_len, &content_start);
        const char *content = line_start + content_start;
        size_t content_len = trimmed_len - content_start;

        /* Write normalized indent + content */
        write_indent(out, normalized_indent);
        kern_buf_write(out, content, content_len);
        kern_buf_write(out, "\n", 1);

        /* Move past newline */
        p = line_end;
        if (*p == '\n')
        {
            p++;
        }
    }

    return 0;
}

/* ============================================================
 * File-level operations
 * ============================================================ */

/**
 * Read an entire file into a malloc'd string. Returns NULL on failure.
 */
static char *read_file_contents(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

/**
 * Detect file type by extension.
 * Returns: 1 for C/H files, 2 for khtml, 0 for unknown.
 */
static int detect_file_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
    {
        return 0;
    }

    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0)
    {
        return 1;
    }
    if (strcmp(dot, ".khtml") == 0)
    {
        return 2;
    }
    return 0;
}

int kern_fmt_file(const char *path)
{
    if (!path)
    {
        return -1;
    }

    int file_type = detect_file_type(path);
    if (file_type == 0)
    {
        return -1; /* Unknown file type */
    }

    char *source = read_file_contents(path);
    if (!source)
    {
        return -1;
    }

    kern_buf_t *out = kern_buf_new(strlen(source) + 256);
    if (!out)
    {
        free(source);
        return -1;
    }

    int rc;
    if (file_type == 1)
    {
        rc = kern_fmt_c(source, out);
    }
    else
    {
        rc = kern_fmt_khtml(source, out);
    }

    if (rc != 0)
    {
        kern_buf_free(out);
        free(source);
        return -1;
    }

    /* Write formatted output back to file */
    FILE *f = fopen(path, "w");
    if (!f)
    {
        kern_buf_free(out);
        free(source);
        return -1;
    }

    const char *data = kern_buf_data(out);
    size_t len = kern_buf_len(out);
    fwrite(data, 1, len, f);
    fclose(f);

    kern_buf_free(out);
    free(source);
    return 0;
}

int kern_fmt_check(const char *path)
{
    if (!path)
    {
        return -1;
    }

    int file_type = detect_file_type(path);
    if (file_type == 0)
    {
        return -1; /* Unknown file type */
    }

    char *source = read_file_contents(path);
    if (!source)
    {
        return -1;
    }

    kern_buf_t *out = kern_buf_new(strlen(source) + 256);
    if (!out)
    {
        free(source);
        return -1;
    }

    int rc;
    if (file_type == 1)
    {
        rc = kern_fmt_c(source, out);
    }
    else
    {
        rc = kern_fmt_khtml(source, out);
    }

    if (rc != 0)
    {
        kern_buf_free(out);
        free(source);
        return -1;
    }

    /* Compare formatted output to original */
    const char *formatted = kern_buf_data(out);
    int needs_formatting = strcmp(source, formatted) != 0 ? 1 : 0;

    kern_buf_free(out);
    free(source);
    return needs_formatting;
}
