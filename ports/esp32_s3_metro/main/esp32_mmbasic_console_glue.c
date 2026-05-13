/*
 * esp32_mmbasic_console_glue.c — MMBasic-facing console layer.
 *
 * Routes MMBasic's character-level I/O (MMputchar, MMPrintString,
 * MMInkey, SerialConsolePutC, MMgetline, …) through esp32_console.c's
 * lower-level esp32_console_* primitives.
 *
 * Lives in its own TU because MMBasic_Includes.h pulls in ff.h, whose
 * `DIR` typedef clashes with the POSIX <dirent.h> DIR that IDF's
 * driver/usb_serial_jtag_vfs.h drags in transitively. esp32_console.c
 * needs the IDF headers; this file needs the MMBasic ones; keeping
 * them apart sidesteps the conflict.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include <stdio.h>
#include <stdint.h>

#include "esp32_telnet.h"

/* Provided by esp32_console.c. */
extern void esp32_console_write_bytes(const char *text, int len);
extern int  esp32_console_read_byte_nonblock(void);
extern int  esp32_console_read_byte_blocking_ms(int ms);
extern void esp32_console_push_back_byte(int c);

/* MMBasic accumulates output via MMputchar / putConsole / SerialConsolePutC
 * and reads input via MMInkey / MMgetchar / getConsole / kbhitConsole.
 * No SCREEN/SERIAL split on this port — the stdio scope only has the
 * USB Serial/JTAG pipe. No line-discipline translation either: bytes go
 * through unchanged, with line endings handled by the IDF VFS settings
 * in esp32_console_init. */

char SerialConsolePutC(char c, int flush) {
    if (Option.Telnet != -1) {
        esp32_console_write_bytes(&c, 1);
        if (flush) fflush(stdout);
    }
    esp32_telnet_putc(c, flush);
    if (flush) esp32_telnet_putc(0, -1);
    return c;
}

void putConsole(int c, int flush) {
    SerialConsolePutC((char)c, flush);
}

char MMputchar(char c, int flush) {
    putConsole(c, flush);
    return c;
}

void MMPrintString(char *s) {
    while (*s) MMputchar(*s++, 0);
    fflush(stdout);
    esp32_telnet_putc(0, -1);
}

void SSPrintString(char *s) {
    /* Bypass-the-options variant — same target on this port. */
    MMPrintString(s);
}

void myprintf(char *s) { MMPrintString(s); }

static int esp32_console_ring_pop(void) {
    if (ConsoleRxBufHead == ConsoleRxBufTail) return -1;
    int c = (unsigned char)ConsoleRxBuf[ConsoleRxBufTail];
    ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
    return c;
}

int getConsole(void) {
    int c = esp32_console_ring_pop();
    if (c >= 0) return c;
    return esp32_console_read_byte_nonblock();
}

int kbhitConsole(void) {
    if (ConsoleRxBufHead != ConsoleRxBufTail) return 1;
    int c = esp32_console_read_byte_nonblock();
    if (c < 0) return 0;
    esp32_console_push_back_byte(c);
    return 1;
}

static int esp32_normalise_console_byte(int c) {
    if (c == 0x7f) return BKSP;       /* macOS/iTerm/USB-CDC backspace */
    if (c == '\n') return ENTER;      /* normalise LF to CR for MMBasic */
    return c;
}

static int esp32_csi_tilde_key(int n) {
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
        default: return ESC;
    }
}

static int esp32_csi_final_key(int final) {
    switch (final) {
        case 'A': return UP;
        case 'B': return DOWN;
        case 'C': return RIGHT;
        case 'D': return LEFT;
        case 'H': return HOME;
        case 'F': return END;
        case 'Z': return SHIFT_TAB;
        default:  return ESC;
    }
}

/* ANSI escape decoder: ESC was just consumed; read the rest of the
 * sequence with a 30 ms inter-byte timeout and return the synthesised
 * MMBasic key code. Standalone ESC (timeout) returns ESC.
 *
 * Supports the common forms:
 *   ESC [ A/B/C/D/H/F
 *   ESC [ Z
 *   ESC [ n ~
 *   ESC [ n ; modifier final
 *   ESC O P/Q/R/S
 */
