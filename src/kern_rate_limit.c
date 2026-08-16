/*
 * kern_rate_limit.c - Token bucket rate limiter
 *
 * Provides per-key (IP/route) rate limiting using the token bucket
 * algorithm. Buckets are stored in-memory using kern_dict_t.
 * Uses CLOCK_MONOTONIC for accurate elapsed time calculations.
 * All public functions are protected by a pthread_mutex_t for
 * thread safety under concurrent access.
 */

#include "kern.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    double tokens;
    double max_tokens;
    double refill_rate;
    struct timespec last_refill;
} kern_bucket_t;

struct kern_rate_limiter {
    kern_dict_t *buckets;
    double max_tokens;
    double refill_rate;
    struct timespec last_cleanup;
    pthread_mutex_t mutex;
};

kern_rate_limiter_t *kern_rate_limiter_new(double max_tokens,
                                           double refill_rate) {
    kern_rate_limiter_t *rl = calloc(1, sizeof(kern_rate_limiter_t));
    if (!rl) return NULL;

    rl->buckets = kern_dict_new_with_free(free);
    if (!rl->buckets) {
        free(rl);
        return NULL;
    }

    if (pthread_mutex_init(&rl->mutex, NULL) != 0) {
        kern_dict_free(rl->buckets);
        free(rl);
        return NULL;
    }

    rl->max_tokens = max_tokens;
    rl->refill_rate = refill_rate;
    clock_gettime(CLOCK_MONOTONIC, &rl->last_cleanup);

    return rl;
}

void kern_rate_limiter_free(kern_rate_limiter_t *rl) {
    if (!rl) return;
    pthread_mutex_destroy(&rl->mutex);
    kern_dict_free(rl->buckets);
    free(rl);
}

bool kern_rate_limiter_check(kern_rate_limiter_t *rl, const char *key) {
    if (!rl || !key) return false;

    pthread_mutex_lock(&rl->mutex);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    kern_bucket_t *bucket = kern_dict_get(rl->buckets, key);
    if (!bucket) {
        bucket = calloc(1, sizeof(kern_bucket_t));
        if (!bucket) {
            pthread_mutex_unlock(&rl->mutex);
            return false;
        }
        bucket->tokens = rl->max_tokens;
        bucket->max_tokens = rl->max_tokens;
        bucket->refill_rate = rl->refill_rate;
        bucket->last_refill = now;
        kern_dict_set(rl->buckets, key, bucket);
    }

    /* Calculate elapsed time in seconds */
    double elapsed = (double)(now.tv_sec - bucket->last_refill.tv_sec)
        + (double)(now.tv_nsec - bucket->last_refill.tv_nsec) / 1e9;

    /* Refill tokens */
    if (elapsed > 0) {
        bucket->tokens += elapsed * bucket->refill_rate;
        if (bucket->tokens > bucket->max_tokens) {
            bucket->tokens = bucket->max_tokens;
        }
        bucket->last_refill = now;
    }

    /* Try to consume a token */
    if (bucket->tokens >= 1.0) {
        bucket->tokens -= 1.0;
        pthread_mutex_unlock(&rl->mutex);
        return true;
    }

    pthread_mutex_unlock(&rl->mutex);
    return false;
}

kern_response_t *kern_rate_limit_check(kern_rate_limiter_t *rl,
                                       const char *key) {
    if (!rl || !key) return NULL;

    /* Delegate to kern_rate_limiter_check which handles locking
     * and the full token bucket algorithm. */
    if (kern_rate_limiter_check(rl, key)) {
        return NULL; /* allowed */
    }

    /* Rate limited - build 429 response.
     * Compute Retry-After from the bucket state (under lock). */
    pthread_mutex_lock(&rl->mutex);
    kern_bucket_t *bucket = kern_dict_get(rl->buckets, key);
    double wait = 1.0; /* default */
    if (bucket && bucket->refill_rate > 0) {
        wait = (1.0 - bucket->tokens) / bucket->refill_rate;
    }
    pthread_mutex_unlock(&rl->mutex);

    int retry_after = (int)ceil(wait);
    if (retry_after < 1) retry_after = 1;

    kern_response_t *res = kern_response_new(429);
    if (!res) return NULL;

    kern_response_header(res, "Content-Type", "text/plain");

    char retry_str[32];
    snprintf(retry_str, sizeof(retry_str), "%d", retry_after);
    kern_response_header(res, "Retry-After", retry_str);
    kern_response_body_str(res, "429 Too Many Requests");

    return res;
}

/* Context for cleanup iteration */
typedef struct {
    const char **keys_to_remove;
    size_t count;
    size_t capacity;
    struct timespec now;
    double idle_threshold;
} cleanup_ctx_t;

static bool cleanup_iter(const char *key, void *value, void *userdata) {
    cleanup_ctx_t *ctx = (cleanup_ctx_t *)userdata;
    kern_bucket_t *bucket = (kern_bucket_t *)value;

    double elapsed = (double)(ctx->now.tv_sec - bucket->last_refill.tv_sec)
        + (double)(ctx->now.tv_nsec - bucket->last_refill.tv_nsec) / 1e9;

    /* Remove if bucket is full and idle for > threshold seconds */
    double refilled = bucket->tokens + elapsed * bucket->refill_rate;
    if (refilled >= bucket->max_tokens && elapsed > ctx->idle_threshold) {
        if (ctx->count < ctx->capacity) {
            ctx->keys_to_remove[ctx->count++] = key;
        }
    }

    return true; /* continue iteration */
}

void kern_rate_limiter_cleanup(kern_rate_limiter_t *rl) {
    if (!rl) return;

    pthread_mutex_lock(&rl->mutex);

    size_t count = kern_dict_count(rl->buckets);
    if (count == 0) {
        pthread_mutex_unlock(&rl->mutex);
        return;
    }

    cleanup_ctx_t ctx;
    ctx.keys_to_remove = calloc(count, sizeof(const char *));
    if (!ctx.keys_to_remove) {
        pthread_mutex_unlock(&rl->mutex);
        return;
    }
    ctx.count = 0;
    ctx.capacity = count;
    ctx.idle_threshold = 300.0;
    clock_gettime(CLOCK_MONOTONIC, &ctx.now);

    kern_dict_iter(rl->buckets, cleanup_iter, &ctx);

    for (size_t i = 0; i < ctx.count; i++) {
        kern_dict_del(rl->buckets, ctx.keys_to_remove[i]);
    }

    free(ctx.keys_to_remove);
    clock_gettime(CLOCK_MONOTONIC, &rl->last_cleanup);

    pthread_mutex_unlock(&rl->mutex);
}
