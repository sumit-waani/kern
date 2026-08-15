/*
 * kern_queue.c - In-process job queue with worker threads,
 * exponential backoff retry, and dead-letter handling.
 */

#include "kern.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KERN_QUEUE_MAX_ATTEMPTS 5
#define KERN_QUEUE_BASE_DELAY_US 1000000  /* 1 second in microseconds */

/* ============================================================
 * Internal Types
 * ============================================================ */

typedef struct kern_job {
    char *name;             /* Job name (matches handler registration) */
    void *arg;              /* Copy of argument data */
    size_t arg_size;        /* Size of argument data */
    int attempt;            /* Current attempt number (starts at 1) */
    struct kern_job *next;  /* Next job in linked list */
} kern_job_t;

struct kern_queue {
    /* Worker pool */
    int num_workers;
    pthread_t *workers;

    /* Job list (pending) */
    kern_job_t *head;
    kern_job_t *tail;

    /* Synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    /* State */
    bool running;
    bool started;

    /* Handler registry: job_name -> kern_queue_handler_fn */
    kern_dict_t *handlers;

    /* Dead-letter list */
    kern_job_t *failed_head;
    kern_job_t *failed_tail;
    int failed_count;
};

/* ============================================================
 * Internal Helpers
 * ============================================================ */

static kern_job_t *job_new(const char *name, const void *arg, size_t arg_size) {
    kern_job_t *job = calloc(1, sizeof(kern_job_t));
    if (!job) return NULL;

    job->name = strdup(name);
    if (!job->name) {
        free(job);
        return NULL;
    }

    if (arg && arg_size > 0) {
        job->arg = malloc(arg_size);
        if (!job->arg) {
            free(job->name);
            free(job);
            return NULL;
        }
        memcpy(job->arg, arg, arg_size);
        job->arg_size = arg_size;
    }

    job->attempt = 1;
    job->next = NULL;
    return job;
}

static void job_free(kern_job_t *job) {
    if (!job) return;
    free(job->name);
    free(job->arg);
    free(job);
}

static void enqueue_job(kern_queue_t *queue, kern_job_t *job) {
    job->next = NULL;
    if (queue->tail) {
        queue->tail->next = job;
    } else {
        queue->head = job;
    }
    queue->tail = job;
}

static kern_job_t *dequeue_job(kern_queue_t *queue) {
    kern_job_t *job = queue->head;
    if (!job) return NULL;

    queue->head = job->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    job->next = NULL;
    return job;
}

static void enqueue_failed(kern_queue_t *queue, kern_job_t *job) {
    job->next = NULL;
    if (queue->failed_tail) {
        queue->failed_tail->next = job;
    } else {
        queue->failed_head = job;
    }
    queue->failed_tail = job;
    queue->failed_count++;
}

