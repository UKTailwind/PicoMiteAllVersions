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
#include "runtime/runtime_console_escdecode.h"
#include "shared/audio/audio_runtime.h"

/* Provided by esp32_console.c. */
extern void esp32_console_write_bytes(const char * text, int len);
extern int esp32_console_read_byte_nonblock(void);
extern int esp32_console_read_byte_blocking_ms(int ms);
extern void esp32_console_push_back_byte(int c);
extern int esp32_usb_keyboard_pop_key(void);
extern int esp32_usb_keyboard_input_available(void);
extern int esp32_usb_role_is_serial(void);

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
    if (Option.Telnet != -1 && esp32_usb_role_is_serial()) {
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

// MMputchar lives in runtime/runtime_console_putchar.c — shared across every port.
// MMPrintString / SSPrintString live in runtime/runtime_console_printstring.c.
// Bulk-string printers no longer force a per-call telnet drain on ESP32:
// the 5 ms gate in SerialConsolePutC + the 252-byte buffer-full auto-flush
// in esp32_telnet_putc cap tail latency to <5 ms or one full buffer.
// That batching is preserved because the shared printstring TU only drains
// telnet at end-of-bulk when console_adapter->telnet_putc is plugged —
// ESP32 deliberately leaves it NULL.

void myprintf(char * s) {
    MMPrintString(s);
}

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
    c = esp32_usb_keyboard_pop_key();
    if (c >= 0) return c;
    return esp32_console_read_byte_nonblock();
}

int kbhitConsole(void) {
    if (ConsoleRxBufHead != ConsoleRxBufTail) return 1;
    if (esp32_web_console_input_available()) return 1;
    if (esp32_usb_keyboard_input_available()) return 1;
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

/* Byte normalisation lives in runtime/runtime_console_escdecode.c
 * (mmbasic_console_normalise_byte) — shared across every port. */

/* Escape decoder lives in runtime/runtime_console_escdecode.c — shared
 * across every port. esp32 supplies the byte-reader wrapper. */
static int esp32_escdecode_read_byte_ms(int timeout_ms) {
    return esp32_console_next_byte_ms(timeout_ms);
}

int MMInkey(void) {
    static int in_web_poll;
    extern void ProcessWeb(int mode);
    if (!in_web_poll) {
        in_web_poll = 1;
        ProcessWeb(0);
        /* Keep file playback progressing while the prompt polls for input. */
        audio_runtime_service();
        in_web_poll = 0;
    }
    /* Drain any chars left over from an earlier unrecognised escape
     * sequence before consulting the input source. */
    {
        int pb = mmbasic_escdecode_pop_pushback();
        if (pb >= 0) return pb;
    }
    int from_web = 0;
    int c = esp32_console_ring_pop();
    if (c < 0) {
        c = esp32_web_console_pop_key();
        if (c >= 0) from_web = 1;
    }
    int from_keyboard = 0;
    if (c < 0) {
        c = esp32_usb_keyboard_pop_key();
        if (c >= 0) from_keyboard = 1;
    }
    if (c < 0) c = esp32_console_read_byte_nonblock();
    if (c < 0) return -1;
    /* Web console delivers fully-decoded key codes; only raw byte streams
     * (USB stdin + telnet ring buffer) need escape-sequence assembly.
     * esp32_decode_escape_sequence drains continuation bytes from whichever
     * source they arrived on. */
    if (from_web || from_keyboard) return c; /* already decoded key codes */
    if (c == 0x1b) return mmbasic_escdecode_run(esp32_escdecode_read_byte_ms);
    return mmbasic_console_normalise_byte(c);
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
        /* MMgetchar blocks here, so runtime CheckAbort polling will not run. */
        audio_runtime_service();
        c = esp32_console_ring_pop();
        if (c < 0) {
            c = esp32_web_console_pop_key();
            if (c >= 0) from_web = 1;
        }
        int from_keyboard = 0;
        if (c < 0) {
            c = esp32_usb_keyboard_pop_key();
            if (c >= 0) from_keyboard = 1;
        }
        if (c < 0) c = esp32_console_read_byte_blocking_ms(1);
        if (from_keyboard && c >= 0) {
            ShowCursor(0);
            return c;
        }
    } while (c < 0);
    ShowCursor(0);
    if (from_web) return c; /* web console delivers pre-decoded key codes */
    if (c == 0x1b) return mmbasic_escdecode_run(esp32_escdecode_read_byte_ms);
    return mmbasic_console_normalise_byte(c);
}

/* Console RX/TX rings — Pico ports drive these from the UART IRQ. ESP32
 * USB Serial/JTAG goes through esp32_console_init's VFS routing instead,
 * so the rings stay unused. The buffer + head/tail symbols still need to
 * exist because core code reads them as state. CONSOLE_RX_BUF_SIZE comes
 * from configuration.h via HAL_PORT_CONSOLE_RX_BUF_SIZE; TX has its own
 * fixed CONSOLE_TX_BUF_SIZE. */
volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE] = {0};
volatile int ConsoleRxBufHead = 0;
volatile int ConsoleRxBufTail = 0;
volatile int ConsoleTxBufHead = 0;
volatile int ConsoleTxBufTail = 0;

/* MMgetline — read a line into p, stripping CR / expanding TAB / honouring
 * MAXSTRLEN. Body lifted from host_runtime.c. Routes through FileIO's
 * MMfgetc / FileEOF, which work via the FileIO + esp32_lfs stack. */
// MMgetline lives in runtime/runtime_getline.c — shared across every port.
