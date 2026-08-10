/*
 * cmd_test.c - 'kern test' command
 *
 * Discovers and runs test executables as subprocesses.
 * Collects results and prints a summary with colored output.
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ANSI color codes */
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET  "\033[0m"
#define COLOR_BOLD   "\033[1m"

/* Check if stdout is a terminal (for color support) */
static int use_colors(void)
{
    return isatty(STDOUT_FILENO);
}

static const char *color_green(void)
{
    return use_colors() ? COLOR_GREEN : "";
}

static const char *color_red(void)
{
    return use_colors() ? COLOR_RED : "";
}

static const char *color_reset(void)
{
    return use_colors() ? COLOR_RESET : "";
}

static const char *color_bold(void)
{
    return use_colors() ? COLOR_BOLD : "";
}

static void print_test_usage(void)
{
    printf("kern test - Run project tests\n\n");
    printf("Usage:\n");
    printf("  kern test              Run all test executables in build/\n");
    printf("  kern test <path>       Run a specific test binary\n");
    printf("  kern test --verbose    Show full test output\n");
    printf("  kern test --help       Print this help message\n");
    printf("\n");
    printf("Test executables are discovered by looking for files\n");
    printf("matching 'test_*' in the build directory.\n");
}

/**
 * Run a single test executable and return its exit code.
 * If verbose is false, output is suppressed on success.
 */
static int run_test_binary(const char *path, int verbose)
{
    if (verbose) {
        /* Run directly, letting output go to stdout/stderr */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }
        if (pid == 0) {
            execl(path, path, (char *)NULL);
            perror("execl");
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }

    /* Non-verbose: capture output, only show on failure */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s 2>&1", path);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return -1;
    }

    /* Buffer output */
    char *output = NULL;
    size_t output_len = 0;
    size_t output_cap = 0;
    char buf[1024];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (output_len + n >= output_cap) {
            output_cap = (output_cap == 0) ? 4096 : output_cap * 2;
            if (output_len + n >= output_cap) {
                output_cap = output_len + n + 1;
            }
            char *tmp = realloc(output, output_cap);
            if (!tmp) {
                free(output);
                pclose(fp);
                return -1;
            }
            output = tmp;
        }
        memcpy(output + output_len, buf, n);
        output_len += n;
    }

    int status = pclose(fp);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    /* On failure, print the captured output */
    if (exit_code != 0 && output && output_len > 0) {
        output[output_len] = '\0';
        fprintf(stderr, "%s", output);
    }

    free(output);
    return exit_code;
}

/**
 * Check if a file is an executable.
 */
static int is_executable(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}

/**
 * Discover test executables in a directory.
 * Looks for files matching test_* that are executable.
 * Returns the count found. Fills paths array up to max_tests.
 */
static int discover_tests(const char *dir, char **paths, int max_tests)
{
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max_tests) {
        /* Look for test_* files */
        if (strncmp(ent->d_name, "test_", 5) != 0) {
            continue;
        }
        /* Skip files with extensions (e.g., .c, .o) */
        if (strchr(ent->d_name, '.') != NULL) {
            continue;
        }

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, ent->d_name);

        if (is_executable(full_path)) {
            paths[count] = strdup(full_path);
            if (paths[count]) {
                count++;
            }
        }
    }

    closedir(d);

    /* Sort for deterministic output */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(paths[i], paths[j]) > 0) {
                char *tmp = paths[i];
                paths[i] = paths[j];
                paths[j] = tmp;
            }
        }
    }

    return count;
}

/**
 * Find the build directory by checking common locations.
 */
static const char *find_build_dir(void)
{
    struct stat st;
    if (stat("build", &st) == 0 && S_ISDIR(st.st_mode)) {
        return "build";
    }
    if (stat("cmake-build-debug", &st) == 0 && S_ISDIR(st.st_mode)) {
        return "cmake-build-debug";
    }
    return NULL;
}

int cmd_test(int argc, char **argv)
{
    int verbose = 0;
    const char *specific_test = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_test_usage();
            return 0;
        }
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else {
            specific_test = argv[i];
        }
    }

    /* Run a specific test binary */
    if (specific_test) {
        if (!is_executable(specific_test)) {
            fprintf(stderr, "Error: '%s' is not an executable file.\n", specific_test);
            return 1;
        }
        printf("%s[kern test]%s running: %s\n\n",
               color_bold(), color_reset(), specific_test);
        int rc = run_test_binary(specific_test, verbose);
        if (rc == 0) {
            printf("\n%sPASSED%s: %s\n", color_green(), color_reset(), specific_test);
        } else {
            printf("\n%sFAILED%s: %s (exit code %d)\n",
                   color_red(), color_reset(), specific_test, rc);
        }
        return rc == 0 ? 0 : 1;
    }

    /* Discover and run all tests */
    const char *build_dir = find_build_dir();
    if (!build_dir) {
        fprintf(stderr, "Error: no build directory found.\n");
        fprintf(stderr, "  Run 'kern build' or 'cmake --build build' first.\n");
        return 1;
    }

    char *test_paths[512];
    int test_count = discover_tests(build_dir, test_paths, 512);

    if (test_count == 0) {
        printf("[kern test] no test executables found in %s/\n", build_dir);
        printf("  Test executables should be named 'test_*'\n");
        return 0;
    }

    printf("%s[kern test]%s running %d test(s) from %s/\n\n",
           color_bold(), color_reset(), test_count, build_dir);

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < test_count; i++) {
        /* Extract just the test name from the path */
        const char *name = strrchr(test_paths[i], '/');
        name = name ? name + 1 : test_paths[i];

        if (verbose) {
            printf("--- %s ---\n", name);
        }

        int rc = run_test_binary(test_paths[i], verbose);

        if (rc == 0) {
            printf("  %s PASS %s %s\n", color_green(), color_reset(), name);
            passed++;
        } else {
            printf("  %s FAIL %s %s (exit code %d)\n",
                   color_red(), color_reset(), name, rc);
            failed++;
        }

        if (verbose) {
            printf("\n");
        }
    }

    /* Print summary */
    printf("\n%s[kern test] Summary:%s\n", color_bold(), color_reset());
    printf("  %s%d passed%s", color_green(), passed, color_reset());
    if (failed > 0) {
        printf(", %s%d failed%s", color_red(), failed, color_reset());
    }
    printf(", %d total\n", test_count);

    /* Free allocated paths */
    for (int i = 0; i < test_count; i++) {
        free(test_paths[i]);
    }

    return failed > 0 ? 1 : 0;
}
