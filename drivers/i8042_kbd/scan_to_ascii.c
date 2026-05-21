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
static bool break_prefix = false; /* set 2: last byte was 0xF0 */
static bool down[2][256];
static uint64_t next_repeat_us[2][256];
static bool repeat_active = false;
static bool repeat_set2 = false;
static bool repeat_extended = false;
static uint8_t repeat_mc = 0;

#if PC386_KBD_SCANCODE_SET == 1
static int scancode_set = 1;
#elif PC386_KBD_SCANCODE_SET == 2
static int scancode_set = 2;
#else
static int scancode_set = 0;      /* auto: 0 unknown, then 1 or 2 */
#endif

static bool repeat_allowed(uint8_t mc, bool extended, bool released) {
    unsigned ext = extended ? 1u : 0u;
    if (released) {
        down[ext][mc] = false;
        next_repeat_us[ext][mc] = 0;
        if (repeat_active && repeat_extended == extended && repeat_mc == mc) {
            repeat_active = false;
        }
        return false;
    }

    uint64_t now = hal_time_us_64();
    int start_ms = pc386_keyboard_repeat_start_ms();
    int rate_ms = pc386_keyboard_repeat_rate_ms();
    if (!down[ext][mc]) {
        down[ext][mc] = true;
        next_repeat_us[ext][mc] = now + (uint64_t)start_ms * 1000ull;
        repeat_active = true;
        repeat_set2 = (scancode_set == 2);
        repeat_extended = extended;
        repeat_mc = mc;
        return true;
    }
    if (now < next_repeat_us[ext][mc]) return false;
    uint64_t interval_us = (uint64_t)rate_ms * 1000ull;
    next_repeat_us[ext][mc] += interval_us;
    if (next_repeat_us[ext][mc] <= now) next_repeat_us[ext][mc] = now + interval_us;
    repeat_active = true;
    repeat_set2 = (scancode_set == 2);
    repeat_extended = extended;
    repeat_mc = mc;
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

static const uint8_t set2_unshifted[256] = {
    [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4', [0x2E] = '5',
    [0x36] = '6', [0x3D] = '7', [0x3E] = '8', [0x46] = '9', [0x45] = '0',
    [0x4E] = '-', [0x55] = '=',
    [0x15] = 'q', [0x1D] = 'w', [0x24] = 'e', [0x2D] = 'r', [0x2C] = 't',
    [0x35] = 'y', [0x3C] = 'u', [0x43] = 'i', [0x44] = 'o', [0x4D] = 'p',
    [0x54] = '[', [0x5B] = ']',
    [0x1C] = 'a', [0x1B] = 's', [0x23] = 'd', [0x2B] = 'f', [0x34] = 'g',
    [0x33] = 'h', [0x3B] = 'j', [0x42] = 'k', [0x4B] = 'l',
    [0x4C] = ';', [0x52] = '\'', [0x0E] = '`', [0x5D] = '\\',
    [0x1A] = 'z', [0x22] = 'x', [0x21] = 'c', [0x2A] = 'v', [0x32] = 'b',
    [0x31] = 'n', [0x3A] = 'm',
    [0x41] = ',', [0x49] = '.', [0x4A] = '/',
    [0x29] = ' ',
};

static const uint8_t set2_shifted[256] = {
    [0x16] = '!', [0x1E] = '@', [0x26] = '#', [0x25] = '$', [0x2E] = '%',
    [0x36] = '^', [0x3D] = '&', [0x3E] = '*', [0x46] = '(', [0x45] = ')',
    [0x4E] = '_', [0x55] = '+',
    [0x15] = 'Q', [0x1D] = 'W', [0x24] = 'E', [0x2D] = 'R', [0x2C] = 'T',
    [0x35] = 'Y', [0x3C] = 'U', [0x43] = 'I', [0x44] = 'O', [0x4D] = 'P',
    [0x54] = '{', [0x5B] = '}',
    [0x1C] = 'A', [0x1B] = 'S', [0x23] = 'D', [0x2B] = 'F', [0x34] = 'G',
    [0x33] = 'H', [0x3B] = 'J', [0x42] = 'K', [0x4B] = 'L',
    [0x4C] = ':', [0x52] = '"', [0x0E] = '~', [0x5D] = '|',
    [0x1A] = 'Z', [0x22] = 'X', [0x21] = 'C', [0x2A] = 'V', [0x32] = 'B',
    [0x31] = 'N', [0x3A] = 'M',
    [0x41] = '<', [0x49] = '>', [0x4A] = '?',
    [0x29] = ' ',
};

static int named_key_set2(uint8_t mc, bool extended) {
    if (extended) {
        switch (mc) {
            case 0x75: return KEY_UP;
            case 0x72: return KEY_DOWN;
            case 0x6B: return KEY_LEFT;
            case 0x74: return KEY_RIGHT;
            case 0x6C: return KEY_HOME;
            case 0x69: return KEY_END;
            case 0x7D: return KEY_PUP;
            case 0x7A: return KEY_PDOWN;
            case 0x70: return KEY_INSERT;
            case 0x71: return KEY_DEL;
            case 0x5A: return KEY_ENTER;
        }
        return 0;
    }
    switch (mc) {
        case 0x76: return KEY_ESC;
        case 0x66: return KEY_BKSP;
        case 0x0D: return KEY_TAB;
        case 0x5A: return KEY_ENTER;
        case 0x05: return KEY_F1;
        case 0x06: return KEY_F1 + 1;
        case 0x04: return KEY_F1 + 2;
        case 0x0C: return KEY_F1 + 3;
        case 0x03: return KEY_F1 + 4;
        case 0x0B: return KEY_F1 + 5;
        case 0x83: return KEY_F1 + 6;
        case 0x0A: return KEY_F1 + 7;
        case 0x01: return KEY_F1 + 8;
        case 0x09: return KEY_F1 + 9;
        case 0x78: return KEY_F1 + 10;
        case 0x07: return KEY_F1 + 11;
    }
    return 0;
}

static int translate_key(bool set2, uint8_t mc, bool extended) {
    int nk = set2 ? named_key_set2(mc, extended) : named_key(mc, extended);
    if (nk) return nk;

    uint8_t base = set2 ? set2_unshifted[mc] : unshifted[mc];
    if (!base) return 0;
    bool letter = (base >= 'a' && base <= 'z');
    bool upper = shift_down ^ (caps_lock && letter);
    uint8_t ch = upper ? (set2 ? set2_shifted[mc] : shifted[mc]) : base;
    if (!ch) ch = base;
    if (ctrl_down && letter) ch = (uint8_t)(ch & 0x1F);
    return ch;
}

static int synth_repeat_key(void) {
    if (!repeat_active) return -1;
    unsigned ext = repeat_extended ? 1u : 0u;
    if (!down[ext][repeat_mc]) {
        repeat_active = false;
        return -1;
    }

    uint64_t now = hal_time_us_64();
    if (now < next_repeat_us[ext][repeat_mc]) return -1;

    int rate_ms = pc386_keyboard_repeat_rate_ms();
    uint64_t interval_us = (uint64_t)rate_ms * 1000ull;
    next_repeat_us[ext][repeat_mc] += interval_us;
    if (next_repeat_us[ext][repeat_mc] <= now) {
        next_repeat_us[ext][repeat_mc] = now + interval_us;
    }

    int key = translate_key(repeat_set2, repeat_mc, repeat_extended);
    return key ? key : -1;
}

void kbd_clear_repeat_state(void) {
    for (int ext = 0; ext < 2; ext++) {
        for (int i = 0; i < 256; i++) {
            down[ext][i] = false;
            next_repeat_us[ext][i] = 0;
        }
    }
    repeat_active = false;
    ext_prefix = false;
    break_prefix = false;
}

int kbd_get_key(void) {
    while (1) {
        int sc = kbd_get_scancode();
        if (sc < 0) return synth_repeat_key();

        if (sc == 0xE0) { ext_prefix = true; continue; }
        if (sc == 0xF0 && scancode_set != 1) {
            scancode_set = 2;
            break_prefix = true;
            continue;
        }

        if (scancode_set == 0) {
            if ((uint8_t)sc > 0x83 || set2_unshifted[(uint8_t)sc] || named_key_set2((uint8_t)sc, ext_prefix)) {
                scancode_set = 2;
            } else {
                scancode_set = 1;
            }
        }

        bool set2 = scancode_set == 2;
        bool released = set2 ? break_prefix : ((sc & 0x80) != 0);
        uint8_t mc = set2 ? (uint8_t)sc : (uint8_t)(sc & 0x7F);
        break_prefix = false;

        /* Modifier press/release. */
        if (set2 && !ext_prefix) {
            if (mc == 0x12 || mc == 0x59) { shift_down = !released; continue; }
            if (mc == 0x14)               { ctrl_down  = !released; continue; }
            if (mc == 0x11)               { alt_down   = !released; continue; }
            if (mc == 0x58 && !released)  { caps_lock  = !caps_lock; continue; }
        } else if (set2) {
            if (mc == 0x14) { ctrl_down = !released; ext_prefix = false; continue; }
            if (mc == 0x11) { alt_down  = !released; ext_prefix = false; continue; }
        } else if (!ext_prefix) {
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

        ext_prefix = false;
        int key = translate_key(set2, mc, extended);
        if (key) return key;
    }
}
