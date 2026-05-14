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

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

#include "esp32_tcp_server.h"
#include "esp32_telnet.h"
#include "hal/hal_time.h"

/* Provided by esp32_console.c. */
extern void esp32_console_write_bytes(const char *text, int len);
extern int  esp32_console_read_byte_nonblock(void);
extern int  esp32_console_read_byte_blocking_ms(int ms);
extern void esp32_console_push_back_byte(int c);

volatile int esp32_console_display_rendering;

void port_display_render_begin(void) {
    esp32_console_display_rendering++;
}

void port_display_render_end(void) {
    if (esp32_console_display_rendering > 0) esp32_console_display_rendering--;
}

int port_editor_vt100_enabled(void) {
    return !esp32_web_console_connected();
}

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
    /* Explicit-flush requests still force a telnet drain. The catch-all
     * for non-flush bursts is the 5 ms timer in esp32_telnet_poll, which
     * now fires from any ProcessWeb call (including MMInkey's idle
     * polls), so tail bytes from MMPrintString / editor redraws land
     * within 5 ms without per-byte TCP sends. */
    if (flush) esp32_telnet_putc(0, -1);
    return c;
}

void putConsole(int c, int flush) {
    if (OptionConsole & 2) {
        DisplayPutC((char)c);
    }
    if (OptionConsole & 1) SerialConsolePutC((char)c, flush);
}

char MMputchar(char c, int flush) {
    putConsole(c, flush);
    if (isprint((unsigned char)c)) MMCharPos++;
    if (c == '\r') MMCharPos = 1;
    return c;
}

/* Bulk-string printers don't force a per-call telnet drain anymore.
 * Each fragment used to fire its own TCP send (cmd_files calls
 * MMPrintString ~10 times per file -> ~10 small packets per row);
 * coalescing them gives FILES/LIST close to USB throughput. The 5 ms
 * gate in SerialConsolePutC and the 252-byte buffer-full auto-flush
 * in esp32_telnet_putc still cap tail latency to <5 ms or one full
 * buffer, whichever comes first. */
void MMPrintString(char *s) {
    while (*s) MMputchar(*s++, 0);
    fflush(stdout);
}

void SSPrintString(char *s) {
    while (*s) SerialConsolePutC(*s++, 0);
    fflush(stdout);
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
    c = esp32_web_console_pop_key();
    if (c >= 0) return c;
    return esp32_console_read_byte_nonblock();
}

int kbhitConsole(void) {
    if (ConsoleRxBufHead != ConsoleRxBufTail) return 1;
    if (esp32_web_console_input_available()) return 1;
    int c = esp32_console_read_byte_nonblock();
    if (c < 0) return 0;
    esp32_console_push_back_byte(c);
    return 1;
}

/* Unified "next byte" source: drains the telnet ring buffer first, then
 * falls back to USB stdin. Used by the ANSI escape decoder so continuation
 * bytes are read from whichever source ESC came from. */
static int esp32_console_next_byte_ms(int ms) {
    int c = esp32_console_ring_pop();
    if (c >= 0) return c;
    /* Spin briefly to let telnet poll deliver continuation bytes. The 30 ms
     * inter-byte budget is plenty; we poll every ~1 ms. */
    int waited = 0;
    while (ms < 0 || waited <= ms) {
        c = esp32_console_read_byte_blocking_ms(1);
        if (c >= 0) return c;
        c = esp32_console_ring_pop();
        if (c >= 0) return c;
        if (ms == 0) break;
        waited++;
    }
    return -1;
}

