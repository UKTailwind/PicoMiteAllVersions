/*
 * shared/net/mm_net_http.c - HTTP helpers for BASIC WEB surfaces.
 */

#include <stdio.h>
#include <string.h>

#include "shared/net/mm_net_http.h"

size_t mm_net_http_extract_path(const void *request, size_t request_len,
                                char *out, size_t out_len) {
    if (!out || out_len == 0) return 0;
    out[0] = 0;
    if (!request || request_len == 0) return 0;

    const unsigned char *buf = (const unsigned char *)request;
    size_t sp1 = request_len;
    size_t sp2 = request_len;
    for (size_t i = 0; i < request_len; i++) {
        if (buf[i] == ' ') {
            if (sp1 == request_len) sp1 = i;
            else {
                sp2 = i;
                break;
            }
        }
        if (buf[i] == '\r' || buf[i] == '\n') break;
    }
    if (sp1 == request_len || sp2 <= sp1 + 1) return 0;

    size_t n = sp2 - sp1 - 1;
    if (n > out_len - 1) n = out_len - 1;
    memcpy(out, buf + sp1 + 1, n);
    out[n] = 0;
    return n;
}

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int ascii_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a++);
        int cb = ascii_tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

const char *mm_net_http_mime_from_name(const char *name,
                                       const char *fallback) {
    if (!fallback) fallback = "application/octet-stream";
    if (!name) return fallback;

    const char *dot = strrchr(name, '.');
    if (!dot) return fallback;
    if (ascii_strcasecmp(dot, ".html") == 0 ||
        ascii_strcasecmp(dot, ".htm") == 0) return "text/html";
    if (ascii_strcasecmp(dot, ".css") == 0) return "text/css";
    if (ascii_strcasecmp(dot, ".js") == 0 ||
        ascii_strcasecmp(dot, ".mjs") == 0) return "application/javascript";
    if (ascii_strcasecmp(dot, ".json") == 0) return "application/json";
    if (ascii_strcasecmp(dot, ".txt") == 0 ||
        ascii_strcasecmp(dot, ".bas") == 0) return "text/plain";
    if (ascii_strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (ascii_strcasecmp(dot, ".png") == 0) return "image/png";
    if (ascii_strcasecmp(dot, ".jpg") == 0 ||
        ascii_strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (ascii_strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (ascii_strcasecmp(dot, ".ico") == 0) return "image/x-icon";
    if (ascii_strcasecmp(dot, ".wasm") == 0) return "application/wasm";
    return fallback;
}

const char *mm_net_http_status_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

int mm_net_http_format_status_body(char *out, size_t out_len, int status,
                                   const char *reason) {
    if (!out || out_len == 0) return -1;
    if (!reason) reason = mm_net_http_status_reason(status);
    int n = snprintf(out, out_len, "%d %s\r\n", status, reason);
    if (n < 0 || (size_t)n >= out_len) return -1;
    return n;
}

int mm_net_http_format_response_header(char *out, size_t out_len, int status,
                                       const char *reason,
                                       const char *server,
                                       const char *content_type,
                                       size_t content_len) {
    if (!out || out_len == 0) return -1;
    if (!reason) reason = mm_net_http_status_reason(status);
    if (!server) server = "MMBasic";
    if (!content_type || !*content_type) content_type = "application/octet-stream";
    int n = snprintf(out, out_len,
                     "HTTP/1.1 %d %s\r\n"
                     "Server: %s\r\n"
                     "Connection: close\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n\r\n",
                     status, reason, server, content_type,
                     (unsigned)content_len);
    if (n < 0 || (size_t)n >= out_len) return -1;
    return n;
}

int mm_net_http_send_response(int status, const char *reason,
                              const char *content_type,
                              const void *body, size_t body_len,
                              const char *server_name,
                              mm_net_http_send_fn send_fn, void *send_ctx) {
    if (!send_fn) return -2;
    char header[256];
    int header_len = mm_net_http_format_response_header(
        header, sizeof header, status, reason, server_name, content_type,
        body_len);
    if (header_len < 0) return -2;
    if (!send_fn(send_ctx, header, (size_t)header_len)) return -2;
    if (body_len && body && !send_fn(send_ctx, body, body_len)) return -2;
    return 0;
}
