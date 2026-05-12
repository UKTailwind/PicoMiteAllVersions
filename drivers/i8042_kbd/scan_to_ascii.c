/*
 * drivers/i8042_kbd/scan_to_ascii.c — PS/2 set 1 → MMBasic key codes.
 *
 * QEMU's emulated PS/2 + every real PC chipset speak set 1 by default.
 * This file is layout-specific (US); a future BR/FR variant just
 * swaps the `unshifted`/`shifted` tables.
 *
 * Modifier state machine: tracks shift / ctrl / alt / caps. Press codes
 * are 0x00..0x7F; release (break) codes are press | 0x80. 0xE0 prefix
 * marks an "extended" code (arrow keys, right-side modifier variants,
 * keypad / etc.).
 *
 * Output (kbd_get_key) is either:
 *   - ASCII for printables (with shift/caps applied)
 *   - one of Hardware_Includes.h's named constants for non-printables
 *     (UP/DOWN/LEFT/RIGHT/HOME/END/INSERT/DEL/PUP/PDOWN/F1..F12/BKSP/
 *      ENTER/TAB/ESC)
 *   - -1 when no key is ready
 */

#include <stdint.h>
#include <stdbool.h>

#include "i8042_kbd.h"

extern uint64_t hal_time_us_64(void);
extern int pc386_keyboard_repeat_start_ms(void);
extern int pc386_keyboard_repeat_rate_ms(void);

/* Mirror Hardware_Includes.h's named-key codes here so this driver
 * doesn't pull MMBasic_Includes.h into a freestanding driver TU. */
#define KEY_TAB     0x09
#define KEY_BKSP    0x08
#define KEY_ENTER   0x0D
#define KEY_ESC     0x1B
#define KEY_DEL     0x7F
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_INSERT  0x84
#define KEY_HOME    0x86
#define KEY_END     0x87
#define KEY_PUP     0x88
#define KEY_PDOWN   0x89
#define KEY_F1      0x91
/* F2..F12 are F1+1, F1+2, ... up through 0x9C */

/* US-layout set-1 makecode → ASCII. Length 128 (0x00..0x7F). 0 means
 * "no printable mapping" (likely a modifier or non-printable handled
 * in the switch below). */
static const uint8_t unshifted[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const uint8_t shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' ',
};

static bool shift_down = false;
static bool ctrl_down  = false;
static bool alt_down   = false;
static bool caps_lock  = false;
static bool ext_prefix = false;   /* last byte was 0xE0 */
static bool down[2][128];
static uint64_t next_repeat_us[2][128];

static bool repeat_allowed(uint8_t mc, bool extended, bool released) {
    unsigned ext = extended ? 1u : 0u;
    if (released) {
        down[ext][mc] = false;
        next_repeat_us[ext][mc] = 0;
        return false;
    }

    uint64_t now = hal_time_us_64();
    int start_ms = pc386_keyboard_repeat_start_ms();
    int rate_ms = pc386_keyboard_repeat_rate_ms();
    if (!down[ext][mc]) {
        down[ext][mc] = true;
        next_repeat_us[ext][mc] = now + (uint64_t)start_ms * 1000ull;
        return true;
    }
    if (now < next_repeat_us[ext][mc]) return false;
    uint64_t interval_us = (uint64_t)rate_ms * 1000ull;
    next_repeat_us[ext][mc] += interval_us;
    if (next_repeat_us[ext][mc] <= now) next_repeat_us[ext][mc] = now + interval_us;
    return true;
}

/* Translate set-1 makecode (without 0x80 release bit) to a named-key
 * code, or 0 if it's a normal printable handled via the tables above. */
static int named_key(uint8_t mc, bool extended) {
    if (extended) {
        switch (mc) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x49: return KEY_PUP;
            case 0x51: return KEY_PDOWN;
            case 0x52: return KEY_INSERT;
            case 0x53: return KEY_DEL;
        }
        return 0;
    }
    switch (mc) {
        case 0x01: return KEY_ESC;
        case 0x0E: return KEY_BKSP;
        case 0x0F: return KEY_TAB;
        case 0x1C: return KEY_ENTER;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F1 + 1;
        case 0x3D: return KEY_F1 + 2;
        case 0x3E: return KEY_F1 + 3;
        case 0x3F: return KEY_F1 + 4;
        case 0x40: return KEY_F1 + 5;
        case 0x41: return KEY_F1 + 6;
        case 0x42: return KEY_F1 + 7;
        case 0x43: return KEY_F1 + 8;
        case 0x44: return KEY_F1 + 9;
        case 0x57: return KEY_F1 + 10;  /* F11 */
        case 0x58: return KEY_F1 + 11;  /* F12 */
    }
    return 0;
}

int kbd_get_key(void) {
    while (1) {
        int sc = kbd_get_scancode();
        if (sc < 0) return -1;

        if (sc == 0xE0) { ext_prefix = true; continue; }

        bool released = (sc & 0x80) != 0;
        uint8_t mc = sc & 0x7F;

        /* Modifier press/release. */
        if (!ext_prefix) {
            if (mc == 0x2A || mc == 0x36) { shift_down = !released; continue; }
            if (mc == 0x1D)               { ctrl_down  = !released; continue; }
            if (mc == 0x38)               { alt_down   = !released; continue; }
            if (mc == 0x3A && !released)  { caps_lock  = !caps_lock; continue; }
        } else {
            /* E0+1D = right ctrl, E0+38 = right alt; treat the same. */
            if (mc == 0x1D) { ctrl_down = !released; ext_prefix = false; continue; }
            if (mc == 0x38) { alt_down  = !released; ext_prefix = false; continue; }
        }

        bool extended = ext_prefix;
        if (!repeat_allowed(mc, extended, released)) { ext_prefix = false; continue; }

        int nk = named_key(mc, extended);
        ext_prefix = false;
        if (nk) return nk;

        /* Printable. */
        uint8_t base = unshifted[mc];
        if (!base) continue;   /* unmapped — unknown set-1 byte */
        bool letter = (base >= 'a' && base <= 'z');
        bool upper  = shift_down ^ (caps_lock && letter);
        uint8_t ch  = upper ? shifted[mc] : base;
        if (!ch) ch = base;    /* shifted table missing — fall back */
        if (ctrl_down && letter) ch = (uint8_t)((ch & 0x1F));   /* Ctrl-A..Ctrl-Z */
        return ch;
    }
}
