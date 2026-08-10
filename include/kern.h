/*
 * kern.h - Public header for the kern web framework
 *
 * This is the single public header that exposes all core types and
 * function declarations for the kern framework library.
 */

#ifndef KERN_H
#define KERN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Core Types
 * ============================================================ */

/**
 * kern_str_t - Non-owning string slice.
 * Points to a substring without owning the memory.
 */
typedef struct {
    const char *data;
    size_t len;
} kern_str_t;

/**
 * kern_buf_t - Growable byte buffer (opaque).
 * Used for building strings, HTTP responses, etc.
 */
typedef struct kern_buf kern_buf_t;

/**
 * kern_dict_t - String-keyed hash map (opaque).
 * Maps null-terminated string keys to void* values.
 */
typedef struct kern_dict kern_dict_t;

/**
 * kern_arena_t - Arena allocator (opaque).
 * Efficient bump allocator for per-request allocations.
 */
typedef struct kern_arena kern_arena_t;

/**
 * kern_method_t - HTTP request methods.
 */
typedef enum {
    KERN_METHOD_GET,
    KERN_METHOD_POST,
    KERN_METHOD_PUT,
    KERN_METHOD_PATCH,
    KERN_METHOD_DELETE,
    KERN_METHOD_HEAD,
    KERN_METHOD_OPTIONS
} kern_method_t;

/* Forward declarations for request/response/app structs */
typedef struct kern_req kern_req_t;
typedef struct kern_res kern_res_t;
typedef struct kern_app kern_app_t;

/* ============================================================
 * Buffer API (kern_buf.c)
 * ============================================================ */

/**
 * Create a new buffer with the given initial capacity.
 * Returns NULL on allocation failure.
 */
kern_buf_t *kern_buf_new(size_t initial_cap);

/**
 * Append raw bytes to the buffer.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_buf_write(kern_buf_t *buf, const char *data, size_t len);

/**
 * Append a null-terminated string to the buffer.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_buf_writes(kern_buf_t *buf, const char *str);

/**
 * Printf-style append to the buffer.
 * Returns 0 on success, -1 on failure.
 */
int kern_buf_writef(kern_buf_t *buf, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Reset buffer length to 0 without freeing memory.
 */
void kern_buf_reset(kern_buf_t *buf);

/**
 * Free the buffer and all associated memory.
 */
void kern_buf_free(kern_buf_t *buf);

/**
 * Get a pointer to the buffer's data (null-terminated).
 */
const char *kern_buf_data(const kern_buf_t *buf);

/**
 * Get the current length of data in the buffer.
 */
size_t kern_buf_len(const kern_buf_t *buf);

/* ============================================================
 * String Utilities API (kern_str.c)
 * ============================================================ */

/**
 * Create a kern_str_t from a null-terminated C string.
 */
kern_str_t kern_str(const char *cstr);

/**
 * Create a heap-allocated null-terminated copy of a string slice.
 * Caller must free() the returned pointer.
 */
char *kern_str_dup(kern_str_t str);

/**
 * Compare two string slices for equality.
 */
bool kern_str_eq(kern_str_t a, kern_str_t b);

/**
 * Check if str starts with the given prefix.
 */
bool kern_str_starts_with(kern_str_t str, kern_str_t prefix);

/**
 * Split a string slice by a single-character delimiter.
 * Writes up to max_parts pointers into out_parts and sets *out_count.
 * The out_parts array must be pre-allocated by the caller.
 */
void kern_str_split(kern_str_t str, char delim,
                    kern_str_t *out_parts, size_t max_parts,
                    size_t *out_count);

/**
 * Trim leading and trailing whitespace from a string slice.
 */
kern_str_t kern_str_trim(kern_str_t str);

/**
 * URL-encode a string slice, appending the result to buf.
 */
int kern_url_encode(kern_str_t str, kern_buf_t *buf);

/**
 * URL-decode a string slice, appending the result to buf.
 */
int kern_url_decode(kern_str_t str, kern_buf_t *buf);

/* ============================================================
 * Dictionary (Hash Map) API (kern_dict.c)
 * ============================================================ */

/**
 * Callback type for dictionary iteration.
 * Return false to stop iteration early.
 */
typedef bool (*kern_dict_iter_fn)(const char *key, void *value, void *userdata);

/**
 * Create a new empty dictionary.
 */
kern_dict_t *kern_dict_new(void);

/**
 * Set a key-value pair. The key is copied internally.
 * If the key already exists, its value is replaced.
 * Returns 0 on success, -1 on allocation failure.
 */
int kern_dict_set(kern_dict_t *dict, const char *key, void *value);

/**
 * Get the value for a key. Returns NULL if not found.
 */
void *kern_dict_get(const kern_dict_t *dict, const char *key);

/**
 * Check if a key exists in the dictionary.
 */
bool kern_dict_has(const kern_dict_t *dict, const char *key);

/**
 * Delete a key from the dictionary.
 * Returns true if the key was found and removed.
 */
bool kern_dict_del(kern_dict_t *dict, const char *key);

/**
 * Get the number of entries in the dictionary.
 */
size_t kern_dict_count(const kern_dict_t *dict);

/**
 * Free the dictionary and all copied keys.
 * Does NOT free the stored values.
 */
void kern_dict_free(kern_dict_t *dict);

/**
 * Iterate over all entries in the dictionary.
 * Calls the callback for each entry. If callback returns false, stops early.
 */
void kern_dict_iter(const kern_dict_t *dict, kern_dict_iter_fn callback,
                    void *userdata);

/* ============================================================
 * Arena Allocator API (kern_arena.c)
 * ============================================================ */

/**
 * Create a new arena with the given block size.
 * The arena allocates memory in blocks of this size.
 */
kern_arena_t *kern_arena_new(size_t block_size);

/**
 * Allocate memory from the arena (8-byte aligned).
 * Returns NULL if the arena cannot grow.
 */
void *kern_arena_alloc(kern_arena_t *arena, size_t size);

/**
 * Reset the arena, making all previously allocated memory reusable.
 * Does not free the underlying blocks.
 */
void kern_arena_reset(kern_arena_t *arena);

/**
 * Free the arena and all its blocks.
 */
void kern_arena_free(kern_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* KERN_H */
