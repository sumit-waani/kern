/*
 * kern_http_server.c - TCP server using libuv
 *
 * Accepts TCP connections, reads data, feeds to the HTTP parser,
 * invokes the request handler when a complete request is received,
 * and writes the response back to the client.
 * Supports HTTP/1.1 keep-alive (multiple requests per connection).
 */

#include "kern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <uv.h>

/* Internal connection state */
typedef struct kern_conn {
    uv_tcp_t tcp;
    kern_server_t *server;
    kern_http_parser_t *parser;
    uv_timer_t timeout_timer;
    bool closing;
    int close_count;  /* Number of handles still being closed */
} kern_conn_t;

/* Write request context */
typedef struct {
    uv_write_t req;
    uv_buf_t buf;
    kern_conn_t *conn;
    bool close_after;
} write_ctx_t;

struct kern_server {
    uv_loop_t *loop;
    uv_tcp_t tcp;
    uv_async_t stop_async;
    kern_handler_fn handler;
    kern_router_t *router;
    uint64_t timeout_ms;
    bool running;
};

/* Forward declarations */
static void on_connection(uv_stream_t *server, int status);
static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void close_connection(kern_conn_t *conn);
static void on_close(uv_handle_t *handle);
static void on_timer_close(uv_handle_t *handle);
static void on_timeout(uv_timer_t *timer);
static void reset_timeout(kern_conn_t *conn);
static void on_stop_async(uv_async_t *handle);

kern_server_t *kern_server_new(uv_loop_t *loop) {
    if (!loop) return NULL;

    kern_server_t *server = calloc(1, sizeof(kern_server_t));
    if (!server) return NULL;

    server->loop = loop;
    server->handler = NULL;
    server->router = NULL;
    server->timeout_ms = 60000; /* 60 seconds default */
    server->running = false;

    return server;
}

void kern_server_set_handler(kern_server_t *server, kern_handler_fn handler) {
    if (!server) return;
    server->handler = handler;
}

void kern_server_set_router(kern_server_t *server, kern_router_t *router) {
    if (!server) return;
    server->router = router;
}

void kern_server_set_timeout(kern_server_t *server, uint64_t timeout_ms) {
    if (!server) return;
    server->timeout_ms = timeout_ms;
}

int kern_server_listen(kern_server_t *server, const char *host, int port) {
    if (!server || !host) return -1;

    int rc = uv_tcp_init(server->loop, &server->tcp);
    if (rc != 0) return rc;
    server->tcp.data = server;

    /* Initialize async handle for cross-thread stop signal */
    rc = uv_async_init(server->loop, &server->stop_async, on_stop_async);
    if (rc != 0) return rc;
    server->stop_async.data = server;

    struct sockaddr_in addr;
    rc = uv_ip4_addr(host, port, &addr);
    if (rc != 0) return rc;

    rc = uv_tcp_bind(&server->tcp, (const struct sockaddr *)&addr, 0);
    if (rc != 0) return rc;

    rc = uv_listen((uv_stream_t *)&server->tcp, 128, on_connection);
    if (rc != 0) return rc;

    server->running = true;
    return 0;
}

/* Walk callback to close all handles during shutdown */
static void close_walk_cb(uv_handle_t *handle, void *arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

static void on_stop_async(uv_async_t *handle) {
    kern_server_t *server = (kern_server_t *)handle->data;
    if (!server) return;
    server->running = false;

    /* Close all handles to allow the loop to exit */
    uv_walk(server->loop, close_walk_cb, NULL);
}

void kern_server_stop(kern_server_t *server) {
    if (!server) return;
    /* Signal the event loop from any thread */
    uv_async_send(&server->stop_async);
}

void kern_server_free(kern_server_t *server) {
    free(server);
}

int kern_server_port(kern_server_t *server) {
    if (!server) return -1;
    struct sockaddr_in addr;
    int namelen = sizeof(addr);
    int rc = uv_tcp_getsockname(&server->tcp, (struct sockaddr *)&addr, &namelen);
    if (rc != 0) return -1;
    return ntohs(addr.sin_port);
}

/* --- Internal callbacks --- */

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    (void)suggested_size;
    buf->base = malloc(4096);
    buf->len = buf->base ? 4096 : 0;
}

