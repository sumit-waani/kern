/*
 * kern_migrate.c - SQL migration runner
 *
 * Reads and applies migration files from a directory.
 * Supports both single-file (NNN_name.sql) and directory
 * (NNN_name/up.sql + down.sql) migration formats.
 */

#include "kern.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Read entire file into malloc'd string */
static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

/* Compare migration names for sorting */
static int migration_cmp(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

int kern_migrate_init(kern_db_t *db) {
    if (!db) return -1;
    return kern_db_exec(db,
        "CREATE TABLE IF NOT EXISTS kern_migrations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL UNIQUE,"
        "  applied_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");");
}

/* Check if a migration has been applied */
static bool is_applied(kern_db_t *db, const char *name) {
    kern_db_param_t p;
    p.type = KERN_DB_PARAM_STR;
    p.value.s = name;

    kern_db_result_t *res = kern_db_query_params(db,
        "SELECT id FROM kern_migrations WHERE name = ?", &p, 1);
    if (!res) return false;

    bool applied = kern_db_result_row_count(res) > 0;
    kern_db_result_free(res);
    return applied;
}

/* Scan migration directory and return sorted list of migration names */
static char **scan_migrations(const char *dir, int *out_count) {
    DIR *d = opendir(dir);
    if (!d) {
        *out_count = 0;
        return NULL;
    }

    char **names = NULL;
    int count = 0;
    int cap = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        /* Must start with a digit */
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;

        /* Check if it's a .sql file or a directory */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        bool valid = false;
        if (S_ISREG(st.st_mode)) {
            /* Single file: must end with .sql */
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".sql") == 0) {
                valid = true;
            }
        } else if (S_ISDIR(st.st_mode)) {
            /* Directory: must contain up.sql */
            char up_path[1024];
            snprintf(up_path, sizeof(up_path), "%s/%s/up.sql", dir, ent->d_name);
            struct stat up_st;
            if (stat(up_path, &up_st) == 0 && S_ISREG(up_st.st_mode)) {
                valid = true;
            }
        }

        if (!valid) continue;

        if (count >= cap) {
            cap = cap == 0 ? 16 : cap * 2;
            char **new_names = realloc(names, (size_t)cap * sizeof(char *));
            if (!new_names) break;
            names = new_names;
        }
        names[count++] = strdup(ent->d_name);
    }
    closedir(d);

    /* Sort migrations by name (numeric prefix) */
    if (names && count > 1) {
        qsort(names, (size_t)count, sizeof(char *), migration_cmp);
    }

    *out_count = count;
    return names;
}

/* Get the migration name without .sql extension (for single-file migrations) */
static char *migration_name(const char *filename) {
    char *name = strdup(filename);
    if (!name) return NULL;

    /* If it ends with .sql, strip the extension */
    size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".sql") == 0) {
        name[len - 4] = '\0';
    }
    return name;
}

int kern_migrate_up(kern_db_t *db, const char *migrations_dir) {
    if (!db || !migrations_dir) return -1;

    kern_migrate_init(db);

    int count = 0;
    char **files = scan_migrations(migrations_dir, &count);
    if (!files || count == 0) {
        free(files);
        return 0;
    }

    int applied = 0;
    for (int i = 0; i < count; i++) {
        char *name = migration_name(files[i]);
        if (!name) continue;

        if (is_applied(db, name)) {
            free(name);
            continue;
        }

        /* Determine the SQL to run */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", migrations_dir, files[i]);

        struct stat st;
        char *sql = NULL;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            /* Directory format: read up.sql */
            char up_path[1024];
            snprintf(up_path, sizeof(up_path), "%s/up.sql", path);
            sql = read_file_contents(up_path);
        } else {
            /* Single file format */
            sql = read_file_contents(path);
        }

        if (!sql) {
            free(name);
            continue;
        }

        /* Run in a transaction */
        kern_db_begin(db);
        int rc = kern_db_exec(db, sql);
        free(sql);

        if (rc < 0) {
            kern_db_rollback(db);
            free(name);
            continue;
        }

        /* Record migration */
        kern_db_param_t p;
        p.type = KERN_DB_PARAM_STR;
        p.value.s = name;
        kern_db_exec_params(db,
            "INSERT INTO kern_migrations (name) VALUES (?)", &p, 1);

        kern_db_commit(db);
        applied++;
        free(name);
    }

    for (int i = 0; i < count; i++) free(files[i]);
    free(files);

    return applied;
}

int kern_migrate_down(kern_db_t *db, const char *migrations_dir) {
    if (!db || !migrations_dir) return -1;

    kern_migrate_init(db);

    /* Get the last applied migration */
    kern_db_result_t *res = kern_db_query(db,
        "SELECT name FROM kern_migrations ORDER BY id DESC LIMIT 1");
    if (!res || kern_db_result_row_count(res) == 0) {
        kern_db_result_free(res);
        return 0;
    }

    const char *last_name = kern_db_row_str(res, 0, 0);
    if (!last_name) {
        kern_db_result_free(res);
        return 0;
    }

    char *name_copy = strdup(last_name);
    kern_db_result_free(res);

    /* Find the down.sql for this migration */
    char path[1024];

    /* Try directory format first */
    snprintf(path, sizeof(path), "%s/%s/down.sql", migrations_dir, name_copy);
    char *sql = read_file_contents(path);

    if (!sql) {
        /* No down.sql available for this migration */
        free(name_copy);
        return -1;
    }

    /* Run the down migration */
    kern_db_begin(db);
    int rc = kern_db_exec(db, sql);
    free(sql);

    if (rc < 0) {
        kern_db_rollback(db);
        free(name_copy);
        return -1;
    }

    /* Remove migration record */
    kern_db_param_t p;
    p.type = KERN_DB_PARAM_STR;
    p.value.s = name_copy;
    kern_db_exec_params(db,
        "DELETE FROM kern_migrations WHERE name = ?", &p, 1);

    kern_db_commit(db);
    free(name_copy);
    return 1;
}

kern_db_result_t *kern_migrate_status(kern_db_t *db, const char *migrations_dir) {
    if (!db || !migrations_dir) return NULL;

    kern_migrate_init(db);

    /* Build a result showing all migrations and their status */
    int count = 0;
    char **files = scan_migrations(migrations_dir, &count);

    /* Create a temporary table with status info and query it */
    kern_db_exec(db, "CREATE TEMP TABLE IF NOT EXISTS _mig_status (name TEXT, status TEXT)");
    kern_db_exec(db, "DELETE FROM _mig_status");

    for (int i = 0; i < count; i++) {
        char *name = migration_name(files[i]);
        if (!name) continue;

        const char *status = is_applied(db, name) ? "applied" : "pending";

        kern_db_param_t params[2];
        params[0].type = KERN_DB_PARAM_STR;
        params[0].value.s = name;
        params[1].type = KERN_DB_PARAM_STR;
        params[1].value.s = status;
        kern_db_exec_params(db,
            "INSERT INTO _mig_status (name, status) VALUES (?, ?)", params, 2);
        free(name);
    }

    for (int i = 0; i < count; i++) free(files[i]);
    free(files);

    kern_db_result_t *result = kern_db_query(db, "SELECT name, status FROM _mig_status ORDER BY name");
    kern_db_exec(db, "DROP TABLE IF EXISTS _mig_status");
    return result;
}
