/*
 * kern_auth.c - Basic password authentication
 *
 * Uses SHA-256 with random salt for password hashing.
 * Format: "hex_salt$hex_hash" where salt is 8 random bytes (16 hex chars)
 * and hash is SHA-256(salt_bytes + password).
 *
 * Note: This is a simple implementation for v0.1.
 * Argon2id will replace this in v0.3.
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

/* ========== Password Hashing API ========== */

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

char *kern_password_hash(const char *password) {
    if (!password) return NULL;

    /* Generate 8 random bytes for salt */
    uint8_t salt[8];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return NULL;
    size_t r = fread(salt, 1, 8, f);
    fclose(f);
    if (r != 8) return NULL;

    /* Compute SHA-256(salt + password) */
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, salt, 8);
    sha256_update(&ctx, (const uint8_t *)password, strlen(password));

    uint8_t hash[32];
    sha256_final(&ctx, hash);

    /* Format: "hex_salt$hex_hash" */
    /* 16 hex chars for salt + 1 for $ + 64 hex chars for hash + 1 for null */
    char *result = malloc(16 + 1 + 64 + 1);
    if (!result) return NULL;

    bytes_to_hex(salt, 8, result);
    result[16] = '$';
    bytes_to_hex(hash, 32, result + 17);
    result[81] = '\0';

    return result;
}

bool kern_password_verify(const char *password, const char *hash_str) {
    if (!password || !hash_str) return false;

    /* Parse "hex_salt$hex_hash" */
    size_t len = strlen(hash_str);
    if (len != 81) return false;  /* 16 + 1 + 64 */
    if (hash_str[16] != '$') return false;

    /* Decode salt */
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
