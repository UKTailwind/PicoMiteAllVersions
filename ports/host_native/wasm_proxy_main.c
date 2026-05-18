#include "vendor/mongoose.h"
#include "wasm_proxy_http.h"
#include "wasm_proxy_net.h"
#include "wasm_proxy_protocol.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DEFAULT_BIND "127.0.0.1"
#define DEFAULT_PORT 8000
#define DEFAULT_WEB_ROOT "ports/host_wasm/web"

static const char *k_extra_headers =
    "Cross-Origin-Opener-Policy: same-origin\r\n"
    "Cross-Origin-Embedder-Policy: require-corp\r\n"
    "Cache-Control: no-store\r\n";

static const char *k_text_headers =
    "Content-Type: text/plain\r\n"
    "Cross-Origin-Opener-Policy: same-origin\r\n"
    "Cross-Origin-Embedder-Policy: require-corp\r\n"
    "Cache-Control: no-store\r\n";

typedef struct wasm_proxy_server {
    struct mg_mgr mgr;
    char listen_url[160];
    char bind_addr[96];
    char web_root[1024];
    int port;
    bool loopback_only;
    bool allow_public_bind;
    volatile sig_atomic_t *running;
} wasm_proxy_server_t;

static volatile sig_atomic_t g_running = 1;

static void on_signal(int signo) {
    (void)signo;
    g_running = 0;
}

static bool is_loopback_bind(const char *bind_addr) {
    if (!bind_addr || !*bind_addr) return false;
    return strcmp(bind_addr, "127.0.0.1") == 0 ||
           strncmp(bind_addr, "127.", 4) == 0 ||
           strcmp(bind_addr, "localhost") == 0 ||
           strcmp(bind_addr, "::1") == 0 ||
           strcmp(bind_addr, "[::1]") == 0;
}

static void usage(FILE *out, const char *argv0) {
    fprintf(out,
            "Usage: %s [--bind 127.0.0.1] [--port 8000] "
            "[--web-root ports/host_wasm/web] [--allow-public-bind]\n",
            argv0 ? argv0 : "wasm_network_proxy");
}

static int parse_args(int argc, char **argv, wasm_proxy_server_t *s) {
    snprintf(s->bind_addr, sizeof(s->bind_addr), "%s", DEFAULT_BIND);
    s->port = DEFAULT_PORT;
    snprintf(s->web_root, sizeof(s->web_root), "%s", DEFAULT_WEB_ROOT);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            snprintf(s->bind_addr, sizeof(s->bind_addr), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char *end = NULL;
            long port = strtol(argv[++i], &end, 10);
            if (!end || *end || port < 1 || port > 65535) {
                fprintf(stderr, "Invalid --port value\n");
                return -1;
            }
            s->port = (int)port;
        } else if (strcmp(argv[i], "--web-root") == 0 && i + 1 < argc) {
            snprintf(s->web_root, sizeof(s->web_root), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--allow-public-bind") == 0) {
            s->allow_public_bind = true;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(stdout, argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return -1;
        }
    }

    s->loopback_only = is_loopback_bind(s->bind_addr);
    if (!s->loopback_only && !s->allow_public_bind) {
        fprintf(stderr,
                "Refusing non-loopback bind '%s'. Use --allow-public-bind "
                "to expose the proxy beyond loopback.\n",
                s->bind_addr);
        return -1;
    }
    snprintf(s->listen_url, sizeof(s->listen_url), "http://%s:%d",
             s->bind_addr, s->port);
    return 0;
}

static int mg_str_to_cstr(struct mg_str s, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0 || s.len >= dst_len) return -1;
    memcpy(dst, s.buf, s.len);
    dst[s.len] = '\0';
    return 0;
}

