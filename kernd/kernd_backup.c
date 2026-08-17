/*
 * kernd_backup.c - Application backup system implementation
 *
 * Creates tar.gz backups of app source directories and prunes
 * old backups based on age.
 */

#include "kernd_backup.h"
#include "kernd_log.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 0;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/**
 * Escape a string for safe inclusion in single-quoted shell arguments.
 * Replaces each ' with '\'' (end quote, escaped quote, reopen quote).
 * Caller must free the returned string.
 * Returns NULL on allocation failure.
 */
static char *shell_escape_single_quotes(const char *input) {
    if (!input) return NULL;

    size_t quotes = 0;
    for (const char *p = input; *p; p++) {
        if (*p == '\'') quotes++;
    }

    size_t input_len = strlen(input);
    size_t out_len = input_len + (quotes * 3) + 1;
    char *out = malloc(out_len);
    if (!out) return NULL;

    char *dst = out;
    for (const char *p = input; *p; p++) {
        if (*p == '\'') {
            *dst++ = '\'';
            *dst++ = '\\';
            *dst++ = '\'';
            *dst++ = '\'';
        } else {
            *dst++ = *p;
        }
    }
    *dst = '\0';
    return out;
}

int kernd_backup_run(const char *app_name, const char *data_dir, const char *backup_dir) {
    if (!app_name || !data_dir || !backup_dir) {
        return -1;
    }

    /* Verify source directory exists */
    char src_path[1024];
    snprintf(src_path, sizeof(src_path), "%s/%s", data_dir, app_name);

    struct stat st;
    if (stat(src_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        kernd_log_error("backup: source directory %s does not exist", src_path);
        return -1;
    }

    /* Create backup directory structure */
    if (ensure_dir(backup_dir) != 0) {
        kernd_log_error("backup: cannot create backup dir %s: %s", backup_dir, strerror(errno));
        return -1;
    }

    char app_backup_dir[1024];
    snprintf(app_backup_dir, sizeof(app_backup_dir), "%s/%s", backup_dir, app_name);
    if (ensure_dir(app_backup_dir) != 0) {
        kernd_log_error("backup: cannot create app backup dir %s: %s",
                        app_backup_dir, strerror(errno));
        return -1;
    }

    /* Generate timestamp for filename */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_buf);

    /* Build tar command */
    char tar_path[2048];
    snprintf(tar_path, sizeof(tar_path), "%s/%s.tar.gz", app_backup_dir, timestamp);

    char *esc_tar_path = shell_escape_single_quotes(tar_path);
    char *esc_data_dir = shell_escape_single_quotes(data_dir);
    char *esc_app_name = shell_escape_single_quotes(app_name);

    if (!esc_tar_path || !esc_data_dir || !esc_app_name) {
        kernd_log_error("backup: allocation failed for %s", app_name);
        free(esc_tar_path);
        free(esc_data_dir);
        free(esc_app_name);
        return -1;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "tar -czf '%s' -C '%s' '%s' 2>/dev/null",
             esc_tar_path, esc_data_dir, esc_app_name);

    free(esc_tar_path);
    free(esc_data_dir);
    free(esc_app_name);

    int rc = system(cmd);
    if (rc != 0) {
        kernd_log_error("backup: tar command failed for %s (exit=%d)", app_name, rc);
        return -1;
    }

    kernd_log_info("backup: created %s", tar_path);
    return 0;
}

int kernd_backup_prune(const char *app_name, const char *backup_dir, int max_age_days) {
    if (!app_name || !backup_dir || max_age_days < 0) {
        return -1;
    }

    char app_backup_dir[1024];
    snprintf(app_backup_dir, sizeof(app_backup_dir), "%s/%s", backup_dir, app_name);

    DIR *dir = opendir(app_backup_dir);
    if (!dir) {
        kernd_log_warn("backup: cannot open %s for pruning: %s",
                       app_backup_dir, strerror(errno));
        return -1;
    }

    time_t now = time(NULL);
    time_t max_age_secs = (time_t)max_age_days * 24 * 60 * 60;
    int removed = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Only consider .tar.gz files */
        size_t namelen = strlen(entry->d_name);
        if (namelen < 7 || strcmp(entry->d_name + namelen - 7, ".tar.gz") != 0) {
            continue;
        }

        char file_path[2048];
        snprintf(file_path, sizeof(file_path), "%s/%s", app_backup_dir, entry->d_name);

        struct stat st;
        if (stat(file_path, &st) != 0) {
            continue;
        }

        if (now - st.st_mtime > max_age_secs) {
            if (remove(file_path) == 0) {
                removed++;
                kernd_log_info("backup: pruned %s", file_path);
            }
        }
    }

    closedir(dir);
    kernd_log_info("backup: pruned %d old backups for %s", removed, app_name);
    return 0;
}
