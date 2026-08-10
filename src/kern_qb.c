/*
 * kern_qb.c - SQL query builder
 *
 * Chainable API for building parameterized SQL queries.
 * Generates safe SQL with parameter binding.
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Query types */
typedef enum {
    QB_SELECT,
    QB_INSERT,
    QB_UPDATE,
    QB_DELETE
} kern_qb_type_t;

/* WHERE clause entry */
typedef struct {
    char *clause;           /* e.g., "id = ?" */
} kern_qb_where_t;

/* Column-value pair for INSERT/UPDATE */
typedef struct {
    char *column;
    kern_db_param_t param;
} kern_qb_field_t;

struct kern_qb {
    kern_db_t *db;
    kern_qb_type_t type;

    char *table;
    char *columns;          /* SELECT columns */

    kern_qb_where_t *wheres;
    int where_count;
    int where_cap;

    kern_db_param_t *where_params;
    int where_param_count;
    int where_param_cap;

    kern_qb_field_t *fields;
    int field_count;
    int field_cap;

    char *order_col;
    char *order_dir;
    int limit_val;
    int offset_val;
    bool has_limit;
    bool has_offset;

    kern_buf_t *sql_buf;    /* Generated SQL */
};

kern_qb_t *kern_qb_new(kern_db_t *db) {
    kern_qb_t *qb = calloc(1, sizeof(kern_qb_t));
    if (!qb) return NULL;
    qb->db = db;
    qb->type = QB_SELECT;
    qb->sql_buf = kern_buf_new(256);
    return qb;
}

void kern_qb_free(kern_qb_t *qb) {
    if (!qb) return;

    free(qb->table);
    free(qb->columns);
    free(qb->order_col);
    free(qb->order_dir);

    for (int i = 0; i < qb->where_count; i++) {
        free(qb->wheres[i].clause);
    }
    free(qb->wheres);

    /* Free string params in where_params */
    for (int i = 0; i < qb->where_param_count; i++) {
        if (qb->where_params[i].type == KERN_DB_PARAM_STR) {
            free((char *)qb->where_params[i].value.s);
        }
    }
    free(qb->where_params);

    for (int i = 0; i < qb->field_count; i++) {
        free(qb->fields[i].column);
        if (qb->fields[i].param.type == KERN_DB_PARAM_STR) {
            free((char *)qb->fields[i].param.value.s);
        }
    }
    free(qb->fields);

    kern_buf_free(qb->sql_buf);
    free(qb);
}

void kern_qb_select(kern_qb_t *qb, const char *columns) {
    if (!qb || !columns) return;
    qb->type = QB_SELECT;
    free(qb->columns);
    qb->columns = strdup(columns);
}

void kern_qb_from(kern_qb_t *qb, const char *table) {
    if (!qb || !table) return;
    free(qb->table);
    qb->table = strdup(table);
}

static void qb_add_where(kern_qb_t *qb, const char *clause) {
    if (qb->where_count >= qb->where_cap) {
        int new_cap = qb->where_cap == 0 ? 4 : qb->where_cap * 2;
        kern_qb_where_t *new_w = realloc(qb->wheres, (size_t)new_cap * sizeof(kern_qb_where_t));
        if (!new_w) return;
        qb->wheres = new_w;
        qb->where_cap = new_cap;
    }
    qb->wheres[qb->where_count].clause = strdup(clause);
    qb->where_count++;
}

static void qb_add_where_param(kern_qb_t *qb, kern_db_param_t param) {
    if (qb->where_param_count >= qb->where_param_cap) {
        int new_cap = qb->where_param_cap == 0 ? 4 : qb->where_param_cap * 2;
        kern_db_param_t *new_p = realloc(qb->where_params, (size_t)new_cap * sizeof(kern_db_param_t));
        if (!new_p) return;
        qb->where_params = new_p;
        qb->where_param_cap = new_cap;
    }
    qb->where_params[qb->where_param_count] = param;
    qb->where_param_count++;
}

