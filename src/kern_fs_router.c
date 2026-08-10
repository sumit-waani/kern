/*
 * kern_fs_router.c - File-system based routing scanner
 *
 * Scans a pages/ directory and generates route entries based on
 * file naming conventions. Generates a C source file that registers
 * all discovered routes.
 */

#include "kern.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Maximum entries the scanner can discover */
#define KERN_FS_MAX_ENTRIES 256

/* Maximum path length */
#define KERN_FS_MAX_PATH 2048

/* Maximum methods per route */
#define KERN_FS_MAX_METHODS 8

/* Check if a filename is a special file (starts with _) */
static bool is_special_file(const char *name) {
    return name[0] == '_';
}

/* Check if a filename ends with .c */
static bool is_c_file(const char *name) {
    size_t len = strlen(name);
    return len > 2 && strcmp(name + len - 2, ".c") == 0;
}

/* Convert bracket notation [param] to :param in a path segment */
static char *convert_brackets(const char *segment) {
    size_t len = strlen(segment);
    if (len >= 3 && segment[0] == '[' && segment[len - 1] == ']') {
        /* [param] -> :param */
        char *result = malloc(len); /* len - 2 + 1 (colon) + 1 (nul) = len */
        if (!result) return NULL;
        result[0] = ':';
        memcpy(result + 1, segment + 1, len - 2);
        result[len - 1] = '\0';
        return result;
    }
    return strdup(segment);
}

/* Parse a .c file to find method macros */
static int parse_methods(const char *filepath, char methods[][16], int max_methods) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    int count = 0;
    char line[512];

    while (fgets(line, sizeof(line), f) && count < max_methods) {
        if (strstr(line, "KERN_GET")) {
            strncpy(methods[count++], "GET", 16);
        }
        if (strstr(line, "KERN_POST") && count < max_methods) {
            strncpy(methods[count++], "POST", 16);
        }
        if (strstr(line, "KERN_PUT") && count < max_methods) {
            strncpy(methods[count++], "PUT", 16);
        }
        if (strstr(line, "KERN_PATCH") && count < max_methods) {
            strncpy(methods[count++], "PATCH", 16);
        }
        if (strstr(line, "KERN_DELETE") && count < max_methods) {
            strncpy(methods[count++], "DELETE", 16);
        }
        if (strstr(line, "KERN_PAGE") && count < max_methods) {
            strncpy(methods[count++], "GET", 16);
        }
    }

    fclose(f);
    return count;
}

/* Recursively scan directory and populate entries */
static int scan_directory(const char *base_dir, const char *relative_path,
                          kern_fs_entry_t *entries, int *count, int max_entries) {
    char full_path[KERN_FS_MAX_PATH];
    if (relative_path[0] == '\0') {
        snprintf(full_path, sizeof(full_path), "%s", base_dir);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, relative_path);
    }

    DIR *dir = opendir(full_path);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < max_entries) {
        const char *name = entry->d_name;

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        /* Skip special files */
        if (is_special_file(name)) continue;

        char entry_path[KERN_FS_MAX_PATH];
        if (relative_path[0] == '\0') {
            snprintf(entry_path, sizeof(entry_path), "%s", name);
        } else {
            snprintf(entry_path, sizeof(entry_path), "%s/%s", relative_path, name);
        }

        char abs_path[KERN_FS_MAX_PATH];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", base_dir, entry_path);

        struct stat st;
        if (stat(abs_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            scan_directory(base_dir, entry_path, entries, count, max_entries);
        } else if (S_ISREG(st.st_mode) && is_c_file(name)) {
            /* Process .c file */
            kern_fs_entry_t *e = &entries[*count];

            /* Build the route path from relative_path + filename */
            char route[KERN_FS_MAX_PATH];
            route[0] = '/';
            route[1] = '\0';

            /* Split relative path into segments and convert brackets */
            if (relative_path[0] != '\0') {
                char *rel_copy = strdup(relative_path);
                if (!rel_copy) continue;

                char *saveptr = NULL;
                char *seg = strtok_r(rel_copy, "/", &saveptr);
                size_t route_len = 1;

                while (seg) {
                    char *converted = convert_brackets(seg);
                    if (converted) {
                        size_t clen = strlen(converted);
                        if (route_len + clen + 1 < KERN_FS_MAX_PATH) {
                            if (route_len > 1) {
                                route[route_len++] = '/';
                            }
                            memcpy(route + route_len, converted, clen);
                            route_len += clen;
                            route[route_len] = '\0';
                        }
                        free(converted);
                    }
                    seg = strtok_r(NULL, "/", &saveptr);
                }
                free(rel_copy);
            }

            /* Handle filename: index.c maps to current path, others add segment */
            size_t name_len = strlen(name);
            char *basename = strndup(name, name_len - 2); /* strip .c */
            if (!basename) continue;

            if (strcmp(basename, "index") != 0) {
                char *converted = convert_brackets(basename);
                if (converted) {
                    size_t route_len = strlen(route);
                    size_t clen = strlen(converted);
                    if (route_len + clen + 2 < KERN_FS_MAX_PATH) {
                        if (route_len > 1) {
                            route[route_len++] = '/';
                        }
                        memcpy(route + route_len, converted, clen);
                        route_len += clen;
                        route[route_len] = '\0';
                    }
                    free(converted);
                }
            }
            free(basename);

            /* Store the entry */
            e->route_path = strdup(route);
            e->file_path = strdup(entry_path);

            /* Parse methods from the file */
            char methods[KERN_FS_MAX_METHODS][16];
            int method_count = parse_methods(abs_path, methods, KERN_FS_MAX_METHODS);

            if (method_count == 0) {
                /* Default to GET */
                e->methods[0] = strdup("GET");
                e->method_count = 1;
            } else {
                e->method_count = method_count < KERN_FS_MAX_METHODS ? method_count : KERN_FS_MAX_METHODS;
                for (int i = 0; i < e->method_count; i++) {
                    e->methods[i] = strdup(methods[i]);
                }
            }

            (*count)++;
        }
    }

    closedir(dir);
    return 0;
}

