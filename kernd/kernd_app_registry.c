/*
 * kernd_app_registry.c - SQLite-backed application registry
 *
 * Implements CRUD operations for the apps table using
 * libkern's database API (kern_db_*).
 */

#include "kernd_app_registry.h"
#include "kernd_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KERND_PORT_BASE 3001

static char *current_timestamp(void) {
    time_t now = time(NULL);
    struct tm tm_buf;
    gmtime_r(&now, &tm_buf);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return strdup(buf);
}

int kernd_registry_init(kern_db_t *db) {
    if (!db) {
        return -1;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS apps ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE,"
        "  repo_url TEXT,"
        "  branch TEXT,"
        "  build_cmd TEXT,"
        "  start_cmd TEXT,"
        "  port INTEGER NOT NULL,"
        "  domain TEXT,"
        "  status TEXT NOT NULL DEFAULT 'created',"
        "  created_at TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL"
        ")";

    int rc = kern_db_exec(db, sql);
    if (rc < 0) {
        kernd_log_error("failed to create apps table");
        return -1;
    }
    return 0;
}

int kernd_app_add(kern_db_t *db, kernd_app_t *app) {
    if (!db || !app) {
        return -1;
    }

    if (!app->name || app->name[0] == '\0') {
        return -1;
    }

    /* Check for duplicate name */
    kernd_app_t *existing = kernd_app_find_by_name(db, app->name);
    if (existing) {
        kernd_app_free(existing);
        return -1;
    }

    /* Auto-assign port: find max port in use */
    kern_db_result_t *res = kern_db_query(db, "SELECT MAX(port) FROM apps");
    int next_port = KERND_PORT_BASE;
    if (res) {
        int row_count = kern_db_result_row_count(res);
        if (row_count > 0) {
            int64_t max_port = kern_db_row_int(res, 0, 0);
            if (max_port >= KERND_PORT_BASE) {
                next_port = (int)max_port + 1;
            }
        }
        kern_db_result_free(res);
    }

    app->port = next_port;

    /* Set status */
    if (app->status) {
        free(app->status);
    }
    app->status = strdup("created");

    /* Set timestamps */
    char *ts = current_timestamp();
    if (app->created_at) {
        free(app->created_at);
    }
    app->created_at = strdup(ts);

    if (app->updated_at) {
        free(app->updated_at);
    }
    app->updated_at = strdup(ts);
    free(ts);

    /* Insert into database */
    const char *sql =
        "INSERT INTO apps (name, repo_url, branch, build_cmd, start_cmd, "
        "port, domain, status, created_at, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)";

    kern_db_param_t params[10];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = app->name;

    params[1].type = app->repo_url ? KERN_DB_PARAM_STR : KERN_DB_PARAM_NULL;
    params[1].value.s = app->repo_url;

    params[2].type = app->branch ? KERN_DB_PARAM_STR : KERN_DB_PARAM_NULL;
    params[2].value.s = app->branch;

    params[3].type = app->build_cmd ? KERN_DB_PARAM_STR : KERN_DB_PARAM_NULL;
    params[3].value.s = app->build_cmd;

    params[4].type = app->start_cmd ? KERN_DB_PARAM_STR : KERN_DB_PARAM_NULL;
    params[4].value.s = app->start_cmd;

    params[5].type = KERN_DB_PARAM_INT;
    params[5].value.i = app->port;

    params[6].type = app->domain ? KERN_DB_PARAM_STR : KERN_DB_PARAM_NULL;
    params[6].value.s = app->domain;

    params[7].type = KERN_DB_PARAM_STR;
    params[7].value.s = app->status;

    params[8].type = KERN_DB_PARAM_STR;
    params[8].value.s = app->created_at;

    params[9].type = KERN_DB_PARAM_STR;
    params[9].value.s = app->updated_at;

    int rc = kern_db_exec_params(db, sql, params, 10);
    if (rc < 0) {
        kernd_log_error("failed to insert app '%s'", app->name);
        return -1;
    }

    return 0;
}

kernd_app_t *kernd_app_find_by_name(kern_db_t *db, const char *name) {
    if (!db || !name) {
        return NULL;
    }

    const char *sql =
        "SELECT name, repo_url, branch, build_cmd, start_cmd, "
        "port, domain, status, created_at, updated_at "
        "FROM apps WHERE name = ?1";

    kern_db_param_t params[1];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = name;

    kern_db_result_t *res = kern_db_query_params(db, sql, params, 1);
    if (!res || kern_db_result_row_count(res) == 0) {
        if (res) {
            kern_db_result_free(res);
        }
        return NULL;
    }

    kernd_app_t *app = calloc(1, sizeof(kernd_app_t));
    if (!app) {
        kern_db_result_free(res);
        return NULL;
    }

    const char *val;

    val = kern_db_row_str(res, 0, 0);
    app->name = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 1);
    app->repo_url = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 2);
    app->branch = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 3);
    app->build_cmd = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 4);
    app->start_cmd = val ? strdup(val) : NULL;

    app->port = (int)kern_db_row_int(res, 0, 5);

    val = kern_db_row_str(res, 0, 6);
    app->domain = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 7);
    app->status = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 8);
    app->created_at = val ? strdup(val) : NULL;

    val = kern_db_row_str(res, 0, 9);
    app->updated_at = val ? strdup(val) : NULL;

    kern_db_result_free(res);
    return app;
}

kern_db_result_t *kernd_app_list(kern_db_t *db) {
    if (!db) {
        return NULL;
    }

    const char *sql =
        "SELECT id, name, repo_url, branch, build_cmd, start_cmd, "
        "port, domain, status, created_at, updated_at "
        "FROM apps ORDER BY name";

    return kern_db_query(db, sql);
}

int kernd_app_update_status(kern_db_t *db, const char *name, const char *status) {
    if (!db || !name || !status) {
        return -1;
    }

    char *ts = current_timestamp();
    const char *sql =
        "UPDATE apps SET status = ?1, updated_at = ?2 WHERE name = ?3";

    kern_db_param_t params[3];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = status;

    params[1].type = KERN_DB_PARAM_STR;
    params[1].value.s = ts;

    params[2].type = KERN_DB_PARAM_STR;
    params[2].value.s = name;

    int rc = kern_db_exec_params(db, sql, params, 3);
    free(ts);

    if (rc < 0) {
        kernd_log_error("failed to update status for '%s'", name);
        return -1;
    }
    return 0;
}

int kernd_app_remove(kern_db_t *db, const char *name) {
    if (!db || !name) {
        return -1;
    }

    const char *sql = "DELETE FROM apps WHERE name = ?1";

    kern_db_param_t params[1];
    params[0].type = KERN_DB_PARAM_STR;
    params[0].value.s = name;

    int rc = kern_db_exec_params(db, sql, params, 1);
    if (rc < 0) {
        kernd_log_error("failed to remove app '%s'", name);
        return -1;
    }
    return 0;
}

void kernd_app_free(kernd_app_t *app) {
    if (!app) {
        return;
    }
    free(app->name);
    free(app->repo_url);
    free(app->branch);
    free(app->build_cmd);
    free(app->start_cmd);
    free(app->domain);
    free(app->status);
    free(app->created_at);
    free(app->updated_at);
    free(app);
}
