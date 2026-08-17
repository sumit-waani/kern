/*
 * kernd_log.c - Logging implementation for the kernd daemon
 *
 * Writes timestamped messages to stderr using vfprintf.
 */

#include "kernd_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static void log_write(const char *level, const char *fmt, va_list ap) {
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm = localtime_r(&now, &tm_buf);

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);

    fprintf(stderr, "[%s] [%s] ", ts, level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void kernd_log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("INFO", fmt, ap);
    va_end(ap);
}

void kernd_log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("WARN", fmt, ap);
    va_end(ap);
}

void kernd_log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("ERROR", fmt, ap);
    va_end(ap);
}
