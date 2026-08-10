/*
 * kern_auth.c - Password authentication with PBKDF2-HMAC-SHA256
 *
 * Uses PBKDF2 with HMAC-SHA256 as PRF, 100,000 iterations,
 * 16-byte salt, and 32-byte derived key.
 *
 * Output format: "pbkdf2-sha256:100000:hex_salt:hex_hash"
 *
 * Backward-compatible verify detects old "hex_salt$hex_hash" format
 * (plain SHA-256) and verifies them correctly for migration purposes.
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========== Minimal SHA-256 Implementation ========== */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx;

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x) (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x) (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx *ctx, const uint8_t *data) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + K256[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
    memset(ctx->buffer, 0, 64);
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0) {
            sha256_transform(ctx, ctx->buffer);
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint64_t bits = ctx->count * 8;
    size_t pad_start = ctx->count % 64;

    /* Padding */
    ctx->buffer[pad_start++] = 0x80;
    if (pad_start > 56) {
        memset(ctx->buffer + pad_start, 0, 64 - pad_start);
        sha256_transform(ctx, ctx->buffer);
        pad_start = 0;
    }
    memset(ctx->buffer + pad_start, 0, 56 - pad_start);

    /* Length in bits (big-endian) */
    for (int i = 0; i < 8; i++) {
        ctx->buffer[63 - i] = (uint8_t)(bits >> (i * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    /* Output hash */
    for (int i = 0; i < 8; i++) {
        hash[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/* ========== HMAC-SHA256 Implementation ========== */

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t i_pad[SHA256_BLOCK_SIZE];
    uint8_t o_pad[SHA256_BLOCK_SIZE];
    sha256_ctx ctx;

    /* If key is longer than block size, hash it first */
    uint8_t key_hash[SHA256_DIGEST_SIZE];
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, key_hash);
        key = key_hash;
        key_len = SHA256_DIGEST_SIZE;
    }

    /* Pad key to block size */
    memset(k_pad, 0, SHA256_BLOCK_SIZE);
    memcpy(k_pad, key, key_len);

    /* Compute inner and outer padded keys */
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }

    /* Inner hash: H(i_pad || data) */
    uint8_t inner_hash[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, i_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    /* Outer hash: H(o_pad || inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, o_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}

/* ========== PBKDF2-HMAC-SHA256 Implementation ========== */

#define KERN_PBKDF2_ITERATIONS 100000
#define KERN_PBKDF2_SALT_LEN   16
#define KERN_PBKDF2_KEY_LEN    32

static void pbkdf2_hmac_sha256(const uint8_t *password, size_t password_len,
                               const uint8_t *salt, size_t salt_len,
                               uint32_t iterations,
                               uint8_t *out, size_t out_len) {
    uint32_t block_num = 1;
    size_t offset = 0;

    while (offset < out_len) {
        /* U_1 = PRF(password, salt || INT_32_BE(block_num)) */
        size_t msg_len = salt_len + 4;
        uint8_t *msg = malloc(msg_len);
        if (!msg) return;
        memcpy(msg, salt, salt_len);
        msg[salt_len]     = (uint8_t)(block_num >> 24);
        msg[salt_len + 1] = (uint8_t)(block_num >> 16);
        msg[salt_len + 2] = (uint8_t)(block_num >> 8);
        msg[salt_len + 3] = (uint8_t)(block_num);

        uint8_t u[SHA256_DIGEST_SIZE];
        uint8_t t[SHA256_DIGEST_SIZE];

        hmac_sha256(password, password_len, msg, msg_len, u);
        free(msg);
        memcpy(t, u, SHA256_DIGEST_SIZE);

        /* U_2 ... U_c */
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_sha256(password, password_len, u, SHA256_DIGEST_SIZE, u);
            for (int j = 0; j < SHA256_DIGEST_SIZE; j++) {
                t[j] ^= u[j];
            }
        }

        /* Copy result bytes */
        size_t to_copy = out_len - offset;
        if (to_copy > SHA256_DIGEST_SIZE) to_copy = SHA256_DIGEST_SIZE;
        memcpy(out + offset, t, to_copy);

        offset += to_copy;
        block_num++;
    }
}

/* ========== Utility Functions ========== */

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex) {
    for (size_t i = 0; i < len; i++) {
        snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    }
}

static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t byte_len) {
    for (size_t i = 0; i < byte_len; i++) {
        unsigned int val;
        if (sscanf(hex + i * 2, "%02x", &val) != 1) return -1;
        bytes[i] = (uint8_t)val;
    }
    return 0;
}

/* ========== Password Hashing API (PBKDF2) ========== */

char *kern_password_hash(const char *password) {
    if (!password) return NULL;

    /* Generate 16 random bytes for salt */
    uint8_t salt[KERN_PBKDF2_SALT_LEN];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return NULL;
    size_t r = fread(salt, 1, KERN_PBKDF2_SALT_LEN, f);
    fclose(f);
    if (r != KERN_PBKDF2_SALT_LEN) return NULL;

    /* Derive key using PBKDF2-HMAC-SHA256 */
    uint8_t derived_key[KERN_PBKDF2_KEY_LEN];
    pbkdf2_hmac_sha256((const uint8_t *)password, strlen(password),
                       salt, KERN_PBKDF2_SALT_LEN,
                       KERN_PBKDF2_ITERATIONS,
                       derived_key, KERN_PBKDF2_KEY_LEN);

    /* Format: "pbkdf2-sha256:100000:hex_salt:hex_hash" */
    /* "pbkdf2-sha256" (13) + ":" (1) + "100000" (6) + ":" (1)
     * + hex_salt (32) + ":" (1) + hex_hash (64) + null (1) = 119 */
    char *result = malloc(119);
    if (!result) return NULL;

    char hex_salt[33];
    char hex_hash[65];
    bytes_to_hex(salt, KERN_PBKDF2_SALT_LEN, hex_salt);
    hex_salt[32] = '\0';
    bytes_to_hex(derived_key, KERN_PBKDF2_KEY_LEN, hex_hash);
    hex_hash[64] = '\0';

    snprintf(result, 119, "pbkdf2-sha256:%d:%s:%s",
             KERN_PBKDF2_ITERATIONS, hex_salt, hex_hash);

    return result;
}

/* Verify a PBKDF2-format hash */
static bool verify_pbkdf2(const char *password, const char *hash_str) {
    /* Parse "pbkdf2-sha256:iterations:hex_salt:hex_hash" */
    /* Skip "pbkdf2-sha256:" prefix (14 chars) */
    const char *p = hash_str + 14;

    /* Parse iterations */
    char *endptr;
    unsigned long iterations = strtoul(p, &endptr, 10);
    if (*endptr != ':') return false;
    p = endptr + 1;

    /* Parse hex salt (32 hex chars = 16 bytes) */
    if (strlen(p) < 32) return false;
    uint8_t salt[KERN_PBKDF2_SALT_LEN];
    if (hex_to_bytes(p, salt, KERN_PBKDF2_SALT_LEN) != 0) return false;
    p += 32;
    if (*p != ':') return false;
    p++;

    /* Parse hex hash (64 hex chars = 32 bytes) */
    if (strlen(p) < 64) return false;
    uint8_t expected[KERN_PBKDF2_KEY_LEN];
    if (hex_to_bytes(p, expected, KERN_PBKDF2_KEY_LEN) != 0) return false;

    /* Derive key with same params */
    uint8_t computed[KERN_PBKDF2_KEY_LEN];
    pbkdf2_hmac_sha256((const uint8_t *)password, strlen(password),
                       salt, KERN_PBKDF2_SALT_LEN,
                       (uint32_t)iterations,
                       computed, KERN_PBKDF2_KEY_LEN);

    /* Constant-time comparison */
    uint8_t diff = 0;
    for (int i = 0; i < KERN_PBKDF2_KEY_LEN; i++) {
        diff |= computed[i] ^ expected[i];
    }
    return diff == 0;
}

/* Verify an old-format hash (SHA-256 with 8-byte salt) */
static bool verify_legacy_sha256(const char *password, const char *hash_str) {
    /* Old format: "hex_salt$hex_hash" (16 + 1 + 64 = 81 chars) */
    size_t len = strlen(hash_str);
    if (len != 81) return false;
    if (hash_str[16] != '$') return false;

    /* Decode salt (8 bytes from 16 hex chars) */
    uint8_t salt[8];
    if (hex_to_bytes(hash_str, salt, 8) != 0) return false;

    /* Decode expected hash */
    uint8_t expected[32];
    if (hex_to_bytes(hash_str + 17, expected, 32) != 0) return false;

    /* Compute SHA-256(salt + password) */
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, salt, 8);
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));

    uint8_t computed[32];
    sha256_final(&ctx, computed);

    /* Constant-time comparison */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) {
        diff |= computed[i] ^ expected[i];
    }
    return diff == 0;
}

bool kern_password_verify(const char *password, const char *hash_str) {
    if (!password || !hash_str) return false;

    /* Detect format by prefix */
    if (strncmp(hash_str, "pbkdf2-sha256:", 14) == 0) {
        return verify_pbkdf2(password, hash_str);
    }

    /* Fall back to legacy format (hex_salt$hex_hash) */
    return verify_legacy_sha256(password, hash_str);
}
