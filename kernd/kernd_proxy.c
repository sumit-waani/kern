/*
 * kernd_proxy.c - Reverse proxy with host-header routing
 *
 * Uses libuv TCP to accept connections, parses the Host header,
 * looks up the backend port, and forwards traffic.
 */

#include "kernd_proxy.h"
#include "kernd_log.h"
#include "kern.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Maximum request buffer size for initial parsing */
#define PROXY_BUF_SIZE 8192

/* Vhost table: maps hostname -> port (stored as intptr_t in dict value) */
static kern_dict_t *vhost_table = NULL;
static uv_tcp_t proxy_server;
static int proxy_running = 0;

typedef struct {
    uv_tcp_t client;
    uv_tcp_t backend;
    uv_connect_t connect_req;
    uv_write_t write_req;
    char request_buf[PROXY_BUF_SIZE];
    size_t request_len;
    int backend_port;
    int backend_connected;
    char client_ip[64];
} proxy_conn_t;

/* Forward declarations */
static void on_proxy_connection(uv_stream_t *server, int status);
static void on_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void on_backend_connect(uv_connect_t *req, int status);
static void on_backend_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void on_backend_write(uv_write_t *req, int status);
static void on_client_write(uv_write_t *req, int status);
static void proxy_conn_close(proxy_conn_t *conn);
static void send_error_response(proxy_conn_t *conn, int code, const char *message);
static int extract_host(const char *buf, size_t len, char *host, size_t host_size);

int kernd_proxy_init(void) {
    if (vhost_table) {
        kern_dict_free(vhost_table);
    }
    vhost_table = kern_dict_new();
    if (!vhost_table) {
        return -1;
    }
    return 0;
}

int kernd_proxy_add_vhost(const char *hostname, int backend_port) {
    if (!hostname || !vhost_table || backend_port <= 0) {
        return -1;
    }

    /* Store port as a heap-allocated int */
    int *port_ptr = malloc(sizeof(int));
    if (!port_ptr) {
        return -1;
    }
    *port_ptr = backend_port;

    /* Free existing entry if present */
    void *existing = kern_dict_get(vhost_table, hostname);
    if (existing) {
        free(existing);
    }

    kern_dict_set(vhost_table, hostname, port_ptr);
    kernd_log_info("proxy: added vhost %s -> port %d", hostname, backend_port);
    return 0;
}

int kernd_proxy_remove_vhost(const char *hostname) {
    if (!hostname || !vhost_table) {
        return -1;
    }

    void *existing = kern_dict_get(vhost_table, hostname);
    if (!existing) {
        return -1;
    }

    free(existing);
    kern_dict_del(vhost_table, hostname);
    kernd_log_info("proxy: removed vhost %s", hostname);
    return 0;
}

int kernd_proxy_lookup(const char *hostname) {
    if (!hostname || !vhost_table) {
        return -1;
    }

    void *val = kern_dict_get(vhost_table, hostname);
    if (!val) {
        return -1;
    }

    return *(int *)val;
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    (void)suggested_size;
    buf->base = malloc(PROXY_BUF_SIZE);
    buf->len = buf->base ? PROXY_BUF_SIZE : 0;
}

static void on_close(uv_handle_t *handle) {
    (void)handle;
    /* Freed by proxy_conn_close */
}

static void proxy_conn_close(proxy_conn_t *conn) {
    if (!conn) return;

    if (!uv_is_closing((uv_handle_t *)&conn->client)) {
        uv_close((uv_handle_t *)&conn->client, on_close);
    }
    if (conn->backend_connected && !uv_is_closing((uv_handle_t *)&conn->backend)) {
        uv_close((uv_handle_t *)&conn->backend, on_close);
    }

    /* Note: conn is freed when all handles close, but since we embed handles
     * in the struct, we free after a brief delay. For simplicity in this MVP,
     * we let the event loop handle the close callbacks first. We rely on
     * the fact that the handles are embedded (not heap-allocated separately). */
}

static void send_error_response(proxy_conn_t *conn, int code, const char *message) {
    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, message, strlen(message), message);

    uv_buf_t buf = uv_buf_init(response, (unsigned int)len);
    /* Allocate a write request on heap since conn may be freed */
    uv_write_t *wreq = malloc(sizeof(uv_write_t));
    if (!wreq) {
        proxy_conn_close(conn);
        return;
    }
    wreq->data = conn;
    uv_write(wreq, (uv_stream_t *)&conn->client, &buf, 1, on_client_write);
}

