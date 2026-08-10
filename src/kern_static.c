/*
 * kern_static.c - Static file handler
 *
 * Serves files from a directory with proper Content-Type,
 * ETag support (mtime + size based), 304 Not Modified,
 * directory traversal prevention, and symlink escape protection.
 *
 * Security:
 * - Rejects ".." path segments to prevent directory traversal.
 * - Uses realpath() to resolve symlinks and verify the resolved
 *   path stays within the canonical public directory.
 * - Returns 403 if the resolved path escapes the document root.
 * - Returns 404 if the file does not exist (realpath returns NULL).
 */

#include "kern.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* MIME type mapping */
typedef struct {
    const char *ext;
    const char *mime;
} mime_entry_t;

static const mime_entry_t mime_types[] = {
    {".html", "text/html; charset=utf-8"},
    {".htm",  "text/html; charset=utf-8"},
    {".css",  "text/css; charset=utf-8"},
    {".js",   "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".txt",  "text/plain; charset=utf-8"},
    {".xml",  "application/xml; charset=utf-8"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif",  "image/gif"},
    {".svg",  "image/svg+xml"},
    {".ico",  "image/x-icon"},
    {".woff", "font/woff"},
    {".woff2","font/woff2"},
    {".ttf",  "font/ttf"},
    {".pdf",  "application/pdf"},
    {".wasm", "application/wasm"},
    {NULL, NULL}
};

/* Case-insensitive strcmp (C11-portable) */
static int strcasecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
    int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    return ca - cb;
}

static const char *get_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    for (const mime_entry_t *m = mime_types; m->ext; m++) {
        if (strcasecmp_local(dot, m->ext) == 0) {
            return m->mime;
        }
    }
    return "application/octet-stream";
}

/* Check for directory traversal attempts */
static bool has_traversal(const char *path) {
    if (!path) return true;
    if (strstr(path, "..") != NULL) return true;
    if (path[0] == '/') return true; /* absolute path */
    return false;
}

kern_response_t *kern_static_handler(kern_req_t *req, const char *public_dir) {
    if (!req || !public_dir) return kern_404_response();

    const char *req_path = kern_req_path(req);
    if (!req_path) return kern_404_response();

    /* Strip leading slash */
    const char *rel_path = req_path;
    while (*rel_path == '/') rel_path++;

    /* Default to index.html */
    if (rel_path[0] == '\0') {
        rel_path = "index.html";
    }

    /* Security: reject directory traversal */
    if (has_traversal(rel_path)) {
        kern_response_t *res = kern_response_new(403);
        kern_response_header(res, "Content-Type", "text/plain");
        kern_response_body_str(res, "403 Forbidden");
        return res;
    }

    /* Build full path */
    size_t dir_len = strlen(public_dir);
    size_t rel_len = strlen(rel_path);
    /* +2 for potential slash and null */
    char *full_path = malloc(dir_len + rel_len + 2);
    if (!full_path) return kern_500_response("Out of memory");

    memcpy(full_path, public_dir, dir_len);
    if (dir_len > 0 && public_dir[dir_len - 1] != '/') {
        full_path[dir_len] = '/';
        dir_len++;
    }
    memcpy(full_path + dir_len, rel_path, rel_len + 1);

    /* Resolve the canonical public directory path */
    char *canonical_dir = realpath(public_dir, NULL);
    if (!canonical_dir) {
        free(full_path);
        return kern_404_response();
    }

    /* Resolve the full file path via realpath to follow symlinks */
    char *canonical_file = realpath(full_path, NULL);
    free(full_path);

    if (!canonical_file) {
        /* File does not exist (or cannot be resolved) */
        free(canonical_dir);
        return kern_404_response();
    }

    /* Verify the resolved path starts with the canonical public directory */
    size_t canonical_dir_len = strlen(canonical_dir);
    bool path_safe = (strncmp(canonical_file, canonical_dir, canonical_dir_len) == 0) &&
                     (canonical_file[canonical_dir_len] == '/' ||
                      canonical_file[canonical_dir_len] == '\0');

    if (!path_safe) {
        /* Path escapes the document root (symlink traversal) */
        free(canonical_dir);
        free(canonical_file);
        kern_response_t *res = kern_response_new(403);
        kern_response_header(res, "Content-Type", "text/plain");
        kern_response_body_str(res, "403 Forbidden");
        return res;
    }

    free(canonical_dir);

    /* Stat the file */
    struct stat st;
    if (stat(canonical_file, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(canonical_file);
        return kern_404_response();
    }

    /* Generate ETag: "mtime-size" */
    char etag[64];
    snprintf(etag, sizeof(etag), "\"%lx-%lx\"",
             (unsigned long)st.st_mtime, (unsigned long)st.st_size);

    /* Check If-None-Match for 304 */
    const char *if_none_match = kern_header(req, "If-None-Match");
    if (if_none_match && strcmp(if_none_match, etag) == 0) {
        free(canonical_file);
        kern_response_t *res = kern_response_new(304);
        kern_response_header(res, "ETag", etag);
        return res;
    }

    /* Read file */
    FILE *fp = fopen(canonical_file, "rb");
    if (!fp) {
        free(canonical_file);
        return kern_404_response();
    }

    char *data = malloc((size_t)st.st_size);
    if (!data) {
        fclose(fp);
        free(canonical_file);
        return kern_500_response("Out of memory");
    }

    size_t read_bytes = fread(data, 1, (size_t)st.st_size, fp);
    fclose(fp);

    /* Build response */
    kern_response_t *res = kern_response_new(200);
    kern_response_header(res, "Content-Type", get_mime_type(canonical_file));
    kern_response_header(res, "ETag", etag);
    kern_response_header(res, "Cache-Control", "public, max-age=3600");
    kern_response_body(res, data, read_bytes);

    free(data);
    free(canonical_file);
    return res;
}
