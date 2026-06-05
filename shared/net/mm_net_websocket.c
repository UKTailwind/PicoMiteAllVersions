/*
 * shared/net/mm_net_websocket.c - Minimal RFC 6455 handshake/framing helpers.
 */

#include "shared/net/mm_net_websocket.h"

#include <stdio.h>
#include <string.h>

static int ascii_tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int ascii_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int ascii_strncasecmp(const char * a, const char * b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = ascii_tolower((unsigned char)a[i]);
        int cb = ascii_tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static int ascii_strcasecmp(const char * a, const char * b) {
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a++);
        int cb = ascii_tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

static size_t trim_span(const char ** p, size_t len) {
    while (len && ascii_isspace((unsigned char)(*p)[0])) {
        (*p)++;
        len--;
    }
    while (len && ascii_isspace((unsigned char)(*p)[len - 1])) len--;
    return len;
}

typedef struct {
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t buffer[64];
} sha1_ctx_t;

static uint32_t rol32(uint32_t v, unsigned bits) {
    return (v << bits) | (v >> (32 - bits));
}

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(sha1_ctx_t * ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
    ctx->bit_count = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void sha1_update(sha1_ctx_t * ctx, const uint8_t * data, size_t len) {
    size_t offset = (size_t)((ctx->bit_count / 8) & 63u);
    ctx->bit_count += (uint64_t)len * 8u;

    size_t i = 0;
    if (offset) {
        size_t fill = 64u - offset;
        if (fill > len) fill = len;
        memcpy(ctx->buffer + offset, data, fill);
        offset += fill;
        i += fill;
        if (offset == 64u) sha1_transform(ctx->state, ctx->buffer);
    }

    for (; i + 64u <= len; i += 64u) sha1_transform(ctx->state, data + i);

    if (i < len) memcpy(ctx->buffer, data + i, len - i);
}

static void sha1_final(sha1_ctx_t * ctx, uint8_t digest[20]) {
    uint8_t pad = 0x80;
    uint8_t zero = 0;
    uint8_t len_be[8];
    uint64_t bits = ctx->bit_count;
    for (int i = 0; i < 8; i++) len_be[7 - i] = (uint8_t)(bits >> (i * 8));

    sha1_update(ctx, &pad, 1);
    while (((ctx->bit_count / 8) & 63u) != 56u) sha1_update(ctx, &zero, 1);
    sha1_update(ctx, len_be, sizeof(len_be));

    for (int i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static int base64_value(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode_len(const char * src, size_t len, size_t * decoded_len) {
    if (!src || !decoded_len || len == 0 || (len & 3u) != 0) return 0;
    size_t pad = 0;
    if (len && src[len - 1] == '=') pad++;
    if (len > 1 && src[len - 2] == '=') pad++;
    if (pad > 2) return 0;

    for (size_t i = 0; i < len; i++) {
        if (src[i] == '=') {
            if (i < len - pad) return 0;
        } else if (base64_value((unsigned char)src[i]) < 0) {
            return 0;
        }
    }
    *decoded_len = (len / 4u) * 3u - pad;
    return 1;
}

static int base64_encode(const uint8_t * src, size_t len, char * out,
                         size_t out_len) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((len + 2u) / 3u) * 4u + 1u;
    if (!out || out_len < need) return 0;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 3u) {
        uint32_t v = (uint32_t)src[i] << 16;
        int have2 = i + 1u < len;
        int have3 = i + 2u < len;
        if (have2) v |= (uint32_t)src[i + 1u] << 8;
        if (have3) v |= src[i + 2u];
        out[o++] = tab[(v >> 18) & 63u];
        out[o++] = tab[(v >> 12) & 63u];
        out[o++] = have2 ? tab[(v >> 6) & 63u] : '=';
        out[o++] = have3 ? tab[v & 63u] : '=';
    }
    out[o] = 0;
    return 1;
}

int mm_net_ws_compute_accept(const char * key,
                             char out[MM_NET_WS_ACCEPT_LEN]) {
    if (!key || !out) return MM_NET_WS_ERR;
    sha1_ctx_t sha;
    uint8_t digest[20];
    sha1_init(&sha);
    sha1_update(&sha, (const uint8_t *)key, strlen(key));
    sha1_update(&sha, (const uint8_t *)MM_NET_WS_GUID,
                strlen(MM_NET_WS_GUID));
    sha1_final(&sha, digest);
    if (!base64_encode(digest, sizeof(digest), out, MM_NET_WS_ACCEPT_LEN)) {
        return MM_NET_WS_ERR_BUFFER;
    }
    return MM_NET_WS_OK;
}

static int connection_has_upgrade(const char * value, size_t len) {
    while (len) {
        const char * token = value;
        size_t token_len = 0;
        while (token_len < len && value[token_len] != ',') token_len++;
        const char * trimmed = token;
        size_t trimmed_len = trim_span(&trimmed, token_len);
        if (trimmed_len == 7 &&
            ascii_strncasecmp(trimmed, "upgrade", 7) == 0) {
            return 1;
        }
        if (token_len == len) break;
        value += token_len + 1;
        len -= token_len + 1;
    }
    return 0;
}

int mm_net_ws_validate_upgrade_request(const void * request, size_t request_len,
                                       const char * expected_path,
                                       char key_out[MM_NET_WS_MAX_KEY_LEN]) {
    if (!request || request_len == 0 || !expected_path) {
        return MM_NET_WS_ERR_BAD_REQUEST;
    }
    if (key_out) key_out[0] = 0;

    const char * buf = (const char *)request;
    const char * end = buf + request_len;
    const char * line_end = NULL;
    for (const char * p = buf; p + 1 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n') {
            line_end = p;
            break;
        }
    }
    if (!line_end) return MM_NET_WS_NEED_MORE;

    const char * sp1 = memchr(buf, ' ', (size_t)(line_end - buf));
    if (!sp1 || sp1 == buf) return MM_NET_WS_ERR_BAD_REQUEST;
    const char * sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - sp1 - 1));
    if (!sp2 || sp2 == sp1 + 1) return MM_NET_WS_ERR_BAD_REQUEST;
    if ((size_t)(sp1 - buf) != 3 || memcmp(buf, "GET", 3) != 0) {
        return MM_NET_WS_ERR_BAD_REQUEST;
    }
    size_t path_len = (size_t)(sp2 - sp1 - 1);
    if (strlen(expected_path) != path_len ||
        memcmp(sp1 + 1, expected_path, path_len) != 0) {
        return MM_NET_WS_ERR_BAD_REQUEST;
    }

    int saw_upgrade = 0;
    int saw_connection = 0;
    int saw_version = 0;
    int saw_key = 0;
    char key[MM_NET_WS_MAX_KEY_LEN];
    key[0] = 0;

    const char * line = line_end + 2;
    while (line < end) {
        const char * next = NULL;
        for (const char * p = line; p + 1 < end; p++) {
            if (p[0] == '\r' && p[1] == '\n') {
                next = p;
                break;
            }
        }
        if (!next) return MM_NET_WS_NEED_MORE;
        if (next == line) break;

        const char * colon = memchr(line, ':', (size_t)(next - line));
        if (!colon) return MM_NET_WS_ERR_BAD_REQUEST;
        const char * name = line;
        size_t name_len = (size_t)(colon - line);
        const char * value = colon + 1;
        size_t value_len = (size_t)(next - value);
        value_len = trim_span(&value, value_len);

        if (name_len == 7 && ascii_strncasecmp(name, "Upgrade", 7) == 0) {
            saw_upgrade = value_len == 9 &&
                          ascii_strncasecmp(value, "websocket", 9) == 0;
        } else if (name_len == 10 &&
                   ascii_strncasecmp(name, "Connection", 10) == 0) {
            saw_connection = connection_has_upgrade(value, value_len);
        } else if (name_len == 17 &&
                   ascii_strncasecmp(name, "Sec-WebSocket-Key", 17) == 0) {
            size_t decoded_len = 0;
            if (value_len >= sizeof(key) ||
                !base64_decode_len(value, value_len, &decoded_len) ||
                decoded_len != 16) {
                return MM_NET_WS_ERR_BAD_REQUEST;
            }
            memcpy(key, value, value_len);
            key[value_len] = 0;
            saw_key = 1;
        } else if (name_len == 21 &&
                   ascii_strncasecmp(name, "Sec-WebSocket-Version", 21) == 0) {
            saw_version = value_len == 2 && value[0] == '1' && value[1] == '3';
        }
        line = next + 2;
    }

    if (!saw_upgrade || !saw_connection || !saw_key || !saw_version) {
        return MM_NET_WS_ERR_BAD_REQUEST;
    }
    if (key_out) memcpy(key_out, key, strlen(key) + 1);
    return MM_NET_WS_OK;
}

