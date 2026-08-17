/*
 * kernd_proxy.h - Reverse proxy with host-header routing
 *
 * Accepts TCP connections on the HTTP port, parses the Host header
 * from the request, looks up the backend port in a vhost table,
 * and forwards the request to 127.0.0.1:<port>. Returns 404 for
 * unknown hosts and 502 if the backend is unreachable.
 */

#ifndef KERND_PROXY_H
#define KERND_PROXY_H

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the proxy vhost table.
 * Must be called before any other kernd_proxy_* functions.
 * Returns 0 on success, -1 on failure.
 */
int kernd_proxy_init(void);

/**
 * Add a virtual host mapping.
 * Maps hostname to a backend port on 127.0.0.1.
 * Returns 0 on success, -1 on failure.
 */
int kernd_proxy_add_vhost(const char *hostname, int backend_port);

/**
 * Remove a virtual host mapping.
 * Returns 0 on success, -1 if hostname not found.
 */
int kernd_proxy_remove_vhost(const char *hostname);

/**
 * Look up the backend port for a given hostname.
 * Returns the port number on success, -1 if not found.
 */
int kernd_proxy_lookup(const char *hostname);

/**
 * Start the reverse proxy server on the given event loop.
 * Listens on http_port for incoming connections.
 * Returns 0 on success, -1 on failure.
 */
int kernd_proxy_start(uv_loop_t *loop, int http_port);

/**
 * Stop the reverse proxy server and free resources.
 */
void kernd_proxy_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* KERND_PROXY_H */