static int parse_authority(const char *authority, char *host, size_t host_len,
                           int *port) {
    if (!authority || !*authority || !host || host_len == 0 || !port)
        return -1;
    *port = 0;
    host[0] = '\0';

    const char *host_start = authority;
    const char *host_end = authority + strlen(authority);
    const char *port_start = NULL;
    if (*host_start == '[') {
        host_start++;
        const char *close = strchr(host_start, ']');
        if (!close) return -1;
        host_end = close;
        if (close[1] == ':') port_start = close + 2;
        else if (close[1] != '\0') return -1;
    } else {
        const char *colon = strrchr(authority, ':');
        if (colon && strchr(colon + 1, ':') == NULL) {
            host_end = colon;
            port_start = colon + 1;
        }
    }

    size_t n = (size_t)(host_end - host_start);
    if (n == 0 || n >= host_len) return -1;
    memcpy(host, host_start, n);
    host[n] = '\0';

    if (port_start && *port_start) {
        char *end = NULL;
        long parsed = strtol(port_start, &end, 10);
        if (!end || *end || parsed < 1 || parsed > 65535) return -1;
        *port = (int)parsed;
    }
    return 0;
}

static bool is_loopback_host(const char *host) {
    if (!host || !*host) return false;
    return strcmp(host, "localhost") == 0 ||
           strcmp(host, "::1") == 0 ||
           strcmp(host, "127.0.0.1") == 0 ||
           strncmp(host, "127.", 4) == 0;
}

static bool parse_origin(const char *origin, char *host, size_t host_len,
                         int *port) {
    const char *scheme_end = strstr(origin, "://");
    if (!scheme_end) return false;
    bool is_http = strncasecmp(origin, "http", 4) == 0 &&
                   scheme_end == origin + 4;
    bool is_https = strncasecmp(origin, "https", 5) == 0 &&
                    scheme_end == origin + 5;
    if (!is_http && !is_https) return false;

    const char *authority = scheme_end + 3;
    char authority_buf[256];
    const char *end = strchr(authority, '/');
    size_t len = end ? (size_t)(end - authority) : strlen(authority);
    if (len == 0 || len >= sizeof(authority_buf)) return false;
    memcpy(authority_buf, authority, len);
    authority_buf[len] = '\0';
    if (parse_authority(authority_buf, host, host_len, port) != 0)
        return false;
    if (*port == 0) *port = is_https ? 443 : 80;
    return true;
}

static bool validate_ws_request(const wasm_proxy_server_t *s,
                                struct mg_http_message *hm,
                                char *reason, size_t reason_len) {
    struct mg_str *host_hdr = mg_http_get_header(hm, "Host");
    if (!host_hdr || host_hdr->len == 0) {
        snprintf(reason, reason_len, "missing Host");
        return false;
    }

    char host_authority[256];
    char host_name[128];
    int host_port = 0;
    if (mg_str_to_cstr(*host_hdr, host_authority,
                       sizeof(host_authority)) != 0 ||
        parse_authority(host_authority, host_name, sizeof(host_name),
                        &host_port) != 0) {
        snprintf(reason, reason_len, "invalid Host");
        return false;
    }
    if (host_port == 0) host_port = s->port;
    if (host_port != s->port) {
        snprintf(reason, reason_len, "Host port mismatch");
        return false;
    }
    if (s->loopback_only && !is_loopback_host(host_name)) {
        snprintf(reason, reason_len, "Host is not loopback");
        return false;
    }

    struct mg_str *origin_hdr = mg_http_get_header(hm, "Origin");
    if (!origin_hdr || origin_hdr->len == 0) return true;

    char origin_value[256];
    char origin_host[128];
    int origin_port = 0;
    if (mg_str_to_cstr(*origin_hdr, origin_value, sizeof(origin_value)) != 0 ||
        !parse_origin(origin_value, origin_host, sizeof(origin_host),
                      &origin_port)) {
        snprintf(reason, reason_len, "invalid Origin");
        return false;
    }
    if (strcasecmp(origin_host, host_name) != 0 || origin_port != host_port) {
        snprintf(reason, reason_len, "Origin/Host mismatch");
        return false;
    }
    return true;
}