void kern_qb_where_str(kern_qb_t *qb, const char *col, const char *op, const char *val) {
    if (!qb || !col || !op) return;

    /* Build clause like "col op ?" */
    size_t len = strlen(col) + strlen(op) + 4;
    char *clause = malloc(len);
    if (!clause) return;
    snprintf(clause, len, "%s %s ?", col, op);
    qb_add_where(qb, clause);
    free(clause);

    kern_db_param_t p;
    p.type = KERN_DB_PARAM_STR;
    p.value.s = val ? strdup(val) : NULL;
    qb_add_where_param(qb, p);
}

void kern_qb_where_int(kern_qb_t *qb, const char *col, const char *op, int64_t val) {
    if (!qb || !col || !op) return;

    size_t len = strlen(col) + strlen(op) + 4;
    char *clause = malloc(len);
    if (!clause) return;
    snprintf(clause, len, "%s %s ?", col, op);
    qb_add_where(qb, clause);
    free(clause);

    kern_db_param_t p;
    p.type = KERN_DB_PARAM_INT;
    p.value.i = val;
    qb_add_where_param(qb, p);
}

void kern_qb_order(kern_qb_t *qb, const char *col, const char *direction) {
    if (!qb || !col) return;
    free(qb->order_col);
    free(qb->order_dir);
    qb->order_col = strdup(col);
    qb->order_dir = direction ? strdup(direction) : strdup("ASC");
}

void kern_qb_limit(kern_qb_t *qb, int n) {
    if (!qb) return;
    qb->limit_val = n;
    qb->has_limit = true;
}

void kern_qb_offset(kern_qb_t *qb, int n) {
    if (!qb) return;
    qb->offset_val = n;
    qb->has_offset = true;
}

void kern_qb_insert(kern_qb_t *qb, const char *table) {
    if (!qb || !table) return;
    qb->type = QB_INSERT;
    free(qb->table);
    qb->table = strdup(table);
}

void kern_qb_update(kern_qb_t *qb, const char *table) {
    if (!qb || !table) return;
    qb->type = QB_UPDATE;
    free(qb->table);
    qb->table = strdup(table);
}

void kern_qb_delete(kern_qb_t *qb, const char *table) {
    if (!qb || !table) return;
    qb->type = QB_DELETE;
    free(qb->table);
    qb->table = strdup(table);
}

static void qb_add_field(kern_qb_t *qb, const char *col, kern_db_param_t param) {
    if (qb->field_count >= qb->field_cap) {
        int new_cap = qb->field_cap == 0 ? 8 : qb->field_cap * 2;
        kern_qb_field_t *new_f = realloc(qb->fields, (size_t)new_cap * sizeof(kern_qb_field_t));
        if (!new_f) return;
        qb->fields = new_f;
        qb->field_cap = new_cap;
    }
    qb->fields[qb->field_count].column = strdup(col);
    qb->fields[qb->field_count].param = param;
    qb->field_count++;
}

void kern_qb_set_str(kern_qb_t *qb, const char *col, const char *val) {
    if (!qb || !col) return;
    kern_db_param_t p;
    p.type = KERN_DB_PARAM_STR;
    p.value.s = val ? strdup(val) : NULL;
    qb_add_field(qb, col, p);
}

void kern_qb_set_int(kern_qb_t *qb, const char *col, int64_t val) {
    if (!qb || !col) return;
    kern_db_param_t p;
    p.type = KERN_DB_PARAM_INT;
    p.value.i = val;
    qb_add_field(qb, col, p);
}

