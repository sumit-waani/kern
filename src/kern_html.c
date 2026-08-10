/*
 * kern_html.c - HTML utility functions for the template engine.
 *
 * Provides HTML escaping for XSS prevention in generated template code.
 */

#include "kern.h"
#include <string.h>

int kern_html_escape(kern_buf_t *buf, const char *str) {
    if (!buf) return -1;
    if (!str) return 0;

    const char *p = str;
    const char *start = p;

    while (*p) {
        const char *replacement = NULL;
        size_t rep_len = 0;

        switch (*p) {
        case '&':
            replacement = "&amp;";
            rep_len = 5;
            break;
        case '<':
            replacement = "&lt;";
            rep_len = 4;
            break;
        case '>':
            replacement = "&gt;";
            rep_len = 4;
            break;
        case '"':
            replacement = "&quot;";
            rep_len = 6;
            break;
        case '\'':
            replacement = "&#39;";
            rep_len = 5;
            break;
        default:
            p++;
            continue;
        }

        /* Flush pending normal characters */
        if (p > start) {
            int rc = kern_buf_write(buf, start, (size_t)(p - start));
            if (rc != 0) return -1;
        }
        /* Write replacement */
        int rc = kern_buf_write(buf, replacement, rep_len);
        if (rc != 0) return -1;

        p++;
        start = p;
    }

    /* Flush remaining normal characters */
    if (p > start) {
        int rc = kern_buf_write(buf, start, (size_t)(p - start));
        if (rc != 0) return -1;
    }

    return 0;
}