static void send_caps(struct mg_connection *c, const wasm_proxy_server_t *s) {
    char body[2048];
    wasm_proxy_caps_config_t cfg = {
        .bind_addr = s->bind_addr,
        .port = s->port,
        .loopback_only = s->loopback_only,
        .public_bind_allowed = s->allow_public_bind,
    };
    if (wasm_proxy_build_caps_json(body, sizeof(body), &cfg) != 0) {
        mg_http_reply(c, 500, k_extra_headers, "caps serialization failed\n");
        return;
    }
    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n"
                  "Cross-Origin-Opener-Policy: same-origin\r\n"
                  "Cross-Origin-Embedder-Policy: require-corp\r\n"
                  "Cache-Control: no-store\r\n",
                  "%s\n", body);
}

static void send_ws_caps(struct mg_connection *c, const wasm_proxy_server_t *s) {
    char body[2048];
    wasm_proxy_caps_config_t cfg = {
        .bind_addr = s->bind_addr,
        .port = s->port,
        .loopback_only = s->loopback_only,
        .public_bind_allowed = s->allow_public_bind,
    };
    if (wasm_proxy_build_caps_json(body, sizeof(body), &cfg) == 0) {
        mg_ws_send(c, body, strlen(body), WEBSOCKET_OP_TEXT);
    } else {
        const char *err = "{\"type\":\"error\",\"error\":\"caps\"}";
        mg_ws_send(c, err, strlen(err), WEBSOCKET_OP_TEXT);
    }
}

static void send_binary_reply(struct mg_connection *c, int status,
                              const void *body, size_t body_len,
                              int truncated) {
    const char *text = status == 200 ? "OK" : "Error";
    mg_printf(c,
              "HTTP/1.1 %d %s\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: %lu\r\n"
              "X-PicoMite-Proxy-Truncated: %s\r\n"
              "%s"
              "\r\n",
              status, text,
              (unsigned long)body_len, truncated ? "true" : "false",
              k_extra_headers);
    if (body && body_len) mg_send(c, body, body_len);
    c->is_resp = 0;
}

static int parse_int_var(struct mg_http_message *hm, const char *name,
                         int min_value, int max_value, int default_value,
                         int *out) {
    char buf[32];
    int n = mg_http_get_var(&hm->query, name, buf, sizeof(buf));
    if (n <= 0) {
        *out = default_value;
        return 0;
    }
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (!end || *end || v < min_value || v > max_value) return -1;
    *out = (int)v;
    return 0;
}

static void handle_http_proxy(struct mg_connection *c,
                              const wasm_proxy_server_t *s,
                              struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    char host[256];
    if (mg_http_get_var(&hm->query, "host", host, sizeof(host)) <= 0) {
        mg_http_reply(c, 400, k_extra_headers, "missing host\n");
        return;
    }
    int port = 0;
    int timeout_ms = 5000;
    int max_bytes = WASM_PROXY_HTTP_MAX_RESPONSE_BYTES;
    if (parse_int_var(hm, "port", 1, 65535, 0, &port) != 0 || port == 0 ||
        parse_int_var(hm, "timeout_ms", 1, 100000, 5000,
                      &timeout_ms) != 0 ||
        parse_int_var(hm, "max_bytes", 1, WASM_PROXY_HTTP_MAX_RESPONSE_BYTES,
                      WASM_PROXY_HTTP_MAX_RESPONSE_BYTES, &max_bytes) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    if (hm->body.len == 0 ||
        hm->body.len > WASM_PROXY_HTTP_MAX_REQUEST_BYTES) {
        mg_http_reply(c, 413, k_extra_headers, "request too large\n");
        return;
    }

    wasm_proxy_http_response_t resp;
    int rc = wasm_proxy_http_request(host, port, (const uint8_t *)hm->body.buf,
                                     hm->body.len, (size_t)max_bytes,
                                     timeout_ms, &resp);
    if (rc == WASM_PROXY_HTTP_OK) {
        send_binary_reply(c, 200, resp.data, resp.len, resp.truncated);
    } else {
        int status = 502;
        if (rc == WASM_PROXY_HTTP_BAD_REQUEST) status = 400;
        else if (rc == WASM_PROXY_HTTP_TIMEOUT) status = 504;
        else if (rc == WASM_PROXY_HTTP_NO_MEMORY) status = 500;
        mg_http_reply(c, status, k_extra_headers, "%s\n",
                      resp.error[0] ? resp.error : "proxy request failed");
    }
    wasm_proxy_http_response_free(&resp);
}

static int tcp_status_to_http(int rc) {
    if (rc == WASM_PROXY_TCP_BAD_REQUEST) return 400;
    if (rc == WASM_PROXY_TCP_TIMEOUT) return 504;
    if (rc == WASM_PROXY_TCP_NO_MEMORY) return 500;
    if (rc == WASM_PROXY_TCP_NOT_FOUND) return 404;
    return 502;
}

static void handle_tcp_listen_proxy(struct mg_connection *c,
                                    const wasm_proxy_server_t *s,
                                    struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int port = 0;
    int backlog = 1;
    if (parse_int_var(hm, "port", 1, 65535, 0, &port) != 0 || port == 0 ||
        parse_int_var(hm, "backlog", 1, WASM_PROXY_TCP_MAX_SERVER_CONNS,
                      1, &backlog) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    int id = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_listen(port, backlog, &id, error, sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        mg_http_reply(c, 200, k_text_headers, "%d\n", id);
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp listen failed");
    }
}

static void handle_tcp_listener_close_proxy(struct mg_connection *c,
                                            const wasm_proxy_server_t *s,
                                            struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    int rc = wasm_proxy_tcp_listener_close(id);
    if (rc == WASM_PROXY_TCP_OK || rc == WASM_PROXY_TCP_NOT_FOUND) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers,
                      "tcp listener close failed\n");
    }
}

