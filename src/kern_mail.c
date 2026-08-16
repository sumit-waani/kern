/*
 * kern_mail.c - Email composition, SMTP client (plain TCP), and log driver.
 */

#include "kern.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ============================================================
 * Internal Types
 * ============================================================ */

struct kern_mail {
    char *to;
    char *from;
    char *subject;
    char *body;
    kern_dict_t *headers;
};

/* ============================================================
 * Base64 Encoding (internal)
 * ============================================================ */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }

    /* Padding */
    size_t mod = len % 3;
    if (mod == 1) {
        out[out_len - 1] = '=';
        out[out_len - 2] = '=';
    } else if (mod == 2) {
        out[out_len - 1] = '=';
    }

    out[out_len] = '\0';
    return out;
}

/* ============================================================
 * Mail Builder API
 * ============================================================ */

kern_mail_t *kern_mail_new(void) {
    kern_mail_t *mail = calloc(1, sizeof(kern_mail_t));
    if (!mail) return NULL;

    mail->headers = kern_dict_new_with_free(free);
    if (!mail->headers) {
        free(mail);
        return NULL;
    }

    return mail;
}

void kern_mail_to(kern_mail_t *mail, const char *addr) {
    if (!mail || !addr) return;
    free(mail->to);
    mail->to = strdup(addr);
}

void kern_mail_from(kern_mail_t *mail, const char *addr) {
    if (!mail || !addr) return;
    free(mail->from);
    mail->from = strdup(addr);
}

void kern_mail_subject(kern_mail_t *mail, const char *subj) {
    if (!mail || !subj) return;
    free(mail->subject);
    mail->subject = strdup(subj);
}

void kern_mail_body(kern_mail_t *mail, const char *body) {
    if (!mail || !body) return;
    free(mail->body);
    mail->body = strdup(body);
}

void kern_mail_header(kern_mail_t *mail, const char *key, const char *val) {
    if (!mail || !key || !val) return;
    kern_dict_set(mail->headers, key, strdup(val));
}

void kern_mail_free(kern_mail_t *mail) {
    if (!mail) return;
    free(mail->to);
    free(mail->from);
    free(mail->subject);
    free(mail->body);
    kern_dict_free(mail->headers);
    free(mail);
}

const char *kern_mail_get_to(const kern_mail_t *mail) {
    if (!mail) return NULL;
    return mail->to;
}

const char *kern_mail_get_from(const kern_mail_t *mail) {
    if (!mail) return NULL;
    return mail->from;
}

const char *kern_mail_get_subject(const kern_mail_t *mail) {
    if (!mail) return NULL;
    return mail->subject;
}

const char *kern_mail_get_body(const kern_mail_t *mail) {
    if (!mail) return NULL;
    return mail->body;
}

/* ============================================================
 * Log Driver
 * ============================================================ */

static bool log_header_printer(const char *key, void *value, void *userdata) {
    (void)userdata;
    fprintf(stderr, "  %s: %s\n", key, (const char *)value);
    return true;
}

int kern_mail_send_log(kern_mail_t *mail) {
    if (!mail) return -1;

    fprintf(stderr, "--- [MAIL LOG] ---\n");
    fprintf(stderr, "  To: %s\n", mail->to ? mail->to : "(none)");
    fprintf(stderr, "  From: %s\n", mail->from ? mail->from : "(none)");
    fprintf(stderr, "  Subject: %s\n", mail->subject ? mail->subject : "(none)");
    kern_dict_iter(mail->headers, log_header_printer, NULL);
    fprintf(stderr, "  Body:\n%s\n", mail->body ? mail->body : "(empty)");
    fprintf(stderr, "--- [/MAIL LOG] ---\n");

    return 0;
}

/* ============================================================
 * SMTP Client (plain TCP, no TLS)
 * ============================================================ */

static int smtp_recv_line(int fd, char *buf, size_t buf_size) {
    size_t pos = 0;
    while (pos < buf_size - 1) {
        ssize_t n = recv(fd, buf + pos, 1, 0);
        if (n <= 0) return -1;
        if (buf[pos] == '\n') {
            pos++;
            break;
        }
        pos++;
    }
    buf[pos] = '\0';
    return (int)pos;
}

static int smtp_expect(int fd, int expected_code) {
    char buf[512];
    /* Read lines until we get a final response (no continuation) */
    while (1) {
        if (smtp_recv_line(fd, buf, sizeof(buf)) < 0) return -1;
        /* Final response line has space at position 3 */
        if (strlen(buf) >= 4 && buf[3] == ' ') {
            int code = atoi(buf);
            return (code == expected_code) ? 0 : -1;
        }
        /* Continuation line (buf[3] == '-'), keep reading */
        if (strlen(buf) >= 4 && buf[3] == '-') continue;
        /* Unexpected format */
        return -1;
    }
}

static int smtp_send_cmd(int fd, const char *cmd) {
    size_t len = strlen(cmd);
    ssize_t sent = send(fd, cmd, len, 0);
    return (sent == (ssize_t)len) ? 0 : -1;
}