int mm_net_ws_format_upgrade_response(char * out, size_t out_len,
                                      const char * accept) {
    if (!out || !accept) return MM_NET_WS_ERR_BUFFER;
    int n = snprintf(out, out_len,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     accept);
    if (n < 0 || (size_t)n >= out_len) return MM_NET_WS_ERR_BUFFER;
    return n;
}

static int opcode_supported(uint8_t opcode) {
    return opcode == MM_NET_WS_OPCODE_TEXT ||
           opcode == MM_NET_WS_OPCODE_BINARY ||
           opcode == MM_NET_WS_OPCODE_CLOSE ||
           opcode == MM_NET_WS_OPCODE_PING ||
           opcode == MM_NET_WS_OPCODE_PONG;
}

int mm_net_ws_encode_frame(uint8_t opcode, const void * payload,
                           size_t payload_len, uint8_t * out, size_t out_len,
                           size_t * written) {
    if (written) *written = 0;
    if (!out || (payload_len && !payload) || !opcode_supported(opcode)) {
        return MM_NET_WS_ERR;
    }
    if ((opcode & 0x8u) && payload_len > 125u) return MM_NET_WS_ERR_TOO_LARGE;
    if (payload_len > 65535u) return MM_NET_WS_ERR_TOO_LARGE;

    size_t header_len = payload_len <= 125u ? 2u : 4u;
    if (out_len < header_len + payload_len) return MM_NET_WS_ERR_BUFFER;

    out[0] = 0x80u | (opcode & 0x0fu);
    if (payload_len <= 125u) {
        out[1] = (uint8_t)payload_len;
    } else {
        out[1] = 126u;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)payload_len;
    }
    if (payload_len) memcpy(out + header_len, payload, payload_len);
    if (written) *written = header_len + payload_len;
    return MM_NET_WS_OK;
}

