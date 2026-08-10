/*
 * cmd_fmt.c - 'kern fmt' command
 *
 * Formats C source files (.c/.h) and .khtml templates in a project.
 * Supports formatting specific files, directories, or the entire project.
 * --check mode reports unformatted files without modifying them.
 */

#include "kern.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void print_fmt_usage(void)
{
    printf("kern fmt - Format source files\n\n");
    printf("Usage:\n");
    printf("  kern fmt              Format all .c/.h/.khtml files recursively\n");
    printf("  kern fmt <path>       Format a specific file or directory\n");
    printf("  kern fmt --check      Check if files need formatting (exit 1 if any do)\n");
    printf("  kern fmt --help       Print this help message\n");
    printf("\n");
    printf("Supported file types:\n");
    printf("  .c, .h     - C source files (4-space indent)\n");
    printf("  .khtml     - Template files (2-space indent)\n");
}

/**
 * Check if a filename has a formattable extension.
 */
static int is_formattable(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
    {
        return 0;
    }
    return (strcmp(dot, ".c") == 0 ||
            strcmp(dot, ".h") == 0 ||
            strcmp(dot, ".khtml") == 0);
}

/**
 * Recursively format or check all files in a directory.
 * Returns number of files that were formatted (or need formatting in check mode).
 */
static int process_directory(const char *path, int check_only, int *total_files)
{
    DIR *d = opendir(path);
    if (!d)
    {
        return 0;
    }

    int count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL)
    {
        /* Skip hidden dirs and common build/output directories */
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        if (strcmp(ent->d_name, "build") == 0 ||
            strcmp(ent->d_name, "dist") == 0 ||
            strcmp(ent->d_name, "node_modules") == 0)
        {
            continue;
        }

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
        {
            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            count += process_directory(full_path, check_only, total_files);
        }
        else if (S_ISREG(st.st_mode) && is_formattable(ent->d_name))
        {
            (*total_files)++;

            if (check_only)
            {
                int result = kern_fmt_check(full_path);
                if (result == 1)
                {
                    printf("  needs formatting: %s\n", full_path);
                    count++;
                }
            }
            else
            {
                int result = kern_fmt_file(full_path);
                if (result == 0)
                {
                    count++;
                }
                else
                {
                    fprintf(stderr, "  warning: could not format %s\n", full_path);
                }
            }
        }
    }

    closedir(d);
    return count;
}

int cmd_fmt(int argc, char **argv)
{
    int check_only = 0;
    const char *target = NULL;

    /* Parse arguments */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_fmt_usage();
            return 0;
        }
        else if (strcmp(argv[i], "--check") == 0)
        {
            check_only = 1;
        }
        else if (argv[i][0] != '-')
        {
            target = argv[i];
        }
        else
        {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Run 'kern fmt --help' for usage.\n");
            return 1;
        }
    }

    /* If a specific file is given */
    if (target)
    {
        struct stat st;
        if (stat(target, &st) != 0)
        {
            fprintf(stderr, "Error: '%s' not found\n", target);
            return 1;
        }

        if (S_ISREG(st.st_mode))
        {
            if (!is_formattable(target))
            {
                fprintf(stderr, "Error: unsupported file type '%s'\n", target);
                return 1;
            }

            if (check_only)
            {
                int result = kern_fmt_check(target);
                if (result == 1)
                {
                    printf("  needs formatting: %s\n", target);
                    return 1;
                }
                printf("  all files formatted\n");
                return 0;
            }
            else
            {
                int result = kern_fmt_file(target);
                if (result != 0)
                {
                    fprintf(stderr, "Error: could not format '%s'\n", target);
                    return 1;
                }
                printf("  formatted: %s\n", target);
                return 0;
            }
        }
        else if (S_ISDIR(st.st_mode))
        {
            int total_files = 0;
            int count = process_directory(target, check_only, &total_files);

            if (check_only)
            {
                if (count > 0)
                {
                    printf("\n  %d file(s) need formatting\n", count);
                    return 1;
                }
                printf("  all %d file(s) formatted\n", total_files);
                return 0;
            }
            else
            {
                printf("  formatted %d file(s)\n", count);
                return 0;
            }
        }
        else
        {
            fprintf(stderr, "Error: '%s' is not a file or directory\n", target);
            return 1;
        }
    }

    /* No target: format current directory recursively */
    int total_files = 0;
    int count = process_directory(".", check_only, &total_files);

    if (check_only)
    {
        if (count > 0)
        {
            printf("\n  %d file(s) need formatting\n", count);
            return 1;
        }
        printf("  all %d file(s) formatted\n", total_files);
        return 0;
    }
    else
    {
        printf("  formatted %d file(s)\n", count);
        return 0;
    }
}
