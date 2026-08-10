/*
 * kern_str.c - String utilities implementation
 */

#include "kern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

kern_str_t kern_str(const char *cstr) {
    kern_str_t s;
    if (cstr) {
        s.data = cstr;
        s.len = strlen(cstr);
    } else {
        s.data = NULL;
        s.len = 0;
    }
    return s;
}

char *kern_str_dup(kern_str_t str) {
    if (!str.data) {
        return NULL;
    }

    char *copy = malloc(str.len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, str.data, str.len);
    copy[str.len] = '\0';
    return copy;
}

bool kern_str_eq(kern_str_t a, kern_str_t b) {
    if (a.len != b.len) {
        return false;
    }
    if (a.len == 0) {
        return true;
    }
    if (!a.data || !b.data) {
        return a.data == b.data;
    }
    return memcmp(a.data, b.data, a.len) == 0;
}

bool kern_str_starts_with(kern_str_t str, kern_str_t prefix) {
    if (prefix.len > str.len) {
        return false;
    }
    if (prefix.len == 0) {
        return true;
    }
    if (!str.data || !prefix.data) {
        return false;
    }
    return memcmp(str.data, prefix.data, prefix.len) == 0;
}

void kern_str_split(kern_str_t str, char delim,
                    kern_str_t *out_parts, size_t max_parts,
                    size_t *out_count) {
    *out_count = 0;

    if (!str.data || str.len == 0 || max_parts == 0) {
        return;
    }

    size_t start = 0;
    size_t count = 0;

    for (size_t i = 0; i <= str.len; i++) {
        if (i == str.len || str.data[i] == delim) {
            if (count < max_parts) {
                out_parts[count].data = str.data + start;
                out_parts[count].len = i - start;
                count++;
            }
            start = i + 1;

            /* If we have reached max_parts - 1 splits and there is remaining
               content, put the rest in the last slot */
            if (count == max_parts && i < str.len) {
                break;
            }
        }
    }

    *out_count = count;
}

kern_str_t kern_str_trim(kern_str_t str) {
    kern_str_t result;

    if (!str.data || str.len == 0) {
        result.data = str.data;
        result.len = 0;
        return result;
    }

    size_t start = 0;
    while (start < str.len && isspace((unsigned char)str.data[start])) {
        start++;
    }

    size_t end = str.len;
    while (end > start && isspace((unsigned char)str.data[end - 1])) {
        end--;
    }

    result.data = str.data + start;
    result.len = end - start;
    return result;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool is_unreserved(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

int kern_url_encode(kern_str_t str, kern_buf_t *buf) {
    if (!buf) {
        return -1;
    }
    if (!str.data || str.len == 0) {
        return 0;
    }

    for (size_t i = 0; i < str.len; i++) {
        char c = str.data[i];
        if (is_unreserved(c)) {
            if (kern_buf_write(buf, &c, 1) != 0) {
                return -1;
            }
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
            if (kern_buf_writes(buf, hex) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int kern_url_decode(kern_str_t str, kern_buf_t *buf) {
    if (!buf) {
        return -1;
    }
    if (!str.data || str.len == 0) {
        return 0;
    }

    for (size_t i = 0; i < str.len; i++) {
        if (str.data[i] == '%' && i + 2 < str.len) {
            int hi = hex_digit(str.data[i + 1]);
            int lo = hex_digit(str.data[i + 2]);
            if (hi >= 0 && lo >= 0) {
                char decoded = (char)((hi << 4) | lo);
                if (kern_buf_write(buf, &decoded, 1) != 0) {
                    return -1;
                }
                i += 2;
            } else {
                /* Invalid percent encoding, pass through */
                if (kern_buf_write(buf, &str.data[i], 1) != 0) {
                    return -1;
                }
            }
        } else if (str.data[i] == '+') {
            char space = ' ';
            if (kern_buf_write(buf, &space, 1) != 0) {
                return -1;
            }
        } else {
            if (kern_buf_write(buf, &str.data[i], 1) != 0) {
                return -1;
            }
        }
    }

    return 0;
}
