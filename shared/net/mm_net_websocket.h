/*
 * shared/net/mm_net_websocket.h - Minimal RFC 6455 helpers for firmware-owned
 * WebSocket endpoints.
 */

#ifndef MM_NET_WEBSOCKET_H
#define MM_NET_WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MM_NET_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define MM_NET_WS_ACCEPT_LEN 29
#define MM_NET_WS_MAX_KEY_LEN 64

enum {
    MM_NET_WS_OPCODE_CONTINUATION = 0x0,
    MM_NET_WS_OPCODE_TEXT = 0x1,
    MM_NET_WS_OPCODE_BINARY = 0x2,
    MM_NET_WS_OPCODE_CLOSE = 0x8,
    MM_NET_WS_OPCODE_PING = 0x9,
    MM_NET_WS_OPCODE_PONG = 0xA,
};

typedef enum {
    MM_NET_WS_OK = 0,
    MM_NET_WS_NEED_MORE = 1,
    MM_NET_WS_ERR = -1,
    MM_NET_WS_ERR_BAD_REQUEST = -2,
    MM_NET_WS_ERR_BAD_FRAME = -3,
    MM_NET_WS_ERR_UNSUPPORTED = -4,
    MM_NET_WS_ERR_TOO_LARGE = -5,
    MM_NET_WS_ERR_BUFFER = -6,
} mm_net_ws_result_t;

typedef struct {
    uint8_t fin;
    uint8_t opcode;
    size_t payload_len;
    uint16_t close_code;
} mm_net_ws_frame_t;

int mm_net_ws_compute_accept(const char *key,
                             char out[MM_NET_WS_ACCEPT_LEN]);
int mm_net_ws_validate_upgrade_request(const void *request, size_t request_len,
                                       const char *expected_path,
                                       char key_out[MM_NET_WS_MAX_KEY_LEN]);
int mm_net_ws_format_upgrade_response(char *out, size_t out_len,
                                      const char *accept);
int mm_net_ws_encode_frame(uint8_t opcode, const void *payload,
                           size_t payload_len, uint8_t *out, size_t out_len,
                           size_t *written);
int mm_net_ws_decode_frame(const uint8_t *in, size_t in_len,
                           size_t payload_limit, uint8_t *payload_out,
                           size_t payload_out_len, mm_net_ws_frame_t *frame,
                           size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* MM_NET_WEBSOCKET_H */