static void send_tcp_accept_reply(struct mg_connection *c, int conn_id,
                                  const void *body, size_t body_len) {
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: %lu\r\n"
              "X-PicoMite-Proxy-Conn: %d\r\n"
              "%s"
              "\r\n",
              (unsigned long)body_len, conn_id, k_extra_headers);
    if (body && body_len) mg_send(c, body, body_len);
    c->is_resp = 0;
}

static void handle_tcp_accept_proxy(struct mg_connection *c,
                                    const wasm_proxy_server_t *s,
                                    struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    int max_bytes = 2048;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0 ||
        parse_int_var(hm, "max_bytes", 1, WASM_PROXY_TCP_MAX_READ_BYTES,
                      2048, &max_bytes) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)max_bytes);
    if (!buf) {
        mg_http_reply(c, 500, k_extra_headers, "out of memory\n");
        return;
    }
    size_t got = 0;
    int conn_id = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_accept_event(id, buf, (size_t)max_bytes, &got,
                                         &conn_id, error, sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        send_tcp_accept_reply(c, conn_id, buf, got);
    } else if (rc == WASM_PROXY_TCP_TIMEOUT) {
        mg_http_reply(c, 204, k_extra_headers, "");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp accept failed");
    }
    free(buf);
}

static void handle_tcp_accept_conn_proxy(struct mg_connection *c,
                                         const wasm_proxy_server_t *s,
                                         struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    int conn_id = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_accept_conn(id, &conn_id, error, sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        mg_http_reply(c, 200, k_text_headers, "%d\n", conn_id);
    } else if (rc == WASM_PROXY_TCP_TIMEOUT) {
        mg_http_reply(c, 204, k_extra_headers, "");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp accept failed");
    }
}

static void handle_tcp_conn_recv_proxy(struct mg_connection *c,
                                       const wasm_proxy_server_t *s,
                                       struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    int max_bytes = 2048;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0 ||
        parse_int_var(hm, "max_bytes", 1, WASM_PROXY_TCP_MAX_READ_BYTES,
                      2048, &max_bytes) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)max_bytes);
    if (!buf) {
        mg_http_reply(c, 500, k_extra_headers, "out of memory\n");
        return;
    }
    size_t got = 0;
    int closed = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_conn_recv(id, buf, (size_t)max_bytes, &got,
                                      &closed, error, sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        send_binary_reply(c, 200, buf, got, 0);
    } else if (rc == WASM_PROXY_TCP_TIMEOUT) {
        mg_http_reply(c, 204, k_extra_headers, "");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp recv failed");
    }
    free(buf);
    (void)closed;
}

