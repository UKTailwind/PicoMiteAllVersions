/*
 * drivers/web_console/web_console_input.h
 *
 * Target-clean key-code constants, DOM key mapping helpers, and a small
 * single-owner input queue for browser-backed consoles.
 */

#ifndef WEB_CONSOLE_INPUT_H
#define WEB_CONSOLE_INPUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_CONSOLE_KEY_TAB 0x09
#define WEB_CONSOLE_KEY_BKSP 0x08
#define WEB_CONSOLE_KEY_ENTER 0x0d
#define WEB_CONSOLE_KEY_ESC 0x1b
#define WEB_CONSOLE_KEY_DEL 0x7f
#define WEB_CONSOLE_KEY_UP 0x80
#define WEB_CONSOLE_KEY_DOWN 0x81
#define WEB_CONSOLE_KEY_LEFT 0x82
#define WEB_CONSOLE_KEY_RIGHT 0x83
#define WEB_CONSOLE_KEY_INSERT 0x84
#define WEB_CONSOLE_KEY_HOME 0x86
#define WEB_CONSOLE_KEY_END 0x87
#define WEB_CONSOLE_KEY_PAGE_UP 0x88
#define WEB_CONSOLE_KEY_PAGE_DOWN 0x89
#define WEB_CONSOLE_KEY_F1 0x91
#define WEB_CONSOLE_KEY_F12 0x9c
#define WEB_CONSOLE_KEY_SHIFT_TAB 0x9f

typedef struct {
    uint8_t * buf;
    size_t cap;
    size_t head;
    size_t tail;
    uintptr_t owner;
    unsigned dropped;
} web_console_input_t;

void web_console_input_init(web_console_input_t * input, uint8_t * buf,
                            size_t cap);
int web_console_input_acquire(web_console_input_t * input, uintptr_t owner);
void web_console_input_release(web_console_input_t * input, uintptr_t owner);
uintptr_t web_console_input_owner(const web_console_input_t * input);
int web_console_input_push(web_console_input_t * input, uintptr_t owner,
                           int code);
int web_console_input_pop(web_console_input_t * input, int * out_code);
int web_console_input_available(const web_console_input_t * input);
void web_console_input_clear(web_console_input_t * input);

int web_console_key_code_from_dom(const char * key, int ctrl, int alt,
                                  int meta, int shift, int * out_code);

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_INPUT_H */
