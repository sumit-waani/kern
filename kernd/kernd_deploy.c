/*
 * kernd_deploy.c - Deploy worker implementation
 *
 * Runs git clone + build in a background pthread.
 *
 * Thread safety: kern_db_open() uses sqlite3_open() which defaults to
 * serialized threading mode (SQLITE_THREADSAFE=1) on standard builds.
 * Combined with WAL mode and a 5000ms busy timeout configured in
 * kern_db_open(), concurrent access from this worker thread and the
 * main event loop is safe without an additional mutex.
 */

#include "kernd_deploy.h"
#include "kernd_app_registry.h"
#include "kernd_log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    kern_db_t *db;
    char app_name[256];
    char data_dir[512];
} deploy_ctx_t;

/**
 * Escape a string for safe inclusion in single-quoted shell arguments.
 * Replaces each ' with '\'' (end quote, escaped quote, reopen quote).
 * Caller must free the returned string.
 * Returns NULL on allocation failure.
 */
static char *shell_escape_single_quotes(const char *input) {
    if (!input) return NULL;

    /* Count single quotes to determine output size */
    size_t quotes = 0;
    for (const char *p = input; *p; p++) {
        if (*p == '\'') quotes++;
    }

    size_t input_len = strlen(input);
    /* Each quote becomes 4 chars: '\'' (replacing original 1 char = net +3) */
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

static void *deploy_worker(void *arg) {
    deploy_ctx_t *ctx = (deploy_ctx_t *)arg;

    kernd_app_t *app = kernd_app_find_by_name(ctx->db, ctx->app_name);
    if (!app) {
        kernd_log_error("deploy: app '%s' not found", ctx->app_name);
        free(ctx);
        return NULL;
    }

    kernd_app_update_status(ctx->db, ctx->app_name, "deploying");

    /* Create app directory */
    char app_dir[1024];
    snprintf(app_dir, sizeof(app_dir), "%s/%s", ctx->data_dir, ctx->app_name);
    mkdir(app_dir, 0755);

    /* Clone or pull the repository */
    if (app->repo_url) {
        char *esc_dir = shell_escape_single_quotes(app_dir);
        char *esc_url = shell_escape_single_quotes(app->repo_url);
        char *esc_branch = shell_escape_single_quotes(
            app->branch ? app->branch : "main");

        if (!esc_dir || !esc_url || !esc_branch) {
            kernd_log_error("deploy: allocation failed for '%s'", ctx->app_name);
            kernd_app_update_status(ctx->db, ctx->app_name, "failed");
            free(esc_dir);
            free(esc_url);
            free(esc_branch);
            kernd_app_free(app);
            free(ctx);
            return NULL;
        }

        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && if [ -d .git ]; then git pull; "
                 "else git clone '%s' -b '%s' .; fi",
                 esc_dir, esc_url, esc_branch);

        free(esc_dir);
        free(esc_url);
        free(esc_branch);

        kernd_log_info("deploy: cloning '%s'", ctx->app_name);
        int rc = system(cmd);
        if (rc != 0) {
            kernd_log_error("deploy: git clone failed for '%s'", ctx->app_name);
            kernd_app_update_status(ctx->db, ctx->app_name, "failed");
            kernd_app_free(app);
            free(ctx);
            return NULL;
        }
    }

    /* Run build command */
    if (app->build_cmd && app->build_cmd[0] != '\0') {
        char *esc_dir = shell_escape_single_quotes(app_dir);

        if (!esc_dir) {
            kernd_log_error("deploy: allocation failed for '%s'", ctx->app_name);
            kernd_app_update_status(ctx->db, ctx->app_name, "failed");
            free(esc_dir);
            kernd_app_free(app);
            free(ctx);
            return NULL;
        }

        char cmd[4096];
        /* build_cmd is an intentional shell expression entered by the admin;
         * quote only the cd target to prevent path injection, but pass
         * build_cmd unquoted so multi-word commands and pipes work. */
        snprintf(cmd, sizeof(cmd), "cd '%s' && %s", esc_dir, app->build_cmd);

        free(esc_dir);

        kernd_log_info("deploy: building '%s'", ctx->app_name);
        int rc = system(cmd);
        if (rc != 0) {
            kernd_log_error("deploy: build failed for '%s'", ctx->app_name);
            kernd_app_update_status(ctx->db, ctx->app_name, "failed");
            kernd_app_free(app);
            free(ctx);
            return NULL;
        }
    }

    kernd_app_update_status(ctx->db, ctx->app_name, "built");
    kernd_log_info("deploy: '%s' built successfully", ctx->app_name);

    kernd_app_free(app);
    free(ctx);
    return NULL;
}

int kernd_deploy_start(kern_db_t *db, const char *app_name, const char *data_dir) {
    if (!db || !app_name || !data_dir) {
        return -1;
    }

    deploy_ctx_t *ctx = calloc(1, sizeof(deploy_ctx_t));
    if (!ctx) {
        return -1;
    }

    ctx->db = db;
    snprintf(ctx->app_name, sizeof(ctx->app_name), "%s", app_name);
    snprintf(ctx->data_dir, sizeof(ctx->data_dir), "%s", data_dir);

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int rc = pthread_create(&thread, &attr, deploy_worker, ctx);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        kernd_log_error("failed to create deploy thread for '%s'", app_name);
        free(ctx);
        return -1;
    }

    return 0;
}
