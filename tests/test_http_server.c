/*
 * test_http_server.c - Integration tests for HTTP server
 *
 * Starts the server in a background thread, connects via plain TCP sockets,
 * sends HTTP requests, and validates responses.
 */

#include "kern.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <uv.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  [RUN] %s\n", #name); \
    name(); \
    tests_passed++; \
    printf("  [PASS] %s\n", #name); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "    ASSERT FAILED: %s (%s:%d)\n", \
                #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ_INT(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "    ASSERT_EQ_INT FAILED: %d != %d (%s:%d)\n", \
                (int)(a), (int)(b), __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* --- Test handler --- */

static kern_response_t *test_handler(kern_req_t *req) {
    const char *path = kern_req_path(req);

    if (strcmp(path, "/hello") == 0) {
        kern_response_t *res = kern_response_new(200);
        kern_response_header(res, "Content-Type", "text/plain");
        kern_response_body_str(res, "Hello, World!");
        return res;
    }

    if (strcmp(path, "/echo") == 0) {
        kern_str_t body = kern_body(req);
        kern_response_t *res = kern_response_new(200);
        kern_response_header(res, "Content-Type", "text/plain");
        if (body.data && body.len > 0) {
            kern_response_body(res, body.data, body.len);
        } else {
            kern_response_body_str(res, "");
        }
        return res;
    }

    return kern_404_response();
}

/* --- Server thread --- */

typedef struct {
    uv_loop_t *loop;
    kern_server_t *server;
    int port;
    volatile int ready;
} server_ctx_t;

static void *server_thread_fn(void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)arg;

    ctx->loop = uv_loop_new();
    ctx->server = kern_server_new(ctx->loop);
    kern_server_set_handler(ctx->server, test_handler);
    kern_server_set_timeout(ctx->server, 5000); /* 5s for tests */

    int rc = kern_server_listen(ctx->server, "127.0.0.1", 0);
    if (rc != 0) {
        fprintf(stderr, "Failed to listen: %s\n", uv_strerror(rc));
        ctx->ready = -1;
        return NULL;
    }

    ctx->port = kern_server_port(ctx->server);
    ctx->ready = 1;

    uv_run(ctx->loop, UV_RUN_DEFAULT);

    kern_server_free(ctx->server);
    uv_loop_delete(ctx->loop);
    return NULL;
}

/* --- Simple TCP client using plain sockets --- */

static int tcp_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Set a read timeout to avoid hanging */
    struct timeval tv = {5, 0}; /* 5 seconds */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return fd;
}

/* Read a full HTTP response from fd. Returns bytes read. */
static int read_response(int fd, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total < buf_size - 1) {
        ssize_t n = read(fd, buf + total, buf_size - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
        buf[total] = '\0';

        /* Check if we have a complete response */
        char *headers_end = strstr(buf, "\r\n\r\n");
        if (headers_end) {
            char *cl = strstr(buf, "Content-Length: ");
            if (cl) {
                size_t content_length = (size_t)atoi(cl + 16);
                size_t header_size = (size_t)(headers_end - buf) + 4;
                if (total >= header_size + content_length) {
                    break;
                }
            } else {
                /* No Content-Length, check for zero-length body (e.g., 304) */
                break;
            }
        }
    }
    return (int)total;
}

/* --- Global server state --- */

static server_ctx_t g_server;
static pthread_t g_server_thread;

static void start_server(void) {
    memset(&g_server, 0, sizeof(g_server));
    pthread_create(&g_server_thread, NULL, server_thread_fn, &g_server);

    /* Wait for server to be ready */
    int wait = 0;
    while (g_server.ready == 0 && wait < 3000) {
        usleep(1000);
        wait++;
    }
    ASSERT(g_server.ready == 1);
}

static void stop_server(void) {
    kern_server_stop(g_server.server);
    usleep(100000); /* 100ms for close callbacks */
    uv_stop(g_server.loop);
    pthread_join(g_server_thread, NULL);
}

/* --- Tests --- */

TEST(test_simple_get_response) {
    int fd = tcp_connect(g_server.port);
    ASSERT(fd >= 0);

    const char *req = "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    ssize_t sent = write(fd, req, strlen(req));
    ASSERT(sent == (ssize_t)strlen(req));

    char response[8192] = {0};
    int len = read_response(fd, response, sizeof(response));
    close(fd);

    ASSERT(len > 0);
    ASSERT(strstr(response, "HTTP/1.1 200 OK") != NULL);
    ASSERT(strstr(response, "Hello, World!") != NULL);
    ASSERT(strstr(response, "Content-Type: text/plain") != NULL);
}

TEST(test_404_response) {
    int fd = tcp_connect(g_server.port);
    ASSERT(fd >= 0);

    const char *req = "GET /nonexistent HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    write(fd, req, strlen(req));

    char response[8192] = {0};
    int len = read_response(fd, response, sizeof(response));
    close(fd);

    ASSERT(len > 0);
    ASSERT(strstr(response, "HTTP/1.1 404") != NULL);
    ASSERT(strstr(response, "404 Not Found") != NULL);
}

TEST(test_keepalive) {
    int fd = tcp_connect(g_server.port);
    ASSERT(fd >= 0);

    /* First request (keep-alive by default for HTTP/1.1) */
    const char *req1 = "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n";
    write(fd, req1, strlen(req1));

    char response[16384] = {0};
    int len = read_response(fd, response, sizeof(response));
    ASSERT(len > 0);
    ASSERT(strstr(response, "HTTP/1.1 200 OK") != NULL);
    ASSERT(strstr(response, "Hello, World!") != NULL);

    /* Second request on same connection */
    const char *req2 = "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    write(fd, req2, strlen(req2));

    char response2[8192] = {0};
    int len2 = read_response(fd, response2, sizeof(response2));
    close(fd);

    ASSERT(len2 > 0);
    ASSERT(strstr(response2, "HTTP/1.1 200 OK") != NULL);
    ASSERT(strstr(response2, "Hello, World!") != NULL);
}

TEST(test_post_echo) {
    int fd = tcp_connect(g_server.port);
    ASSERT(fd >= 0);

    const char *req =
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello";
    write(fd, req, strlen(req));

    char response[8192] = {0};
    int len = read_response(fd, response, sizeof(response));
    close(fd);

    ASSERT(len > 0);
    ASSERT(strstr(response, "HTTP/1.1 200 OK") != NULL);
    char *body_start = strstr(response, "\r\n\r\n");
    ASSERT(body_start != NULL);
    body_start += 4;
    ASSERT(strncmp(body_start, "hello", 5) == 0);
}

TEST(test_server_headers) {
    int fd = tcp_connect(g_server.port);
    ASSERT(fd >= 0);

    const char *req = "GET /hello HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    write(fd, req, strlen(req));

    char response[8192] = {0};
    int len = read_response(fd, response, sizeof(response));
    close(fd);

    ASSERT(len > 0);
    ASSERT(strstr(response, "Server: kern/0.1") != NULL);
    ASSERT(strstr(response, "Content-Length:") != NULL);
}

int main(void) {
    printf("test_http_server:\n");

    start_server();

    RUN_TEST(test_simple_get_response);
    RUN_TEST(test_404_response);
    RUN_TEST(test_keepalive);
    RUN_TEST(test_post_echo);
    RUN_TEST(test_server_headers);

    stop_server();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
