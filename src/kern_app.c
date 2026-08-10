/*
 * kern_app.c - Application bootstrap
 *
 * Provides the kern_app_t that user code instantiates in app.c.
 * Handles loading config, initializing subsystems, and running
 * the HTTP server event loop.
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

/* ============================================================
 * Internal structure
 * ============================================================ */

struct kern_app {
    char *name;
    int port;
    kern_config_t *config;
    kern_router_t *router;
    kern_server_t *server;
    kern_db_t *db;
    uv_loop_t *loop;
};

/* ============================================================
 * Public API
 * ============================================================ */

kern_app_t *kern_app_new(const char *name) {
    kern_app_t *app = calloc(1, sizeof(kern_app_t));
    if (!app) return NULL;

    app->name = strdup(name ? name : "kern_app");
    app->port = 3000; /* default */

    /* Try to load kern.toml if it exists */
    app->config = kern_config_load("kern.toml");

    /* If config specifies a port, use it */
    if (app->config) {
        int64_t cfg_port = kern_config_get_int(app->config, "app.port");
        if (cfg_port > 0 && cfg_port < 65536) {
            app->port = (int)cfg_port;
        }
    }

    /* Initialize session store */
    kern_session_init();

    /* Initialize database if configured */
    if (app->config) {
        const char *db_path = kern_config_get_str(app->config, "db.path");
        if (db_path) {
            app->db = kern_db_open(db_path);
        }
    }

    /* Create router */
    app->router = kern_router_new();

    return app;
}

void kern_app_listen(kern_app_t *app, int port) {
    if (app) {
        app->port = port;
    }
}

int kern_app_run(kern_app_t *app) {
    if (!app) return 1;

    app->loop = uv_default_loop();
    if (!app->loop) {
        fprintf(stderr, "[kern] failed to create event loop\n");
        return 1;
    }

    app->server = kern_server_new(app->loop);
    if (!app->server) {
        fprintf(stderr, "[kern] failed to create server\n");
        return 1;
    }

    kern_server_set_router(app->server, app->router);

    int rc = kern_server_listen(app->server, "0.0.0.0", app->port);
    if (rc != 0) {
        fprintf(stderr, "[kern] failed to listen on port %d: %s\n",
                app->port, uv_strerror(rc));
        return 1;
    }

    printf("[kern] %s listening on http://localhost:%d\n", app->name, app->port);

    /* Run the event loop */
    uv_run(app->loop, UV_RUN_DEFAULT);

    return 0;
}

kern_router_t *kern_app_router(kern_app_t *app) {
    return app ? app->router : NULL;
}

kern_db_t *kern_app_db(kern_app_t *app) {
    return app ? app->db : NULL;
}

const char *kern_app_name(kern_app_t *app) {
    return app ? app->name : NULL;
}

int kern_app_port(kern_app_t *app) {
    return app ? app->port : 0;
}

void kern_app_free(kern_app_t *app) {
    if (!app) return;

    if (app->server) kern_server_free(app->server);
    if (app->router) kern_router_free(app->router);
    if (app->db) kern_db_close(app->db);
    if (app->config) kern_config_free(app->config);
    free(app->name);
    free(app);
}