static int esp32_decode_escape_sequence(void) {
    int c1 = esp32_console_read_byte_blocking_ms(30);
    if (c1 < 0) return ESC;
    if (c1 == '[') {
        int c2 = esp32_console_read_byte_blocking_ms(30);
        if (c2 < 0) return ESC;
        int direct = esp32_csi_final_key(c2);
        if (direct != ESC) return direct;
        if (c2 >= '0' && c2 <= '9') {
            int n = c2 - '0';
            int c3;
            while ((c3 = esp32_console_read_byte_blocking_ms(30)) >= 0) {
                if (c3 >= '0' && c3 <= '9') { n = n * 10 + (c3 - '0'); continue; }
                if (c3 == ';') {
                    /* Ignore modifier parameters (Shift/Ctrl/Alt) and
                     * return the base key. */
                    do {
                        c3 = esp32_console_read_byte_blocking_ms(30);
                    } while (c3 >= '0' && c3 <= '9');
                }
                break;
            }
            if (c3 == '~') return esp32_csi_tilde_key(n);
            direct = esp32_csi_final_key(c3);
            if (direct != ESC) return direct;
        }
        return ESC;
    }
    if (c1 == 'O') {
        int c2 = esp32_console_read_byte_blocking_ms(30);
        switch (c2) {
            case 'P': return F1;
            case 'Q': return F2;
            case 'R': return F3;
            case 'S': return F4;
        }
        return ESC;
    }
    /* ESC followed by a regular char (Alt-<key>): return ESC now and
     * preserve the char for the next read. */
    esp32_console_push_back_byte(c1);
    return ESC;
}

int MMInkey(void) {
    static int in_web_poll;
    extern void ProcessWeb(int mode);
    if (!in_web_poll) {
        in_web_poll = 1;
        ProcessWeb(0);
        in_web_poll = 0;
    }
    int c = esp32_console_ring_pop();
    if (c < 0) c = esp32_console_read_byte_nonblock();
    if (c < 0) return -1;
    if (c == 0x1b && ConsoleRxBufHead == ConsoleRxBufTail)
        return esp32_decode_escape_sequence();
    return esp32_normalise_console_byte(c);
}

int MMgetchar(void) {
    /* Same decoding as MMInkey, but blocking. The Editor's main loop
     * calls MMInkey in a poll — but other paths (PRESS-ANY-KEY prompts)
     * call MMgetchar and want a key, not a raw byte. */
    int c;
    do {
        extern void ProcessWeb(int mode);
        ProcessWeb(0);
        c = esp32_console_ring_pop();
        if (c < 0) c = esp32_console_read_byte_blocking_ms(1);
    } while (c < 0);
    if (c == 0x1b && ConsoleRxBufHead == ConsoleRxBufTail)
        return esp32_decode_escape_sequence();
    return esp32_normalise_console_byte(c);
}

/* Console RX/TX rings — Pico ports drive these from the UART IRQ. ESP32
 * USB Serial/JTAG goes through esp32_console_init's VFS routing instead,
 * so the rings stay unused. The buffer + head/tail symbols still need to
 * exist because core code reads them as state. CONSOLE_RX_BUF_SIZE comes
 * from configuration.h via HAL_PORT_CONSOLE_RX_BUF_SIZE; TX has its own
 * fixed CONSOLE_TX_BUF_SIZE. */
volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE] = {0};
volatile int  ConsoleRxBufHead = 0;
volatile int  ConsoleRxBufTail = 0;
volatile int  ConsoleTxBufHead = 0;
volatile int  ConsoleTxBufTail = 0;

/* MMgetline — read a line into p, stripping CR / expanding TAB / honouring
 * MAXSTRLEN. Body lifted from host_runtime.c. Routes through FileIO's
 * MMfgetc / FileEOF, which work via the FileIO + esp32_lfs stack. */
extern int  MMfgetc(int fnbr);
extern int  FileEOF(int fnbr);
void MMgetline(int filenbr, char *p) {
    int c;
    int nbrchars = 0;
    while (1) {
        if (filenbr != 0 && FileEOF(filenbr)) break;
        c = MMfgetc(filenbr);
        if (c <= 0) {
            if (filenbr == 0) break;
            continue;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        if (c == '\t') {
            do {
                if (++nbrchars > MAXSTRLEN) error("Line is too long");
                *p++ = ' ';
            } while (nbrchars % 4);
            continue;
        }
        if (++nbrchars > MAXSTRLEN) error("Line is too long");
        *p++ = (char)c;
    }
    *p = 0;
}
