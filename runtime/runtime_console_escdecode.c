/*
 * runtime/runtime_console_escdecode.c — shared ANSI/VT100 escape-sequence
 * decoder used by every port's MMInkey.
 *
 * Background: prior to consolidation, four separate decoders lived in
 * pico_console.c, esp32_mmbasic_console_glue.c, runtime_console.c, and
 * pc386_runtime.c (which had no decoder at all). Each supported a
 * different subset of sequences:
 *
 *   - pico:  matched legacy PicoMite.c exactly (F1-F12 + Shift-F3..F10 +
 *            HOME/INS/DEL/END/PUP/PDN + arrows)
 *   - esp32: legacy + ESC[Z → SHIFT_TAB + modifier-parameter skip
 *            (ESC[n;m~ and ESC[n;mA-Z)
 *   - host:  arrows + base CSI ~ forms + xterm ESC[H/F; lost ESC O T → F5,
 *            lost alt-form F1-F4 (ESC[11~..14~), lost all Shift-F*
 *   - pc386: no escape handling — arrow keys never worked over serial
 *
 * This file is the union of all three feature sets:
 *   - everything legacy MMInkey supported (F1-F12, arrows, navigation,
 *     Shift-F3..F10 via both ESC[25~..34~ and ESC O 2 R)
 *   - xterm extras (ESC[H → HOME, ESC[F → END)
 *   - ESP32 extras (ESC[Z → SHIFT_TAB, modifier-parameter skip)
 *
 * Each port supplies a one-arg byte-reader callback with a per-call
 * timeout. The decoder returns the decoded MMBasic key code, or ESC
 * (0x1B) if no recognised sequence matched within the timeout.
 *
 * See docs/port-duplication-audit.md Finding 2 for the drift analysis
 * this resolves.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "runtime/runtime_console_escdecode.h"

/* Pushback ring for unrecognised escape sequences. Legacy MMInkey
 * pushed back up to 4 chars (c1..c4) so subsequent calls would return
 * them one at a time. The shared decoder owns this ring; each port's
 * MMInkey drains it via mmbasic_escdecode_pop_pushback() before
 * consulting its own input source. */
#define ESCDECODE_PUSHBACK_SIZE 4
static int g_pushback[ESCDECODE_PUSHBACK_SIZE];
static unsigned g_pushback_head;
static unsigned g_pushback_tail;

static void push_back(int c) {
    unsigned next = (g_pushback_head + 1) & (ESCDECODE_PUSHBACK_SIZE - 1);
    if (next == g_pushback_tail) return;  /* ring full — drop */
    g_pushback[g_pushback_head] = c;
    g_pushback_head = next;
}

int mmbasic_escdecode_pop_pushback(void) {
    if (g_pushback_head == g_pushback_tail) return -1;
    int c = g_pushback[g_pushback_tail];
    g_pushback_tail = (g_pushback_tail + 1) & (ESCDECODE_PUSHBACK_SIZE - 1);
    return c;
}

int mmbasic_console_normalise_byte(int c) {
    if (c == 0x7f) return BKSP;
    if (c == '\n') return ENTER;
    return c;
}

int mmbasic_escdecode_run(int (*read_byte_ms)(int timeout_ms)) {
    int c1 = read_byte_ms(30);
    if (c1 < 0) return ESC;

    /* ESC O <letter> — SS3 sequences, used by some terminals for F1-F5
     * and by xterm for the application-mode arrow keys. Legacy MMInkey
     * also recognised the `ESC O 2 R` quirk that maps to SHIFT_F3. */
    if (c1 == 'O') {
        int c2 = read_byte_ms(50);
        if (c2 < 0) { push_back('O'); return ESC; }
        switch (c2) {
            case 'P': return F1;
            case 'Q': return F2;
            case 'R': return F3;
            case 'S': return F4;
            case 'T': return F5;
        }
        if (c2 == '2') {
            int c3 = read_byte_ms(70);
            if (c3 == 'R') return F3 + 0x20;     /* SHIFT_F3 */
            push_back('O'); push_back(c2); if (c3 >= 0) push_back(c3);
            return ESC;
        }
        push_back('O'); push_back(c2);
        return ESC;
    }

    /* Anything other than ESC [ is unrecognised — push back the byte. */
    if (c1 != '[') {
        push_back(c1);
        return ESC;
    }

    /* ESC [ <something>. Single-letter terminators come first. */
    int c2 = read_byte_ms(50);
    if (c2 < 0) { push_back('['); return ESC; }
    switch (c2) {
        case 'A': return UP;
        case 'B': return DOWN;
        case 'C': return RIGHT;
        case 'D': return LEFT;
        case 'H': return HOME;                   /* xterm */
        case 'F': return END;                    /* xterm */
        case 'Z': return SHIFT_TAB;              /* esp32 / xterm extension */
    }

    /* Numeric parameter forms: ESC [ <n>[~|;<m>{~|letter}] */
    if (c2 < '0' || c2 > '9') {
        push_back('['); push_back(c2);
        return ESC;
    }
    int n = c2 - '0';
    int c3;
    while ((c3 = read_byte_ms(70)) >= 0 && c3 >= '0' && c3 <= '9') {
        n = n * 10 + (c3 - '0');
    }
    /* Modifier-parameter form (esp32-added): ESC [ <n> ; <m> {~|letter}.
     * Skip the modifier digits; the base key value is what matters. */
    if (c3 == ';') {
        while ((c3 = read_byte_ms(70)) >= 0 && c3 >= '0' && c3 <= '9');
    }
    /* Letter terminator with modifier — base key only. */
    switch (c3) {
        case 'A': return UP;
        case 'B': return DOWN;
        case 'C': return RIGHT;
        case 'D': return LEFT;
        case 'H': return HOME;
        case 'F': return END;
    }
    /* Tilde terminator — legacy `n ~` form, possibly with modifier skipped. */
    if (c3 == '~') {
        switch (n) {
            case 1:  return HOME;
            case 2:  return INSERT;
            case 3:  return DEL;
            case 4:  return END;
            case 5:  return PUP;
            case 6:  return PDOWN;
            case 11: return F1;
            case 12: return F2;
            case 13: return F3;
            case 14: return F4;
            case 15: return F5;
            case 17: return F6;
            case 18: return F7;
            case 19: return F8;
            case 20: return F9;
            case 21: return F10;
            case 23: return F11;
            case 24: return F12;
            case 25: return F3 + 0x20;       /* SHIFT_F3 */
            case 26: return F4 + 0x20;       /* SHIFT_F4 */
            case 28: return F5 + 0x20;       /* SHIFT_F5 */
            case 29: return F6 + 0x20;       /* SHIFT_F6 */
            case 31: return F7 + 0x20;       /* SHIFT_F7 */
            case 32: return F8 + 0x20;       /* SHIFT_F8 */
            case 33: return F9 + 0x20;       /* SHIFT_F9 */
            case 34: return F10 + 0x20;      /* SHIFT_F10 */
        }
    }
    return ESC;
}
