#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "Editor.h"
#include "runtime/runtime.h"
#include "runtime/runtime_console_escdecode.h"

static const mm_runtime_console_adapter *console_adapter;

static void console_service(void)
{
    if (console_adapter && console_adapter->service) console_adapter->service();
}

static int console_raw_mode_active(void)
{
    return console_adapter && console_adapter->raw_mode_active
        ? console_adapter->raw_mode_active()
        : 0;
}

static int console_read_byte_nonblock(void)
{
    return console_adapter && console_adapter->read_byte_nonblock
        ? console_adapter->read_byte_nonblock()
        : -1;
}

static int console_read_byte_blocking_ms(int ms)
{
    return console_adapter && console_adapter->read_byte_blocking_ms
        ? console_adapter->read_byte_blocking_ms(ms)
        : -1;
}

static void console_push_back_byte(int c)
{
    if (console_adapter && console_adapter->push_back_byte) {
        console_adapter->push_back_byte(c);
    }
}

static void console_sleep_us(uint32_t us)
{
    if (console_adapter && console_adapter->sleep_us) console_adapter->sleep_us(us);
}

static int console_repl_mode(void)
{
    return console_adapter && console_adapter->repl_mode
        ? console_adapter->repl_mode()
        : 0;
}

static void console_stdout_flush(void)
{
    if (console_adapter && console_adapter->stdout_flush) {
        console_adapter->stdout_flush();
    } else {
        fflush(stdout);
    }
}

static void console_telnet_putc(int c, int flush)
{
    if (console_adapter && console_adapter->telnet_putc) {
        console_adapter->telnet_putc(c, flush);
    }
}

static void console_display_putc(char c)
{
    if (console_adapter && console_adapter->display_putc) {
        console_adapter->display_putc(c);
    }
}

static char console_serial_putc(char c, int flush)
{
    if (console_adapter && console_adapter->serial_putc) {
        return console_adapter->serial_putc(c, flush);
    }
    fputc((unsigned char)c, stdout);
    if (flush) fflush(stdout);
    return c;
}

void mmbasic_runtime_console_set_adapter(const mm_runtime_console_adapter *adapter)
{
    console_adapter = adapter;
}

const mm_runtime_console_adapter *mmbasic_runtime_console_get_adapter(void)
{
    return console_adapter;
}

void mmbasic_runtime_console_print_raw(const char *text, int len)
{
    if (!text || len <= 0) return;
    if (console_adapter && console_adapter->raw_write) {
        console_adapter->raw_write(text, len);
    } else {
        fwrite(text, 1, (size_t)len, stdout);
    }
}

/* mmbasic_runtime_console_decode_escape_sequence — kept as a thin
 * forwarder so any in-tree caller still resolves; the real decoder
 * lives in runtime/runtime_console_escdecode.c (shared across ports). */
static int host_escdecode_read_byte_ms(int timeout_ms) {
    return console_read_byte_blocking_ms(timeout_ms);
}

int mmbasic_runtime_console_decode_escape_sequence(void)
{
    return mmbasic_escdecode_run(host_escdecode_read_byte_ms);
}

int MMInkey(void)
{
    console_service();

    /* Drain any chars left over from an earlier unrecognised escape
     * sequence before consulting the input source. */
    {
        int pb = mmbasic_escdecode_pop_pushback();
        if (pb >= 0) return pb;
    }

    if (ConsoleRxBufHead != ConsoleRxBufTail) {
        int c = (unsigned char)ConsoleRxBuf[ConsoleRxBufTail];
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
        return mmbasic_console_normalise_byte(c);
    }

    if (console_adapter && console_adapter->scripted_key) {
        int scripted = console_adapter->scripted_key();
        if (scripted != -2) return scripted;
    }

    if (console_adapter && console_adapter->sim_key) {
        int sim_key = console_adapter->sim_key();
        if (sim_key != -2) return sim_key;
    }

    if (console_raw_mode_active()) {
        int c = console_read_byte_nonblock();
        if (c < 0) return -1;
        if (c == 4 && !editactive) {
            if (console_adapter && console_adapter->on_ctrl_d) {
                console_adapter->on_ctrl_d();
            } else {
                MMPrintString("\r\n");
                exit(0);
            }
        }
        if (c == 0x1b) return mmbasic_runtime_console_decode_escape_sequence();
        return mmbasic_console_normalise_byte(c);
    }

    if (console_repl_mode()) {
        int c = fgetc(stdin);
        if (c == EOF) exit(0);
        if (c == '\n' &&
            !(console_adapter &&
              (console_adapter->flags & MM_RUNTIME_CONSOLE_FLAG_KEEP_STDIN_LF))) {
            return ENTER;
        }
        return c;
    }

    return -1;
}

int MMgetchar(void)
{
    int ch;
    do {
        ShowCursor(1);
        ch = MMInkey();
        if (ch == -1) console_sleep_us(1000);
    } while (ch == -1);
    ShowCursor(0);
    return ch;
}

void putConsole(int c, int flush)
{
    if (OptionConsole & 2) console_display_putc((char)c);
    if (OptionConsole & 1) {
        console_serial_putc((char)c, flush);
    } else {
        console_telnet_putc(c, flush);
        if (flush) console_telnet_putc(0, -1);
    }
}

// MMputchar lives in runtime/runtime_console_putchar.c — shared across every port.

void MMPrintString(char *s)
{
    while (*s) MMputchar(*s++, 0);
    console_stdout_flush();
    console_telnet_putc(0, -1);
}

void SSPrintString(char *s)
{
    while (*s) console_serial_putc(*s++, 0);
    console_stdout_flush();
    console_telnet_putc(0, -1);
}

// MMgetline lives in runtime/runtime_getline.c — shared across every port.
