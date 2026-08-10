/*
 * kern_tpl_compile.c - Template compilation orchestrator
 *
 * Reads .khtml files, parses them into ASTs, generates C code,
 * and writes the output. Also handles batch compilation of view directories.
 */

#include "kern.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ============================================================
 * Internal helpers
 * ============================================================ */

static char *compile_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* Convert a file path to a function name:
 * "views/pages/home.khtml" -> "kern_render_pages_home"
 * Strips the base directory prefix and extension.
 */
static char *path_to_func_name(const char *path, const char *base_dir) {
    /* Skip base directory prefix */
    const char *relative = path;
    if (base_dir) {
        size_t base_len = strlen(base_dir);
        if (strncmp(path, base_dir, base_len) == 0) {
            relative = path + base_len;
            if (*relative == '/') relative++;
        }
    }

    kern_buf_t *buf = kern_buf_new(128);
    if (!buf) return NULL;

    kern_buf_writes(buf, "kern_render_");

    const char *p = relative;
    while (*p) {
        if (*p == '.' && (strcmp(p, ".khtml") == 0 || strcmp(p, ".kfrag") == 0)) {
            break; /* Stop at extension */
        }
        if (*p == '/' || *p == '-' || *p == '.') {
            kern_buf_writes(buf, "_");
        } else {
            kern_buf_write(buf, p, 1);
        }
        p++;
    }

    char *result = compile_strdup(kern_buf_data(buf));
    kern_buf_free(buf);
    return result;
}

/* ============================================================
 * Public API
 * ============================================================ */

int kern_tpl_compile_file(const char *input_path, const char *output_path) {
    if (!input_path || !output_path) return -1;

    /* Parse the template */
    kern_tpl_node_t *ast = kern_tpl_parse_file(input_path);
    if (!ast) return -1;

    /* Derive function name from input path */
    char *func_name = path_to_func_name(input_path, NULL);
    if (!func_name) {
        kern_tpl_node_free(ast);
        return -1;
    }

    /* Generate C code */
    char *code = kern_tpl_codegen(ast, func_name);
    kern_tpl_node_free(ast);
    free(func_name);

    if (!code) return -1;

    /* Write output file */
    FILE *f = fopen(output_path, "w");
    if (!f) {
        free(code);
        return -1;
    }

    fputs(code, f);
    fclose(f);
    free(code);
    return 0;
}

int kern_tpl_compile_string(const char *source, const char *func_name,
                            char **out_code) {
    if (!source || !func_name || !out_code) return -1;

    kern_tpl_node_t *ast = kern_tpl_parse(source);
    if (!ast) return -1;

    char *code = kern_tpl_codegen(ast, func_name);
    kern_tpl_node_free(ast);

    if (!code) return -1;

    *out_code = code;
    return 0;
}

/* Recursively scan directory for .khtml and .kfrag files */
static int scan_dir_recursive(const char *dir_path, const char *base_dir,
                              kern_buf_t *header_buf, int *count) {
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        /* Build full path */
        kern_buf_t *path_buf = kern_buf_new(256);
        kern_buf_writes(path_buf, dir_path);
        kern_buf_writes(path_buf, "/");
        kern_buf_writes(path_buf, entry->d_name);
        const char *full_path = kern_buf_data(path_buf);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                /* Recurse into subdirectory */
                scan_dir_recursive(full_path, base_dir, header_buf, count);
            } else if (S_ISREG(st.st_mode)) {
                /* Check extension */
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcmp(ext, ".khtml") == 0 || strcmp(ext, ".kfrag") == 0)) {
                    char *func_name = path_to_func_name(full_path, base_dir);
                    if (func_name) {
                        /* Add declaration to header */
                        kern_buf_writes(header_buf, "void ");
                        kern_buf_writes(header_buf, func_name);
                        kern_buf_writes(header_buf, "(kern_buf_t *buf, kern_dict_t *vars);\n");
                        free(func_name);
                        (*count)++;
                    }
                }
            }
        }
        kern_buf_free(path_buf);
    }

    closedir(dir);
    return 0;
}

int kern_tpl_compile_dir(const char *views_dir, const char *output_dir) {
    if (!views_dir || !output_dir) return -1;

    /* Generate header with declarations */
    kern_buf_t *header = kern_buf_new(1024);
    if (!header) return -1;

    kern_buf_writes(header, "/* Auto-generated by kern template compiler. Do not edit. */\n");
    kern_buf_writes(header, "#ifndef KERN_VIEWS_H\n");
    kern_buf_writes(header, "#define KERN_VIEWS_H\n\n");
    kern_buf_writes(header, "#include \"kern.h\"\n\n");

    int count = 0;
    scan_dir_recursive(views_dir, views_dir, header, &count);

    kern_buf_writes(header, "\n#endif /* KERN_VIEWS_H */\n");

    /* Write header file */
    kern_buf_t *header_path = kern_buf_new(256);
    kern_buf_writes(header_path, output_dir);
    kern_buf_writes(header_path, "/kern_views.h");

    FILE *f = fopen(kern_buf_data(header_path), "w");
    if (f) {
        fputs(kern_buf_data(header), f);
        fclose(f);
    }

    kern_buf_free(header_path);
    kern_buf_free(header);

    return count;
}
