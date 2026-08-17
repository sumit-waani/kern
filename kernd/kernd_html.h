/*
 * kernd_html.h - HTML rendering helpers for kernd admin UI
 *
 * Provides page layout, navigation, and HTML escaping utilities.
 */

#ifndef KERND_HTML_H
#define KERND_HTML_H

#include "kern.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wrap body HTML in a full HTML5 document with inline CSS.
 * Writes the complete page to buf.
 */
void kernd_html_layout(kern_buf_t *buf, const char *title, const char *body_html);

/**
 * Render the navigation bar HTML into buf.
 */
void kernd_html_nav(kern_buf_t *buf, bool logged_in);

/**
 * HTML-escape a string into the buffer.
 * Escapes &, <, >, ", and '.
 */
void kernd_html_escape(kern_buf_t *buf, const char *str);

#ifdef __cplusplus
}
#endif

#endif /* KERND_HTML_H */
