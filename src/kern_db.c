/*
 * kern_db.c - SQLite database wrapper
 *
 * Provides a simple interface for SQLite operations with
 * WAL mode, prepared statements, and parameter binding.
 */

#include "kern.h"

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct kern_db {
    sqlite3 *handle;
};

/* Result row: array of string values (NULL for SQL NULL) */
typedef struct {
    char **values;
} kern_db_row_t;

struct kern_db_result {
    char **col_names;
    kern_db_row_t *rows;
    int col_count;
    int row_count;
    int row_cap;
};

kern_db_t *kern_db_open(const char *path) {
    if (!path) return NULL;

    kern_db_t *db = calloc(1, sizeof(kern_db_t));
    if (!db) return NULL;

    int rc = sqlite3_open(path, &db->handle);
    if (rc != SQLITE_OK) {
        free(db);
        return NULL;
    }

    /* Enable WAL mode */
    sqlite3_exec(db->handle, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    /* Set busy timeout to 5000ms */
    sqlite3_busy_timeout(db->handle, 5000);

    return db;
}

void kern_db_close(kern_db_t *db) {
    if (!db) return;
    if (db->handle) {
        sqlite3_close(db->handle);
    }
    free(db);
}

int kern_db_exec(kern_db_t *db, const char *sql) {
    if (!db || !sql) return -1;

    char *errmsg = NULL;
    int rc = sqlite3_exec(db->handle, sql, NULL, NULL, &errmsg);
    if (errmsg) {
        sqlite3_free(errmsg);
    }
    if (rc != SQLITE_OK) return -1;

    return sqlite3_changes(db->handle);
}

static kern_db_result_t *result_new(void) {
    kern_db_result_t *res = calloc(1, sizeof(kern_db_result_t));
    return res;
}

static void result_add_row(kern_db_result_t *res, sqlite3_stmt *stmt) {
    if (res->row_count >= res->row_cap) {
        int new_cap = res->row_cap == 0 ? 8 : res->row_cap * 2;
        kern_db_row_t *new_rows = realloc(res->rows, (size_t)new_cap * sizeof(kern_db_row_t));
        if (!new_rows) return;
        res->rows = new_rows;
        res->row_cap = new_cap;
    }

    kern_db_row_t *row = &res->rows[res->row_count];
    row->values = calloc((size_t)res->col_count, sizeof(char *));
    if (!row->values) return;

    for (int i = 0; i < res->col_count; i++) {
        const char *text = (const char *)sqlite3_column_text(stmt, i);
        if (text) {
            row->values[i] = strdup(text);
        } else {
            row->values[i] = NULL;
        }
    }
    res->row_count++;
}

kern_db_result_t *kern_db_query(kern_db_t *db, const char *sql) {
    if (!db || !sql) return NULL;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;

    kern_db_result_t *res = result_new();
    if (!res) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    res->col_count = sqlite3_column_count(stmt);
    res->col_names = calloc((size_t)res->col_count, sizeof(char *));
    if (!res->col_names) {
        free(res);
        sqlite3_finalize(stmt);
        return NULL;
    }

    for (int i = 0; i < res->col_count; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        res->col_names[i] = name ? strdup(name) : NULL;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result_add_row(res, stmt);
    }

    sqlite3_finalize(stmt);
    return res;
}

static void bind_params(sqlite3_stmt *stmt, const kern_db_param_t *params, int param_count) {
    for (int i = 0; i < param_count; i++) {
        switch (params[i].type) {
            case KERN_DB_PARAM_INT:
                sqlite3_bind_int64(stmt, i + 1, params[i].value.i);
                break;
            case KERN_DB_PARAM_STR:
                if (params[i].value.s) {
                    sqlite3_bind_text(stmt, i + 1, params[i].value.s, -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_null(stmt, i + 1);
                }
                break;
            case KERN_DB_PARAM_NULL:
                sqlite3_bind_null(stmt, i + 1);
                break;
            case KERN_DB_PARAM_DOUBLE:
                sqlite3_bind_double(stmt, i + 1, params[i].value.d);
                break;
        }
    }
}

int kern_db_exec_params(kern_db_t *db, const char *sql,
                        const kern_db_param_t *params, int param_count) {
    if (!db || !sql) return -1;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    if (params && param_count > 0) {
        bind_params(stmt, params, param_count);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) return -1;
    return sqlite3_changes(db->handle);
}

kern_db_result_t *kern_db_query_params(kern_db_t *db, const char *sql,
                                       const kern_db_param_t *params, int param_count) {
    if (!db || !sql) return NULL;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;

    if (params && param_count > 0) {
        bind_params(stmt, params, param_count);
    }

    kern_db_result_t *res = result_new();
    if (!res) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    res->col_count = sqlite3_column_count(stmt);
    res->col_names = calloc((size_t)res->col_count, sizeof(char *));
    if (!res->col_names) {
        free(res);
        sqlite3_finalize(stmt);
        return NULL;
    }

    for (int i = 0; i < res->col_count; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        res->col_names[i] = name ? strdup(name) : NULL;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result_add_row(res, stmt);
    }

    sqlite3_finalize(stmt);
    return res;
}

const char *kern_db_row_str(const kern_db_result_t *result, int row, int col) {
    if (!result) return NULL;
    if (row < 0 || row >= result->row_count) return NULL;
    if (col < 0 || col >= result->col_count) return NULL;
    return result->rows[row].values[col];
}

int64_t kern_db_row_int(const kern_db_result_t *result, int row, int col) {
    const char *s = kern_db_row_str(result, row, col);
    if (!s) return 0;
    return strtoll(s, NULL, 10);
}

int kern_db_result_row_count(const kern_db_result_t *result) {
    if (!result) return 0;
    return result->row_count;
}

int kern_db_result_col_count(const kern_db_result_t *result) {
    if (!result) return 0;
    return result->col_count;
}

const char *kern_db_result_col_name(const kern_db_result_t *result, int col) {
    if (!result || col < 0 || col >= result->col_count) return NULL;
    return result->col_names[col];
}

void kern_db_result_free(kern_db_result_t *result) {
    if (!result) return;

    for (int r = 0; r < result->row_count; r++) {
        if (result->rows[r].values) {
            for (int c = 0; c < result->col_count; c++) {
                free(result->rows[r].values[c]);
            }
            free(result->rows[r].values);
        }
    }
    free(result->rows);

    if (result->col_names) {
        for (int i = 0; i < result->col_count; i++) {
            free(result->col_names[i]);
        }
        free(result->col_names);
    }

    free(result);
}

int kern_db_begin(kern_db_t *db) {
    return kern_db_exec(db, "BEGIN TRANSACTION;");
}

int kern_db_commit(kern_db_t *db) {
    return kern_db_exec(db, "COMMIT;");
}

int kern_db_rollback(kern_db_t *db) {
    return kern_db_exec(db, "ROLLBACK;");
}
