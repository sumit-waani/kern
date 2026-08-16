/*
 * kern_scheduler.c - Cron-style task scheduler with background thread.
 * Dispatches matching jobs to a kern_queue_t every 60 seconds.
 */

#include "kern.h"

#include <ctype.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Internal Types
 * ============================================================ */

typedef struct {
    char *cron_expr;
    char *job_name;
} kern_schedule_entry_t;

struct kern_scheduler {
    kern_queue_t *queue;  /* Not owned, not freed */

    kern_schedule_entry_t *entries;
    int entry_count;
    int entry_cap;

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool running;
    bool started;
};

/* ============================================================
 * Cron Expression Parsing
 * ============================================================ */

/*
 * Parse a single cron field and check if the given value matches.
 * Supports: * (any), specific value (5), ranges (1-5),
 * steps (x/n where x is * or a range).
 *
 * Returns true if value matches the field.
 */
static bool cron_field_matches(const char *field, int value) {
    if (!field) return false;

    /* Wildcard */
    if (strcmp(field, "*") == 0) return true;

    /* Step on wildcard: *//*15 */
    if (field[0] == '*' && field[1] == '/') {
        int step = atoi(field + 2);
        if (step <= 0) return false;
        return (value % step) == 0;
    }

    /* Could be comma-separated list or single value/range/step */
    /* We'll parse comma-separated parts */
    const char *p = field;
    while (*p) {
        /* Parse one part */
        int low = 0, high = 0, step = 1;
        bool is_range = false;

        /* Read first number */
        if (!isdigit((unsigned char)*p)) return false;
        low = atoi(p);
        while (isdigit((unsigned char)*p)) p++;

        if (*p == '-') {
            /* Range */
            p++;
            if (!isdigit((unsigned char)*p)) return false;
            high = atoi(p);
            while (isdigit((unsigned char)*p)) p++;
            is_range = true;
        }

        if (*p == '/') {
            /* Step */
            p++;
            step = atoi(p);
            if (step <= 0) return false;
            while (isdigit((unsigned char)*p)) p++;
            if (!is_range) {
                is_range = true;
                high = 59; /* Default upper bound */
            }
        }

        /* Check match */
        if (is_range) {
            if (value >= low && value <= high &&
                ((value - low) % step) == 0) {
                return true;
            }
        } else {
            if (value == low) return true;
        }

        /* Next part (comma separated) */
        if (*p == ',') {
            p++;
        } else {
            break;
        }
    }

    return false;
}

bool kern_cron_matches(const char *expr, struct tm *now) {
    if (!expr || !now) return false;

    /* Copy the expression to tokenize */
    char buf[256];
    size_t len = strlen(expr);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, expr, len + 1);

    /* Split into 5 fields: min hour dom month dow */
    char *fields[5];
    int count = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(buf, " \t", &saveptr);
    while (tok && count < 5) {
        fields[count++] = tok;
        tok = strtok_r(NULL, " \t", &saveptr);
    }

    if (count != 5) return false;

    /* Check each field */
    if (!cron_field_matches(fields[0], now->tm_min)) return false;
    if (!cron_field_matches(fields[1], now->tm_hour)) return false;
    if (!cron_field_matches(fields[2], now->tm_mday)) return false;
    if (!cron_field_matches(fields[3], now->tm_mon + 1)) return false;
    if (!cron_field_matches(fields[4], now->tm_wday)) return false;

    return true;
}

/* ============================================================
 * Background Thread
 * ============================================================ */

