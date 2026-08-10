/*
 * cmd_dev.c - 'kern dev' command
 *
 * Development mode with file watching:
 * 1. Run kern build
 * 2. Start the built binary as a child process
 * 3. Watch for file changes using libuv fs_event
 * 4. On change: kill child, rebuild, restart
 */

#include "kern.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>

/* Colors for terminal output */
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

/* Forward declaration */
int cmd_build(int argc, char **argv);

/* Global state for the dev server */
static pid_t child_pid = -1;
static uv_loop_t *dev_loop = NULL;
static const char *binary_path = NULL;
static volatile int shutting_down = 0;

/* Kill the child process */
static void kill_child(void) {
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        int status;
        waitpid(child_pid, &status, 0);
        child_pid = -1;
    }
}

/* Start the application process */
static int start_child(void) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[kern] failed to fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        execl(binary_path, binary_path, NULL);
        /* If exec fails */
        fprintf(stderr, "[kern] failed to exec %s: %s\n", binary_path, strerror(errno));
        _exit(1);
    }

    child_pid = pid;
    return 0;
}

/* Signal handler for graceful shutdown */
static void signal_handler(uv_signal_t *handle, int signum) {
    (void)handle;
    (void)signum;
    shutting_down = 1;
    printf("\n" COLOR_CYAN "[kern]" COLOR_RESET " shutting down...\n");
    kill_child();
    uv_stop(dev_loop);
}

/* File change callback */
static void on_fs_event(uv_fs_event_t *handle, const char *filename,
                        int events, int status) {
    (void)handle;
    (void)events;

    if (status < 0 || shutting_down) return;

    const char *name = filename ? filename : "unknown";
    printf(COLOR_CYAN "[kern]" COLOR_RESET " change detected: %s\n", name);
    printf(COLOR_YELLOW "[kern]" COLOR_RESET " rebuilding...\n");

    /* Kill current server */
    kill_child();

    /* Rebuild */
    int rc = cmd_build(0, NULL);
    if (rc != 0) {
        fprintf(stderr, COLOR_YELLOW "[kern]" COLOR_RESET
                " build failed, waiting for changes...\n");
        return;
    }

    /* Restart */
    if (start_child() == 0) {
        printf(COLOR_GREEN "[kern]" COLOR_RESET " server restarted\n");
    }
}

/* Watch a directory for changes */
static int watch_dir(uv_loop_t *loop, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0; /* Skip if doesn't exist */

    uv_fs_event_t *watcher = malloc(sizeof(uv_fs_event_t));
    if (!watcher) return -1;

    uv_fs_event_init(loop, watcher);
    int rc = uv_fs_event_start(watcher, on_fs_event, path, UV_FS_EVENT_RECURSIVE);
    if (rc != 0) {
        free(watcher);
        return -1;
    }
    return 0;
}

int cmd_dev(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Check we are in a kern project root */
    struct stat st;
    if (stat("kern.toml", &st) != 0) {
        fprintf(stderr, "Error: kern.toml not found.\n");
        fprintf(stderr, "  Run 'kern dev' from a kern project root directory.\n");
        return 1;
    }

    /* Load config to get app name and port */
    kern_config_t *cfg = kern_config_load("kern.toml");
    if (!cfg) {
        fprintf(stderr, "Error: could not parse kern.toml\n");
        return 1;
    }

    const char *app_name = kern_config_get_str(cfg, "app.name");
    if (!app_name) app_name = "app";
    int64_t port = kern_config_get_int(cfg, "app.port");
    if (port <= 0) port = 3000;

    /* Construct binary path */
    char bin_path[256];
    snprintf(bin_path, sizeof(bin_path), "dist/%s", app_name);
    binary_path = bin_path;

    printf(COLOR_CYAN "[kern]" COLOR_RESET " development mode\n");

    /* Initial build */
    printf(COLOR_CYAN "[kern]" COLOR_RESET " running initial build...\n");
    int rc = cmd_build(0, NULL);
    if (rc != 0) {
        fprintf(stderr, "[kern] initial build failed\n");
        kern_config_free(cfg);
        return 1;
    }

    /* Start the dev loop */
    dev_loop = uv_loop_new();
    if (!dev_loop) {
        fprintf(stderr, "[kern] failed to create event loop\n");
        kern_config_free(cfg);
        return 1;
    }

    /* Start the server */
    if (start_child() != 0) {
        fprintf(stderr, "[kern] failed to start server\n");
        kern_config_free(cfg);
        uv_loop_delete(dev_loop);
        return 1;
    }

    printf(COLOR_GREEN "[kern]" COLOR_RESET
           " server running on http://localhost:%d\n", (int)port);
    printf(COLOR_CYAN "[kern]" COLOR_RESET " watching for changes...\n");

    /* Set up file watchers */
    watch_dir(dev_loop, "pages");
    watch_dir(dev_loop, "views");
    watch_dir(dev_loop, "assets");
    watch_dir(dev_loop, "models");

    /* Watch individual files */
    watch_dir(dev_loop, "kern.toml");
    watch_dir(dev_loop, "app.c");

    /* Set up signal handlers */
    uv_signal_t sigint, sigterm;
    uv_signal_init(dev_loop, &sigint);
    uv_signal_init(dev_loop, &sigterm);
    uv_signal_start(&sigint, signal_handler, SIGINT);
    uv_signal_start(&sigterm, signal_handler, SIGTERM);

    /* Run the event loop */
    uv_run(dev_loop, UV_RUN_DEFAULT);

    /* Cleanup */
    kill_child();
    uv_loop_delete(dev_loop);
    kern_config_free(cfg);

    printf(COLOR_CYAN "[kern]" COLOR_RESET " stopped\n");
    return 0;
}
