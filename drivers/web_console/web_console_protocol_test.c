/*
 * Focused native tests for web_console_protocol.c.
 */

#include "web_console_protocol.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr)                                                   \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void expect_bytes(const uint8_t * got, const uint8_t * want, size_t len,
                         const char * label) {
    if (memcmp(got, want, len) == 0) return;
    fprintf(stderr, "FAIL %s\n got :", label);
    for (size_t i = 0; i < len; ++i) fprintf(stderr, " %02x", got[i]);
    fprintf(stderr, "\n want:");
    for (size_t i = 0; i < len; ++i) fprintf(stderr, " %02x", want[i]);
    fprintf(stderr, "\n");
    failures++;
}

static void test_frames(void) {
    const uint32_t pixels[] = {0x00112233u, 0x00aabbccu};
    uint8_t buf[32];
    size_t n = web_console_pack_frmb(buf, sizeof(buf), 2, 1, pixels, 2);
    const uint8_t want[] = {
        'F',
        'R',
        'M',
        'B',
        0x02,
        0x00,
        0x01,
        0x00,
        0x11,
        0x22,
        0x33,
        0xff,
        0xaa,
        0xbb,
        0xcc,
        0xff,
    };
    EXPECT_TRUE(n == sizeof(want));
    expect_bytes(buf, want, sizeof(want), "FRMB");

    const uint8_t cmd[] = {WEB_CONSOLE_OP_CLS, 0x33, 0x22, 0x11, 0x00};
    n = web_console_pack_cmds(buf, sizeof(buf), 320, 240, cmd, sizeof(cmd));
    const uint8_t want_cmds[] = {
        'C',
        'M',
        'D',
        'S',
        0x40,
        0x01,
        0xf0,
        0x00,
        WEB_CONSOLE_OP_CLS,
        0x33,
        0x22,
        0x11,
        0x00,
    };
    EXPECT_TRUE(n == sizeof(want_cmds));
    expect_bytes(buf, want_cmds, sizeof(want_cmds), "CMDS");
}

static void test_commands(void) {
    uint8_t buf[64];
    size_t n = web_console_pack_cmd_rect(buf, sizeof(buf),
                                         5, 6, 2, 3, 0xff445566);
    const uint8_t want_rect[] = {
        WEB_CONSOLE_OP_RECT,
        0x02,
        0x00,
        0x03,
        0x00,
        0x04,
        0x00,
        0x04,
        0x00,
        0x66,
        0x55,
        0x44,
        0x00,
    };
    EXPECT_TRUE(n == sizeof(want_rect));
    expect_bytes(buf, want_rect, sizeof(want_rect), "RECT");

    n = web_console_pack_cmd_pixel(buf, sizeof(buf), -1, 2, 0x010203);
    const uint8_t want_pixel[] = {
        WEB_CONSOLE_OP_PIXEL,
        0xff,
        0xff,
        0x02,
        0x00,
        0x03,
        0x02,
        0x01,
        0x00,
    };
    EXPECT_TRUE(n == sizeof(want_pixel));
    expect_bytes(buf, want_pixel, sizeof(want_pixel), "PIXEL");

    const uint32_t pixels[] = {0x00010203u};
    n = web_console_pack_cmd_blit(buf, sizeof(buf), 7, 8, 1, 1, pixels);
    const uint8_t want_blit[] = {
        WEB_CONSOLE_OP_BLIT,
        0x07,
        0x00,
        0x08,
        0x00,
        0x01,
        0x00,
        0x01,
        0x00,
        0x01,
        0x02,
        0x03,
        0xff,
    };
    EXPECT_TRUE(n == sizeof(want_blit));
    expect_bytes(buf, want_blit, sizeof(want_blit), "BLIT");
}

static void test_key_json(void) {
    int code = -1;
    const char * key_a = "{\"op\":\"key\",\"code\":65}";
    const char * key_b = "{ \"code\" : 13, \"op\" : \"key\" }";
    const char * key_big = "{\"op\":\"key\",\"code\":256}";
    const char * key_noop = "{\"op\":\"noop\",\"code\":65}";
    EXPECT_TRUE(web_console_parse_key_json(key_a, strlen(key_a), &code));
    EXPECT_TRUE(code == 65);
    EXPECT_TRUE(web_console_parse_key_json(key_b, strlen(key_b), &code));
    EXPECT_TRUE(code == 13);
    EXPECT_TRUE(!web_console_parse_key_json(key_big, strlen(key_big), &code));
    EXPECT_TRUE(!web_console_parse_key_json(key_noop, strlen(key_noop), &code));
}

static void test_audio(void) {
    char buf[192];
    EXPECT_TRUE(web_console_audio_build_tone(buf, sizeof(buf),
                                             440.0, 220.0, 1, 500) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"tone\",\"l\":440,\"r\":220,\"ms\":500}") == 0);
    EXPECT_TRUE(web_console_audio_build_tone(buf, sizeof(buf),
                                             440.0, 440.0, 0, 0) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"tone\",\"l\":440,\"r\":440}") == 0);
    EXPECT_TRUE(web_console_audio_build_sound(buf, sizeof(buf),
                                              2, NULL, NULL, 1234.5, 25) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"sound\",\"slot\":2,\"ch\":\"B\",\"type\":\"O\",\"f\":1234.5,\"vol\":25}") == 0);
    EXPECT_TRUE(web_console_audio_build_volume(buf, sizeof(buf), 80, 70) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"volume\",\"l\":80,\"r\":70}") == 0);
    EXPECT_TRUE(web_console_audio_build_stop(buf, sizeof(buf)) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"stop\"}") == 0);
    EXPECT_TRUE(web_console_audio_build_pause(buf, sizeof(buf)) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"pause\"}") == 0);
    EXPECT_TRUE(web_console_audio_build_resume(buf, sizeof(buf)) > 0);
    EXPECT_TRUE(strcmp(buf, "{\"op\":\"resume\"}") == 0);
}

int main(void) {
    test_frames();
    test_commands();
    test_key_json();
    test_audio();
    if (failures) return 1;
    puts("web_console_protocol_test: PASS");
    return 0;
}