static void *scheduler_thread(void *arg) {
    kern_scheduler_t *sched = (kern_scheduler_t *)arg;

    pthread_mutex_lock(&sched->mutex);

    while (sched->running) {
        /* Calculate timeout: sleep until the next minute boundary */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec = (ts.tv_sec / 60 + 1) * 60;
        ts.tv_nsec = 0;

        /* Wait for signal or timeout */
        int rc = pthread_cond_timedwait(&sched->cond, &sched->mutex, &ts);
        (void)rc;

        if (!sched->running) break;

        /* Get current time */
        time_t t = time(NULL);
        struct tm now;
        localtime_r(&t, &now);

        /* Check each entry */
        for (int i = 0; i < sched->entry_count; i++) {
            if (kern_cron_matches(sched->entries[i].cron_expr, &now)) {
                kern_queue_dispatch(sched->queue,
                                    sched->entries[i].job_name, NULL, 0);
            }
        }
    }

    pthread_mutex_unlock(&sched->mutex);
    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */

kern_scheduler_t *kern_scheduler_new(kern_queue_t *queue) {
    if (!queue) return NULL;

    kern_scheduler_t *sched = calloc(1, sizeof(kern_scheduler_t));
    if (!sched) return NULL;

    sched->queue = queue;
    sched->entry_cap = 16;
    sched->entries = calloc((size_t)sched->entry_cap,
                            sizeof(kern_schedule_entry_t));
    if (!sched->entries) {
        free(sched);
        return NULL;
    }
    sched->entry_count = 0;

    if (pthread_mutex_init(&sched->mutex, NULL) != 0) {
        free(sched->entries);
        free(sched);
        return NULL;
    }

    if (pthread_cond_init(&sched->cond, NULL) != 0) {
        pthread_mutex_destroy(&sched->mutex);
        free(sched->entries);
        free(sched);
        return NULL;
    }

    sched->running = false;
    sched->started = false;

    return sched;
}

int kern_scheduler_add(kern_scheduler_t *sched, const char *cron_expr,
                       const char *job_name) {
    if (!sched || !cron_expr || !job_name) return -1;

    /* Validate cron expression at registration time by checking
     * that it has exactly 5 whitespace-separated fields. */
    {
        char buf[256];
        size_t len = strlen(cron_expr);
        if (len == 0 || len >= sizeof(buf)) return -1;
        memcpy(buf, cron_expr, len + 1);

        char *fields[5];
        int fcount = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(buf, " \t", &saveptr);
        while (tok && fcount < 5) {
            fields[fcount++] = tok;
            tok = strtok_r(NULL, " \t", &saveptr);
        }
        if (fcount != 5) return -1;
    }

    /* Grow array if needed */
    if (sched->entry_count >= sched->entry_cap) {
        int new_cap = sched->entry_cap * 2;
        kern_schedule_entry_t *new_entries = realloc(
            sched->entries,
            (size_t)new_cap * sizeof(kern_schedule_entry_t));
        if (!new_entries) return -1;
        sched->entries = new_entries;
        sched->entry_cap = new_cap;
    }

    kern_schedule_entry_t *entry = &sched->entries[sched->entry_count];
    entry->cron_expr = strdup(cron_expr);
    entry->job_name = strdup(job_name);
    if (!entry->cron_expr || !entry->job_name) {
        free(entry->cron_expr);
        free(entry->job_name);
        return -1;
    }

    sched->entry_count++;
    return 0;
}

int kern_scheduler_start(kern_scheduler_t *sched) {
    if (!sched) return -1;
    if (sched->started) return -1;

    sched->running = true;
    sched->started = true;

    if (pthread_create(&sched->thread, NULL, scheduler_thread, sched) != 0) {
        sched->running = false;
        sched->started = false;
        return -1;
    }

    return 0;
}

void kern_scheduler_stop(kern_scheduler_t *sched) {
    if (!sched) return;
    if (!sched->started) return;

    pthread_mutex_lock(&sched->mutex);
    sched->running = false;
    pthread_cond_signal(&sched->cond);
    pthread_mutex_unlock(&sched->mutex);

    pthread_join(sched->thread, NULL);
    sched->started = false;
}

void kern_scheduler_free(kern_scheduler_t *sched) {
    if (!sched) return;

    if (sched->started) {
        kern_scheduler_stop(sched);
    }

    for (int i = 0; i < sched->entry_count; i++) {
        free(sched->entries[i].cron_expr);
        free(sched->entries[i].job_name);
    }
    free(sched->entries);

    pthread_mutex_destroy(&sched->mutex);
    pthread_cond_destroy(&sched->cond);
    free(sched);
}
