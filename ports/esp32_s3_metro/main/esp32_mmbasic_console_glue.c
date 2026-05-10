/*
 * esp32_mmbasic_console_glue.c — MMBasic-facing console layer.
 *
 * Routes MMBasic's character-level I/O (MMputchar, MMPrintString,
 * MMInkey, SerialConsolePutC, MMgetline, …) through esp32_console.c's
 * lower-level host_output_hook + host_read_byte_* primitives.
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

/* Provided by esp32_console.c. */
extern void (*host_output_hook)(const char *text, int len);
extern int  host_read_byte_nonblock(void);
extern int  host_read_byte_blocking_ms(int ms);
extern void host_push_back_byte(int c);

/* MMBasic accumulates output via MMputchar / putConsole / SerialConsolePutC
 * and reads input via MMInkey / MMgetchar / getConsole / kbhitConsole.
 * No SCREEN/SERIAL split on this port — the stdio scope only has the
 * USB Serial/JTAG pipe. No line-discipline translation either: bytes go
 * through unchanged, with line endings handled by the IDF VFS settings
 * in esp32_console_init. */

char SerialConsolePutC(char c, int flush) {
    host_output_hook(&c, 1);
    if (flush) fflush(stdout);
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
}

void SSPrintString(char *s) {
    /* Bypass-the-options variant — same target on this port. */
    MMPrintString(s);
}

void myprintf(char *s) { MMPrintString(s); }

int getConsole(void) {
    return host_read_byte_nonblock();
}

int kbhitConsole(void) {
    int c = host_read_byte_nonblock();
    if (c < 0) return 0;
    host_push_back_byte(c);
    return 1;
}

int MMInkey(void) {
    return host_read_byte_nonblock();
}

int MMgetchar(void) {
    int c;
    do { c = host_read_byte_blocking_ms(-1); } while (c < 0);
    return c;
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
