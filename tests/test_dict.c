/*
 * test_dict.c - Unit tests for kern_dict_t
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

#define ASSERT_EQ_STR(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "    ASSERT_EQ_STR FAILED: \"%s\" != \"%s\" (%s:%d)\n", \
                (a), (b), __FILE__, __LINE__); \
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

TEST(test_dict_new) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);
    ASSERT_EQ_INT(kern_dict_count(dict), 0);
    kern_dict_free(dict);
}

TEST(test_dict_set_get) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int val1 = 42;
    int val2 = 99;
    int rc = kern_dict_set(dict, "key1", &val1);
    ASSERT_EQ_INT(rc, 0);
    rc = kern_dict_set(dict, "key2", &val2);
    ASSERT_EQ_INT(rc, 0);

    ASSERT_EQ_INT(kern_dict_count(dict), 2);
    ASSERT(kern_dict_get(dict, "key1") == &val1);
    ASSERT(kern_dict_get(dict, "key2") == &val2);
    ASSERT(kern_dict_get(dict, "key3") == NULL);

    kern_dict_free(dict);
}

TEST(test_dict_overwrite) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int val1 = 1;
    int val2 = 2;

    kern_dict_set(dict, "key", &val1);
    ASSERT(kern_dict_get(dict, "key") == &val1);
    ASSERT_EQ_INT(kern_dict_count(dict), 1);

    kern_dict_set(dict, "key", &val2);
    ASSERT(kern_dict_get(dict, "key") == &val2);
    ASSERT_EQ_INT(kern_dict_count(dict), 1);

    kern_dict_free(dict);
}

TEST(test_dict_has) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int val = 42;
    kern_dict_set(dict, "exists", &val);

    ASSERT(kern_dict_has(dict, "exists"));
    ASSERT(!kern_dict_has(dict, "missing"));

    kern_dict_free(dict);
}

TEST(test_dict_del) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int val = 42;
    kern_dict_set(dict, "key", &val);
    ASSERT_EQ_INT(kern_dict_count(dict), 1);

    bool deleted = kern_dict_del(dict, "key");
    ASSERT(deleted);
    ASSERT_EQ_INT(kern_dict_count(dict), 0);
    ASSERT(!kern_dict_has(dict, "key"));
    ASSERT(kern_dict_get(dict, "key") == NULL);

    /* Deleting non-existent key returns false */
    ASSERT(!kern_dict_del(dict, "nope"));

    kern_dict_free(dict);
}

TEST(test_dict_del_then_set) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int val1 = 1;
    int val2 = 2;

    kern_dict_set(dict, "key", &val1);
    kern_dict_del(dict, "key");
    kern_dict_set(dict, "key", &val2);

    ASSERT(kern_dict_get(dict, "key") == &val2);
    ASSERT_EQ_INT(kern_dict_count(dict), 1);

    kern_dict_free(dict);
}

TEST(test_dict_many_entries) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    /* Insert many entries to trigger resizing */
    char key[32];
    int values[100];
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        values[i] = i * 10;
        int rc = kern_dict_set(dict, key, &values[i]);
        ASSERT_EQ_INT(rc, 0);
    }

    ASSERT_EQ_INT(kern_dict_count(dict), 100);

    /* Verify all entries are retrievable */
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        int *v = kern_dict_get(dict, key);
        ASSERT(v != NULL);
        ASSERT_EQ_INT(*v, i * 10);
    }

    kern_dict_free(dict);
}

static int iter_count;
static bool iter_callback(const char *key, void *value, void *userdata) {
    (void)key;
    (void)value;
    (void)userdata;
    iter_count++;
    return true;
}

static bool iter_stop_callback(const char *key, void *value, void *userdata) {
    (void)key;
    (void)value;
    (void)userdata;
    iter_count++;
    return iter_count < 3;  /* Stop after 3 */
}

TEST(test_dict_iter) {
    kern_dict_t *dict = kern_dict_new();
    ASSERT(dict != NULL);

    int vals[5] = {1, 2, 3, 4, 5};
    kern_dict_set(dict, "a", &vals[0]);
    kern_dict_set(dict, "b", &vals[1]);
    kern_dict_set(dict, "c", &vals[2]);
    kern_dict_set(dict, "d", &vals[3]);
    kern_dict_set(dict, "e", &vals[4]);

    iter_count = 0;
    kern_dict_iter(dict, iter_callback, NULL);
    ASSERT_EQ_INT(iter_count, 5);

    /* Test early stop */
    iter_count = 0;
    kern_dict_iter(dict, iter_stop_callback, NULL);
    ASSERT_EQ_INT(iter_count, 3);

    kern_dict_free(dict);
}

TEST(test_dict_null_safety) {
    ASSERT_EQ_INT(kern_dict_count(NULL), 0);
    ASSERT(kern_dict_get(NULL, "key") == NULL);
    ASSERT(!kern_dict_has(NULL, "key"));
    ASSERT(!kern_dict_del(NULL, "key"));
    kern_dict_free(NULL); /* Should not crash */
    kern_dict_iter(NULL, iter_callback, NULL); /* Should not crash */
}

int main(void) {
    printf("test_dict:\n");

    RUN_TEST(test_dict_new);
    RUN_TEST(test_dict_set_get);
    RUN_TEST(test_dict_overwrite);
    RUN_TEST(test_dict_has);
    RUN_TEST(test_dict_del);
    RUN_TEST(test_dict_del_then_set);
    RUN_TEST(test_dict_many_entries);
    RUN_TEST(test_dict_iter);
    RUN_TEST(test_dict_null_safety);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
