/*
 * kernd_deploy.h - Deploy worker for kernd
 *
 * Handles git clone and build operations in a background thread.
 */

#ifndef KERND_DEPLOY_H
#define KERND_DEPLOY_H

#include "kern.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start a deploy for the named application.
 * Spawns a background thread that:
 *   1. Clones (or pulls) the app's git repository into data_dir/app_name/
 *   2. Runs the build command
 *   3. Updates the app status in the registry
 *
 * Returns 0 on success (thread launched), -1 on failure.
 */
int kernd_deploy_start(kern_db_t *db, const char *app_name, const char *data_dir);

#ifdef __cplusplus
}
#endif

#endif /* KERND_DEPLOY_H */
