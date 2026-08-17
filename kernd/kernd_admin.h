/*
 * kernd_admin.h - Admin HTTP server for kernd
 *
 * Provides the web-based admin UI for managing applications,
 * viewing status, and triggering deploys.
 */

#ifndef KERND_ADMIN_H
#define KERND_ADMIN_H

#include "kernd_config.h"
#include "kern.h"

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the admin HTTP server.
 * Binds to the configured admin_port and sets up all routes.
 * Returns 0 on success, -1 on failure.
 */
int kernd_admin_start(kernd_config_t *cfg, uv_loop_t *loop, kern_db_t *db);

/**
 * Stop the admin HTTP server.
 */
void kernd_admin_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* KERND_ADMIN_H */
