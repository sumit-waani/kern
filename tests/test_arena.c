/*
 * test_arena.c - Unit tests for kern_arena_t
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  [RUN] %s\n", #name); \
    name(); \
    tests_passed++; \
    printf("  [PASS] %s\n", #name); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

TEST(test_arena_new) {
    kern_arena_t *arena = kern_arena_new(4096);
    ASSERT(arena != NULL);
    kern_arena_free(arena);
}

TEST(test_arena_alloc) {
    kern_arena_t *arena = kern_arena_new(4096);
    ASSERT(arena != NULL);

    void *p1 = kern_arena_alloc(arena, 64);
    ASSERT(p1 != NULL);

    void *p2 = kern_arena_alloc(arena, 128);
    ASSERT(p2 != NULL);

    /* Pointers should be different */
    ASSERT(p1 != p2);

    /* Can write to allocated memory */
    memset(p1, 0xAA, 64);
    memset(p2, 0xBB, 128);

    /* Verify no overlap */
    unsigned char *c1 = (unsigned char *)p1;
    unsigned char *c2 = (unsigned char *)p2;
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ_INT(c1[i], 0xAA);
    }
    for (int i = 0; i < 128; i++) {
        ASSERT_EQ_INT(c2[i], 0xBB);
    }

    kern_arena_free(arena);
}

TEST(test_arena_alignment) {
    kern_arena_t *arena = kern_arena_new(4096);
    ASSERT(arena != NULL);

    /* Allocate odd sizes and check alignment */
    void *p1 = kern_arena_alloc(arena, 1);
    void *p2 = kern_arena_alloc(arena, 3);
    void *p3 = kern_arena_alloc(arena, 7);

    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p3 != NULL);

    /* All should be 8-byte aligned */
    ASSERT(((size_t)p1 % 8) == 0);
    ASSERT(((size_t)p2 % 8) == 0);
    ASSERT(((size_t)p3 % 8) == 0);

    kern_arena_free(arena);
}

TEST(test_arena_large_alloc) {
    kern_arena_t *arena = kern_arena_new(256);
    ASSERT(arena != NULL);

    /* Allocate more than block size */
    void *p = kern_arena_alloc(arena, 1024);
    ASSERT(p != NULL);

    memset(p, 0xFF, 1024);

    kern_arena_free(arena);
}

TEST(test_arena_many_allocs) {
    kern_arena_t *arena = kern_arena_new(256);
    ASSERT(arena != NULL);

    /* Many small allocations that span multiple blocks */
    for (int i = 0; i < 1000; i++) {
        void *p = kern_arena_alloc(arena, 32);
        ASSERT(p != NULL);
        memset(p, (unsigned char)i, 32);
    }

    kern_arena_free(arena);
}

TEST(test_arena_reset) {
    kern_arena_t *arena = kern_arena_new(4096);
    ASSERT(arena != NULL);

    void *p1 = kern_arena_alloc(arena, 64);
    ASSERT(p1 != NULL);

    kern_arena_reset(arena);

    /* After reset, allocations should work again */
    void *p2 = kern_arena_alloc(arena, 64);
    ASSERT(p2 != NULL);

    kern_arena_free(arena);
}

TEST(test_arena_null_safety) {
    ASSERT(kern_arena_alloc(NULL, 10) == NULL);
    kern_arena_reset(NULL); /* Should not crash */
    kern_arena_free(NULL);  /* Should not crash */
}

TEST(test_arena_zero_size) {
    kern_arena_t *arena = kern_arena_new(4096);
    ASSERT(arena != NULL);

    void *p = kern_arena_alloc(arena, 0);
    ASSERT(p == NULL);

    kern_arena_free(arena);
}

int main(void) {
    printf("test_arena:\n");

    RUN_TEST(test_arena_new);
    RUN_TEST(test_arena_alloc);
    RUN_TEST(test_arena_alignment);
    RUN_TEST(test_arena_large_alloc);
    RUN_TEST(test_arena_many_allocs);
    RUN_TEST(test_arena_reset);
    RUN_TEST(test_arena_null_safety);
    RUN_TEST(test_arena_zero_size);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
