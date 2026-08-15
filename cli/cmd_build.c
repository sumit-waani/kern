/*
 * cmd_build.c - 'kern build' command
 *
 * Production build pipeline:
 * 1. Load kern.toml for project config
 * 2. Compile .khtml templates to C
 * 3. Generate pages registry
 * 4. Hash assets
 * 5. Invoke C compiler to produce the binary
 */

#include "kern.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Create directory (no error if exists) */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

/* Check if a file exists */
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Check if a directory exists */
static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Find a working C compiler */
static const char *find_cc(void) {
    const char *cc = getenv("CC");
    if (cc && cc[0]) return cc;

    /* Try cc, gcc, clang in order */
    static const char *compilers[] = { "cc", "gcc", "clang", NULL };
    for (int i = 0; compilers[i]; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "which %s >/dev/null 2>&1", compilers[i]);
        if (system(cmd) == 0) {
            return compilers[i];
        }
    }
    return NULL;
}

/* Collect all .c files from a directory into a buffer (space-separated) */
static void collect_c_files(const char *dir, kern_buf_t *buf) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 2 && strcmp(ent->d_name + len - 2, ".c") == 0) {
            kern_buf_writef(buf, " %s/%s", dir, ent->d_name);
        }
    }
    closedir(d);
}

/* Find libkern.a in standard locations */
static const char *find_libkern_path(void) {
    const char *env_path = getenv("KERN_LIB_PATH");
    if (env_path && file_exists(env_path)) return env_path;

    /* Check common locations */
    static const char *paths[] = {
        "../lib/libkern.a",
        "/usr/local/lib/libkern.a",
        "/usr/lib/libkern.a",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        if (file_exists(paths[i])) return paths[i];
    }
    return NULL;
}

/* Find kern.h include directory */
static const char *find_kern_include(void) {
    const char *env_path = getenv("KERN_INCLUDE_PATH");
    if (env_path && dir_exists(env_path)) return env_path;

    static const char *paths[] = {
        "../include",
        "/usr/local/include",
        "/usr/include",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        char check[2048];
        snprintf(check, sizeof(check), "%s/kern.h", paths[i]);
        if (file_exists(check)) return paths[i];
    }
    return NULL;
}