static void handle_tcp_conn_send_proxy(struct mg_connection *c,
                                       const wasm_proxy_server_t *s,
                                       struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    int timeout_ms = 5000;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0 ||
        parse_int_var(hm, "timeout_ms", 1, 100000, 5000,
                      &timeout_ms) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    if (hm->body.len > WASM_PROXY_TCP_MAX_WRITE_BYTES) {
        mg_http_reply(c, 413, k_extra_headers, "write too large\n");
        return;
    }

    char error[160] = {0};
    int rc = wasm_proxy_tcp_conn_send(id, (const uint8_t *)hm->body.buf,
                                      hm->body.len, timeout_ms, error,
                                      sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp send failed");
    }
}

static void handle_tcp_conn_close_proxy(struct mg_connection *c,
                                        const wasm_proxy_server_t *s,
                                        struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    int rc = wasm_proxy_tcp_conn_close(id);
    if (rc == WASM_PROXY_TCP_OK || rc == WASM_PROXY_TCP_NOT_FOUND) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers,
                      "tcp conn close failed\n");
    }
}

static int udp_status_to_http(int rc) {
    if (rc == WASM_PROXY_UDP_BAD_REQUEST) return 400;
    if (rc == WASM_PROXY_UDP_TIMEOUT) return 504;
    if (rc == WASM_PROXY_UDP_NO_MEMORY) return 500;
    if (rc == WASM_PROXY_UDP_NOT_FOUND) return 404;
    if (rc == WASM_PROXY_UDP_BIND_FAILED) return 502;
    return 502;
}

static void handle_tcp_open_proxy(struct mg_connection *c,
                                  const wasm_proxy_server_t *s,
                                  struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    char host[256];
    if (mg_http_get_var(&hm->query, "host", host, sizeof(host)) <= 0) {
        mg_http_reply(c, 400, k_extra_headers, "missing host\n");
        return;
    }
    int port = 0;
    int timeout_ms = 5000;
    if (parse_int_var(hm, "port", 1, 65535, 0, &port) != 0 || port == 0 ||
        parse_int_var(hm, "timeout_ms", 1, 100000, 5000,
                      &timeout_ms) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    int id = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_open(host, port, timeout_ms, &id, error,
                                 sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        mg_http_reply(c, 200, k_text_headers, "%d\n", id);
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp open failed");
    }
}

static void handle_tcp_stream_proxy(struct mg_connection *c,
                                    const wasm_proxy_server_t *s,
                                    struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    int timeout_ms = 5000;
    int max_bytes = 4096;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0 ||
        parse_int_var(hm, "timeout_ms", 1, 100000, 5000,
                      &timeout_ms) != 0 ||
        parse_int_var(hm, "max_bytes", 1, WASM_PROXY_TCP_MAX_READ_BYTES,
                      4096, &max_bytes) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    if (hm->body.len > WASM_PROXY_TCP_MAX_WRITE_BYTES) {
        mg_http_reply(c, 413, k_extra_headers, "write too large\n");
        return;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)max_bytes);
    if (!buf) {
        mg_http_reply(c, 500, k_extra_headers, "out of memory\n");
        return;
    }

    size_t got = 0;
    int closed = 0;
    char error[160] = {0};
    int rc = wasm_proxy_tcp_write_read(
        id, (const uint8_t *)hm->body.buf, hm->body.len, buf,
        (size_t)max_bytes, &got, timeout_ms, &closed, error, sizeof(error));
    if (rc == WASM_PROXY_TCP_OK) {
        send_binary_reply(c, 200, buf, got, 0);
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "tcp stream failed");
    }
    free(buf);
    (void)closed;
}

static void handle_tcp_close_proxy(struct mg_connection *c,
                                   const wasm_proxy_server_t *s,
                                   struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    int rc = wasm_proxy_tcp_close(id);
    if (rc == WASM_PROXY_TCP_OK || rc == WASM_PROXY_TCP_NOT_FOUND) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, tcp_status_to_http(rc), k_extra_headers,
                      "tcp close failed\n");
    }
}

