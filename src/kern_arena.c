/*
 * kern_arena.c - Arena allocator implementation
 */

#include "kern.h"

#include <stdlib.h>
#include <string.h>

#define KERN_ARENA_ALIGN 8

typedef struct kern_arena_block {
    struct kern_arena_block *next;
    size_t size;
    size_t used;
    /* Data follows immediately after this struct */
} kern_arena_block_t;

struct kern_arena {
    kern_arena_block_t *head;   /* First block (most recently allocated) */
    size_t block_size;          /* Default block size */
};

static size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

static kern_arena_block_t *kern_arena_block_new(size_t size) {
    kern_arena_block_t *block = malloc(sizeof(kern_arena_block_t) + size);
    if (!block) {
        return NULL;
    }
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

kern_arena_t *kern_arena_new(size_t block_size) {
    kern_arena_t *arena = malloc(sizeof(kern_arena_t));
    if (!arena) {
        return NULL;
    }

    if (block_size < 256) {
        block_size = 256;
    }

    arena->block_size = block_size;
    arena->head = kern_arena_block_new(block_size);
    if (!arena->head) {
        free(arena);
        return NULL;
    }

    return arena;
}

void *kern_arena_alloc(kern_arena_t *arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }

    size = align_up(size, KERN_ARENA_ALIGN);

    /* Try to allocate from the current head block */
    if (arena->head->used + size <= arena->head->size) {
        void *ptr = (char *)(arena->head + 1) + arena->head->used;
        arena->head->used += size;
        return ptr;
    }

    /* Need a new block. If the requested size is larger than block_size,
       allocate a block that fits it exactly. */
    size_t new_block_size = size > arena->block_size ? size : arena->block_size;
    kern_arena_block_t *block = kern_arena_block_new(new_block_size);
    if (!block) {
        return NULL;
    }

    block->next = arena->head;
    arena->head = block;

    void *ptr = (char *)(block + 1) + block->used;
    block->used += size;
    return ptr;
}

void kern_arena_reset(kern_arena_t *arena) {
    if (!arena) {
        return;
    }

    /* Free all blocks except the first one, then reset it */
    kern_arena_block_t *block = arena->head;

    /* Find the last block (the original first one) */
    while (block->next) {
        kern_arena_block_t *next = block->next;
        free(block);
        block = next;
    }

    /* Reset the last remaining block */
    block->used = 0;
    arena->head = block;
}

void kern_arena_free(kern_arena_t *arena) {
    if (!arena) {
        return;
    }

    kern_arena_block_t *block = arena->head;
    while (block) {
        kern_arena_block_t *next = block->next;
        free(block);
        block = next;
    }

    free(arena);
}
