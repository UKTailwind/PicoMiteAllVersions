// Shared MMgetline used by every port — see CLAUDE.md, "extracting a
// function/module" rule. Previously copy-pasted into pico_console.c,
// pc386_runtime.c, esp32_mmbasic_console_glue.c, and runtime_console.c;
// the spine-extraction copies drifted (stripped F-keys, echo, backspace,
// tab expansion, CR-on-console-breaks) and silently broke console INPUT
// on host/WASM/pc386 — Pico/ESP32 kept the full version. Standardised
// here so a single source drives every port.

#include <ctype.h>
#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

// filenbr == 0 means the console input
void MMgetline(int filenbr, char * p) {
    int c, nbrchars = 0;
    char * tp;

    while (1) {
        CheckAbort();
        if (FileTable[filenbr].com > MAXCOMPORTS && FileEOF(filenbr)) break;
        c = MMfgetc(filenbr);
        if (c <= 0) continue; // keep looping if there are no chars

        // if this is the console, check for a programmed function key and insert the text
        if (filenbr == 0) {
            tp = NULL;
            if (c == F2) tp = "RUN";
            if (c == F3) tp = "LIST";
            if (c == F4) tp = "EDIT";
            if (c == F10) tp = "AUTOSAVE";
            if (c == F11) tp = "XMODEM RECEIVE";
            if (c == F12) tp = "XMODEM SEND";
            if (c == F1) tp = (char *)Option.F1key;
            if (c == F5) tp = (char *)Option.F5key;
            if (c == F6) tp = (char *)Option.F6key;
            if (c == F7) tp = (char *)Option.F7key;
            if (c == F8) tp = (char *)Option.F8key;
            if (c == F9) tp = (char *)Option.F9key;
            if (tp) {
                strcpy(p, tp);
                if (EchoOption) {
                    MMPrintString(tp);
                    MMPrintString("\r\n");
                }
                return;
            }
        }

        if (c == '\t') { // expand tabs to spaces
            do {
                if (++nbrchars > MAXSTRLEN) error("Line is too long");
                *p++ = ' ';
                if (filenbr == 0 && EchoOption) MMputchar(' ', 1);
            } while (nbrchars % 4);
            continue;
        }

        if (c == '\b') { // handle the backspace
            if (nbrchars) {
                if (filenbr == 0 && EchoOption) MMPrintString("\b \b");
                nbrchars--;
                p--;
            }
            continue;
        }

        if (c == '\n') { // what to do with a newline
            break;       // a newline terminates a line (for a file)
        }

        if (c == '\r') {
            if (filenbr == 0 && EchoOption) {
                MMPrintString("\r\n");
                break; // on the console this means the end of the line - stop collecting
            } else
                continue; // for files loop around looking for the following newline
        }

        if (isprint(c)) {
            if (filenbr == 0 && EchoOption) MMputchar((char)c, 1); // The console requires that chars be echoed
        }
        if (++nbrchars > MAXSTRLEN) error("Line is too long"); // stop collecting if maximum length
        *p++ = (char)c;                                        // save our char
    }
    *p = 0;
}