kern_fs_entry_t *kern_fs_scan(const char *pages_dir, int *out_count) {
    if (!pages_dir || !out_count) return NULL;

    kern_fs_entry_t *entries = calloc(KERN_FS_MAX_ENTRIES, sizeof(kern_fs_entry_t));
    if (!entries) return NULL;

    *out_count = 0;
    int rc = scan_directory(pages_dir, "", entries, out_count, KERN_FS_MAX_ENTRIES);
    if (rc != 0 && *out_count == 0) {
        free(entries);
        return NULL;
    }

    return entries;
}

void kern_fs_entries_free(kern_fs_entry_t *entries, int count) {
    if (!entries) return;
    for (int i = 0; i < count; i++) {
        free(entries[i].route_path);
        free(entries[i].file_path);
        for (int j = 0; j < entries[i].method_count; j++) {
            free(entries[i].methods[j]);
        }
    }
    free(entries);
}

int kern_fs_generate_registry(kern_fs_entry_t *entries, int count, const char *output_path) {
    if (!entries || !output_path) return -1;

    FILE *f = fopen(output_path, "w");
    if (!f) return -1;

    fprintf(f, "/* Auto-generated by kern file-system router. Do not edit. */\n\n");
    fprintf(f, "#include \"kern.h\"\n\n");

    /* Generate extern declarations */
    for (int i = 0; i < count; i++) {
        /* Convert file path to function name: posts/index.c -> page_posts_index */
        char func_name[256];
        snprintf(func_name, sizeof(func_name), "page_");
        size_t pos = 5;

        const char *fp = entries[i].file_path;
        while (*fp && pos < sizeof(func_name) - 1) {
            if (*fp == '/' || *fp == '-' || *fp == '.') {
                if (pos > 5 && func_name[pos - 1] != '_') {
                    func_name[pos++] = '_';
                }
            } else if (*fp == '[' || *fp == ']') {
                /* skip brackets */
            } else {
                func_name[pos++] = *fp;
            }
            fp++;
        }
        /* Remove trailing _c */
        if (pos >= 2 && func_name[pos - 1] == 'c' && func_name[pos - 2] == '_') {
            pos -= 2;
        }
        func_name[pos] = '\0';

        fprintf(f, "extern kern_response_t *%s(kern_req_t *req);\n", func_name);
        /* Store func name back for registration */
        entries[i]._func_name = strdup(func_name);
    }

    fprintf(f, "\n");
    fprintf(f, "void kern_register_all_routes(kern_router_t *router) {\n");

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < entries[i].method_count; j++) {
            fprintf(f, "    kern_router_add(router, \"%s\", \"%s\", %s);\n",
                    entries[i].methods[j],
                    entries[i].route_path,
                    entries[i]._func_name);
        }
    }

    fprintf(f, "}\n");
    fclose(f);

    /* Clean up temporary func names */
    for (int i = 0; i < count; i++) {
        free(entries[i]._func_name);
        entries[i]._func_name = NULL;
    }

    return 0;
}