static void *worker_thread(void *arg) {
    kern_queue_t *queue = (kern_queue_t *)arg;

    while (1) {
        pthread_mutex_lock(&queue->mutex);

        /* Wait for work or shutdown signal */
        while (queue->running && !queue->head) {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }

        if (!queue->running && !queue->head) {
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        kern_job_t *job = dequeue_job(queue);
        pthread_mutex_unlock(&queue->mutex);

        if (!job) continue;

        /* Look up handler */
        kern_queue_handler_fn handler = NULL;
        pthread_mutex_lock(&queue->mutex);
        void *handler_ptr = kern_dict_get(queue->handlers, job->name);
        memcpy(&handler, &handler_ptr, sizeof(handler));
        pthread_mutex_unlock(&queue->mutex);

        if (!handler) {
            /* No handler registered - send to dead letter */
            pthread_mutex_lock(&queue->mutex);
            enqueue_failed(queue, job);
            pthread_mutex_unlock(&queue->mutex);
            continue;
        }

        /* Execute the handler */
        kern_queue_result_t result = handler(job);

        if (result == KERN_QUEUE_OK) {
            job_free(job);
        } else if (result == KERN_QUEUE_RETRY) {
            if (job->attempt >= KERN_QUEUE_MAX_ATTEMPTS) {
                /* Max retries exceeded - dead letter */
                pthread_mutex_lock(&queue->mutex);
                enqueue_failed(queue, job);
                pthread_mutex_unlock(&queue->mutex);
            } else {
                /* Exponential backoff: base * 2^(attempt-1) */
                unsigned int delay_us = KERN_QUEUE_BASE_DELAY_US
                    * (1u << (unsigned)(job->attempt - 1));
                usleep(delay_us);

                job->attempt++;
                pthread_mutex_lock(&queue->mutex);
                enqueue_job(queue, job);
                pthread_cond_signal(&queue->cond);
                pthread_mutex_unlock(&queue->mutex);
            }
        } else {
            /* KERN_QUEUE_FAIL - immediate dead letter */
            pthread_mutex_lock(&queue->mutex);
            enqueue_failed(queue, job);
            pthread_mutex_unlock(&queue->mutex);
        }
    }

    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */

kern_queue_t *kern_queue_new(int num_workers) {
    if (num_workers <= 0) return NULL;

    kern_queue_t *queue = calloc(1, sizeof(kern_queue_t));
    if (!queue) return NULL;

    queue->num_workers = num_workers;
    queue->workers = calloc((size_t)num_workers, sizeof(pthread_t));
    if (!queue->workers) {
        free(queue);
        return NULL;
    }

    queue->handlers = kern_dict_new();
    if (!queue->handlers) {
        free(queue->workers);
        free(queue);
        return NULL;
    }

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        kern_dict_free(queue->handlers);
        free(queue->workers);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->cond, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        kern_dict_free(queue->handlers);
        free(queue->workers);
        free(queue);
        return NULL;
    }

    queue->running = false;
    queue->started = false;
    queue->head = NULL;
    queue->tail = NULL;
    queue->failed_head = NULL;
    queue->failed_tail = NULL;
    queue->failed_count = 0;

    return queue;
}

int kern_queue_start(kern_queue_t *queue) {
    if (!queue) return -1;
    if (queue->started) return -1;

    queue->running = true;
    queue->started = true;

    for (int i = 0; i < queue->num_workers; i++) {
        if (pthread_create(&queue->workers[i], NULL, worker_thread, queue) != 0) {
            /* Failed to create thread - stop already-created threads */
            queue->running = false;
            pthread_cond_broadcast(&queue->cond);
            for (int j = 0; j < i; j++) {
                pthread_join(queue->workers[j], NULL);
            }
            queue->started = false;
            return -1;
        }
    }

    return 0;
}

void kern_queue_stop(kern_queue_t *queue) {
    if (!queue) return;
    if (!queue->started) return;

    pthread_mutex_lock(&queue->mutex);
    queue->running = false;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    for (int i = 0; i < queue->num_workers; i++) {
        pthread_join(queue->workers[i], NULL);
    }

    queue->started = false;
}

void kern_queue_free(kern_queue_t *queue) {
    if (!queue) return;

    /* Stop if still running */
    if (queue->started) {
        kern_queue_stop(queue);
    }

    /* Free pending jobs */
    kern_job_t *job = queue->head;
    while (job) {
        kern_job_t *next = job->next;
        job_free(job);
        job = next;
    }

    /* Free failed jobs */
    job = queue->failed_head;
    while (job) {
        kern_job_t *next = job->next;
        job_free(job);
        job = next;
    }

    kern_dict_free(queue->handlers);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    free(queue->workers);
    free(queue);
}

int kern_queue_register(kern_queue_t *queue, const char *job_name,
                        kern_queue_handler_fn handler) {
    if (!queue || !job_name || !handler) return -1;

    pthread_mutex_lock(&queue->mutex);
    void *handler_ptr;
    memcpy(&handler_ptr, &handler, sizeof(handler_ptr));
    int rc = kern_dict_set(queue->handlers, job_name, handler_ptr);
    pthread_mutex_unlock(&queue->mutex);

    return rc;
}

int kern_queue_dispatch(kern_queue_t *queue, const char *job_name,
                        const void *arg, size_t arg_size) {
    if (!queue || !job_name) return -1;

    kern_job_t *job = job_new(job_name, arg, arg_size);
    if (!job) return -1;

    pthread_mutex_lock(&queue->mutex);
    enqueue_job(queue, job);
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

const char *kern_job_name(const kern_job_t *job) {
    if (!job) return NULL;
    return job->name;
}

const void *kern_job_arg(const kern_job_t *job) {
    if (!job) return NULL;
    return job->arg;
}

size_t kern_job_arg_size(const kern_job_t *job) {
    if (!job) return 0;
    return job->arg_size;
}

int kern_job_attempt(const kern_job_t *job) {
    if (!job) return 0;
    return job->attempt;
}

int kern_queue_failed_count(const kern_queue_t *queue) {
    if (!queue) return 0;
    return queue->failed_count;
}

void kern_queue_failed_clear(kern_queue_t *queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    kern_job_t *job = queue->failed_head;
    while (job) {
        kern_job_t *next = job->next;
        job_free(job);
        job = next;
    }
    queue->failed_head = NULL;
    queue->failed_tail = NULL;
    queue->failed_count = 0;
    pthread_mutex_unlock(&queue->mutex);
}
