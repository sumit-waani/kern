/*
 * kern_buf.c - Growable byte buffer implementation
 */

#include "kern.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct kern_buf {
    char *data;
    size_t len;
    size_t cap;
};

static int kern_buf_ensure(kern_buf_t *buf, size_t additional) {
    size_t needed = buf->len + additional + 1; /* +1 for null terminator */
    if (needed <= buf->cap) {
        return 0;
    }

    size_t new_cap = buf->cap;
    while (new_cap < needed) {
        new_cap = new_cap < 16 ? 16 : new_cap * 2;
    }

    char *new_data = realloc(buf->data, new_cap);
    if (!new_data) {
        return -1;
    }

    buf->data = new_data;
    buf->cap = new_cap;
    return 0;
}

kern_buf_t *kern_buf_new(size_t initial_cap) {
    kern_buf_t *buf = malloc(sizeof(kern_buf_t));
    if (!buf) {
        return NULL;
    }

    if (initial_cap < 16) {
        initial_cap = 16;
    }

    buf->data = malloc(initial_cap);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = initial_cap;
    return buf;
}

int kern_buf_write(kern_buf_t *buf, const char *data, size_t len) {
    if (!buf || !data) {
        return -1;
    }

    if (kern_buf_ensure(buf, len) != 0) {
        return -1;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 0;
}

int kern_buf_writes(kern_buf_t *buf, const char *str) {
    if (!buf || !str) {
        return -1;
    }
    return kern_buf_write(buf, str, strlen(str));
}

int kern_buf_writef(kern_buf_t *buf, const char *fmt, ...) {
    if (!buf || !fmt) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);

    /* First, determine the required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return -1;
    }

    if (kern_buf_ensure(buf, (size_t)needed) != 0) {
        va_end(args);
        return -1;
    }

    vsnprintf(buf->data + buf->len, (size_t)needed + 1, fmt, args);
    buf->len += (size_t)needed;
    va_end(args);

    return 0;
}

void kern_buf_reset(kern_buf_t *buf) {
    if (!buf) {
        return;
    }
    buf->len = 0;
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

void kern_buf_free(kern_buf_t *buf) {
    if (!buf) {
        return;
    }
    free(buf->data);
    free(buf);
}

const char *kern_buf_data(const kern_buf_t *buf) {
    if (!buf) {
        return NULL;
    }
    return buf->data;
}

size_t kern_buf_len(const kern_buf_t *buf) {
    if (!buf) {
        return 0;
    }
    return buf->len;
}
