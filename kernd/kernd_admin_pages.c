/*
 * kernd_admin_pages.c - Admin page handlers
 *
 * Implements the dashboard, apps listing, and app management pages.
 */

#include "kernd_admin.h"
#include "kernd_app_registry.h"
#include "kernd_config.h"
#include "kernd_deploy.h"
#include "kernd_html.h"
#include "kernd_log.h"
#include "kernd_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Declared in kernd_admin.c */
extern kern_db_t *kernd_admin_get_db(void);
extern kernd_config_t *kernd_admin_get_cfg(void);

/* Declared in kernd_admin_auth.c */
extern kern_session_t *kernd_auth_check(kern_req_t *req);

#define KERND_VERSION "0.4.0"

/**
 * GET / - Dashboard page
 */
kern_response_t *kernd_page_dashboard(kern_req_t *req) {
    kern_session_t *session = kernd_auth_check(req);
    if (!session) {
        return kern_redirect_response("/login", 302);
    }

    kern_db_t *db = kernd_admin_get_db();

    /* Get app count */
    kern_db_result_t *apps = kernd_app_list(db);
    int app_count = apps ? kern_db_result_row_count(apps) : 0;
    if (apps) {
        kern_db_result_free(apps);
    }

    /* Get hostname */
    char hostname[256] = "unknown";
    gethostname(hostname, sizeof(hostname));

    kern_buf_t *body = kern_buf_new(2048);
    kernd_html_nav(body, true);

    kern_buf_writes(body, "<div class=\"container\">\n");
    kern_buf_writes(body, "  <h1>Dashboard</h1>\n");
    kern_buf_writes(body, "  <div class=\"card\">\n");
    kern_buf_writes(body, "    <table>\n");
    kern_buf_writes(body, "      <tr><td><strong>Hostname</strong></td><td>");
    kernd_html_escape(body, hostname);
    kern_buf_writes(body, "</td></tr>\n");
    kern_buf_writef(body, "      <tr><td><strong>Version</strong></td><td>%s</td></tr>\n",
                    KERND_VERSION);
    kern_buf_writef(body, "      <tr><td><strong>Apps</strong></td><td>%d</td></tr>\n",
                    app_count);
    kern_buf_writes(body, "    </table>\n");
    kern_buf_writes(body, "  </div>\n");
    kern_buf_writes(body, "</div>\n");

    kern_buf_t *page = kern_buf_new(4096);
    kernd_html_layout(page, "kernd - Dashboard", kern_buf_data(body));
    kern_buf_free(body);

    kern_response_t *res = kern_response_new(200);
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");
    kern_response_body_str(res, kern_buf_data(page));
    kern_buf_free(page);
    return res;
}

/**
 * GET /apps - Apps listing page
 */
kern_response_t *kernd_page_apps(kern_req_t *req) {
    kern_session_t *session = kernd_auth_check(req);
    if (!session) {
        return kern_redirect_response("/login", 302);
    }

    kern_db_t *db = kernd_admin_get_db();
    kern_db_result_t *apps = kernd_app_list(db);
    int app_count = apps ? kern_db_result_row_count(apps) : 0;

    kern_buf_t *body = kern_buf_new(4096);
    kernd_html_nav(body, true);

    kern_buf_writes(body, "<div class=\"container\">\n");
    kern_buf_writes(body, "  <h1>Applications</h1>\n");

    /* Add app form */
    kern_buf_writes(body,
        "  <div class=\"card\">\n"
        "    <h2>Add Application</h2>\n"
        "    <form method=\"POST\" action=\"/apps\">\n"
        "      <label>Name</label>\n"
        "      <input type=\"text\" name=\"name\" required>\n"
        "      <label>Repository URL</label>\n"
        "      <input type=\"text\" name=\"repo_url\">\n"
        "      <label>Branch</label>\n"
        "      <input type=\"text\" name=\"branch\" value=\"main\">\n"
        "      <label>Build Command</label>\n"
        "      <input type=\"text\" name=\"build_cmd\">\n"
        "      <label>Start Command</label>\n"
        "      <input type=\"text\" name=\"start_cmd\" required>\n"
        "      <label>Domain</label>\n"
        "      <input type=\"text\" name=\"domain\">\n"
        "      <button type=\"submit\" class=\"btn btn-primary\">Add</button>\n"
        "    </form>\n"
        "  </div>\n");

    /* Apps table */
    kern_buf_writes(body, "  <div class=\"card\">\n");
    if (app_count == 0) {
        kern_buf_writes(body, "    <p>No applications registered.</p>\n");
    } else {
        kern_buf_writes(body,
            "    <table>\n"
            "      <thead>\n"
            "        <tr><th>Name</th><th>Port</th><th>Status</th><th>Actions</th></tr>\n"
            "      </thead>\n"
            "      <tbody>\n");

        for (int i = 0; i < app_count; i++) {
            const char *name = kern_db_row_str(apps, i, 1);
            int64_t port = kern_db_row_int(apps, i, 6);
            const char *status = kern_db_row_str(apps, i, 8);

            kern_buf_writes(body, "        <tr><td>");
            kernd_html_escape(body, name ? name : "");
            kern_buf_writef(body, "</td><td>%d</td><td><span class=\"status status-%s\">",
                            (int)port, status ? status : "created");
            kernd_html_escape(body, status ? status : "created");
            kern_buf_writes(body, "</span></td><td>");

            kern_buf_writef(body,
                "<form method=\"POST\" action=\"/apps/%s/deploy\" style=\"display:inline\">"
                "<button class=\"btn btn-primary btn-sm\">Deploy</button></form> ",
                name ? name : "");
            kern_buf_writef(body,
                "<form method=\"POST\" action=\"/apps/%s/delete\" style=\"display:inline\">"
                "<button class=\"btn btn-danger btn-sm\">Delete</button></form>",
                name ? name : "");

            kern_buf_writes(body, "</td></tr>\n");
        }
        kern_buf_writes(body, "      </tbody>\n    </table>\n");
    }
    kern_buf_writes(body, "  </div>\n</div>\n");

    if (apps) {
        kern_db_result_free(apps);
    }

    kern_buf_t *page = kern_buf_new(8192);
    kernd_html_layout(page, "kernd - Apps", kern_buf_data(body));
    kern_buf_free(body);

    kern_response_t *res = kern_response_new(200);
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");
    kern_response_body_str(res, kern_buf_data(page));
    kern_buf_free(page);
    return res;
}

