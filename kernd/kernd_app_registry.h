/*
 * kernd_app_registry.h - Application registry for kernd
 *
 * SQLite-backed registry for managing deployed applications.
 * Tracks app metadata, assigned ports, and deployment status.
 */

#ifndef KERND_APP_REGISTRY_H
#define KERND_APP_REGISTRY_H

#include "kern.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * kernd_app_t - Registered application metadata.
 *
 * All string fields are heap-allocated and owned by the struct
 * (caller must free individually, or use kernd_app_free for
 * a fully-allocated kernd_app_t*).
 */
typedef struct {
    char *name;
    char *repo_url;
    char *branch;
    char *build_cmd;
    char *start_cmd;
    int port;
    char *domain;
    char *status;
    char *created_at;
    char *updated_at;
} kernd_app_t;

/**
 * Initialize the app registry table in the database.
 * Creates the 'apps' table if it does not exist (idempotent).
 * Returns 0 on success, -1 on failure or if db is NULL.
 */
int kernd_registry_init(kern_db_t *db);

/**
 * Add an application to the registry.
 * Auto-assigns a port starting at 3001 (increments based on existing apps).
 * Sets status to "created" and populates created_at/updated_at timestamps.
 * Returns 0 on success, -1 on failure (NULL params, missing name, duplicate name).
 */
int kernd_app_add(kern_db_t *db, kernd_app_t *app);

/**
 * Find an application by name.
 * Returns a heap-allocated kernd_app_t or NULL if not found.
 * Caller must free with kernd_app_free().
 */
kernd_app_t *kernd_app_find_by_name(kern_db_t *db, const char *name);

/**
 * List all applications, ordered by name.
 * Returns a result set (caller must free with kern_db_result_free()).
 * Column 0 is rowid/id, column 1 is name.
 */
kern_db_result_t *kernd_app_list(kern_db_t *db);

/**
 * Update the status of an application.
 * Also updates the updated_at timestamp.
 * Returns 0 on success, -1 on failure.
 */
int kernd_app_update_status(kern_db_t *db, const char *name, const char *status);

/**
 * Remove an application from the registry.
 * Returns 0 on success, -1 on failure.
 */
int kernd_app_remove(kern_db_t *db, const char *name);

/**
 * Free a heap-allocated kernd_app_t (and all owned strings).
 */
void kernd_app_free(kernd_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* KERND_APP_REGISTRY_H */
