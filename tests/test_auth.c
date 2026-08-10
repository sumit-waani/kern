/*
 * test_auth.c - Unit tests for kern_auth (password hashing)
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

TEST(test_hash_and_verify) {
    char *hash = kern_password_hash("mysecretpassword");
    ASSERT(hash != NULL);

    /* Verify correct password */
    ASSERT(kern_password_verify("mysecretpassword", hash) == true);

    free(hash);
}

TEST(test_wrong_password) {
    char *hash = kern_password_hash("correctpassword");
    ASSERT(hash != NULL);

    /* Wrong password should fail */
    ASSERT(kern_password_verify("wrongpassword", hash) == false);
    ASSERT(kern_password_verify("", hash) == false);
    ASSERT(kern_password_verify("correctpasswor", hash) == false);  /* Prefix */
    ASSERT(kern_password_verify("correctpassword1", hash) == false);  /* Suffix */

    free(hash);
}

TEST(test_different_salts) {
    char *hash1 = kern_password_hash("samepassword");
    char *hash2 = kern_password_hash("samepassword");
    ASSERT(hash1 != NULL);
    ASSERT(hash2 != NULL);

    /* Different hashes due to random salt */
    ASSERT(strcmp(hash1, hash2) != 0);

    /* But both should verify */
    ASSERT(kern_password_verify("samepassword", hash1) == true);
    ASSERT(kern_password_verify("samepassword", hash2) == true);

    free(hash1);
    free(hash2);
}

TEST(test_hash_format) {
    char *hash = kern_password_hash("test");
    ASSERT(hash != NULL);

    /* Format should be "hex_salt$hex_hash" */
    /* 16 hex chars + '$' + 64 hex chars = 81 chars total */
    ASSERT_EQ_INT((int)strlen(hash), 81);
    ASSERT(hash[16] == '$');

    /* Verify all characters are hex or $ */
    for (int i = 0; i < 81; i++) {
        char c = hash[i];
        if (i == 16) {
            ASSERT(c == '$');
        } else {
            ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
    }

    free(hash);
}

TEST(test_empty_password) {
    char *hash = kern_password_hash("");
    ASSERT(hash != NULL);

    ASSERT(kern_password_verify("", hash) == true);
    ASSERT(kern_password_verify("notempty", hash) == false);

    free(hash);
}

TEST(test_null_safety) {
    ASSERT(kern_password_hash(NULL) == NULL);
    ASSERT(kern_password_verify(NULL, "hash") == false);
    ASSERT(kern_password_verify("pass", NULL) == false);
    ASSERT(kern_password_verify(NULL, NULL) == false);
}

TEST(test_long_password) {
    /* Test with a very long password */
    char long_pass[1025];
    memset(long_pass, 'A', 1024);
    long_pass[1024] = '\0';

    char *hash = kern_password_hash(long_pass);
    ASSERT(hash != NULL);
    ASSERT(kern_password_verify(long_pass, hash) == true);

    /* Slightly different should fail */
    long_pass[500] = 'B';
    ASSERT(kern_password_verify(long_pass, hash) == false);

    free(hash);
}

int main(void) {
    printf("test_auth:\n");

    RUN_TEST(test_hash_and_verify);
    RUN_TEST(test_wrong_password);
    RUN_TEST(test_different_salts);
    RUN_TEST(test_hash_format);
    RUN_TEST(test_empty_password);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_long_password);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