int cmd_build(int argc, char **argv) {
    (void)argc; (void)argv;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Check we are in a kern project root */
    if (!file_exists("kern.toml")) {
        fprintf(stderr, "Error: kern.toml not found.\n");
        fprintf(stderr, "  Run 'kern build' from a kern project root directory.\n");
        return 1;
    }

    printf("[kern] building project...\n");

    /* Load config */
    kern_config_t *cfg = kern_config_load("kern.toml");
    if (!cfg) {
        fprintf(stderr, "Error: could not parse kern.toml\n");
        return 1;
    }

    const char *cfg_name = kern_config_get_str(cfg, "app.name");
    char *app_name = strdup(cfg_name ? cfg_name : "app");

    /* Create output directories */
    ensure_dir("build");
    ensure_dir("build/views");
    ensure_dir("dist");

    int files_compiled = 0;

    /* Step 1: Compile templates */
    if (dir_exists("views")) {
        printf("[kern] compiling templates...\n");
        int tpl_count = kern_tpl_compile_dir("views", "build/views");
        if (tpl_count < 0) {
            fprintf(stderr, "Warning: template compilation had errors\n");
        } else {
            printf("[kern] compiled %d template(s)\n", tpl_count);
            files_compiled += tpl_count;
        }
    }

    /* Step 2: Generate pages registry */
    if (dir_exists("pages")) {
        printf("[kern] scanning routes...\n");
        int route_count = 0;
        kern_fs_entry_t *entries = kern_fs_scan("pages", &route_count);
        if (entries && route_count > 0) {
            kern_fs_generate_registry(entries, route_count, "build/_pages_registry.c");
            printf("[kern] found %d route(s)\n", route_count);
            kern_fs_entries_free(entries, route_count);
            files_compiled++;
        } else {
            /* Create an empty registry if no routes found */
            FILE *f = fopen("build/_pages_registry.c", "w");
            if (f) {
                fprintf(f, "/* AUTO-GENERATED - no routes found */\n");
                fprintf(f, "#include <kern.h>\n");
                fprintf(f, "void kern_register_all_routes(kern_router_t *router) {\n");
                fprintf(f, "    (void)router;\n");
                fprintf(f, "}\n");
                fclose(f);
            }
        }
    } else {
        /* No pages directory, create empty registry */
        FILE *f = fopen("build/_pages_registry.c", "w");
        if (f) {
            fprintf(f, "/* AUTO-GENERATED - no routes */\n");
            fprintf(f, "#include <kern.h>\n");
            fprintf(f, "void kern_register_all_routes(kern_router_t *router) {\n");
            fprintf(f, "    (void)router;\n");
            fprintf(f, "}\n");
            fclose(f);
        }
    }

    /* Step 3: Hash assets */
    if (dir_exists("assets")) {
        printf("[kern] processing assets...\n");
        ensure_dir("public/assets");
        int asset_count = kern_asset_process_dir("assets", "public/assets",
                                                  "build/_asset_manifest.c");
        if (asset_count > 0) {
            printf("[kern] hashed %d asset(s)\n", asset_count);
        }
    } else {
        /* Create empty manifest */
        FILE *f = fopen("build/_asset_manifest.c", "w");
        if (f) {
            fprintf(f, "/* AUTO-GENERATED - no assets */\n");
            fprintf(f, "#ifndef KERN_ASSET_MANIFEST_H\n");
            fprintf(f, "#define KERN_ASSET_MANIFEST_H\n");
            fprintf(f, "#endif\n");
            fclose(f);
        }
    }

    /* Step 4: Compile everything */
    const char *cc = find_cc();
    if (!cc) {
        fprintf(stderr, "Error: no C compiler found. Set CC environment variable.\n");
        kern_config_free(cfg);
        free(app_name);
        return 1;
    }

    const char *kern_lib = find_libkern_path();
    const char *kern_inc = find_kern_include();

    printf("[kern] compiling with %s...\n", cc);

    /* Build the compile command */
    kern_buf_t *cmd = kern_buf_new(4096);
    kern_buf_writef(cmd, "%s -std=c11 -O2 -o dist/%s", cc, app_name);

    /* Include paths */
    kern_buf_writes(cmd, " -I.");
    if (kern_inc) {
        kern_buf_writef(cmd, " -I%s", kern_inc);
    }

    /* Source files: app.c */
    if (file_exists("app.c")) {
        kern_buf_writes(cmd, " app.c");
        files_compiled++;
    }

    /* Source files from standard directories */
    collect_c_files("pages", cmd);
    collect_c_files("models", cmd);
    collect_c_files("queries", cmd);
    collect_c_files("mutations", cmd);
    collect_c_files("middleware", cmd);
    collect_c_files("auth", cmd);
    collect_c_files("jobs", cmd);

    /* Generated files */
    collect_c_files("build", cmd);
    collect_c_files("build/views", cmd);

    /* Link flags */
    if (kern_lib) {
        kern_buf_writef(cmd, " %s", kern_lib);
    } else {
        kern_buf_writes(cmd, " -lkern");
    }
    kern_buf_writes(cmd, " -luv -lsqlite3 -lpthread");

    /* Run the compiler */
    const char *compile_cmd = kern_buf_data(cmd);
    int rc = system(compile_cmd);

    kern_buf_free(cmd);
    kern_config_free(cfg);

    if (rc != 0) {
        fprintf(stderr, "\n[kern] build FAILED\n");
        free(app_name);
        return 1;
    }

    /* Build summary */
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (double)(end.tv_sec - start.tv_sec) +
                     (double)(end.tv_nsec - start.tv_nsec) / 1e9;

    /* Get binary size */
    char dist_path[256];
    snprintf(dist_path, sizeof(dist_path), "dist/%s", app_name);
    struct stat st;
    long binary_size = 0;
    if (stat(dist_path, &st) == 0) {
        binary_size = (long)st.st_size;
    }

    printf("\n[kern] build OK (%.1fs)\n", elapsed);
    printf("  binary: dist/%s (%ld bytes)\n", app_name, binary_size);
    printf("  files:  %d compiled\n\n", files_compiled);

    free(app_name);
    return 0;
}
