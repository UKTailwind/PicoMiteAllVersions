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
 * Per the D-decouple plan, this file owns ESP32 runtime hooks directly.
 */

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bc_alloc.h"
#include "runtime/runtime.h"

/* esp32_console.c provides the USB Serial/JTAG byte ring; we drain
 * pending input here so Ctrl-C breaks runaway loops even when MMInkey
 * isn't being called (e.g. tight FOR/NEXT). Non-Ctrl-C bytes get
 * pushed back so MMInkey sees them on the next poll. */
extern int  esp32_console_read_byte_nonblock(void);
extern void esp32_console_push_back_byte(int c);
extern volatile bool TCPreceived;
extern char *TCPreceiveInterrupt;
extern volatile bool UDPreceive;
extern char *UDPinterrupt;
extern volatile bool MQTTComplete;
extern char *MQTTInterrupt;
extern bool g_TempMemoryIsChanged;
extern int esp32_tcp_interrupt_pending(void);
extern int esp32_udp_interrupt_pending(void);
extern void ProcessWeb(int mode);
/* MMAbort is declared in MMBasic.h as `volatile int`. */

static int s_save_option_error_skip;
static char s_save_error_message[MAXERRMSG];
static int s_save_errno;
static char s_interrupt_return_token[3];
static int s_network_service_active;

static inline CommandToken esp32_commandtbl_decode(const unsigned char *p) {
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f) << 7);
}

static void esp32_runtime_pump_input(void) {
    int c = esp32_console_read_byte_nonblock();
    if (c < 0) return;
    if (c == 0x03 /* Ctrl-C */) {
        MMAbort = 1;
        return;
    }
    esp32_console_push_back_byte(c);
}

static void esp32_runtime_network_service(void) {
    ProcessWeb(0);
}

static void esp32_runtime_service(void) {
    esp32_runtime_pump_input();
    mmbasic_runtime_poll_service_once(&s_network_service_active,
                                      esp32_runtime_network_service);
}

static void esp32_runtime_before_abort(void) {
    WDTimer = 0;
}

static void esp32_runtime_yield(void) {
    vTaskDelay(1);
}

static const mmbasic_runtime_abort_adapter s_abort_adapter = {
    .service = esp32_runtime_service,
    .abort_flag = &MMAbort,
    .flags = MMBASIC_RUNTIME_ABORT_FLAG_CHECK_ABORT |
             MMBASIC_RUNTIME_ABORT_FLAG_DO_END_LONGJMP,
    .before_abort = esp32_runtime_before_abort,
    .after_poll = esp32_runtime_yield,
};

/* Interpreter abort poll. Called from the parser hot path on every
 * statement; from MMInkey on every keypress poll. */
void CheckAbort(void) {
    mmbasic_runtime_checkabort(&s_abort_adapter);
}

void cmd_ireturn(void) {
    if (InterruptReturn == NULL) error("Not in interrupt");
    checkend(cmdline);
    mmbasic_runtime_interrupt_leave_state(&nextstmt, &InterruptReturn,
                                          &g_LocalIndex, ClearVars,
                                          &g_TempMemoryIsChanged,
                                          CurrentInterruptName);
    mmbasic_runtime_interrupt_restore_error_state(s_save_option_error_skip,
                                                  s_save_error_message,
                                                  s_save_errno,
                                                  &OptionErrorSkip, MMErrMsg,
                                                  &MMerrno);
}

int check_interrupt(void) {
    esp32_runtime_service();
    if (!InterruptUsed) return 0;
    if (InterruptReturn != NULL) return 0;

    unsigned char *intaddr = NULL;
    if (TCPreceiveInterrupt && (TCPreceived || esp32_tcp_interrupt_pending())) {
        intaddr = (unsigned char *)TCPreceiveInterrupt;
        TCPreceived = false;
    } else if (MQTTInterrupt && MQTTComplete) {
        intaddr = (unsigned char *)MQTTInterrupt;
        MQTTComplete = false;
    } else if (UDPinterrupt && (UDPreceive || esp32_udp_interrupt_pending())) {
        intaddr = (unsigned char *)UDPinterrupt;
        UDPreceive = false;
    } else {
        return 0;
    }

    g_LocalIndex++;
    mmbasic_runtime_interrupt_save_error_state(&s_save_option_error_skip,
                                               s_save_error_message,
                                               sizeof(s_save_error_message),
                                               &s_save_errno,
                                               &OptionErrorSkip, MMErrMsg,
                                               &MMerrno);
    InterruptReturn = nextstmt;

    if (esp32_commandtbl_decode(intaddr) == cmdSUB) {
        if (gosubindex >= MAXGOSUB) error("Too many SUBs for interrupt");
        intaddr = mmbasic_runtime_interrupt_prepare_sub_return(
            cmdIRET, C_BASETOKEN, intaddr,
            CurrentInterruptName, MAXVARLEN, true,
            s_interrupt_return_token, sizeof(s_interrupt_return_token),
            &gosubindex, errorstack, gosubstack, CurrentLinePtr,
            &g_LocalIndex);
    }

    nextstmt = intaddr;
    return 1;
}

/* Long-running-routine yield. Called from PAUSE, FOR-loop iterations,
 * etc. */
void routinechecks(void) {
    mmbasic_runtime_routinechecks(&s_abort_adapter);
}

void port_bc_runtime_free_source(const char **source) {
    if (source && *source) {
        if (bc_compile_owns(*source)) BC_COMPILER_FREE((void *)*source);
        else BC_FREE((void *)*source);
        *source = NULL;
    }
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
 * matching tick; left at 0. Volatile to match existing extern decls. */
volatile unsigned int ScrewUpTimer = 0;
