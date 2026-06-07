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
#include "esp_timer.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bc_alloc.h"
#include "runtime/runtime.h"
#include "shared/audio/audio_runtime.h"

/* esp32_console.c provides the USB Serial/JTAG byte ring; we drain
 * pending input here so Ctrl-C breaks runaway loops even when MMInkey
 * isn't being called (e.g. tight FOR/NEXT). Non-Ctrl-C bytes get
 * pushed back so MMInkey sees them on the next poll. */
extern int esp32_console_read_byte_nonblock(void);
extern void esp32_console_push_back_byte(int c);
extern volatile bool TCPreceived;
extern char * TCPreceiveInterrupt;
extern volatile bool UDPreceive;
extern char * UDPinterrupt;
extern volatile bool MQTTComplete;
extern char * MQTTInterrupt;
extern bool g_TempMemoryIsChanged;
extern int esp32_tcp_interrupt_pending(void);
extern int esp32_udp_interrupt_pending(void);
extern void ProcessWeb(int mode);
extern volatile int esp32_console_display_rendering;
/* MMAbort is declared in MMBasic.h as `volatile int`. */

static int s_save_option_error_skip;
static char s_save_error_message[MAXERRMSG];
static int s_save_errno;
static char s_interrupt_return_token[3];
static int s_network_service_active;

static inline CommandToken esp32_commandtbl_decode(const unsigned char * p) {
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
    /* Cooperative decode pump for file playback during interpreter polls. */
    audio_runtime_service();
    mmbasic_runtime_poll_service_once(&s_network_service_active,
                                      esp32_runtime_network_service);
}

static void esp32_runtime_before_abort(void) {
    WDTimer = 0;
}

static void esp32_runtime_yield(void) {
    static int64_t next_yield_us;
    int64_t now = esp_timer_get_time();
    if (now < next_yield_us) return;
    next_yield_us = now + 10000;
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

static const mmbasic_runtime_interrupt_dispatch_adapter esp32_interrupt_dispatch = {
    .service = esp32_runtime_service,
    .tcp_pending = esp32_tcp_interrupt_pending,
    .udp_pending = esp32_udp_interrupt_pending,
    .commandtbl_decode = esp32_commandtbl_decode,
    .save_option_error_skip = &s_save_option_error_skip,
    .save_error_message = s_save_error_message,
    .save_error_message_size = sizeof(s_save_error_message),
    .save_errno = &s_save_errno,
    .interrupt_return_token = s_interrupt_return_token,
    .interrupt_return_token_size = sizeof(s_interrupt_return_token),
};

void cmd_ireturn(void) {
    mmbasic_runtime_cmd_ireturn(&esp32_interrupt_dispatch);
}

int check_interrupt(void) {
    return mmbasic_runtime_check_interrupt(&esp32_interrupt_dispatch);
}

/* Long-running-routine yield. Called from PAUSE, FOR-loop iterations,
 * etc. */
void routinechecks(void) {
    mmbasic_runtime_routinechecks(&s_abort_adapter);
}

void port_bc_runtime_free_source(const char ** source) {
    if (source && *source) {
        if (bc_compile_owns(*source))
            BC_COMPILER_FREE((void *)*source);
        else
            BC_FREE((void *)*source);
        *source = NULL;
    }
}

/* CFUNCTION dispatch trampoline. ESP32 doesn't ship MMBasic CFUNCTION
 * support — the `_S` / `_F` flash regions Pico CFUNCTIONs live in
 * don't have an ESP-IDF analogue. cmd_cfunction errors before this
 * runs; the symbol exists only because Commands.c references it. */
int64_t CallCFunction(unsigned char * cmd, unsigned char * args, unsigned char * def, unsigned char * caller) {
    (void)cmd;
    (void)args;
    (void)def;
    (void)caller;
    return 0;
}

/* Used by the interactive editor's `RUN <fragment>` shortcut. ESP32
 * REPL goes through MMBasic_RunPromptLoop's normal tokenize-and-execute
 * path; this trampoline is unreachable but referenced from the editor
 * stub. */
void CallExecuteProgram(char * p) {
    (void)p;
}

/* core1stack — the magic-number sentinel MMBasic.c checks for stack
 * overflow on the second core. ESP32-S3 has dual Xtensa LX7 cores but
 * the stdio REPL stays on PRO_CPU; APP_CPU is idle. The sentinel must
 * be present so MMBasic.c:633's overflow check returns clean. */
uint32_t core1stack[256] = {[0] = 0x12345678};

/* ON-interrupt-return state, used when an ON KEY / ON ERROR / etc.
 * handler is mid-flight. Initialized to NULL/0 — no interrupts
 * currently registered on this port. */
unsigned char * InterruptReturn = NULL;
int InterruptUsed = 0;

/* Diagnostic timer — bumped from a 1 ms tick on Pico, observed by
 * various long-running-loop watchdog paths. ESP32 doesn't drive a
 * matching tick; left at 0. Volatile to match existing extern decls. */
volatile unsigned int ScrewUpTimer = 0;
