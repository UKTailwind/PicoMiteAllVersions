/*
 * drivers/web_console/web_console_input.c
 */

#include "web_console_input.h"

#include <string.h>

void web_console_input_init(web_console_input_t * input, uint8_t * buf,
                            size_t cap) {
    if (!input) return;
    input->buf = buf;
    input->cap = cap;
    input->head = 0;
    input->tail = 0;
    input->owner = 0;
    input->dropped = 0;
}

int web_console_input_acquire(web_console_input_t * input, uintptr_t owner) {
    if (!input || !owner) return 0;
    if (input->owner && input->owner != owner) return 0;
    input->owner = owner;
    return 1;
}

void web_console_input_release(web_console_input_t * input, uintptr_t owner) {
    if (!input || !owner || input->owner != owner) return;
    web_console_input_clear(input);
    input->owner = 0;
}

uintptr_t web_console_input_owner(const web_console_input_t * input) {
    return input ? input->owner : 0;
}

int web_console_input_push(web_console_input_t * input, uintptr_t owner,
                           int code) {
    if (!input || !input->buf || input->cap < 2u || owner != input->owner ||
        code < 0 || code > 0xff) {
        return 0;
    }
    size_t next = (input->head + 1u) % input->cap;
    if (next == input->tail) {
        input->tail = (input->tail + 1u) % input->cap;
        input->dropped++;
    }
    input->buf[input->head] = (uint8_t)code;
    input->head = next;
    return 1;
}

int web_console_input_pop(web_console_input_t * input, int * out_code) {
    if (!input || !input->buf || !out_code || input->head == input->tail) {
        return 0;
    }
    *out_code = input->buf[input->tail];
    input->tail = (input->tail + 1u) % input->cap;
    return 1;
}

int web_console_input_available(const web_console_input_t * input) {
    return input && input->buf && input->head != input->tail;
}

void web_console_input_clear(web_console_input_t * input) {
    if (!input) return;
    input->head = 0;
    input->tail = 0;
}

static int ascii_alpha_control(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    return -1;
}

int web_console_key_code_from_dom(const char * key, int ctrl, int alt,
                                  int meta, int shift, int * out_code) {
    if (!key || !out_code) return 0;
    if (key[0] && !key[1]) {
        if (ctrl && !alt && !meta) {
            int cc = ascii_alpha_control(key[0]);
            if (cc >= 0) {
                *out_code = cc;
                return 1;
            }
        }
        *out_code = (unsigned char)key[0];
        return 1;
    }

    struct named_key {
        const char * name;
        int code;
    };
    static const struct named_key keys[] = {
        {"Backspace", WEB_CONSOLE_KEY_BKSP},
        {"Enter", WEB_CONSOLE_KEY_ENTER},
        {"Escape", WEB_CONSOLE_KEY_ESC},
        {"Delete", WEB_CONSOLE_KEY_DEL},
        {"ArrowUp", WEB_CONSOLE_KEY_UP},
        {"ArrowDown", WEB_CONSOLE_KEY_DOWN},
        {"ArrowLeft", WEB_CONSOLE_KEY_LEFT},
        {"ArrowRight", WEB_CONSOLE_KEY_RIGHT},
        {"Insert", WEB_CONSOLE_KEY_INSERT},
        {"Home", WEB_CONSOLE_KEY_HOME},
        {"End", WEB_CONSOLE_KEY_END},
        {"PageUp", WEB_CONSOLE_KEY_PAGE_UP},
        {"PageDown", WEB_CONSOLE_KEY_PAGE_DOWN},
        {"F1", WEB_CONSOLE_KEY_F1},
        {"F2", WEB_CONSOLE_KEY_F1 + 1},
        {"F3", WEB_CONSOLE_KEY_F1 + 2},
        {"F4", WEB_CONSOLE_KEY_F1 + 3},
        {"F5", WEB_CONSOLE_KEY_F1 + 4},
        {"F6", WEB_CONSOLE_KEY_F1 + 5},
        {"F7", WEB_CONSOLE_KEY_F1 + 6},
        {"F8", WEB_CONSOLE_KEY_F1 + 7},
        {"F9", WEB_CONSOLE_KEY_F1 + 8},
        {"F10", WEB_CONSOLE_KEY_F1 + 9},
        {"F11", WEB_CONSOLE_KEY_F1 + 10},
        {"F12", WEB_CONSOLE_KEY_F12},
    };

    if (strcmp(key, "Tab") == 0) {
        *out_code = shift ? WEB_CONSOLE_KEY_SHIFT_TAB : WEB_CONSOLE_KEY_TAB;
        return 1;
    }
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (strcmp(key, keys[i].name) == 0) {
            *out_code = keys[i].code;
            return 1;
        }
    }
    return 0;
}
