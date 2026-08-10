/*
 * kern_dict.c - Hash map implementation (open addressing, linear probing)
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>

#define KERN_DICT_INITIAL_CAP 16
#define KERN_DICT_LOAD_FACTOR 0.75

typedef struct {
    char *key;      /* Heap-allocated copy of the key (NULL = empty slot) */
    void *value;
    bool occupied;  /* true if this slot holds a valid entry */
    bool tombstone; /* true if this slot was deleted */
} kern_dict_entry_t;

struct kern_dict {
    kern_dict_entry_t *entries;
    size_t cap;
    size_t count;
    kern_dict_free_fn free_fn;  /* Optional value destructor (NULL = no-op) */
};

/* FNV-1a hash */
static uint32_t kern_dict_hash(const char *key) {
    uint32_t hash = 2166136261u;
    for (const char *p = key; *p; p++) {
        hash ^= (uint32_t)(unsigned char)*p;
        hash *= 16777619u;
    }
    return hash;
}

static int kern_dict_resize(kern_dict_t *dict, size_t new_cap) {
    kern_dict_entry_t *new_entries = calloc(new_cap, sizeof(kern_dict_entry_t));
    if (!new_entries) {
        return -1;
    }

    /* Rehash all existing entries */
    for (size_t i = 0; i < dict->cap; i++) {
        if (dict->entries[i].occupied && !dict->entries[i].tombstone) {
            uint32_t hash = kern_dict_hash(dict->entries[i].key);
            size_t idx = hash & (new_cap - 1);

            while (new_entries[idx].occupied) {
                idx = (idx + 1) & (new_cap - 1);
            }

            new_entries[idx].key = dict->entries[i].key;
            new_entries[idx].value = dict->entries[i].value;
            new_entries[idx].occupied = true;
            new_entries[idx].tombstone = false;
        }
    }

    free(dict->entries);
    dict->entries = new_entries;
    dict->cap = new_cap;
    return 0;
}

kern_dict_t *kern_dict_new(void) {
    kern_dict_t *dict = malloc(sizeof(kern_dict_t));
    if (!dict) {
        return NULL;
    }

    dict->entries = calloc(KERN_DICT_INITIAL_CAP, sizeof(kern_dict_entry_t));
    if (!dict->entries) {
        free(dict);
        return NULL;
    }

    dict->cap = KERN_DICT_INITIAL_CAP;
    dict->count = 0;
    dict->free_fn = NULL;
    return dict;
}

kern_dict_t *kern_dict_new_with_free(kern_dict_free_fn free_fn) {
    kern_dict_t *dict = kern_dict_new();
    if (dict) {
        dict->free_fn = free_fn;
    }
    return dict;
}

int kern_dict_set(kern_dict_t *dict, const char *key, void *value) {
    if (!dict || !key) {
        return -1;
    }

    /* Check if we need to grow */
    if ((double)(dict->count + 1) > (double)dict->cap * KERN_DICT_LOAD_FACTOR) {
        if (kern_dict_resize(dict, dict->cap * 2) != 0) {
            return -1;
        }
    }

    uint32_t hash = kern_dict_hash(key);
    size_t idx = hash & (dict->cap - 1);
    size_t first_tombstone = (size_t)-1;

    while (dict->entries[idx].occupied) {
        if (dict->entries[idx].tombstone) {
            if (first_tombstone == (size_t)-1) {
                first_tombstone = idx;
            }
        } else if (strcmp(dict->entries[idx].key, key) == 0) {
            /* Key already exists, update value */
            if (dict->free_fn && dict->entries[idx].value) {
                dict->free_fn(dict->entries[idx].value);
            }
            dict->entries[idx].value = value;
            return 0;
        }
        idx = (idx + 1) & (dict->cap - 1);
    }

    /* Use first tombstone slot if available */
    if (first_tombstone != (size_t)-1) {
        idx = first_tombstone;
        /* Free old tombstone key if any */
        free(dict->entries[idx].key);
    }

    /* Insert new entry - manually duplicate key (strdup is not C11) */
    size_t key_len = strlen(key);
    dict->entries[idx].key = malloc(key_len + 1);
    if (!dict->entries[idx].key) {
        return -1;
    }
    memcpy(dict->entries[idx].key, key, key_len + 1);
    dict->entries[idx].value = value;
    dict->entries[idx].occupied = true;
    dict->entries[idx].tombstone = false;
    dict->count++;
    return 0;
}

static kern_dict_entry_t *kern_dict_find(const kern_dict_t *dict,
                                          const char *key) {
    if (!dict || !key) {
        return NULL;
    }

    uint32_t hash = kern_dict_hash(key);
    size_t idx = hash & (dict->cap - 1);

    while (dict->entries[idx].occupied) {
        if (!dict->entries[idx].tombstone &&
            strcmp(dict->entries[idx].key, key) == 0) {
            return &dict->entries[idx];
        }
        idx = (idx + 1) & (dict->cap - 1);
    }

    return NULL;
}

void *kern_dict_get(const kern_dict_t *dict, const char *key) {
    kern_dict_entry_t *entry = kern_dict_find(dict, key);
    return entry ? entry->value : NULL;
}

bool kern_dict_has(const kern_dict_t *dict, const char *key) {
    return kern_dict_find(dict, key) != NULL;
}

bool kern_dict_del(kern_dict_t *dict, const char *key) {
    kern_dict_entry_t *entry = kern_dict_find(dict, key);
    if (!entry) {
        return false;
    }

    free(entry->key);
    entry->key = NULL;
    if (dict->free_fn && entry->value) {
        dict->free_fn(entry->value);
    }
    entry->value = NULL;
    entry->tombstone = true;
    dict->count--;
    return true;
}

size_t kern_dict_count(const kern_dict_t *dict) {
    if (!dict) {
        return 0;
    }
    return dict->count;
}

void kern_dict_free(kern_dict_t *dict) {
    if (!dict) {
        return;
    }

    for (size_t i = 0; i < dict->cap; i++) {
        if (dict->entries[i].occupied && !dict->entries[i].tombstone) {
            free(dict->entries[i].key);
            if (dict->free_fn && dict->entries[i].value) {
                dict->free_fn(dict->entries[i].value);
            }
        }
    }

    free(dict->entries);
    free(dict);
}

void kern_dict_iter(const kern_dict_t *dict, kern_dict_iter_fn callback,
                    void *userdata) {
    if (!dict || !callback) {
        return;
    }

    for (size_t i = 0; i < dict->cap; i++) {
        if (dict->entries[i].occupied && !dict->entries[i].tombstone) {
            if (!callback(dict->entries[i].key, dict->entries[i].value,
                          userdata)) {
                return;
            }
        }
    }
}
