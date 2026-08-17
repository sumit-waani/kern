/*
 * kernd_process.c - Process manager implementation
 *
 * Uses fork()/exec() to manage child processes. Tracks them
 * in a simple array with a maximum of 256 tracked processes.
 */

#include "kernd_process.h"
#include "kernd_log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define KERND_MAX_PROCS 256

typedef struct {
    char name[256];
    kernd_proc_info_t info;
} kernd_proc_entry_t;

static kernd_proc_entry_t proc_table[KERND_MAX_PROCS];
static int proc_count = 0;

static kernd_proc_entry_t *find_entry(const char *name) {
    for (int i = 0; i < proc_count; i++) {
        if (strcmp(proc_table[i].name, name) == 0) {
            return &proc_table[i];
        }
    }
    return NULL;
}

void kernd_process_init(void) {
    memset(proc_table, 0, sizeof(proc_table));
    proc_count = 0;
}

pid_t kernd_process_start(const kernd_app_t *app, const char *data_dir) {
    if (!app || !app->name || !app->start_cmd || app->start_cmd[0] == '\0') {
        return -1;
    }

    if (!data_dir) {
        return -1;
    }

    /* Check if already running */
    kernd_proc_entry_t *existing = find_entry(app->name);
    if (existing && existing->info.status == KERND_PROC_RUNNING) {
        return existing->info.pid;
    }

    /* Build working directory path: data_dir/app->name */
    char work_dir[1024];
    snprintf(work_dir, sizeof(work_dir), "%s/%s", data_dir, app->name);

    pid_t pid = fork();
    if (pid < 0) {
        kernd_log_error("fork failed for '%s'", app->name);
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        if (chdir(work_dir) != 0) {
            _exit(127);
        }

        /* Execute the start command via /bin/sh -c */
        execl("/bin/sh", "sh", "-c", app->start_cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent process - track the child */
    if (existing) {
        /* Reuse existing entry */
        existing->info.pid = pid;
        existing->info.status = KERND_PROC_RUNNING;
    } else {
        if (proc_count >= KERND_MAX_PROCS) {
            kernd_log_error("process table full, cannot track '%s'", app->name);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }

        kernd_proc_entry_t *entry = &proc_table[proc_count++];
        snprintf(entry->name, sizeof(entry->name), "%s", app->name);
        entry->info.pid = pid;
        entry->info.status = KERND_PROC_RUNNING;
    }

    kernd_log_info("started process '%s' with pid %d", app->name, (int)pid);
    return pid;
}

int kernd_process_stop(const char *name) {
    if (!name) {
        return -1;
    }

    kernd_proc_entry_t *entry = find_entry(name);
    if (!entry) {
        return -1;
    }

    if (entry->info.status == KERND_PROC_STOPPED) {
        return 0;
    }

    pid_t pid = entry->info.pid;

    /* Send SIGTERM */
    kill(pid, SIGTERM);

    /* Wait for exit with timeout */
    int status;
    int wait_result = waitpid(pid, &status, 0);
    if (wait_result < 0) {
        /* Process may have already exited */
    }

    entry->info.status = KERND_PROC_STOPPED;
    kernd_log_info("stopped process '%s' (pid %d)", name, (int)pid);
    return 0;
}

const kernd_proc_info_t *kernd_process_status(const char *name) {
    if (!name) {
        return NULL;
    }

    kernd_proc_entry_t *entry = find_entry(name);
    if (!entry) {
        return NULL;
    }

    return &entry->info;
}

void kernd_process_reap(void) {
    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i].info.status == KERND_PROC_RUNNING) {
            int status;
            pid_t result = waitpid(proc_table[i].info.pid, &status, WNOHANG);
            if (result > 0) {
                /* Child has exited */
                proc_table[i].info.status = KERND_PROC_STOPPED;
                kernd_log_info("reaped process '%s' (pid %d)",
                               proc_table[i].name,
                               (int)proc_table[i].info.pid);
            }
        }
    }
}