static int esp32_normalise_console_byte(int c) {
    if (c == 0x7f) return BKSP;       /* DEL: macOS/iTerm/USB-CDC and most telnet clients */
    if (c == 0x08) return BKSP;       /* BS: RFC 854 NVT default + some telnet clients */
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
    int c1 = esp32_console_next_byte_ms(30);
    if (c1 < 0) return ESC;
    if (c1 == '[') {
        int c2 = esp32_console_next_byte_ms(30);
        if (c2 < 0) return ESC;
        int direct = esp32_csi_final_key(c2);
        if (direct != ESC) return direct;
        if (c2 >= '0' && c2 <= '9') {
            int n = c2 - '0';
            int c3;
            while ((c3 = esp32_console_next_byte_ms(30)) >= 0) {
                if (c3 >= '0' && c3 <= '9') { n = n * 10 + (c3 - '0'); continue; }
                if (c3 == ';') {
                    /* Ignore modifier parameters (Shift/Ctrl/Alt) and
                     * return the base key. */
                    do {
                        c3 = esp32_console_next_byte_ms(30);
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
        int c2 = esp32_console_next_byte_ms(30);
        switch (c2) {
            case 'P': return F1;
            case 'Q': return F2;
            case 'R': return F3;
            case 'S': return F4;
        }
        return ESC;
    }
    /* ESC followed by a regular char (Alt-<key>): return ESC now and
     * preserve the char for the next read. The pushback only feeds USB; if
     * the char came from the telnet ring buffer, we lose it. That's
     * acceptable because Alt-<key> Meta sequences are not used by the
     * editor and modern telnet clients send the key code directly. */
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
    int from_web = 0;
    int c = esp32_console_ring_pop();
    if (c < 0) {
        c = esp32_web_console_pop_key();
        if (c >= 0) from_web = 1;
    }
    if (c < 0) c = esp32_console_read_byte_nonblock();
    if (c < 0) return -1;
    /* Web console delivers fully-decoded key codes; only raw byte streams
     * (USB stdin + telnet ring buffer) need escape-sequence assembly.
     * esp32_decode_escape_sequence drains continuation bytes from whichever
     * source they arrived on. */
    if (!from_web && c == 0x1b)
        return esp32_decode_escape_sequence();
    return esp32_normalise_console_byte(c);
}

int MMgetchar(void) {
    /* Same decoding as MMInkey, but blocking. The Editor's main loop
     * calls MMInkey in a poll — but other paths (PRESS-ANY-KEY prompts)
     * call MMgetchar and want a key, not a raw byte. */
    int c;
    int from_web;
    do {
        extern void ProcessWeb(int mode);
        ShowCursor(1);
        from_web = 0;
        ProcessWeb(0);
        c = esp32_console_ring_pop();
        if (c < 0) {
            c = esp32_web_console_pop_key();
            if (c >= 0) from_web = 1;
        }
        if (c < 0) c = esp32_console_read_byte_blocking_ms(1);
    } while (c < 0);
    ShowCursor(0);
    if (!from_web && c == 0x1b)
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
    char *tp;
    while (1) {
        if (filenbr != 0 && FileEOF(filenbr)) break;
        c = MMfgetc(filenbr);
        if (c <= 0) {
            if (filenbr == 0) continue;
            continue;
        }

        if (filenbr == 0) {
            tp = NULL;
            if (c == F2)  tp = "RUN";
            if (c == F3)  tp = "LIST";
            if (c == F4)  tp = "EDIT";
            if (c == F10) tp = "AUTOSAVE";
            if (c == F11) tp = "XMODEM RECEIVE";
            if (c == F12) tp = "XMODEM SEND";
            if (c == F1)  tp = (char *)Option.F1key;
            if (c == F5)  tp = (char *)Option.F5key;
            if (c == F6)  tp = (char *)Option.F6key;
            if (c == F7)  tp = (char *)Option.F7key;
            if (c == F8)  tp = (char *)Option.F8key;
            if (c == F9)  tp = (char *)Option.F9key;
            if (tp) {
                strcpy(p, tp);
                if (EchoOption) {
                    MMPrintString(tp);
                    MMPrintString("\r\n");
                }
                return;
            }
        }

        if (c == '\t') {
            do {
                if (++nbrchars > MAXSTRLEN) error("Line is too long");
                *p++ = ' ';
                if (filenbr == 0 && EchoOption) MMputchar(' ', 1);
            } while (nbrchars % 4);
            continue;
        }
        if (c == '\b') {
            if (nbrchars) {
                if (filenbr == 0 && EchoOption) MMPrintString("\b \b");
                nbrchars--;
                p--;
            }
            continue;
        }
        if (c == '\n') break;
        if (c == '\r') {
            if (filenbr == 0) {
                if (EchoOption) MMPrintString("\r\n");
                break;
            }
            continue;
        }
        if (isprint(c) && filenbr == 0 && EchoOption) MMputchar(c, 1);
        if (++nbrchars > MAXSTRLEN) error("Line is too long");
        *p++ = (char)c;
    }
    *p = 0;
}
