/*
 * drivers/web_console/web_console_protocol.c
 *
 * Shared web-console protocol packing and text-control helpers.
 */

#include "web_console_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_dims(int width, int height) {
    return width > 0 && height > 0 && width <= 0xffff && height <= 0xffff;
}

static void put_u16_le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_i16_le(uint8_t *dst, int16_t v) {
    put_u16_le(dst, (uint16_t)v);
}

static void put_u32_le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xffu);
    dst[1] = (uint8_t)((v >> 8) & 0xffu);
    dst[2] = (uint8_t)((v >> 16) & 0xffu);
    dst[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void put_rgb24_as_rgba8(uint8_t *dst, const uint32_t *src,
                               size_t pixels) {
    for (size_t i = 0; i < pixels; ++i) {
        uint32_t c = src[i];
        dst[i * 4 + 0] = (uint8_t)((c >> 16) & 0xffu);
        dst[i * 4 + 1] = (uint8_t)((c >> 8) & 0xffu);
        dst[i * 4 + 2] = (uint8_t)(c & 0xffu);
        dst[i * 4 + 3] = 0xffu;
    }
}

static size_t pixel_count_for_dims(int width, int height) {
    if (!valid_dims(width, height)) return 0;
    return (size_t)width * (size_t)height;
}

static void pack_frame_header(uint8_t *dst, const char magic[4],
                              int width, int height) {
    memcpy(dst, magic, 4);
    put_u16_le(dst + 4, (uint16_t)width);
    put_u16_le(dst + 6, (uint16_t)height);
}

size_t web_console_frmb_len(int width, int height) {
    size_t pixels = pixel_count_for_dims(width, height);
    if (!pixels) return 0;
    if (pixels > (SIZE_MAX - WEB_CONSOLE_FRAME_HEADER_LEN) / 4u) return 0;
    return WEB_CONSOLE_FRAME_HEADER_LEN + pixels * 4u;
}

size_t web_console_cmds_len(size_t command_len) {
    if (command_len > SIZE_MAX - WEB_CONSOLE_FRAME_HEADER_LEN) return 0;
    return WEB_CONSOLE_FRAME_HEADER_LEN + command_len;
}

size_t web_console_pack_frmb(uint8_t *dst, size_t dst_len,
                             int width, int height,
                             const uint32_t *rgb24_pixels,
                             size_t pixel_count) {
    size_t pixels = pixel_count_for_dims(width, height);
    size_t need = web_console_frmb_len(width, height);
    if (!dst || !rgb24_pixels || !pixels || pixel_count < pixels ||
        !need || dst_len < need) {
        return 0;
    }
    pack_frame_header(dst, "FRMB", width, height);
    put_rgb24_as_rgba8(dst + WEB_CONSOLE_FRAME_HEADER_LEN,
                       rgb24_pixels, pixels);
    return need;
}

size_t web_console_pack_cmds(uint8_t *dst, size_t dst_len,
                             int width, int height,
                             const uint8_t *commands,
                             size_t command_len) {
    size_t need = web_console_cmds_len(command_len);
    if (!dst || !valid_dims(width, height) || !need || dst_len < need ||
        (command_len && !commands)) {
        return 0;
    }
    pack_frame_header(dst, "CMDS", width, height);
    if (command_len) {
        memcpy(dst + WEB_CONSOLE_FRAME_HEADER_LEN, commands, command_len);
    }
    return need;
}

size_t web_console_pack_cmd_cls(uint8_t *dst, size_t dst_len, int colour) {
    if (!dst || dst_len < 5u) return 0;
    dst[0] = WEB_CONSOLE_OP_CLS;
    put_u32_le(dst + 1, (uint32_t)colour & 0x00ffffffu);
    return 5u;
}

size_t web_console_pack_cmd_rect(uint8_t *dst, size_t dst_len,
                                 int x1, int y1, int x2, int y2,
                                 int colour) {
    if (!dst || dst_len < 13u) return 0;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    dst[0] = WEB_CONSOLE_OP_RECT;
    put_i16_le(dst + 1, (int16_t)x1);
    put_i16_le(dst + 3, (int16_t)y1);
    put_u16_le(dst + 5, (uint16_t)(x2 - x1 + 1));
    put_u16_le(dst + 7, (uint16_t)(y2 - y1 + 1));
    put_u32_le(dst + 9, (uint32_t)colour & 0x00ffffffu);
    return 13u;
}

size_t web_console_pack_cmd_pixel(uint8_t *dst, size_t dst_len,
                                  int x, int y, int colour) {
    if (!dst || dst_len < 9u) return 0;
    dst[0] = WEB_CONSOLE_OP_PIXEL;
    put_i16_le(dst + 1, (int16_t)x);
    put_i16_le(dst + 3, (int16_t)y);
    put_u32_le(dst + 5, (uint32_t)colour & 0x00ffffffu);
    return 9u;
}

size_t web_console_pack_cmd_scroll(uint8_t *dst, size_t dst_len,
                                   int lines, int bg) {
    if (!dst || dst_len < 7u) return 0;
    dst[0] = WEB_CONSOLE_OP_SCROLL;
    put_i16_le(dst + 1, (int16_t)lines);
    put_u32_le(dst + 3, (uint32_t)bg & 0x00ffffffu);
    return 7u;
}

size_t web_console_pack_cmd_blit(uint8_t *dst, size_t dst_len,
                                 int x, int y, int width, int height,
                                 const uint32_t *rgb24_pixels) {
    size_t pixels = pixel_count_for_dims(width, height);
    if (!dst || !rgb24_pixels || !pixels ||
        pixels > (SIZE_MAX - 9u) / 4u) {
        return 0;
    }
    size_t need = 9u + pixels * 4u;
    if (dst_len < need) return 0;

    dst[0] = WEB_CONSOLE_OP_BLIT;
    put_i16_le(dst + 1, (int16_t)x);
    put_i16_le(dst + 3, (int16_t)y);
    put_u16_le(dst + 5, (uint16_t)width);
    put_u16_le(dst + 7, (uint16_t)height);
    put_rgb24_as_rgba8(dst + 9, rgb24_pixels, pixels);
    return need;
}

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p;
}

