/*
 * kernd_admin_auth.c - Admin authentication handlers
 *
 * Implements login/logout using sessions and password verification.
 */

#include "kernd_admin.h"
#include "kernd_config.h"
#include "kernd_html.h"
#include "kernd_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Declared in kernd_admin.c */
extern kern_db_t *kernd_admin_get_db(void);
extern kernd_config_t *kernd_admin_get_cfg(void);

/**
 * Check if the current request has a valid session.
 * Returns the session if authenticated, NULL otherwise.
 */
kern_session_t *kernd_auth_check(kern_req_t *req) {
    kern_session_t *session = kern_session_start(req);
    if (!session) {
        return NULL;
    }

    const char *authed = kern_session_get(session, "authenticated");
    if (authed && strcmp(authed, "yes") == 0) {
        return session;
    }

    return NULL;
}

/**
 * GET /login - Render login form
 */
kern_response_t *kernd_page_login(kern_req_t *req) {
    (void)req;

    kern_buf_t *body = kern_buf_new(1024);
    kernd_html_nav(body, false);
    kern_buf_writes(body,
        "<div class=\"container\">\n"
        "  <div class=\"card\" style=\"max-width:400px;margin:2rem auto;\">\n"
        "    <h2>Login</h2>\n"
        "    <form method=\"POST\" action=\"/login\">\n"
        "      <label for=\"password\">Password</label>\n"
        "      <input type=\"password\" name=\"password\" id=\"password\" required>\n"
        "      <button type=\"submit\" class=\"btn btn-primary\">Sign In</button>\n"
        "    </form>\n"
        "  </div>\n"
        "</div>\n");

    kern_buf_t *page = kern_buf_new(4096);
    kernd_html_layout(page, "kernd - Login", kern_buf_data(body));
    kern_buf_free(body);

    kern_response_t *res = kern_response_new(200);
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");
    kern_response_body_str(res, kern_buf_data(page));
    kern_buf_free(page);
    return res;
}

/**
 * POST /login - Authenticate
 */
kern_response_t *kernd_page_login_post(kern_req_t *req) {
    const char *password = kern_form_str(req, "password");
    kernd_config_t *cfg = kernd_admin_get_cfg();

    if (password && cfg->admin_password_hash &&
        kern_password_verify(password, cfg->admin_password_hash)) {

        kern_session_t *session = kern_session_start(req);
        kern_session_set(session, "authenticated", "yes");

        kern_response_t *res = kern_redirect_response("/", 302);
        kern_response_header(res, "Set-Cookie", kern_session_cookie(session));
        return res;
    }

    /* Failed login - show form again with error */
    kern_buf_t *body = kern_buf_new(1024);
    kernd_html_nav(body, false);
    kern_buf_writes(body,
        "<div class=\"container\">\n"
        "  <div class=\"card\" style=\"max-width:400px;margin:2rem auto;\">\n"
        "    <h2>Login</h2>\n"
        "    <p style=\"color:#c0392b;margin-bottom:1rem;\">Invalid password.</p>\n"
        "    <form method=\"POST\" action=\"/login\">\n"
        "      <label for=\"password\">Password</label>\n"
        "      <input type=\"password\" name=\"password\" id=\"password\" required>\n"
        "      <button type=\"submit\" class=\"btn btn-primary\">Sign In</button>\n"
        "    </form>\n"
        "  </div>\n"
        "</div>\n");

    kern_buf_t *page = kern_buf_new(4096);
    kernd_html_layout(page, "kernd - Login", kern_buf_data(body));
    kern_buf_free(body);

    kern_response_t *res = kern_response_new(401);
    kern_response_header(res, "Content-Type", "text/html; charset=utf-8");
    kern_response_body_str(res, kern_buf_data(page));
    kern_buf_free(page);
    return res;
}

/**
 * POST /logout - Destroy session
 */
kern_response_t *kernd_page_logout(kern_req_t *req) {
    kern_session_t *session = kern_session_start(req);
    if (session) {
        kern_session_destroy(kern_session_id(session));
    }

    return kern_redirect_response("/login", 302);
}
