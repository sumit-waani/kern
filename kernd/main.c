/*
 * kernd - VPS dashboard daemon
 *
 * Entry point that parses CLI args and dispatches to subcommands:
 *   kernd init [--config <path>]   - Generate initial configuration
 *   kernd run [--config <path>]    - Run the daemon
 *   kernd --version                - Print version
 *   kernd --help                   - Print usage
 */

#include "kernd_admin.h"
#include "kernd_app_registry.h"
#include "kernd_cgroup.h"
#include "kernd_config.h"
#include "kernd_init.h"
#include "kernd_log.h"
#include "kernd_process.h"
#include "kernd_proxy.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define KERND_VERSION "0.4.0"

static uv_loop_t *main_loop = NULL;

static void signal_handler(uv_signal_t *handle, int signum) {
    (void)handle;
    const char *name = (signum == SIGINT) ? "SIGINT" : "SIGTERM";
    kernd_log_info("received %s, shutting down...", name);
    if (main_loop) {
        uv_stop(main_loop);
    }
}

static void print_usage(void) {
    printf("kernd %s - VPS dashboard daemon\n\n", KERND_VERSION);
    printf("Usage:\n");
    printf("  kernd init [--config <path>]   Generate initial configuration\n");
    printf("  kernd run [--config <path>]    Run the daemon\n");
    printf("  kernd --version                Print version\n");
    printf("  kernd --help                   Print this help message\n");
}

static const char *parse_config_arg(int argc, char **argv) {
    for (int i = 0; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int cmd_run(int argc, char **argv) {
    const char *config_path = parse_config_arg(argc, argv);
    const char *default_path = "/etc/kernd/dashboard.toml";
    const char *path = config_path ? config_path : default_path;

    kernd_config_t *cfg = kernd_config_load(path);
    if (!cfg) {
        kernd_log_error("failed to load configuration");
        return 1;
    }

    kernd_log_info("kernd %s starting", KERND_VERSION);
    kernd_log_info("admin port: %d", cfg->admin_port);
    kernd_log_info("data dir: %s", cfg->data_dir);

    /* Open database */
    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/kernd.db", cfg->data_dir);
    kern_db_t *db = kern_db_open(db_path);
    if (!db) {
        kernd_log_error("failed to open database at %s", db_path);
        kernd_config_free(cfg);
        return 1;
    }

    /* Initialize subsystems */
    if (kernd_registry_init(db) != 0) {
        kernd_log_error("failed to initialize app registry");
        kern_db_close(db);
        kernd_config_free(cfg);
        return 1;
    }

    kernd_process_init();
    kern_session_init();

    /* Initialize cgroup subsystem (non-fatal if unavailable) */
    kernd_cgroup_init();

    /* Initialize reverse proxy vhost table */
    if (kernd_proxy_init() != 0) {
        kernd_log_error("failed to initialize proxy");
        kern_db_close(db);
        kernd_config_free(cfg);
        return 1;
    }

    /* Create event loop */
    main_loop = uv_default_loop();
    if (!main_loop) {
        kernd_log_error("failed to create event loop");
        kern_db_close(db);
        kernd_config_free(cfg);
        return 1;
    }

    /* Set up signal handlers */
    uv_signal_t sigint_handle;
    uv_signal_t sigterm_handle;

    uv_signal_init(main_loop, &sigint_handle);
    uv_signal_init(main_loop, &sigterm_handle);
    uv_signal_start(&sigint_handle, signal_handler, SIGINT);
    uv_signal_start(&sigterm_handle, signal_handler, SIGTERM);

    /* Start admin server */
    if (kernd_admin_start(cfg, main_loop, db) != 0) {
        kernd_log_error("failed to start admin server");
        kern_db_close(db);
        kernd_config_free(cfg);
        return 1;
    }

    /* Start reverse proxy */
    if (kernd_proxy_start(main_loop, cfg->http_port) != 0) {
        kernd_log_error("failed to start reverse proxy");
        kernd_admin_stop();
        kern_db_close(db);
        kernd_config_free(cfg);
        return 1;
    }

    kernd_log_info("event loop running (press Ctrl+C to stop)");

    /* Run event loop */
    uv_run(main_loop, UV_RUN_DEFAULT);

    /* Cleanup */
    kernd_proxy_stop();
    kernd_admin_stop();

    uv_signal_stop(&sigint_handle);
    uv_signal_stop(&sigterm_handle);
    uv_close((uv_handle_t *)&sigint_handle, NULL);
    uv_close((uv_handle_t *)&sigterm_handle, NULL);
    uv_run(main_loop, UV_RUN_DEFAULT);  /* Drain pending close callbacks */

    kern_db_close(db);
    kernd_log_info("shutdown complete");
    kernd_config_free(cfg);
    return 0;
}

static int cmd_init(int argc, char **argv) {
    const char *config_path = parse_config_arg(argc, argv);
    return kernd_init_run(config_path) == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("kernd %s\n", KERND_VERSION);
        return 0;
    }

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "init") == 0) {
        return cmd_init(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "run") == 0) {
        return cmd_run(argc - 1, argv + 1);
    }

    fprintf(stderr, "Error: unknown command '%s'\n", cmd);
    fprintf(stderr, "Run 'kernd --help' for available commands.\n");
    return 1;
}
