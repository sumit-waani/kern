/*
 * cmd_doctor.c - 'kern doctor' command
 *
 * Performs project health checks and diagnostics:
 * 1. kern.toml existence and validity
 * 2. app.secret configuration
 * 3. Required directory structure (pages/, views/, db/migrations/)
 * 4. Compiler availability
 * 5. Raw SQL pattern scanning in source files
 */

#include "kern.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Check result status */
typedef enum {
    DOCTOR_OK,
    DOCTOR_WARN,
    DOCTOR_FAIL
} doctor_status_t;

/* Single check result */
typedef struct {
    const char *name;
    doctor_status_t status;
    char detail[256];
} doctor_check_t;

#define MAX_CHECKS 16

/* --------------- Helper functions --------------- */

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int command_in_path(const char *cmd) {
    char buf[512];
    snprintf(buf, sizeof(buf), "which %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

/*
 * Scan a single .c file for suspicious raw SQL patterns.
 * Returns 1 if both "kern_db_exec" and "sprintf" are found in the file.
 */
static int file_has_raw_sql(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int has_db_exec = 0;
    int has_sprintf = 0;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "kern_db_exec")) has_db_exec = 1;
        if (strstr(line, "sprintf")) has_sprintf = 1;
        if (has_db_exec && has_sprintf) break;
    }

    fclose(f);
    return has_db_exec && has_sprintf;
}

/*
 * Scan all .c files in a directory for raw SQL patterns.
 * Returns the count of files with suspicious patterns.
 * Fills 'first_file' with the name of the first offending file (if any).
 */
static int scan_dir_for_raw_sql(const char *dirpath, char *first_file,
                                size_t first_file_size) {
    DIR *d = opendir(dirpath);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 3) continue;
        if (strcmp(ent->d_name + len - 2, ".c") != 0) continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, ent->d_name);

        if (file_has_raw_sql(filepath)) {
            if (count == 0 && first_file) {
                snprintf(first_file, first_file_size, "%s", ent->d_name);
            }
            count++;
        }
    }

    closedir(d);
    return count;
}

/* --------------- Check functions --------------- */

static void check_kern_toml_exists(doctor_check_t *check, const char *root) {
    check->name = "kern.toml exists";
    char path[1024];
    snprintf(path, sizeof(path), "%s/kern.toml", root);

    if (file_exists(path)) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Configuration file found");
    } else {
        check->status = DOCTOR_FAIL;
        snprintf(check->detail, sizeof(check->detail),
                 "kern.toml not found in project root");
    }
}

static void check_kern_toml_valid(doctor_check_t *check, const char *root) {
    check->name = "kern.toml is valid";
    char path[1024];
    snprintf(path, sizeof(path), "%s/kern.toml", root);

    if (!file_exists(path)) {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "Cannot validate (file missing)");
        return;
    }

    kern_config_t *cfg = kern_config_load(path);
    if (cfg) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Configuration parsed successfully");
        kern_config_free(cfg);
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "Failed to parse kern.toml");
    }
}

static void check_app_secret(doctor_check_t *check, const char *root) {
    check->name = "app.secret is configured";
    char path[1024];
    snprintf(path, sizeof(path), "%s/kern.toml", root);

    if (!file_exists(path)) {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "Cannot check (kern.toml missing)");
        return;
    }

    kern_config_t *cfg = kern_config_load(path);
    if (!cfg) {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "Cannot check (config parse failed)");
        return;
    }

    const char *secret = kern_config_get_str(cfg, "app.secret");
    if (secret && strlen(secret) > 0) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Session secret is configured");
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "app.secret not set (required for secure sessions)");
    }

    kern_config_free(cfg);
}

static void check_pages_dir(doctor_check_t *check, const char *root) {
    check->name = "pages/ directory exists";
    char path[1024];
    snprintf(path, sizeof(path), "%s/pages", root);

    if (dir_exists(path)) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Pages directory found");
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "pages/ directory not found (needed for route handlers)");
    }
}

