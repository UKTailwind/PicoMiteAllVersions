#include "wasm_proxy_protocol.h"
#include "wasm_proxy_http.h"

#include <stdio.h>
#include <string.h>

static int append_json_string(char *dst, size_t dst_len, size_t *used,
                              const char *value) {
    if (!dst || !used || !value) return -1;
    if (*used >= dst_len) return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        const char *esc = NULL;
        char tmp[7];
        if (*p == '\\') esc = "\\\\";
        else if (*p == '"') esc = "\\\"";
        else if (*p == '\n') esc = "\\n";
        else if (*p == '\r') esc = "\\r";
        else if (*p == '\t') esc = "\\t";
        else if (*p < 0x20) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
            esc = tmp;
        }

        if (esc) {
            size_t n = strlen(esc);
            if (*used + n >= dst_len) return -1;
            memcpy(dst + *used, esc, n);
            *used += n;
        } else {
            if (*used + 1 >= dst_len) return -1;
            dst[(*used)++] = (char)*p;
        }
    }
    dst[*used] = '\0';
    return 0;
}

static bool bytes_contains(const char *data, size_t len, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || needle_len > len) return false;
    for (size_t i = 0; i + needle_len <= len; ++i) {
        if (memcmp(data + i, needle, needle_len) == 0) return true;
    }
    return false;
}

int wasm_proxy_build_caps_json(char *dst, size_t dst_len,
                               const wasm_proxy_caps_config_t *cfg) {
    if (!dst || dst_len == 0 || !cfg) return -1;
    const char *bind_addr = cfg->bind_addr ? cfg->bind_addr : "";
    int n = snprintf(dst, dst_len,
        "{\"type\":\"caps\","
        "\"protocol\":\"picomite-wasm-proxy\","
        "\"protocol_version\":%d,"
        "\"features\":{"
        "\"tcp_client\":false,"
        "\"tcp_stream\":true,"
        "\"tcp_server\":true,"
        "\"udp\":true,"
        "\"tftp\":true,"
        "\"telnet\":true,"
        "\"ntp\":true,"
        "\"mqtt_plain\":true,"
        "\"http_proxy\":true,"
        "\"phase0_noop\":false"
        "},"
        "\"limits\":{"
        "\"max_frame_bytes\":65536,"
        "\"max_request_bytes\":%d,"
        "\"max_http_response_bytes\":%d,"
        "\"max_connections\":%d,"
        "\"max_udp_datagram_bytes\":65507"
        "},"
        "\"security\":{\"bind\":\"",
        WASM_PROXY_PROTOCOL_VERSION, WASM_PROXY_HTTP_MAX_REQUEST_BYTES,
        WASM_PROXY_HTTP_MAX_RESPONSE_BYTES, 4);
    if (n < 0 || (size_t)n >= dst_len) return -1;

    size_t used = (size_t)n;
    if (append_json_string(dst, dst_len, &used, bind_addr) != 0) return -1;
    n = snprintf(dst + used, dst_len - used,
                 "\",\"port\":%d,\"loopback_only\":%s,"
                 "\"public_bind_allowed\":%s}}",
                 cfg->port, cfg->loopback_only ? "true" : "false",
                 cfg->public_bind_allowed ? "true" : "false");
    if (n < 0 || (size_t)n >= dst_len - used) return -1;
    return 0;
}

bool wasm_proxy_is_hello_message(const char *data, size_t len) {
    if (!data || len == 0 || len > 4096) return false;
    if (len == 5 && memcmp(data, "hello", 5) == 0) return true;
    return bytes_contains(data, len, "\"type\"") &&
           bytes_contains(data, len, "\"hello\"");
}
