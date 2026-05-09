/*
 * esp32_runtime.c — VM trampoline + abort hooks + interpreter-state
 * globals.
 *
 * On ESP32 these are mostly empty bodies / one-liners. The
 * interpreter's main loop lives on a FreeRTOS task spawned from
 * app_main; CheckAbort / routinechecks just yield and consume the
 * USB Serial/JTAG pending-input check that already runs from
 * esp32_console.c.
 *
 * VM trampolines (CallCFunction / CallExecuteProgram) are no-ops:
 * MMBasic CFUNCTION support requires a Pico-flash-resident hot loop
 * that doesn't translate to ESP-IDF's section model. CFUNCTION dispatch
 * isn't part of the stdio-REPL litmus.
 *
 * Per the D-decouple plan, this file replaces host_runtime.c's
 * versions of these symbols.
 */

#include <stddef.h>
#include <stdint.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* esp32_console.c provides the USB Serial/JTAG byte ring; we drain
 * pending input here so Ctrl-C breaks runaway loops even when MMInkey
 * isn't being called (e.g. tight FOR/NEXT). Non-Ctrl-C bytes get
 * pushed back so MMInkey sees them on the next poll. */
extern int  host_read_byte_nonblock(void);
extern void host_push_back_byte(int c);
/* MMAbort is declared in MMBasic.h as `volatile int`. */

static void esp32_runtime_pump_input(void) {
    int c = host_read_byte_nonblock();
    if (c < 0) return;
    if (c == 0x03 /* Ctrl-C */) {
        MMAbort = 1;
        return;
    }
    host_push_back_byte(c);
}

/* Interpreter abort poll. Called from the parser hot path on every
 * statement; from MMInkey on every keypress poll. */
void CheckAbort(void) {
    esp32_runtime_pump_input();
}

/* Interrupt-poll hook. Returns non-zero if a BASIC ON-interrupt fired
 * and the interpreter should redirect to it. ESP32 stdio scope has no
 * GPIO-driven BASIC interrupts wired yet — return 0 always. */
int check_interrupt(void) { return 0; }

/* Long-running-routine yield. Called from PAUSE, FOR-loop iterations,
 * etc. */
void routinechecks(void) {
    esp32_runtime_pump_input();
}

/* CFUNCTION dispatch trampoline. ESP32 doesn't ship MMBasic CFUNCTION
 * support — the `_S` / `_F` flash regions Pico CFUNCTIONs live in
 * don't have an ESP-IDF analogue. cmd_cfunction errors before this
 * runs; the symbol exists only because Commands.c references it. */
void CallCFunction(unsigned char *p, unsigned char *args, int *t, unsigned char **s) {
    (void)p; (void)args; (void)t; (void)s;
}

/* Used by the interactive editor's `RUN <fragment>` shortcut. ESP32
 * REPL goes through MMBasic_RunPromptLoop's normal tokenize-and-execute
 * path; this trampoline is unreachable but referenced from the editor
 * stub. */
void CallExecuteProgram(char *p) { (void)p; }

/* core1stack — the magic-number sentinel MMBasic.c checks for stack
 * overflow on the second core. ESP32-S3 has dual Xtensa LX7 cores but
 * the stdio REPL stays on PRO_CPU; APP_CPU is idle. The sentinel must
 * be present so MMBasic.c:633's overflow check returns clean. */
uint32_t core1stack[256] = {[0] = 0x12345678};

/* ON-interrupt-return state, used when an ON KEY / ON ERROR / etc.
 * handler is mid-flight. Initialized to NULL/0 — no interrupts
 * currently registered on this port. */
unsigned char *InterruptReturn = NULL;
int InterruptUsed = 0;

/* Diagnostic timer — bumped from a 1 ms tick on Pico, observed by
 * various long-running-loop watchdog paths. ESP32 doesn't drive a
 * matching tick; left at 0. Volatile to match the host_native
 * declaration so existing extern decls match. */
volatile unsigned int ScrewUpTimer = 0;