static void check_views_dir(doctor_check_t *check, const char *root) {
    check->name = "views/ directory exists";
    char path[1024];
    snprintf(path, sizeof(path), "%s/views", root);

    if (dir_exists(path)) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Views directory found");
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "views/ directory not found (needed for templates)");
    }
}

static void check_migrations_dir(doctor_check_t *check, const char *root) {
    check->name = "db/migrations/ directory exists";
    char path[1024];
    snprintf(path, sizeof(path), "%s/db/migrations", root);

    if (dir_exists(path)) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "Migrations directory found");
    } else {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "No migrations directory (ok if not using database)");
    }
}

static void check_compiler(doctor_check_t *check,
                           const char *root __attribute__((unused))) {
    check->name = "Compiler available";

    if (command_in_path("gcc")) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "gcc found in PATH");
    } else if (command_in_path("clang")) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "clang found in PATH");
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "No C compiler (gcc/clang) found in PATH");
    }
}

static void check_raw_sql(doctor_check_t *check, const char *root) {
    check->name = "No raw SQL interpolation";

    char pages_path[1024];
    char models_path[1024];
    snprintf(pages_path, sizeof(pages_path), "%s/pages", root);
    snprintf(models_path, sizeof(models_path), "%s/models", root);

    char first_file[256] = "";
    int total = 0;

    total += scan_dir_for_raw_sql(pages_path, first_file, sizeof(first_file));
    if (first_file[0] == '\0') {
        total += scan_dir_for_raw_sql(models_path, first_file,
                                      sizeof(first_file));
    } else {
        total += scan_dir_for_raw_sql(models_path, NULL, 0);
    }

    if (total == 0) {
        check->status = DOCTOR_OK;
        snprintf(check->detail, sizeof(check->detail),
                 "No suspicious SQL patterns found");
    } else {
        check->status = DOCTOR_WARN;
        snprintf(check->detail, sizeof(check->detail),
                 "%d file(s) with sprintf+kern_db_exec (e.g., %s). "
                 "Use parameterized queries.",
                 total, first_file);
    }
}

/* --------------- Main command --------------- */

static const char *status_label(doctor_status_t s) {
    switch (s) {
    case DOCTOR_OK:   return "\033[32m[OK]\033[0m  ";
    case DOCTOR_WARN: return "\033[33m[WARN]\033[0m";
    case DOCTOR_FAIL: return "\033[31m[FAIL]\033[0m";
    }
    return "[??]  ";
}

int cmd_doctor(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Determine project root (current directory) */
    char root[1024];
    if (getcwd(root, sizeof(root)) == NULL) {
        fprintf(stderr, "Error: cannot determine current directory\n");
        return 1;
    }

    printf("kern doctor - project diagnostics\n");
    printf("==================================\n\n");

    doctor_check_t checks[MAX_CHECKS];
    int num_checks = 0;

    /* Run all checks */
    check_kern_toml_exists(&checks[num_checks++], root);
    check_kern_toml_valid(&checks[num_checks++], root);
    check_app_secret(&checks[num_checks++], root);
    check_pages_dir(&checks[num_checks++], root);
    check_views_dir(&checks[num_checks++], root);
    check_migrations_dir(&checks[num_checks++], root);
    check_compiler(&checks[num_checks++], root);
    check_raw_sql(&checks[num_checks++], root);

    /* Print results */
    int failures = 0;
    int warnings = 0;

    for (int i = 0; i < num_checks; i++) {
        printf("  %s %s\n", status_label(checks[i].status), checks[i].name);
        printf("         %s\n", checks[i].detail);
        if (checks[i].status == DOCTOR_FAIL) failures++;
        if (checks[i].status == DOCTOR_WARN) warnings++;
    }

    /* Summary */
    printf("\n");
    if (failures == 0 && warnings == 0) {
        printf("All checks passed. Project looks healthy!\n");
    } else if (failures == 0) {
        printf("%d warning(s), 0 failures. Project is functional "
               "but has potential issues.\n", warnings);
    } else {
        printf("%d failure(s), %d warning(s). "
               "Please fix critical issues before proceeding.\n",
               failures, warnings);
    }

    return failures > 0 ? 1 : 0;
}