static void on_connection(uv_stream_t *server_stream, int status) {
    if (status < 0) return;

    kern_server_t *server = (kern_server_t *)server_stream->data;

    kern_conn_t *conn = calloc(1, sizeof(kern_conn_t));
    if (!conn) return;

    conn->server = server;
    conn->closing = false;

    int rc = uv_tcp_init(server->loop, &conn->tcp);
    if (rc != 0) {
        free(conn);
        return;
    }
    conn->tcp.data = conn;

    rc = uv_accept(server_stream, (uv_stream_t *)&conn->tcp);
    if (rc != 0) {
        uv_close((uv_handle_t *)&conn->tcp, on_close);
        return;
    }

    /* Initialize parser for this connection */
    conn->parser = kern_http_parser_new();
    if (!conn->parser) {
        uv_close((uv_handle_t *)&conn->tcp, on_close);
        return;
    }

    /* Initialize and start timeout timer */
    uv_timer_init(server->loop, &conn->timeout_timer);
    conn->timeout_timer.data = conn;
    uv_timer_start(&conn->timeout_timer, on_timeout, server->timeout_ms, 0);

    /* Start reading */
    uv_read_start((uv_stream_t *)&conn->tcp, alloc_buffer, on_read);
}

static void on_write_complete(uv_write_t *req, int status) {
    (void)status;
    write_ctx_t *ctx = (write_ctx_t *)req;
    free(ctx->buf.base);

    if (ctx->close_after) {
        close_connection(ctx->conn);
    }
    free(ctx);
}

/* Iterator callback to set route params on request */
static bool kern_server_set_params_iter(const char *key, void *value, void *userdata) {
    kern_req_t *req = (kern_req_t *)userdata;
    kern_req_set_param(req, key, (const char *)value);
    return true;
}