const char *kern_qb_build(kern_qb_t *qb) {
    if (!qb || !qb->table) return NULL;

    kern_buf_reset(qb->sql_buf);

    switch (qb->type) {
        case QB_SELECT:
            kern_buf_writef(qb->sql_buf, "SELECT %s FROM %s",
                           qb->columns ? qb->columns : "*",
                           qb->table);
            break;

        case QB_INSERT: {
            kern_buf_writef(qb->sql_buf, "INSERT INTO %s (", qb->table);
            for (int i = 0; i < qb->field_count; i++) {
                if (i > 0) kern_buf_writes(qb->sql_buf, ", ");
                kern_buf_writes(qb->sql_buf, qb->fields[i].column);
            }
            kern_buf_writes(qb->sql_buf, ") VALUES (");
            for (int i = 0; i < qb->field_count; i++) {
                if (i > 0) kern_buf_writes(qb->sql_buf, ", ");
                kern_buf_writes(qb->sql_buf, "?");
            }
            kern_buf_writes(qb->sql_buf, ")");
            break;
        }

        case QB_UPDATE: {
            kern_buf_writef(qb->sql_buf, "UPDATE %s SET ", qb->table);
            for (int i = 0; i < qb->field_count; i++) {
                if (i > 0) kern_buf_writes(qb->sql_buf, ", ");
                kern_buf_writef(qb->sql_buf, "%s = ?", qb->fields[i].column);
            }
            break;
        }

        case QB_DELETE:
            kern_buf_writef(qb->sql_buf, "DELETE FROM %s", qb->table);
            break;
    }

    /* WHERE clauses */
    if (qb->where_count > 0) {
        kern_buf_writes(qb->sql_buf, " WHERE ");
        for (int i = 0; i < qb->where_count; i++) {
            if (i > 0) kern_buf_writes(qb->sql_buf, " AND ");
            kern_buf_writes(qb->sql_buf, qb->wheres[i].clause);
        }
    }

    /* ORDER BY */
    if (qb->order_col) {
        kern_buf_writef(qb->sql_buf, " ORDER BY %s %s", qb->order_col, qb->order_dir);
    }

    /* LIMIT */
    if (qb->has_limit) {
        kern_buf_writef(qb->sql_buf, " LIMIT %d", qb->limit_val);
    }

    /* OFFSET */
    if (qb->has_offset) {
        kern_buf_writef(qb->sql_buf, " OFFSET %d", qb->offset_val);
    }

    return kern_buf_data(qb->sql_buf);
}

/* Build all params array for execution */
static kern_db_param_t *qb_build_params(kern_qb_t *qb, int *out_count) {
    int total = 0;

    /* For INSERT/UPDATE, field params come first */
    if (qb->type == QB_INSERT || qb->type == QB_UPDATE) {
        total += qb->field_count;
    }

    /* Then WHERE params */
    total += qb->where_param_count;

    if (total == 0) {
        *out_count = 0;
        return NULL;
    }

    kern_db_param_t *params = calloc((size_t)total, sizeof(kern_db_param_t));
    if (!params) {
        *out_count = 0;
        return NULL;
    }

    int idx = 0;
    if (qb->type == QB_INSERT || qb->type == QB_UPDATE) {
        for (int i = 0; i < qb->field_count; i++) {
            params[idx++] = qb->fields[i].param;
        }
    }
    for (int i = 0; i < qb->where_param_count; i++) {
        params[idx++] = qb->where_params[i];
    }

    *out_count = total;
    return params;
}

int kern_qb_exec(kern_qb_t *qb) {
    if (!qb || !qb->db) return -1;

    const char *sql = kern_qb_build(qb);
    if (!sql) return -1;

    int param_count = 0;
    kern_db_param_t *params = qb_build_params(qb, &param_count);

    int result = kern_db_exec_params(qb->db, sql, params, param_count);
    free(params);
    return result;
}

kern_db_result_t *kern_qb_query(kern_qb_t *qb) {
    if (!qb || !qb->db) return NULL;

    const char *sql = kern_qb_build(qb);
    if (!sql) return NULL;

    int param_count = 0;
    kern_db_param_t *params = qb_build_params(qb, &param_count);

    kern_db_result_t *result = kern_db_query_params(qb->db, sql, params, param_count);
    free(params);
    return result;
}

kern_db_result_t *kern_qb_one(kern_qb_t *qb) {
    if (!qb) return NULL;
    kern_qb_limit(qb, 1);
    return kern_qb_query(qb);
}
