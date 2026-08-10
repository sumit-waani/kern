/*
 * test_auth.c - Unit tests for kern_auth (PBKDF2-HMAC-SHA256 password hashing)
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

TEST(test_pbkdf2_hash_format) {
    char *hash = kern_password_hash("test");
    ASSERT(hash != NULL);

    /* Format: "pbkdf2-sha256:100000:hex_salt(32):hex_hash(64)" */
    /* "pbkdf2-sha256:100000:" = 21 chars prefix */
    ASSERT(strncmp(hash, "pbkdf2-sha256:100000:", 21) == 0);

    /* Total: 13 + 1 + 6 + 1 + 32 + 1 + 64 = 118 chars */
    ASSERT_EQ_INT((int)strlen(hash), 118);

    /* Check hex salt (32 chars) and hex hash (64 chars) separated by ':' */
    const char *salt_start = hash + 21;
    ASSERT(salt_start[32] == ':');

    /* Verify salt is hex */
    for (int i = 0; i < 32; i++) {
        char c = salt_start[i];
        ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    /* Verify hash is hex */
    const char *hash_start = salt_start + 33;
    for (int i = 0; i < 64; i++) {
        char c = hash_start[i];
        ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
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

TEST(test_legacy_format_verify) {
    /* Test backward compatibility with old "hex_salt$hex_hash" format.
     * We construct a known legacy hash:
     * password = "hello"
     * salt (8 bytes) = 0102030405060708
     * SHA-256(salt + "hello") is deterministic, so we verify the format is accepted.
     *
     * For this test, we just verify the format detection works correctly.
     * An invalid legacy hash should return false gracefully. */
    ASSERT(kern_password_verify("anything", "not_a_valid_hash") == false);
    ASSERT(kern_password_verify("anything", "1234567890abcdef$") == false);

    /* A valid-looking but incorrect legacy hash (81 chars, '$' at position 16) */
    /* 16 hex + '$' + 64 hex = 81 chars */
    const char *fake_legacy =
        "0102030405060708$"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ASSERT(strlen(fake_legacy) == 81);
    /* Should not verify with random password (wrong hash) */
    ASSERT(kern_password_verify("hello", fake_legacy) == false);
}

TEST(test_pbkdf2_format_detection) {
    /* Verify that format detection works correctly */
    char *hash = kern_password_hash("testpass");
    ASSERT(hash != NULL);
    ASSERT(strncmp(hash, "pbkdf2-sha256:", 14) == 0);
    ASSERT(kern_password_verify("testpass", hash) == true);
    ASSERT(kern_password_verify("wrongpass", hash) == false);
    free(hash);
}

int main(void) {
    printf("test_auth:\n");

    RUN_TEST(test_hash_and_verify);
    RUN_TEST(test_wrong_password);
    RUN_TEST(test_different_salts);
    RUN_TEST(test_pbkdf2_hash_format);
    RUN_TEST(test_empty_password);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_long_password);
    RUN_TEST(test_legacy_format_verify);
    RUN_TEST(test_pbkdf2_format_detection);

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
