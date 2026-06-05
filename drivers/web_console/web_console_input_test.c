/*
 * Focused native tests for web_console_input.c.
 */

#include "web_console_input.h"

#include <stdio.h>

static int failures;

#define EXPECT_TRUE(expr)                                                   \
    do {                                                                    \
        if (!(expr)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void test_owner_queue(void) {
    uint8_t buf[4];
    web_console_input_t input;
    int code = -1;

    web_console_input_init(&input, buf, sizeof buf);
    EXPECT_TRUE(web_console_input_acquire(&input, 0x11u));
    EXPECT_TRUE(!web_console_input_acquire(&input, 0x22u));
    EXPECT_TRUE(web_console_input_push(&input, 0x11u, 'A'));
    EXPECT_TRUE(!web_console_input_push(&input, 0x22u, 'B'));
    EXPECT_TRUE(web_console_input_pop(&input, &code));
    EXPECT_TRUE(code == 'A');
    EXPECT_TRUE(!web_console_input_pop(&input, &code));

    EXPECT_TRUE(web_console_input_push(&input, 0x11u, '1'));
    EXPECT_TRUE(web_console_input_push(&input, 0x11u, '2'));
    EXPECT_TRUE(web_console_input_push(&input, 0x11u, '3'));
    EXPECT_TRUE(web_console_input_push(&input, 0x11u, '4'));
    EXPECT_TRUE(input.dropped == 1);
    EXPECT_TRUE(web_console_input_pop(&input, &code));
    EXPECT_TRUE(code == '2');

    web_console_input_release(&input, 0x11u);
    EXPECT_TRUE(web_console_input_owner(&input) == 0);
    EXPECT_TRUE(!web_console_input_available(&input));
}

static void expect_key(const char * key, int ctrl, int alt, int meta,
                       int shift, int want) {
    int got = -1;
    EXPECT_TRUE(web_console_key_code_from_dom(key, ctrl, alt, meta, shift,
                                              &got));
    EXPECT_TRUE(got == want);
}

static void test_dom_mapping(void) {
    int got = -1;
    expect_key("A", 0, 0, 0, 0, 'A');
    expect_key("c", 1, 0, 0, 0, 0x03);
    expect_key("l", 1, 1, 0, 0, 'l');
    expect_key("Enter", 0, 0, 0, 0, WEB_CONSOLE_KEY_ENTER);
    expect_key("Backspace", 0, 0, 0, 0, WEB_CONSOLE_KEY_BKSP);
    expect_key("Tab", 0, 0, 0, 0, WEB_CONSOLE_KEY_TAB);
    expect_key("Tab", 0, 0, 0, 1, WEB_CONSOLE_KEY_SHIFT_TAB);
    expect_key("Escape", 0, 0, 0, 0, WEB_CONSOLE_KEY_ESC);
    expect_key("ArrowUp", 0, 0, 0, 0, WEB_CONSOLE_KEY_UP);
    expect_key("ArrowDown", 0, 0, 0, 0, WEB_CONSOLE_KEY_DOWN);
    expect_key("ArrowLeft", 0, 0, 0, 0, WEB_CONSOLE_KEY_LEFT);
    expect_key("ArrowRight", 0, 0, 0, 0, WEB_CONSOLE_KEY_RIGHT);
    expect_key("Home", 0, 0, 0, 0, WEB_CONSOLE_KEY_HOME);
    expect_key("End", 0, 0, 0, 0, WEB_CONSOLE_KEY_END);
    expect_key("PageUp", 0, 0, 0, 0, WEB_CONSOLE_KEY_PAGE_UP);
    expect_key("PageDown", 0, 0, 0, 0, WEB_CONSOLE_KEY_PAGE_DOWN);
    expect_key("Insert", 0, 0, 0, 0, WEB_CONSOLE_KEY_INSERT);
    expect_key("Delete", 0, 0, 0, 0, WEB_CONSOLE_KEY_DEL);
    expect_key("F1", 0, 0, 0, 0, WEB_CONSOLE_KEY_F1);
    expect_key("F12", 0, 0, 0, 0, WEB_CONSOLE_KEY_F12);
    EXPECT_TRUE(!web_console_key_code_from_dom("Meta", 0, 0, 0, 0, &got));
}

int main(void) {
    test_owner_queue();
    test_dom_mapping();
    if (failures) return 1;
    puts("web_console_input_test: PASS");
    return 0;
}
