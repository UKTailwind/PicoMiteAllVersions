#ifndef WASM_PROXY_HTTP_H
#define WASM_PROXY_HTTP_H

#include <stddef.h>
#include <stdint.h>

#define WASM_PROXY_HTTP_MAX_REQUEST_BYTES 65536
#define WASM_PROXY_HTTP_MAX_RESPONSE_BYTES 65536

typedef enum wasm_proxy_http_status {
    WASM_PROXY_HTTP_OK = 0,
    WASM_PROXY_HTTP_BAD_REQUEST = -1,
    WASM_PROXY_HTTP_CONNECT_FAILED = -2,
    WASM_PROXY_HTTP_IO_FAILED = -3,
    WASM_PROXY_HTTP_TIMEOUT = -4,
    WASM_PROXY_HTTP_NO_MEMORY = -5,
} wasm_proxy_http_status_t;

typedef struct wasm_proxy_http_response {
    uint8_t * data;
    size_t len;
    int truncated;
    char error[160];
} wasm_proxy_http_response_t;

int wasm_proxy_http_request(const char * host, int port,
                            const uint8_t * request, size_t request_len,
                            size_t max_response_bytes, int timeout_ms,
                            wasm_proxy_http_response_t * out);
void wasm_proxy_http_response_free(wasm_proxy_http_response_t * resp);

#endif