int kern_smtp_send(kern_smtp_config_t *cfg, kern_mail_t *mail) {
    if (!cfg || !mail) return -1;
    if (!cfg->host || !mail->to || !mail->from) return -1;

    /* Resolve host */
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", cfg->port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(cfg->host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    /* Read greeting */
    if (smtp_expect(fd, 220) != 0) goto fail;

    /* EHLO */
    if (smtp_send_cmd(fd, "EHLO localhost\r\n") != 0) goto fail;
    if (smtp_expect(fd, 250) != 0) goto fail;

    /* AUTH PLAIN if credentials provided */
    if (cfg->username && cfg->password) {
        /*
         * WARNING: AUTH PLAIN sends credentials in base64 (trivially
         * reversible) over a plaintext TCP connection. There is NO TLS
         * support in this minimal SMTP client. Do NOT use real credentials
         * unless connecting to localhost or a trusted network. Consider
         * using an external MTA with TLS for production deployments.
         */
        /* Emit a runtime warning if AUTH is used on a non-localhost connection */
        if (strcmp(cfg->host, "127.0.0.1") != 0 &&
            strcmp(cfg->host, "localhost") != 0 &&
            strcmp(cfg->host, "::1") != 0) {
            fprintf(stderr, "[kern] warning: SMTP AUTH over plaintext connection (no TLS)\n");
        }

        size_t ulen = strlen(cfg->username);
        size_t plen = strlen(cfg->password);
        /* AUTH PLAIN format: \0username\0password */
        size_t raw_len = 1 + ulen + 1 + plen;
        unsigned char *raw = malloc(raw_len);
        if (!raw) goto fail;
        raw[0] = '\0';
        memcpy(raw + 1, cfg->username, ulen);
        raw[1 + ulen] = '\0';
        memcpy(raw + 2 + ulen, cfg->password, plen);

        char *encoded = base64_encode(raw, raw_len);
        free(raw);
        if (!encoded) goto fail;

        kern_buf_t *cmd = kern_buf_new(128);
        if (!cmd) { free(encoded); goto fail; }
        kern_buf_writef(cmd, "AUTH PLAIN %s\r\n", encoded);
        free(encoded);

        int rc = smtp_send_cmd(fd, kern_buf_data(cmd));
        kern_buf_free(cmd);
        if (rc != 0) goto fail;
        if (smtp_expect(fd, 235) != 0) goto fail;
    }

    /* MAIL FROM */
    {
        kern_buf_t *cmd = kern_buf_new(128);
        if (!cmd) goto fail;
        kern_buf_writef(cmd, "MAIL FROM:<%s>\r\n", mail->from);
        int rc = smtp_send_cmd(fd, kern_buf_data(cmd));
        kern_buf_free(cmd);
        if (rc != 0) goto fail;
        if (smtp_expect(fd, 250) != 0) goto fail;
    }

    /* RCPT TO */
    {
        kern_buf_t *cmd = kern_buf_new(128);
        if (!cmd) goto fail;
        kern_buf_writef(cmd, "RCPT TO:<%s>\r\n", mail->to);
        int rc = smtp_send_cmd(fd, kern_buf_data(cmd));
        kern_buf_free(cmd);
        if (rc != 0) goto fail;
        if (smtp_expect(fd, 250) != 0) goto fail;
    }

    /* DATA */
    if (smtp_send_cmd(fd, "DATA\r\n") != 0) goto fail;
    if (smtp_expect(fd, 354) != 0) goto fail;

    /* Compose message body */
    {
        kern_buf_t *msg = kern_buf_new(1024);
        if (!msg) goto fail;

        kern_buf_writef(msg, "From: %s\r\n", mail->from);
        kern_buf_writef(msg, "To: %s\r\n", mail->to);
        if (mail->subject) {
            kern_buf_writef(msg, "Subject: %s\r\n", mail->subject);
        }
        kern_buf_writes(msg, "MIME-Version: 1.0\r\n");
        kern_buf_writes(msg, "Content-Type: text/plain; charset=utf-8\r\n");
        kern_buf_writes(msg, "\r\n");

        /* Body with dot-stuffing */
        if (mail->body) {
            const char *p = mail->body;
            bool at_line_start = true;
            while (*p) {
                if (at_line_start && *p == '.') {
                    kern_buf_writes(msg, "..");
                    p++;
                    at_line_start = false;
                } else if (*p == '\n') {
                    kern_buf_writes(msg, "\r\n");
                    p++;
                    at_line_start = true;
                } else if (*p == '\r' && *(p + 1) == '\n') {
                    kern_buf_writes(msg, "\r\n");
                    p += 2;
                    at_line_start = true;
                } else {
                    kern_buf_write(msg, p, 1);
                    p++;
                    at_line_start = false;
                }
            }
        }

        kern_buf_writes(msg, "\r\n.\r\n");

        int rc = smtp_send_cmd(fd, kern_buf_data(msg));
        kern_buf_free(msg);
        if (rc != 0) goto fail;
        if (smtp_expect(fd, 250) != 0) goto fail;
    }

    /* QUIT */
    smtp_send_cmd(fd, "QUIT\r\n");
    /* Don't require specific response code for QUIT */

    close(fd);
    return 0;

fail:
    close(fd);
    return -1;
}

/* ============================================================
 * Unified Send
 * ============================================================ */

int kern_mail_send(kern_mail_t *mail, kern_smtp_config_t *cfg) {
    if (!mail) return -1;
    if (!cfg) {
        return kern_mail_send_log(mail);
    }
    return kern_smtp_send(cfg, mail);
}
