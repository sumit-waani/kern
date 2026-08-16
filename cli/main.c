/*
 * kern CLI - main entry point
 *
 * Parses command line arguments and dispatches to subcommands:
 *   kern new <name>    - Scaffold a new project
 *   kern build         - Production build
 *   kern dev           - Development mode with watch
 *   kern doctor        - Project diagnostics
 *   kern --version     - Print version
 *   kern --help        - Print usage
 */

#include <stdio.h>
#include <string.h>

#define KERN_VERSION "0.1.0"

/* Forward declarations for subcommands */
int cmd_new(int argc, char **argv);
int cmd_build(int argc, char **argv);
int cmd_dev(int argc, char **argv);
int cmd_fmt(int argc, char **argv);
int cmd_test(int argc, char **argv);
int cmd_doctor(int argc, char **argv);

static void print_usage(void) {
    printf("kern %s - A C11 web framework\n\n", KERN_VERSION);
    printf("Usage:\n");
    printf("  kern new <name>    Create a new kern project\n");
    printf("  kern build         Build the project for production\n");
    printf("  kern dev           Start development server with auto-rebuild\n");
    printf("  kern fmt           Format source files (.c, .h, .khtml)\n");
    printf("  kern test          Run project tests\n");
    printf("  kern doctor        Diagnose project issues\n");
    printf("  kern --version     Print version\n");
    printf("  kern --help        Print this help message\n");
    printf("\n");
    printf("Run 'kern <command> --help' for more information on a command.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("kern %s\n", KERN_VERSION);
        return 0;
    }

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "new") == 0) {
        return cmd_new(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "build") == 0) {
        return cmd_build(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "dev") == 0) {
        return cmd_dev(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "fmt") == 0) {
        return cmd_fmt(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "test") == 0) {
        return cmd_test(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "doctor") == 0) {
        return cmd_doctor(argc - 1, argv + 1);
    }

    fprintf(stderr, "Error: unknown command '%s'\n", cmd);
    fprintf(stderr, "Run 'kern --help' for available commands.\n");
    return 1;
}
