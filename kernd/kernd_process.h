/*
 * kernd_process.h - Process manager for kernd
 *
 * Manages child processes using fork()/exec(). Tracks PIDs and allows
 * starting, stopping, and querying process status.
 */

#ifndef KERND_PROCESS_H
#define KERND_PROCESS_H

#include "kernd_app_registry.h"

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Process status values.
 */
typedef enum {
    KERND_PROC_RUNNING = 0,
    KERND_PROC_STOPPED = 1
} kernd_proc_status_t;

/**
 * kernd_proc_info_t - Information about a tracked process.
 */
typedef struct {
    pid_t pid;
    kernd_proc_status_t status;
} kernd_proc_info_t;

/**
 * Initialize the process manager.
 * Must be called before any other kernd_process_* functions.
 * Can be called multiple times (resets internal state).
 */
void kernd_process_init(void);

/**
 * Start a process for the given application.
 * Forks and execs app->start_cmd in the directory data_dir/app->name/.
 * Returns the child PID on success, -1 on failure.
 * If the app is already running, returns the existing PID.
 */
pid_t kernd_process_start(const kernd_app_t *app, const char *data_dir);

/**
 * Stop a running process by app name.
 * Sends SIGTERM and waits for the process to exit.
 * Returns 0 on success, -1 if the process is not tracked.
 */
int kernd_process_stop(const char *name);

/**
 * Get the status of a tracked process.
 * Returns a pointer to internal proc_info, or NULL if not tracked.
 * The returned pointer is valid until the next kernd_process_init() call.
 */
const kernd_proc_info_t *kernd_process_status(const char *name);

/**
 * Reap child processes that have exited.
 * Checks all tracked PIDs and marks exited ones as KERND_PROC_STOPPED.
 */
void kernd_process_reap(void);

#ifdef __cplusplus
}
#endif

#endif /* KERND_PROCESS_H */
