/*
 * kernd_admin.c - Admin HTTP server implementation
 *
 * Sets up routes and dispatches to page handlers.
 */

#include "kernd_admin.h"
#include "kernd_app_registry.h"
#include "kernd_deploy.h"
#include "kernd_html.h"
#include "kernd_log.h"
#include "kernd_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global state accessible to handlers */
static kern_server_t *admin_server = NULL;
static kern_router_t *admin_router = NULL;
static kern_db_t *admin_db = NULL;
static kernd_config_t *admin_cfg = NULL;

/* Forward declarations for handlers (defined in other files) */
kern_response_t *kernd_page_login(kern_req_t *req);
kern_response_t *kernd_page_login_post(kern_req_t *req);
kern_response_t *kernd_page_logout(kern_req_t *req);
kern_response_t *kernd_page_dashboard(kern_req_t *req);
kern_response_t *kernd_page_apps(kern_req_t *req);
kern_response_t *kernd_page_app_add(kern_req_t *req);
kern_response_t *kernd_page_app_deploy(kern_req_t *req);
kern_response_t *kernd_page_app_delete(kern_req_t *req);

/* Accessors for admin state (used by other admin modules) */
kern_db_t *kernd_admin_get_db(void) {
    return admin_db;
}

kernd_config_t *kernd_admin_get_cfg(void) {
    return admin_cfg;
}

int kernd_admin_start(kernd_config_t *cfg, uv_loop_t *loop, kern_db_t *db) {
    if (!cfg || !loop || !db) {
        return -1;
    }

    admin_cfg = cfg;
    admin_db = db;

    /* Create router */
    admin_router = kern_router_new();
    if (!admin_router) {
        kernd_log_error("failed to create admin router");
        return -1;
    }

    /* Register routes */
    kern_router_add(admin_router, "GET", "/", kernd_page_dashboard);
    kern_router_add(admin_router, "GET", "/login", kernd_page_login);
    kern_router_add(admin_router, "POST", "/login", kernd_page_login_post);
    kern_router_add(admin_router, "POST", "/logout", kernd_page_logout);
    kern_router_add(admin_router, "GET", "/apps", kernd_page_apps);
    kern_router_add(admin_router, "POST", "/apps", kernd_page_app_add);
    kern_router_add(admin_router, "POST", "/apps/:name/deploy", kernd_page_app_deploy);
    kern_router_add(admin_router, "POST", "/apps/:name/delete", kernd_page_app_delete);

    /* Create server */
    admin_server = kern_server_new(loop);
    if (!admin_server) {
        kernd_log_error("failed to create admin server");
        kern_router_free(admin_router);
        admin_router = NULL;
        return -1;
    }

    kern_server_set_router(admin_server, admin_router);

    int rc = kern_server_listen(admin_server, "0.0.0.0", cfg->admin_port);
    if (rc != 0) {
        kernd_log_error("failed to listen on port %d", cfg->admin_port);
        kern_server_free(admin_server);
        kern_router_free(admin_router);
        admin_server = NULL;
        admin_router = NULL;
        return -1;
    }

    kernd_log_info("admin server listening on port %d", cfg->admin_port);
    return 0;
}

void kernd_admin_stop(void) {
    if (admin_server) {
        kern_server_stop(admin_server);
        kern_server_free(admin_server);
        admin_server = NULL;
    }
    if (admin_router) {
        kern_router_free(admin_router);
        admin_router = NULL;
    }
}
