/*
 * kernd_log_capture.h - Application log capture
 *
 * Captures stdout/stderr from child processes via pipes and writes
 * output to rotating log files in <log_dir>/apps/<app_name>/.
 */

#ifndef KERND_LOG_CAPTURE_H
#define KERND_LOG_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start log capture for an application.
 * Creates a pipe and spawns a reader thread that writes to
 * <log_dir>/apps/<app_name>/current.log. Rotates when file
 * exceeds 10MB.
 *
 * Returns the write-end file descriptor (for dup2 into child),
 * or -1 on failure.
 */
int kernd_log_capture_start(const char *app_name, const char *log_dir);

/**
 * Stop log capture for an application.
 * Closes file descriptors and joins the reader thread.
 */
void kernd_log_capture_stop(const char *app_name);

/**
 * Get the write-end file descriptor for an app's log capture pipe.
 * Returns -1 if no capture is active for the given app.
 */
int kernd_log_capture_get_fd(const char *app_name);

#ifdef __cplusplus
}
#endif

#endif /* KERND_LOG_CAPTURE_H */
