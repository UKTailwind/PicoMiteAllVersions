#ifndef WASM_PROXY_PROTOCOL_H
#define WASM_PROXY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#define WASM_PROXY_PROTOCOL_VERSION 1

typedef struct wasm_proxy_caps_config {
    const char *bind_addr;
    int port;
    bool loopback_only;
    bool public_bind_allowed;
} wasm_proxy_caps_config_t;

int wasm_proxy_build_caps_json(char *dst, size_t dst_len,
                               const wasm_proxy_caps_config_t *cfg);
bool wasm_proxy_is_hello_message(const char *data, size_t len);

#endif