/**
 * POST /apps - Add a new application
 */
kern_response_t *kernd_page_app_add(kern_req_t *req) {
    kern_session_t *session = kernd_auth_check(req);
    if (!session) {
        return kern_redirect_response("/login", 302);
    }

    kern_db_t *db = kernd_admin_get_db();

    const char *name = kern_form_str(req, "name");
    const char *repo_url = kern_form_str(req, "repo_url");
    const char *branch = kern_form_str(req, "branch");
    const char *build_cmd = kern_form_str(req, "build_cmd");
    const char *start_cmd = kern_form_str(req, "start_cmd");
    const char *domain = kern_form_str(req, "domain");

    if (!name || name[0] == '\0') {
        return kern_redirect_response("/apps", 302);
    }

    kernd_app_t app;
    memset(&app, 0, sizeof(app));
    app.name = strdup(name);
    app.repo_url = (repo_url && repo_url[0]) ? strdup(repo_url) : NULL;
    app.branch = (branch && branch[0]) ? strdup(branch) : NULL;
    app.build_cmd = (build_cmd && build_cmd[0]) ? strdup(build_cmd) : NULL;
    app.start_cmd = (start_cmd && start_cmd[0]) ? strdup(start_cmd) : NULL;
    app.domain = (domain && domain[0]) ? strdup(domain) : NULL;

    kernd_app_add(db, &app);

    free(app.name);
    free(app.repo_url);
    free(app.branch);
    free(app.build_cmd);
    free(app.start_cmd);
    free(app.domain);
    free(app.status);
    free(app.created_at);
    free(app.updated_at);

    return kern_redirect_response("/apps", 302);
}

/**
 * POST /apps/:name/deploy - Deploy an application
 */
kern_response_t *kernd_page_app_deploy(kern_req_t *req) {
    kern_session_t *session = kernd_auth_check(req);
    if (!session) {
        return kern_redirect_response("/login", 302);
    }

    const char *name = kern_param(req, "name");
    if (!name) {
        return kern_redirect_response("/apps", 302);
    }

    kern_db_t *db = kernd_admin_get_db();
    kernd_config_t *cfg = kernd_admin_get_cfg();

    kernd_deploy_start(db, name, cfg->data_dir);

    return kern_redirect_response("/apps", 302);
}

/**
 * POST /apps/:name/delete - Delete an application
 */
kern_response_t *kernd_page_app_delete(kern_req_t *req) {
    kern_session_t *session = kernd_auth_check(req);
    if (!session) {
        return kern_redirect_response("/login", 302);
    }

    const char *name = kern_param(req, "name");
    if (!name) {
        return kern_redirect_response("/apps", 302);
    }

    kern_db_t *db = kernd_admin_get_db();

    /* Stop process if running */
    kernd_process_stop(name);

    /* Remove from registry */
    kernd_app_remove(db, name);

    return kern_redirect_response("/apps", 302);
}