static void process_request(kern_conn_t *conn) {
    kern_server_t *server = conn->server;
    if (!server->handler && !server->router) {
        close_connection(conn);
        return;
    }

    /* Build request object - it takes ownership of the parser */
    kern_req_t *req = kern_req_new(conn->parser);
    conn->parser = NULL; /* parser is now owned by req */

    kern_response_t *res = NULL;
    if (req) {
        if (server->router) {
            /* Router-based dispatch */
            kern_dict_t *params = kern_dict_new_with_free(free);
            kern_route_result_t result = kern_router_match(
                server->router, kern_req_method(req), kern_req_path(req), params);

            if (result.status == KERN_ROUTE_OK && result.handler) {
                /* Set captured params on the request */
                /* We iterate params dict and set each on req */
                /* Since kern_dict doesn't expose raw iteration with values easily,
                   we use a helper approach - match_tree already set them in params dict */
                kern_dict_iter(params, kern_server_set_params_iter, req);
                kern_dict_free(params);

                /* Dispatch through middleware */
                res = kern_router_dispatch(server->router, req, result.handler);
            } else if (result.status == KERN_ROUTE_METHOD_NOT_ALLOWED) {
                kern_dict_free(params);
                /* Return 405 with Allow header */
                res = kern_response_new(405);
                kern_response_header(res, "Content-Type", "text/plain");
                kern_response_body_str(res, "405 Method Not Allowed");
            } else {
                kern_dict_free(params);
                /* 404 - try fallback handler */
                if (server->handler) {
                    res = server->handler(req);
                } else {
                    res = kern_404_response();
                }
            }
        } else {
            /* Simple handler mode */
            res = server->handler(req);
        }
    }

    if (!res) {
        res = kern_500_response("Handler returned NULL");
    }

    /* Determine if we should close after this response */
    bool should_close = false;
    const char *conn_hdr = req ? kern_header(req, "connection") : NULL;
    if (conn_hdr && strcasecmp(conn_hdr, "close") == 0) {
        should_close = true;
    }

    if (!should_close) {
        kern_response_header(res, "Connection", "keep-alive");
    } else {
        kern_response_header(res, "Connection", "close");
    }

    /* Serialize and send */
    kern_buf_t *wire = kern_response_serialize(res);
    kern_response_free(res);
    kern_req_free(req);

    if (!wire) {
        close_connection(conn);
        return;
    }

    size_t wire_len = kern_buf_len(wire);
    char *wire_data = malloc(wire_len);
    if (!wire_data) {
        kern_buf_free(wire);
        close_connection(conn);
        return;
    }
    memcpy(wire_data, kern_buf_data(wire), wire_len);
    kern_buf_free(wire);

    write_ctx_t *wctx = malloc(sizeof(write_ctx_t));
    if (!wctx) {
        free(wire_data);
        close_connection(conn);
        return;
    }
    wctx->buf = uv_buf_init(wire_data, (unsigned int)wire_len);
    wctx->conn = conn;
    wctx->close_after = should_close;

    int rc = uv_write(&wctx->req, (uv_stream_t *)&conn->tcp, &wctx->buf, 1, on_write_complete);
    if (rc != 0) {
        free(wire_data);
        free(wctx);
        close_connection(conn);
        return;
    }

    /* If keep-alive, prepare a new parser for the next request */
    if (!should_close) {
        conn->parser = kern_http_parser_new();
        if (!conn->parser) {
            close_connection(conn);
            return;
        }
        reset_timeout(conn);
    }
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    kern_conn_t *conn = (kern_conn_t *)stream->data;

    if (nread < 0) {
        free(buf->base);
        close_connection(conn);
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    /* Feed data to parser */
    int result = kern_http_parser_feed(conn->parser, buf->base, (size_t)nread);
    free(buf->base);

    if (result == KERN_HTTP_PARSE_ERROR) {
        /* Send 400 Bad Request and close */
        kern_response_t *res = kern_response_new(400);
        kern_response_header(res, "Content-Type", "text/plain");
        kern_response_header(res, "Connection", "close");
        kern_response_body_str(res, "400 Bad Request");

        kern_buf_t *wire = kern_response_serialize(res);
        kern_response_free(res);

        if (wire) {
            size_t wire_len = kern_buf_len(wire);
            char *wire_data = malloc(wire_len);
            if (wire_data) {
                memcpy(wire_data, kern_buf_data(wire), wire_len);
                write_ctx_t *wctx = malloc(sizeof(write_ctx_t));
                if (wctx) {
                    wctx->buf = uv_buf_init(wire_data, (unsigned int)wire_len);
                    wctx->conn = conn;
                    wctx->close_after = true;
                    if (uv_write(&wctx->req, (uv_stream_t *)&conn->tcp,
                                 &wctx->buf, 1, on_write_complete) != 0) {
                        free(wire_data);
                        free(wctx);
                        close_connection(conn);
                    }
                } else {
                    free(wire_data);
                    close_connection(conn);
                }
            } else {
                close_connection(conn);
            }
            kern_buf_free(wire);
        } else {
            close_connection(conn);
        }
        return;
    }

    if (result == KERN_HTTP_PARSE_DONE) {
        /* Stop reading during request processing */
        uv_read_stop(stream);
        process_request(conn);
        /* If keep-alive, re-start reading */
        if (!conn->closing && conn->parser) {
            uv_read_start((uv_stream_t *)&conn->tcp, alloc_buffer, on_read);
        }
    }
    /* KERN_HTTP_PARSE_NEED_MORE: just wait for more data */
}

static void on_timeout(uv_timer_t *timer) {
    kern_conn_t *conn = (kern_conn_t *)timer->data;
    close_connection(conn);
}

static void reset_timeout(kern_conn_t *conn) {
    uv_timer_stop(&conn->timeout_timer);
    uv_timer_start(&conn->timeout_timer, on_timeout, conn->server->timeout_ms, 0);
}

static void on_timer_close(uv_handle_t *handle) {
    kern_conn_t *conn = (kern_conn_t *)handle->data;
    if (!conn) return;
    conn->close_count--;
    if (conn->close_count <= 0) {
        if (conn->parser) {
            kern_http_parser_free(conn->parser);
            conn->parser = NULL;
        }
        free(conn);
    }
}

static void on_close(uv_handle_t *handle) {
    kern_conn_t *conn = (kern_conn_t *)handle->data;
    if (!conn) return;
    conn->close_count--;
    if (conn->close_count <= 0) {
        if (conn->parser) {
            kern_http_parser_free(conn->parser);
            conn->parser = NULL;
        }
        free(conn);
    }
}

static void close_connection(kern_conn_t *conn) {
    if (!conn || conn->closing) return;
    conn->closing = true;
    conn->close_count = 0;

    /* Stop and close the timer */
    uv_timer_stop(&conn->timeout_timer);
    if (!uv_is_closing((uv_handle_t *)&conn->timeout_timer)) {
        conn->close_count++;
        uv_close((uv_handle_t *)&conn->timeout_timer, on_timer_close);
    }

    /* Close the TCP handle */
    if (!uv_is_closing((uv_handle_t *)&conn->tcp)) {
        conn->close_count++;
        uv_close((uv_handle_t *)&conn->tcp, on_close);
    }

    /* If nothing to close (shouldn't happen), free immediately */
    if (conn->close_count == 0) {
        if (conn->parser) {
            kern_http_parser_free(conn->parser);
            conn->parser = NULL;
        }
        free(conn);
    }
}