static void handle_udp_bind_proxy(struct mg_connection *c,
                                  const wasm_proxy_server_t *s,
                                  struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int port = 0;
    if (parse_int_var(hm, "port", 0, 65535, 0, &port) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    int id = 0;
    char error[160] = {0};
    int rc = wasm_proxy_udp_bind(port, &id, error, sizeof(error));
    if (rc == WASM_PROXY_UDP_OK) {
        mg_http_reply(c, 200, k_text_headers, "%d\n", id);
    } else {
        mg_http_reply(c, udp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "udp bind failed");
    }
}

static void handle_udp_close_proxy(struct mg_connection *c,
                                   const wasm_proxy_server_t *s,
                                   struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    int rc = wasm_proxy_udp_close(id);
    if (rc == WASM_PROXY_UDP_OK || rc == WASM_PROXY_UDP_NOT_FOUND) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, udp_status_to_http(rc), k_extra_headers,
                      "udp close failed\n");
    }
}

static void handle_udp_send_proxy(struct mg_connection *c,
                                  const wasm_proxy_server_t *s,
                                  struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    char host[256];
    if (mg_http_get_var(&hm->query, "host", host, sizeof(host)) <= 0) {
        mg_http_reply(c, 400, k_extra_headers, "missing host\n");
        return;
    }
    int id = 0;
    int port = 0;
    int timeout_ms = 5000;
    if (parse_int_var(hm, "id", 0, 2147483647, 0, &id) != 0 ||
        parse_int_var(hm, "port", 1, 65535, 0, &port) != 0 || port == 0 ||
        parse_int_var(hm, "timeout_ms", 1, 100000, 5000,
                      &timeout_ms) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }
    if (hm->body.len > WASM_PROXY_UDP_MAX_DATAGRAM_BYTES) {
        mg_http_reply(c, 413, k_extra_headers, "datagram too large\n");
        return;
    }

    char error[160] = {0};
    int rc = wasm_proxy_udp_send(id, host, port, (const uint8_t *)hm->body.buf,
                                 hm->body.len, timeout_ms, error,
                                 sizeof(error));
    if (rc == WASM_PROXY_UDP_OK) {
        mg_http_reply(c, 200, k_text_headers, "OK\n");
    } else {
        mg_http_reply(c, udp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "udp send failed");
    }
}

static void send_udp_packet_reply(struct mg_connection *c,
                                  const wasm_proxy_udp_packet_t *packet) {
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: %lu\r\n"
              "X-PicoMite-Proxy-Address: %s\r\n"
              "X-PicoMite-Proxy-Family: %d\r\n"
              "X-PicoMite-Proxy-Port: %d\r\n"
              "%s"
              "\r\n",
              (unsigned long)packet->len, packet->address, packet->family,
              packet->port, k_extra_headers);
    if (packet->len) mg_send(c, packet->data, packet->len);
    c->is_resp = 0;
}

