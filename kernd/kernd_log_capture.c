/*
 * kernd_log_capture.c - Application log capture implementation
 *
 * Creates pipes for child process stdout/stderr and spawns reader
 * threads that write to rotating log files.
 */

#include "kernd_log_capture.h"
#include "kernd_log.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_CAPTURES 256
#define LOG_ROTATE_SIZE (10 * 1024 * 1024)  /* 10 MB */
#define READ_BUF_SIZE 4096

typedef struct {
    char app_name[256];
    char log_dir[1024];
    int read_fd;
    int write_fd;
    pthread_t thread;
    int active;
} log_capture_entry_t;

static log_capture_entry_t captures[MAX_CAPTURES];
static int capture_count = 0;

static log_capture_entry_t *find_capture(const char *app_name) {
    for (int i = 0; i < capture_count; i++) {
        if (captures[i].active && strcmp(captures[i].app_name, app_name) == 0) {
            return &captures[i];
        }
    }
    return NULL;
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;
    }
    return mkdir(path, 0755);
}

static void rotate_log(const char *log_path) {
    char new_path[1024];
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    /* Create timestamped filename */
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_buf);

    /* Get the directory part */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", log_path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    snprintf(new_path, sizeof(new_path), "%.900s/%s.log", dir, timestamp);
    rename(log_path, new_path);
    kernd_log_info("log_capture: rotated %s -> %s", log_path, new_path);
}

static void *reader_thread(void *arg) {
    log_capture_entry_t *entry = (log_capture_entry_t *)arg;

    /* Build log file path */
    char app_log_dir[1024];
    snprintf(app_log_dir, sizeof(app_log_dir), "%.900s/apps", entry->log_dir);
    ensure_dir(app_log_dir);

    snprintf(app_log_dir, sizeof(app_log_dir), "%.800s/apps/%.100s", entry->log_dir, entry->app_name);
    ensure_dir(app_log_dir);

    char log_path[1024];
    snprintf(log_path, sizeof(log_path), "%.900s/current.log", app_log_dir);

    FILE *logfile = fopen(log_path, "a");
    if (!logfile) {
        kernd_log_error("log_capture: cannot open %s: %s", log_path, strerror(errno));
        close(entry->read_fd);
        return NULL;
    }

    char buf[READ_BUF_SIZE];
    ssize_t n;
    long file_size = 0;

    /* Get current file size */
    fseek(logfile, 0, SEEK_END);
    file_size = ftell(logfile);

    while ((n = read(entry->read_fd, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, logfile);
        fflush(logfile);
        file_size += n;

        /* Check if rotation is needed */
        if (file_size >= LOG_ROTATE_SIZE) {
            fclose(logfile);
            rotate_log(log_path);
            logfile = fopen(log_path, "w");
            if (!logfile) {
                kernd_log_error("log_capture: cannot reopen %s after rotation", log_path);
                break;
            }
            file_size = 0;
        }
    }

    if (logfile) {
        fclose(logfile);
    }
    close(entry->read_fd);
    return NULL;
}

int kernd_log_capture_start(const char *app_name, const char *log_dir) {
    if (!app_name || !log_dir) {
        return -1;
    }

    /* Check if already capturing */
    if (find_capture(app_name)) {
        kernd_log_warn("log_capture: already capturing for %s", app_name);
        return -1;
    }

    if (capture_count >= MAX_CAPTURES) {
        kernd_log_error("log_capture: capture table full");
        return -1;
    }

    /* Create pipe */
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        kernd_log_error("log_capture: pipe() failed: %s", strerror(errno));
        return -1;
    }

    /* Set up entry */
    log_capture_entry_t *entry = &captures[capture_count++];
    snprintf(entry->app_name, sizeof(entry->app_name), "%s", app_name);
    snprintf(entry->log_dir, sizeof(entry->log_dir), "%s", log_dir);
    entry->read_fd = pipefd[0];
    entry->write_fd = pipefd[1];
    entry->active = 1;

    /* Start reader thread */
    if (pthread_create(&entry->thread, NULL, reader_thread, entry) != 0) {
        kernd_log_error("log_capture: cannot create reader thread: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        entry->active = 0;
        capture_count--;
        return -1;
    }

    kernd_log_info("log_capture: started for %s (write_fd=%d)", app_name, entry->write_fd);
    return entry->write_fd;
}

void kernd_log_capture_stop(const char *app_name) {
    if (!app_name) return;

    log_capture_entry_t *entry = find_capture(app_name);
    if (!entry) return;

    /* Close write end to signal EOF to the reader thread */
    if (entry->write_fd >= 0) {
        close(entry->write_fd);
        entry->write_fd = -1;
    }

    /* Wait for reader thread to finish */
    pthread_join(entry->thread, NULL);
    entry->active = 0;

    kernd_log_info("log_capture: stopped for %s", app_name);
}

int kernd_log_capture_get_fd(const char *app_name) {
    if (!app_name) return -1;

    log_capture_entry_t *entry = find_capture(app_name);
    if (!entry) return -1;

    return entry->write_fd;
}