static void on_client_write(uv_write_t *req, int status) {
    (void)status;
    proxy_conn_t *conn = (proxy_conn_t *)req->data;
    free(req);
    proxy_conn_close(conn);
}

static int extract_host(const char *buf, size_t len, char *host, size_t host_size) {
    /* Simple HTTP Host header extraction */
    const char *search = "Host:";
    const char *pos = NULL;

    for (size_t i = 0; i + 5 < len; i++) {
        if ((buf[i] == 'H' || buf[i] == 'h') &&
            (buf[i + 1] == 'o' || buf[i + 1] == 'O') &&
            (buf[i + 2] == 's' || buf[i + 2] == 'S') &&
            (buf[i + 3] == 't' || buf[i + 3] == 'T') &&
            buf[i + 4] == ':') {
            pos = &buf[i + 5];
            break;
        }
    }

    (void)search;

    if (!pos) {
        return -1;
    }

    /* Skip whitespace */
    while (pos < buf + len && *pos == ' ') {
        pos++;
    }

    /* Copy until \r, \n, or : (strip port) */
    size_t j = 0;
    while (pos < buf + len && *pos != '\r' && *pos != '\n' && *pos != ':' && j < host_size - 1) {
        host[j++] = *pos++;
    }
    host[j] = '\0';

    return j > 0 ? 0 : -1;
}

static void on_backend_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    proxy_conn_t *conn = (proxy_conn_t *)stream->data;

    if (nread <= 0) {
        if (buf->base) free(buf->base);
        proxy_conn_close(conn);
        return;
    }

    /* Forward backend response to client */
    uv_buf_t wbuf = uv_buf_init(buf->base, (unsigned int)nread);
    uv_write_t *wreq = malloc(sizeof(uv_write_t));
    if (!wreq) {
        free(buf->base);
        proxy_conn_close(conn);
        return;
    }
    wreq->data = buf->base;  /* Store so we can free in callback */
    uv_write(wreq, (uv_stream_t *)&conn->client, &wbuf, 1, on_backend_write);
}

static void on_backend_write(uv_write_t *req, int status) {
    free(req->data);  /* Free the buffer */
    free(req);
    (void)status;
}

static void on_request_forwarded(uv_write_t *req, int status) {
    proxy_conn_t *conn = (proxy_conn_t *)req->data;
    (void)status;

    /* Start reading response from backend */
    conn->backend.data = conn;
    uv_read_start((uv_stream_t *)&conn->backend, alloc_buffer, on_backend_read);
}

static void on_backend_connect(uv_connect_t *req, int status) {
    proxy_conn_t *conn = (proxy_conn_t *)req->data;

    if (status < 0) {
        /* Backend unreachable */
        kernd_log_warn("proxy: backend connect failed on port %d: %s",
                       conn->backend_port, uv_strerror(status));
        send_error_response(conn, 502, "Bad Gateway");
        return;
    }

    conn->backend_connected = 1;

    /* Inject X-Forwarded-For header into the request */
    char modified[PROXY_BUF_SIZE + 256];
    size_t mod_len = 0;

    /* Find end of first line (\r\n) to inject header after it */
    const char *first_crlf = strstr(conn->request_buf, "\r\n");
    if (first_crlf) {
        size_t first_line_len = (size_t)(first_crlf - conn->request_buf) + 2;
        memcpy(modified, conn->request_buf, first_line_len);
        mod_len = first_line_len;

        int hdr_len = snprintf(modified + mod_len, sizeof(modified) - mod_len,
                               "X-Forwarded-For: %s\r\n", conn->client_ip);
        mod_len += (size_t)hdr_len;

        size_t remaining = conn->request_len - first_line_len;
        if (mod_len + remaining < sizeof(modified)) {
            memcpy(modified + mod_len, conn->request_buf + first_line_len, remaining);
            mod_len += remaining;
        }
    } else {
        memcpy(modified, conn->request_buf, conn->request_len);
        mod_len = conn->request_len;
    }

    /* Forward request to backend */
    uv_buf_t buf = uv_buf_init(modified, (unsigned int)mod_len);
    conn->write_req.data = conn;
    uv_write(&conn->write_req, (uv_stream_t *)&conn->backend, &buf, 1, on_request_forwarded);
}

