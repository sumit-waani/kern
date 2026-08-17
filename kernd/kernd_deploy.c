/*
 * kernd_deploy.c - Deploy worker implementation
 *
 * Runs git clone + build in a background pthread.
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
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "cd '%s' && if [ -d .git ]; then git pull; "
                 "else git clone '%s' -b '%s' .; fi",
                 app_dir,
                 app->repo_url,
                 app->branch ? app->branch : "main");

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
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "cd '%s' && %s", app_dir, app->build_cmd);

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
