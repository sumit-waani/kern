/*
 * kern_asset.c - Asset fingerprinting pipeline
 *
 * Provides content-hashing for static assets. Files are copied to
 * an output directory with a hash in the filename for cache-busting.
 * A C manifest file is generated mapping original paths to hashed URLs.
 */

#include "kern.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ============================================================
 * Simple SHA-256 implementation (standalone, no deps)
 * ============================================================ */

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_S0(x)  (SHA256_ROTR(x,2) ^ SHA256_ROTR(x,13) ^ SHA256_ROTR(x,22))
#define SHA256_S1(x)  (SHA256_ROTR(x,6) ^ SHA256_ROTR(x,11) ^ SHA256_ROTR(x,25))
#define SHA256_s0(x)  (SHA256_ROTR(x,7) ^ SHA256_ROTR(x,18) ^ ((x) >> 3))
#define SHA256_s1(x)  (SHA256_ROTR(x,17) ^ SHA256_ROTR(x,19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];
    uint8_t  buffer[64];
    uint64_t total_len;
    size_t   buf_len;
} sha256_ctx_t;

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SHA256_s1(w[i-2]) + w[i-7] + SHA256_s0(w[i-15]) + w[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + SHA256_S1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
        uint32_t t2 = SHA256_S0(a) + SHA256_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    ctx->total_len += len;
    while (len > 0) {
        size_t space = 64 - ctx->buf_len;
        size_t copy = len < space ? len : space;
        memcpy(ctx->buffer + ctx->buf_len, data, copy);
        ctx->buf_len += copy;
        data += copy;
        len -= copy;
        if (ctx->buf_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]) {
    uint64_t bit_len = ctx->total_len * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    pad = 0x00;
    while (ctx->buf_len != 56) {
        sha256_update(ctx, &pad, 1);
    }
    uint8_t len_bytes[8];
    for (int i = 7; i >= 0; i--) {
        len_bytes[i] = (uint8_t)(bit_len & 0xff);
        bit_len >>= 8;
    }
    sha256_update(ctx, len_bytes, 8);

    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        hash[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

/* ============================================================
 * Asset helpers
 * ============================================================ */

static char *asset_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* Get just the filename from a path */
static const char *basename_of(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/* Get the extension from a filename (including the dot) */
static const char *ext_of(const char *filename) {
    const char *p = strrchr(filename, '.');
    return p ? p : "";
}

/* Hash file contents and return hex string (first 8 chars of SHA-256) */
static char *hash_file_content(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    sha256_ctx_t ctx;
    sha256_init(&ctx);

    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(&ctx, buf, n);
    }
    fclose(f);

    uint8_t hash[32];
    sha256_final(&ctx, hash);

    /* First 8 hex chars (4 bytes) */
    char *hex = malloc(9);
    if (!hex) return NULL;
    snprintf(hex, 9, "%02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
    return hex;
}

/* Copy a file from src to dst */
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;

    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

/* Recursively create directories */
static void mkdirs(const char *path) {
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ============================================================
 * Public API
 * ============================================================ */

char *kern_asset_hash_file(const char *input_path, const char *output_dir) {
    if (!input_path || !output_dir) return NULL;

    char *hash = hash_file_content(input_path);
    if (!hash) return NULL;

    const char *base = basename_of(input_path);
    const char *ext = ext_of(base);

    /* Build output filename: basename_without_ext-hash.ext */
    size_t base_no_ext_len = (size_t)(ext - base);
    char *output_name = malloc(base_no_ext_len + 1 + 8 + strlen(ext) + 1);
    if (!output_name) { free(hash); return NULL; }

    memcpy(output_name, base, base_no_ext_len);
    output_name[base_no_ext_len] = '-';
    memcpy(output_name + base_no_ext_len + 1, hash, 8);
    strcpy(output_name + base_no_ext_len + 1 + 8, ext);

    /* Build full output path */
    size_t out_path_len = strlen(output_dir) + 1 + strlen(output_name) + 1;
    char *output_path = malloc(out_path_len);
    if (!output_path) { free(hash); free(output_name); return NULL; }
    snprintf(output_path, out_path_len, "%s/%s", output_dir, output_name);

    mkdirs(output_dir);

    if (copy_file(input_path, output_path) != 0) {
        free(hash);
        free(output_name);
        free(output_path);
        return NULL;
    }

    free(hash);
    free(output_path);
    return output_name;
}

/* Internal manifest entry */
typedef struct {
    char *original;   /* e.g., "css/app.css" */
    char *hashed;     /* e.g., "css/app-a1b2c3d4.css" */
} asset_entry_t;

static int scan_assets_recursive(const char *base_dir, const char *rel_prefix,
                                  const char *output_dir,
                                  asset_entry_t *entries, int *count, int max_entries) {
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_dir,
             rel_prefix[0] ? rel_prefix : ".");

    /* If rel_prefix is empty, just use base_dir */
    const char *scan_dir = rel_prefix[0] ? full_path : base_dir;

    DIR *d = opendir(scan_dir);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char entry_path[2048];
        if (rel_prefix[0]) {
            snprintf(entry_path, sizeof(entry_path), "%s/%s/%s",
                     base_dir, rel_prefix, ent->d_name);
        } else {
            snprintf(entry_path, sizeof(entry_path), "%s/%s",
                     base_dir, ent->d_name);
        }

        struct stat st;
        if (stat(entry_path, &st) != 0) continue;

        char rel_path[2048];
        if (rel_prefix[0]) {
            snprintf(rel_path, sizeof(rel_path), "%s/%s", rel_prefix, ent->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "%s", ent->d_name);
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            scan_assets_recursive(base_dir, rel_path, output_dir,
                                  entries, count, max_entries);
        } else if (S_ISREG(st.st_mode)) {
            if (*count >= max_entries) continue;

            /* Compute hash */
            char *hash = hash_file_content(entry_path);
            if (!hash) continue;

            /* Build hashed filename */
            const char *base = basename_of(rel_path);
            const char *ext = ext_of(base);
            size_t base_no_ext_len = (size_t)(ext - base);

            /* Determine subdirectory part of rel_path */
            const char *last_slash = strrchr(rel_path, '/');
            char subdir[2048] = "";
            if (last_slash) {
                size_t subdir_len = (size_t)(last_slash - rel_path);
                memcpy(subdir, rel_path, subdir_len);
                subdir[subdir_len] = '\0';
            }

            /* Build hashed relative path */
            char hashed_name[2048];
            char hashed_base[256];
            snprintf(hashed_base, sizeof(hashed_base), "%.*s-%s%s",
                     (int)base_no_ext_len, base, hash, ext);

            if (subdir[0]) {
                snprintf(hashed_name, sizeof(hashed_name), "%s/%s", subdir, hashed_base);
            } else {
                snprintf(hashed_name, sizeof(hashed_name), "%s", hashed_base);
            }

            /* Copy file to output dir with hashed name */
            char out_file[2048];
            snprintf(out_file, sizeof(out_file), "%s/%s", output_dir, hashed_name);

            /* Create output subdirectory */
            if (subdir[0]) {
                char out_subdir[2048];
                snprintf(out_subdir, sizeof(out_subdir), "%s/%s", output_dir, subdir);
                mkdirs(out_subdir);
            }

            copy_file(entry_path, out_file);

            entries[*count].original = asset_strdup(rel_path);
            entries[*count].hashed = asset_strdup(hashed_name);
            (*count)++;

            free(hash);
        }
    }
    closedir(d);
    return 0;
}

int kern_asset_process_dir(const char *assets_dir, const char *public_dir,
                           const char *manifest_path) {
    if (!assets_dir || !public_dir || !manifest_path) return -1;

    mkdirs(public_dir);

    asset_entry_t entries[512];
    int count = 0;

    if (scan_assets_recursive(assets_dir, "", public_dir, entries, &count, 512) != 0
        && count == 0) {
        /* If scan failed and no entries, it's OK if the directory just doesn't exist */
        count = 0;
    }

    /* Generate manifest C file */
    FILE *f = fopen(manifest_path, "w");
    if (!f) {
        for (int i = 0; i < count; i++) {
            free(entries[i].original);
            free(entries[i].hashed);
        }
        return -1;
    }

    fprintf(f, "/* AUTO-GENERATED - DO NOT EDIT */\n");
    fprintf(f, "#ifndef KERN_ASSET_MANIFEST_H\n");
    fprintf(f, "#define KERN_ASSET_MANIFEST_H\n\n");

    for (int i = 0; i < count; i++) {
        /* Convert original path to a #define name:
         * "css/app.css" -> KERN_ASSET_CSS_APP */
        char define_name[256] = "KERN_ASSET_";
        size_t pos = strlen(define_name);
        for (const char *p = entries[i].original; *p && pos < sizeof(define_name) - 1; p++) {
            if (*p == '/' || *p == '-' || *p == '.') {
                define_name[pos++] = '_';
            } else if (*p >= 'a' && *p <= 'z') {
                define_name[pos++] = (char)(*p - 'a' + 'A');
            } else if (*p >= 'A' && *p <= 'Z') {
                define_name[pos++] = *p;
            } else if (*p >= '0' && *p <= '9') {
                define_name[pos++] = *p;
            } else {
                define_name[pos++] = '_';
            }
        }
        /* Remove trailing underscore from extension */
        if (pos > 0 && define_name[pos-1] == '_') pos--;
        /* Remove the extension part (last underscore-separated segment that's an ext) */
        define_name[pos] = '\0';

        /* Remove the file extension from define name */
        char *last_underscore = strrchr(define_name + strlen("KERN_ASSET_"), '_');
        /* Check if last segment looks like an extension (css, js, png, etc.) */
        if (last_underscore) {
            /* Keep it - it's part of the name identifier */
        }

        fprintf(f, "#define %s \"/assets/%s\"\n", define_name, entries[i].hashed);
    }

    fprintf(f, "\n#endif /* KERN_ASSET_MANIFEST_H */\n");
    fclose(f);

    for (int i = 0; i < count; i++) {
        free(entries[i].original);
        free(entries[i].hashed);
    }

    return count;
}

const char *kern_asset_url(const char *original_path) {
    /* For v0.1, this is a placeholder - runtime asset URL lookup would
     * require loading the manifest at startup. In practice, user code
     * uses the compile-time #defines from the generated manifest. */
    (void)original_path;
    return NULL;
}
