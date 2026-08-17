/*
 * kernd_log.h - Logging for the kernd daemon
 *
 * Simple timestamped logging to stderr with level prefixes.
 */

#ifndef KERND_LOG_H
#define KERND_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log an informational message to stderr.
 * Format: [YYYY-MM-DD HH:MM:SS] [INFO] message
 */
void kernd_log_info(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/**
 * Log a warning message to stderr.
 * Format: [YYYY-MM-DD HH:MM:SS] [WARN] message
 */
void kernd_log_warn(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/**
 * Log an error message to stderr.
 * Format: [YYYY-MM-DD HH:MM:SS] [ERROR] message
 */
void kernd_log_error(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* KERND_LOG_H */