static void on_client_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    proxy_conn_t *conn = (proxy_conn_t *)stream->data;

    if (nread <= 0) {
        if (buf->base) free(buf->base);
        proxy_conn_close(conn);
        return;
    }

    /* Stop reading from client while we process */
    uv_read_stop(stream);

    /* Buffer the request */
    size_t copy_len = (size_t)nread;
    if (copy_len > sizeof(conn->request_buf) - conn->request_len) {
        copy_len = sizeof(conn->request_buf) - conn->request_len;
    }
    memcpy(conn->request_buf + conn->request_len, buf->base, copy_len);
    conn->request_len += copy_len;
    free(buf->base);

    /* Extract Host header */
    char host[256];
    if (extract_host(conn->request_buf, conn->request_len, host, sizeof(host)) != 0) {
        send_error_response(conn, 400, "Bad Request");
        return;
    }

    /* Look up backend port */
    int port = kernd_proxy_lookup(host);
    if (port < 0) {
        kernd_log_warn("proxy: no vhost for host '%s'", host);
        send_error_response(conn, 404, "Not Found");
        return;
    }

    conn->backend_port = port;

    /* Connect to backend */
    uv_tcp_init(stream->loop, &conn->backend);
    conn->connect_req.data = conn;

    struct sockaddr_in backend_addr;
    uv_ip4_addr("127.0.0.1", port, &backend_addr);

    int rc = uv_tcp_connect(&conn->connect_req, &conn->backend,
                            (const struct sockaddr *)&backend_addr,
                            on_backend_connect);
    if (rc != 0) {
        kernd_log_error("proxy: uv_tcp_connect failed: %s", uv_strerror(rc));
        send_error_response(conn, 502, "Bad Gateway");
    }
}

static void on_proxy_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        kernd_log_error("proxy: new connection error: %s", uv_strerror(status));
        return;
    }

    proxy_conn_t *conn = calloc(1, sizeof(proxy_conn_t));
    if (!conn) {
        kernd_log_error("proxy: failed to allocate connection");
        return;
    }

    uv_tcp_init(server->loop, &conn->client);
    conn->client.data = conn;

    if (uv_accept(server, (uv_stream_t *)&conn->client) != 0) {
        proxy_conn_close(conn);
        free(conn);
        return;
    }

    /* Get client IP for X-Forwarded-For */
    struct sockaddr_storage addr;
    int namelen = sizeof(addr);
    if (uv_tcp_getpeername(&conn->client, (struct sockaddr *)&addr, &namelen) == 0) {
        if (addr.ss_family == AF_INET) {
            uv_ip4_name((struct sockaddr_in *)&addr, conn->client_ip, sizeof(conn->client_ip));
        } else {
            snprintf(conn->client_ip, sizeof(conn->client_ip), "unknown");
        }
    } else {
        snprintf(conn->client_ip, sizeof(conn->client_ip), "unknown");
    }

    uv_read_start((uv_stream_t *)&conn->client, alloc_buffer, on_client_read);
}

int kernd_proxy_start(uv_loop_t *loop, int http_port) {
    if (!loop || !vhost_table) {
        return -1;
    }

    uv_tcp_init(loop, &proxy_server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", http_port, &addr);

    int rc = uv_tcp_bind(&proxy_server, (const struct sockaddr *)&addr, 0);
    if (rc != 0) {
        kernd_log_error("proxy: bind failed on port %d: %s", http_port, uv_strerror(rc));
        return -1;
    }

    rc = uv_listen((uv_stream_t *)&proxy_server, 128, on_proxy_connection);
    if (rc != 0) {
        kernd_log_error("proxy: listen failed: %s", uv_strerror(rc));
        return -1;
    }

    proxy_running = 1;
    kernd_log_info("proxy: listening on port %d", http_port);
    return 0;
}

static bool free_vhost_entry(const char *key, void *value, void *userdata) {
    (void)key;
    (void)userdata;
    free(value);
    return true;
}

void kernd_proxy_stop(void) {
    if (proxy_running) {
        uv_close((uv_handle_t *)&proxy_server, NULL);
        proxy_running = 0;
        kernd_log_info("proxy: stopped");
    }

    if (vhost_table) {
        /* Free all stored port pointers */
        kern_dict_iter(vhost_table, free_vhost_entry, NULL);
        kern_dict_free(vhost_table);
        vhost_table = NULL;
    }
}
