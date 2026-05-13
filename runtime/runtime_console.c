#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "Draw.h"
#include "Editor.h"
#include "runtime/runtime.h"

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

int mmbasic_runtime_console_decode_escape_sequence(void)
{
    int c1 = console_read_byte_blocking_ms(30);
    if (c1 < 0) return ESC;

    if (c1 == '[') {
        int c2 = console_read_byte_blocking_ms(30);
        if (c2 < 0) return ESC;
        switch (c2) {
            case 'A': return UP;
            case 'B': return DOWN;
            case 'C': return RIGHT;
            case 'D': return LEFT;
            case 'H': return HOME;
            case 'F': return END;
        }
        if (c2 >= '0' && c2 <= '9') {
            int n = c2 - '0';
            int c3;
            while ((c3 = console_read_byte_blocking_ms(30)) >= 0) {
                if (c3 >= '0' && c3 <= '9') {
                    n = n * 10 + (c3 - '0');
                    continue;
                }
                break;
            }
            if (c3 == '~') {
                switch (n) {
                    case 1:  return HOME;
                    case 2:  return INSERT;
                    case 3:  return DEL;
                    case 4:  return END;
                    case 5:  return PUP;
                    case 6:  return PDOWN;
                    case 15: return F5;
                    case 17: return F6;
                    case 18: return F7;
                    case 19: return F8;
                    case 20: return F9;
                    case 21: return F10;
                    case 23: return F11;
                    case 24: return F12;
                }
            }
        }
        return ESC;
    }

    if (c1 == 'O') {
        int c2 = console_read_byte_blocking_ms(30);
        switch (c2) {
            case 'P': return F1;
            case 'Q': return F2;
            case 'R': return F3;
            case 'S': return F4;
        }
        return ESC;
    }

    console_push_back_byte(c1);
    return ESC;
}

int MMInkey(void)
{
    console_service();

    if (ConsoleRxBufHead != ConsoleRxBufTail) {
        int c = (unsigned char)ConsoleRxBuf[ConsoleRxBufTail];
        ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
        if (c == 0x7f) return BKSP;
        if (c == '\n') return ENTER;
        return c;
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
        if (c == 0x7f) return BKSP;
        if (c == '\n') return ENTER;
        return c;
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

char MMputchar(char c, int flush)
{
    putConsole(c, flush);
    if (isprint((unsigned char)c)) MMCharPos++;
    if (c == '\r') MMCharPos = 1;
    return c;
}

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

void MMgetline(int filenbr, char *p)
{
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