static void handle_udp_recv_proxy(struct mg_connection *c,
                                  const wasm_proxy_server_t *s,
                                  struct mg_http_message *hm) {
    if (!mg_match(hm->method, mg_str("POST"), NULL)) {
        mg_http_reply(c, 405, k_extra_headers, "POST required\n");
        return;
    }
    char reason[80];
    if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
        mg_http_reply(c, 403, k_extra_headers, "Forbidden: %s\n", reason);
        return;
    }

    int id = 0;
    int max_bytes = 2048;
    if (parse_int_var(hm, "id", 1, 2147483647, 0, &id) != 0 || id == 0 ||
        parse_int_var(hm, "max_bytes", 1, WASM_PROXY_UDP_MAX_DATAGRAM_BYTES,
                      2048, &max_bytes) != 0) {
        mg_http_reply(c, 400, k_extra_headers, "invalid query\n");
        return;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)max_bytes);
    if (!buf) {
        mg_http_reply(c, 500, k_extra_headers, "out of memory\n");
        return;
    }
    wasm_proxy_udp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = buf;
    packet.cap = (size_t)max_bytes;

    char error[160] = {0};
    int rc = wasm_proxy_udp_recv(id, &packet, error, sizeof(error));
    if (rc == WASM_PROXY_UDP_OK) {
        send_udp_packet_reply(c, &packet);
    } else if (rc == WASM_PROXY_UDP_WOULD_BLOCK) {
        mg_http_reply(c, 204, k_extra_headers, "");
    } else {
        mg_http_reply(c, udp_status_to_http(rc), k_extra_headers, "%s\n",
                      error[0] ? error : "udp recv failed");
    }
    free(buf);
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    wasm_proxy_server_t *s = (wasm_proxy_server_t *)c->fn_data;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        if (mg_match(hm->uri, mg_str("/__picomite_proxy/caps"), NULL)) {
            send_caps(c, s);
        } else if (mg_match(hm->uri, mg_str("/__picomite_proxy/http"), NULL)) {
            handle_http_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/open"), NULL)) {
            handle_tcp_open_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/stream"), NULL)) {
            handle_tcp_stream_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/close"), NULL)) {
            handle_tcp_close_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/listen"), NULL)) {
            handle_tcp_listen_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/listener-close"),
                            NULL)) {
            handle_tcp_listener_close_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/accept"), NULL)) {
            handle_tcp_accept_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/accept-conn"),
                            NULL)) {
            handle_tcp_accept_conn_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/conn-recv"),
                            NULL)) {
            handle_tcp_conn_recv_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/conn-send"),
                            NULL)) {
            handle_tcp_conn_send_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/tcp/conn-close"),
                            NULL)) {
            handle_tcp_conn_close_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/udp/bind"), NULL)) {
            handle_udp_bind_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/udp/send"), NULL)) {
            handle_udp_send_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/udp/recv"), NULL)) {
            handle_udp_recv_proxy(c, s, hm);
        } else if (mg_match(hm->uri,
                            mg_str("/__picomite_proxy/udp/close"), NULL)) {
            handle_udp_close_proxy(c, s, hm);
        } else if (mg_match(hm->uri, mg_str("/__picomite_proxy/ws"), NULL)) {
            char reason[80];
            if (!validate_ws_request(s, hm, reason, sizeof(reason))) {
                mg_http_reply(c, 403, k_extra_headers,
                              "Forbidden: %s\n", reason);
                return;
            }
            mg_ws_upgrade(c, hm, "%s", k_extra_headers);
        } else {
            struct mg_http_serve_opts opts = {0};
            opts.root_dir = s->web_root;
            opts.extra_headers = k_extra_headers;
            mg_http_serve_dir(c, hm, &opts);
        }
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        if ((wm->flags & 0x0f) != WEBSOCKET_OP_TEXT) return;
        if (wasm_proxy_is_hello_message(wm->data.buf, wm->data.len)) {
            send_ws_caps(c, s);
        } else {
            const char *err =
                "{\"type\":\"error\",\"error\":\"expected hello\"}";
            mg_ws_send(c, err, strlen(err), WEBSOCKET_OP_TEXT);
        }
    }
}

int main(int argc, char **argv) {
    wasm_proxy_server_t server;
    memset(&server, 0, sizeof(server));
    server.running = &g_running;
    if (parse_args(argc, argv, &server) != 0) {
        usage(stderr, argv[0]);
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    mg_log_set(MG_LL_ERROR);
    mg_mgr_init(&server.mgr);
    struct mg_connection *listener =
        mg_http_listen(&server.mgr, server.listen_url, ev_handler, &server);
    if (!listener) {
        fprintf(stderr, "Failed to listen on %s\n", server.listen_url);
        mg_mgr_free(&server.mgr);
        return 1;
    }

    printf("Serving %s at %s/ (PicoMite WASM network proxy)\n",
           server.web_root, server.listen_url);
    fflush(stdout);
    while (*server.running) mg_mgr_poll(&server.mgr, 50);
    wasm_proxy_tcp_close_all();
    wasm_proxy_tcp_listeners_close_all();
    wasm_proxy_udp_close_all();
    mg_mgr_free(&server.mgr);
    return 0;
}