int mm_net_ws_decode_frame(const uint8_t * in, size_t in_len,
                           size_t payload_limit, uint8_t * payload_out,
                           size_t payload_out_len, mm_net_ws_frame_t * frame,
                           size_t * consumed) {
    if (consumed) *consumed = 0;
    if (frame) memset(frame, 0, sizeof(*frame));
    if (!in) return MM_NET_WS_ERR_BAD_FRAME;
    if (in_len < 2u) return MM_NET_WS_NEED_MORE;

    uint8_t fin = (uint8_t)((in[0] >> 7) & 1u);
    uint8_t rsv = (uint8_t)(in[0] & 0x70u);
    uint8_t opcode = (uint8_t)(in[0] & 0x0fu);
    uint8_t masked = (uint8_t)((in[1] >> 7) & 1u);
    uint64_t payload_len = (uint64_t)(in[1] & 0x7fu);
    size_t pos = 2;

    if (rsv || !masked) return MM_NET_WS_ERR_BAD_FRAME;
    if (!opcode_supported(opcode) || opcode == MM_NET_WS_OPCODE_CONTINUATION) {
        return MM_NET_WS_ERR_UNSUPPORTED;
    }
    if (!fin) return MM_NET_WS_ERR_UNSUPPORTED;

    if (payload_len == 126u) {
        if (in_len < pos + 2u) return MM_NET_WS_NEED_MORE;
        payload_len = ((uint64_t)in[pos] << 8) | in[pos + 1u];
        pos += 2u;
        if (payload_len < 126u) return MM_NET_WS_ERR_BAD_FRAME;
    } else if (payload_len == 127u) {
        return MM_NET_WS_ERR_TOO_LARGE;
    }

    if ((opcode & 0x8u) && payload_len > 125u) return MM_NET_WS_ERR_BAD_FRAME;
    if (payload_len > payload_limit || payload_len > payload_out_len) {
        return MM_NET_WS_ERR_TOO_LARGE;
    }
    if (!payload_out && payload_len) return MM_NET_WS_ERR_BUFFER;
    if (in_len < pos + 4u) return MM_NET_WS_NEED_MORE;

    uint8_t mask[4];
    memcpy(mask, in + pos, sizeof(mask));
    pos += 4u;
    if (in_len < pos + (size_t)payload_len) return MM_NET_WS_NEED_MORE;

    for (size_t i = 0; i < (size_t)payload_len; i++) {
        payload_out[i] = in[pos + i] ^ mask[i & 3u];
    }
    if (frame) {
        frame->fin = fin;
        frame->opcode = opcode;
        frame->payload_len = (size_t)payload_len;
        if (opcode == MM_NET_WS_OPCODE_CLOSE && payload_len >= 2u) {
            frame->close_code =
                (uint16_t)(((uint16_t)payload_out[0] << 8) | payload_out[1]);
        }
    }
    if (consumed) *consumed = pos + (size_t)payload_len;
    return MM_NET_WS_OK;
}
