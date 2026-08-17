/*
 * kernd_html.c - HTML rendering helpers implementation
 */

#include "kernd_html.h"

#include <string.h>

void kernd_html_layout(kern_buf_t *buf, const char *title, const char *body_html) {
    kern_buf_writes(buf,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>");
    kernd_html_escape(buf, title);
    kern_buf_writes(buf, "</title>\n"
        "  <style>\n"
        "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "    body { font-family: -apple-system, BlinkMacSystemFont, sans-serif;\n"
        "           background: #f5f5f5; color: #333; line-height: 1.6; }\n"
        "    nav { background: #1a1a2e; padding: 1rem 2rem; display: flex;\n"
        "          align-items: center; justify-content: space-between; }\n"
        "    nav a { color: #fff; text-decoration: none; margin-left: 1rem; }\n"
        "    nav .brand { font-weight: bold; font-size: 1.2rem; color: #e94560; }\n"
        "    .container { max-width: 960px; margin: 2rem auto; padding: 0 1rem; }\n"
        "    .card { background: #fff; border-radius: 8px; padding: 1.5rem;\n"
        "            box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 1rem; }\n"
        "    h1, h2 { margin-bottom: 1rem; }\n"
        "    table { width: 100%; border-collapse: collapse; }\n"
        "    th, td { text-align: left; padding: 0.75rem; border-bottom: 1px solid #eee; }\n"
        "    th { background: #f9f9f9; font-weight: 600; }\n"
        "    .btn { display: inline-block; padding: 0.5rem 1rem; border: none;\n"
        "           border-radius: 4px; cursor: pointer; font-size: 0.9rem;\n"
        "           text-decoration: none; color: #fff; }\n"
        "    .btn-primary { background: #e94560; }\n"
        "    .btn-danger { background: #c0392b; }\n"
        "    .btn-sm { padding: 0.3rem 0.6rem; font-size: 0.8rem; }\n"
        "    input[type=text], input[type=password] {\n"
        "      width: 100%; padding: 0.5rem; margin: 0.3rem 0 1rem;\n"
        "      border: 1px solid #ddd; border-radius: 4px; }\n"
        "    .status { padding: 0.2rem 0.5rem; border-radius: 3px; font-size: 0.8rem; }\n"
        "    .status-running { background: #27ae60; color: #fff; }\n"
        "    .status-stopped { background: #95a5a6; color: #fff; }\n"
        "    .status-created { background: #3498db; color: #fff; }\n"
        "    .status-deploying { background: #f39c12; color: #fff; }\n"
        "    .status-failed { background: #c0392b; color: #fff; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n");
    kern_buf_writes(buf, body_html);
    kern_buf_writes(buf, "\n</body>\n</html>\n");
}

void kernd_html_nav(kern_buf_t *buf, bool logged_in) {
    kern_buf_writes(buf,
        "<nav>\n"
        "  <span class=\"brand\">kernd</span>\n"
        "  <div>\n");
    if (logged_in) {
        kern_buf_writes(buf,
            "    <a href=\"/\">Dashboard</a>\n"
            "    <a href=\"/apps\">Apps</a>\n"
            "    <form method=\"POST\" action=\"/logout\" style=\"display:inline\">\n"
            "      <button type=\"submit\" class=\"btn btn-sm\" "
            "style=\"background:transparent;color:#fff;border:1px solid #fff;\">Logout</button>\n"
            "    </form>\n");
    } else {
        kern_buf_writes(buf, "    <a href=\"/login\">Login</a>\n");
    }
    kern_buf_writes(buf, "  </div>\n</nav>\n");
}

void kernd_html_escape(kern_buf_t *buf, const char *str) {
    if (!str) {
        return;
    }
    kern_html_escape(buf, str);
}
