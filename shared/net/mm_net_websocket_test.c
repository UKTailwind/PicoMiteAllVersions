/*
 * Focused native tests for mm_net_websocket.c.
 */

#include "shared/net/mm_net_websocket.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void expect_bytes(const uint8_t *got, const uint8_t *want, size_t len,
                         const char *label) {
    if (memcmp(got, want, len) == 0) return;
    fprintf(stderr, "FAIL %s\n got :", label);
    for (size_t i = 0; i < len; i++) fprintf(stderr, " %02x", got[i]);
    fprintf(stderr, "\n want:");
    for (size_t i = 0; i < len; i++) fprintf(stderr, " %02x", want[i]);
    fprintf(stderr, "\n");
    failures++;
}

static size_t make_masked_frame(uint8_t opcode, int fin, const uint8_t *payload,
                                size_t payload_len, uint8_t *out,
                                size_t out_len) {
    const uint8_t mask[] = { 0x12, 0x34, 0x56, 0x78 };
    size_t pos = 2;
    if (out_len < 6 || payload_len > 65535u) return 0;
    out[0] = (fin ? 0x80 : 0x00) | (opcode & 0x0f);
    if (payload_len <= 125u) {
        out[1] = 0x80u | (uint8_t)payload_len;
    } else {
        if (out_len < 8) return 0;
        out[1] = 0x80u | 126u;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)payload_len;
        pos = 4;
    }
    if (out_len < pos + 4u + payload_len) return 0;
    memcpy(out + pos, mask, 4);
    pos += 4;
    for (size_t i = 0; i < payload_len; i++) {
        out[pos + i] = payload[i] ^ mask[i & 3u];
    }
    return pos + payload_len;
}