static int parse_json_string(const char **pp, const char *end,
                             char *out, size_t out_len) {
    const char *p = skip_ws(*pp, end);
    size_t n = 0;
    if (p >= end || *p != '"' || out_len == 0) return 0;
    ++p;
    while (p < end && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (p >= end) return 0;
        }
        if (n + 1 < out_len) out[n++] = *p;
        ++p;
    }
    if (p >= end || *p != '"') return 0;
    out[n] = '\0';
    *pp = p + 1;
    return 1;
}

static int parse_json_long(const char **pp, const char *end, long *out) {
    const char *p = skip_ws(*pp, end);
    int neg = 0;
    long v = 0;
    if (p < end && *p == '-') {
        neg = 1;
        ++p;
    }
    if (p >= end || !isdigit((unsigned char)*p)) return 0;
    while (p < end && isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        ++p;
    }
    *out = neg ? -v : v;
    *pp = p;
    return 1;
}

int web_console_parse_key_json(const char *text, size_t len, int *out_code) {
    if (!text || !out_code) return 0;
    const char *p = text;
    const char *end = text + len;
    int saw_op = 0;
    int saw_key_op = 0;
    int saw_code = 0;
    long code = -1;

    p = skip_ws(p, end);
    if (p >= end || *p != '{') return 0;
    ++p;

    for (;;) {
        char key[16];
        p = skip_ws(p, end);
        if (p < end && *p == '}') {
            ++p;
            break;
        }
        if (!parse_json_string(&p, end, key, sizeof(key))) return 0;
        p = skip_ws(p, end);
        if (p >= end || *p != ':') return 0;
        ++p;

        if (strcmp(key, "op") == 0) {
            char op[16];
            if (!parse_json_string(&p, end, op, sizeof(op))) return 0;
            saw_op = 1;
            saw_key_op = strcmp(op, "key") == 0;
        } else if (strcmp(key, "code") == 0) {
            if (!parse_json_long(&p, end, &code)) return 0;
            saw_code = 1;
        } else {
            p = skip_ws(p, end);
            if (p < end && *p == '"') {
                char ignored[2];
                if (!parse_json_string(&p, end, ignored, sizeof(ignored))) {
                    return 0;
                }
            } else {
                long ignored_num;
                if (!parse_json_long(&p, end, &ignored_num)) return 0;
            }
        }

        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            ++p;
            continue;
        }
        if (p < end && *p == '}') {
            ++p;
            break;
        }
        return 0;
    }

    p = skip_ws(p, end);
    if (p != end) return 0;
    if (!saw_code || (saw_op && !saw_key_op) || code < 0 || code > 0xff) {
        return 0;
    }
    *out_code = (int)code;
    return 1;
}

int web_console_audio_build_tone(char *dst, size_t dst_len,
                                 double left_hz, double right_hz,
                                 int has_duration,
                                 long long duration_ms) {
    if (!dst || dst_len == 0) return -1;
    int n;
    if (has_duration) {
        n = snprintf(dst, dst_len,
                     "{\"op\":\"tone\",\"l\":%.6g,\"r\":%.6g,\"ms\":%lld}",
                     left_hz, right_hz, duration_ms);
    } else {
        n = snprintf(dst, dst_len,
                     "{\"op\":\"tone\",\"l\":%.6g,\"r\":%.6g}",
                     left_hz, right_hz);
    }
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}

int web_console_audio_build_stop(char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return -1;
    int n = snprintf(dst, dst_len, "{\"op\":\"stop\"}");
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}

int web_console_audio_build_sound(char *dst, size_t dst_len,
                                  int slot, const char *ch,
                                  const char *type, double freq_hz,
                                  int volume) {
    if (!dst || dst_len == 0) return -1;
    int n = snprintf(dst, dst_len,
                     "{\"op\":\"sound\",\"slot\":%d,\"ch\":\"%s\",\"type\":\"%s\","
                     "\"f\":%.6g,\"vol\":%d}",
                     slot,
                     ch ? ch : "B",
                     type ? type : "O",
                     freq_hz,
                     volume);
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}

int web_console_audio_build_volume(char *dst, size_t dst_len,
                                   int left, int right) {
    if (!dst || dst_len == 0) return -1;
    int n = snprintf(dst, dst_len,
                     "{\"op\":\"volume\",\"l\":%d,\"r\":%d}",
                     left, right);
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}

int web_console_audio_build_pause(char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return -1;
    int n = snprintf(dst, dst_len, "{\"op\":\"pause\"}");
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}

int web_console_audio_build_resume(char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return -1;
    int n = snprintf(dst, dst_len, "{\"op\":\"resume\"}");
    return (n >= 0 && (size_t)n < dst_len) ? n : -1;
}