static void test_accept_and_request(void) {
    char accept[MM_NET_WS_ACCEPT_LEN];
    EXPECT_TRUE(mm_net_ws_compute_accept("dGhlIHNhbXBsZSBub25jZQ==",
                                         accept) == MM_NET_WS_OK);
    EXPECT_TRUE(strcmp(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);

    const char request[] =
        "GET /__web_console/ws HTTP/1.1\r\n"
        "Host: example\r\n"
        "Upgrade: websocket\r\n"
        "Connection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    char key[MM_NET_WS_MAX_KEY_LEN];
    EXPECT_TRUE(mm_net_ws_validate_upgrade_request(
                    request, strlen(request), "/__web_console/ws", key) ==
                MM_NET_WS_OK);
    EXPECT_TRUE(strcmp(key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);

    const char bad_version[] =
        "GET /__web_console/ws HTTP/1.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 12\r\n"
        "\r\n";
    EXPECT_TRUE(mm_net_ws_validate_upgrade_request(
                    bad_version, strlen(bad_version), "/__web_console/ws",
                    key) == MM_NET_WS_ERR_BAD_REQUEST);
}

static void test_encode(void) {
    uint8_t buf[140];
    size_t n = 0;
    EXPECT_TRUE(mm_net_ws_encode_frame(MM_NET_WS_OPCODE_TEXT, "hi", 2, buf,
                                       sizeof(buf), &n) == MM_NET_WS_OK);
    const uint8_t want_text[] = { 0x81, 0x02, 'h', 'i' };
    EXPECT_TRUE(n == sizeof(want_text));
    expect_bytes(buf, want_text, sizeof(want_text), "text frame");

    uint8_t payload[126];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
    EXPECT_TRUE(mm_net_ws_encode_frame(MM_NET_WS_OPCODE_BINARY, payload,
                                       sizeof(payload), buf, sizeof(buf),
                                       &n) == MM_NET_WS_OK);
    EXPECT_TRUE(n == 130);
    EXPECT_TRUE(buf[0] == 0x82);
    EXPECT_TRUE(buf[1] == 126);
    EXPECT_TRUE(buf[2] == 0);
    EXPECT_TRUE(buf[3] == 126);
    EXPECT_TRUE(memcmp(buf + 4, payload, sizeof(payload)) == 0);
}

static void test_decode_text_and_controls(void) {
    uint8_t frame[256];
    uint8_t payload_out[64];
    mm_net_ws_frame_t decoded;
    size_t consumed = 0;

    size_t n = make_masked_frame(MM_NET_WS_OPCODE_TEXT, 1,
                                 (const uint8_t *)"hello", 5, frame,
                                 sizeof(frame));
    EXPECT_TRUE(n > 0);
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) == MM_NET_WS_OK);
    EXPECT_TRUE(consumed == n);
    EXPECT_TRUE(decoded.fin == 1);
    EXPECT_TRUE(decoded.opcode == MM_NET_WS_OPCODE_TEXT);
    EXPECT_TRUE(decoded.payload_len == 5);
    EXPECT_TRUE(memcmp(payload_out, "hello", 5) == 0);

    const uint8_t binary_payload[] = { 0xde, 0xad, 0xbe, 0xef };
    n = make_masked_frame(MM_NET_WS_OPCODE_BINARY, 1, binary_payload,
                          sizeof(binary_payload), frame, sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) == MM_NET_WS_OK);
    EXPECT_TRUE(decoded.opcode == MM_NET_WS_OPCODE_BINARY);
    EXPECT_TRUE(decoded.payload_len == sizeof(binary_payload));
    EXPECT_TRUE(memcmp(payload_out, binary_payload, sizeof(binary_payload)) == 0);

    n = make_masked_frame(MM_NET_WS_OPCODE_PING, 1,
                          (const uint8_t *)"?", 1, frame, sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) == MM_NET_WS_OK);
    EXPECT_TRUE(decoded.opcode == MM_NET_WS_OPCODE_PING);
    EXPECT_TRUE(decoded.payload_len == 1);
    EXPECT_TRUE(payload_out[0] == '?');

    n = make_masked_frame(MM_NET_WS_OPCODE_PONG, 1,
                          (const uint8_t *)"!", 1, frame, sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) == MM_NET_WS_OK);
    EXPECT_TRUE(decoded.opcode == MM_NET_WS_OPCODE_PONG);
    EXPECT_TRUE(payload_out[0] == '!');

    const uint8_t close_payload[] = { 0x03, 0xe8 };
    n = make_masked_frame(MM_NET_WS_OPCODE_CLOSE, 1, close_payload,
                          sizeof(close_payload), frame, sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) == MM_NET_WS_OK);
    EXPECT_TRUE(decoded.opcode == MM_NET_WS_OPCODE_CLOSE);
    EXPECT_TRUE(decoded.close_code == 1000);
}

static void test_decode_rejections(void) {
    uint8_t frame[256];
    uint8_t payload_out[32];
    mm_net_ws_frame_t decoded;
    size_t consumed = 0;

    size_t n = make_masked_frame(MM_NET_WS_OPCODE_TEXT, 0,
                                 (const uint8_t *)"split", 5, frame,
                                 sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, sizeof(payload_out),
                                       payload_out, sizeof(payload_out),
                                       &decoded, &consumed) ==
                MM_NET_WS_ERR_UNSUPPORTED);

    uint8_t big_payload[130];
    memset(big_payload, 'x', sizeof(big_payload));
    n = make_masked_frame(MM_NET_WS_OPCODE_TEXT, 1, big_payload,
                          sizeof(big_payload), frame, sizeof(frame));
    EXPECT_TRUE(mm_net_ws_decode_frame(frame, n, 16, payload_out,
                                       sizeof(payload_out), &decoded,
                                       &consumed) == MM_NET_WS_ERR_TOO_LARGE);

    const uint8_t unmasked[] = { 0x81, 0x02, 'n', 'o' };
    EXPECT_TRUE(mm_net_ws_decode_frame(unmasked, sizeof(unmasked),
                                       sizeof(payload_out), payload_out,
                                       sizeof(payload_out), &decoded,
                                       &consumed) == MM_NET_WS_ERR_BAD_FRAME);
}

int main(void) {
    test_accept_and_request();
    test_encode();
    test_decode_text_and_controls();
    test_decode_rejections();
    if (failures) return 1;
    puts("mm_net_websocket_test: PASS");
    return 0;
}
