/*
 * esp32_globals.c — tentative-def globals not owned by another category.
 *
 * These are interpreter-state symbols core code references but no
 * existing per-port file naturally claims. Most are zero-initialized
 * defaults; a few have meaningful initial values (PromptFC, PromptFont).
 *
 * Per the D-decouple plan, this file replaces the host_native
 * definitions of these symbols.
 */

#include <stddef.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

/* Prompt rendering attributes. Match host_runtime.c's defaults: white
 * foreground, black background, font index 1 (the standard 8x16). */
int PromptFC = 0xFFFFFF;
int PromptBC = 0;
int PromptFont = 1;

/* External-interrupt cleanup helper. ESP32 stdio scope has no GPIO
 * BASIC interrupts wired; nothing to clear. */
void ClearExternalIO(void) {}

/* SoftReset — called from cmd_cpu RESTART on some ports. ESP32 uses
 * esp_restart() directly from esp32_system.c::cmd_cpu, so this is
 * never reached; the symbol exists only because Commands.c references
 * it from non-ESP32 paths. */
void SoftReset(void) {}

/* Interrupt-source address resolver. This is used by WEB TCP INTERRUPT
 * and the generic MMBasic interrupt machinery to resolve a line number,
 * label, or SUB name into tokenized program memory. */
unsigned char * GetIntAddress(unsigned char * p) {
    if (isnamestart((uint8_t)*p)) {
        int i = FindSubFun(p, 0);
        if (i == -1) return findlabel(p);
        return subfun[i];
    }
    return findline(getinteger(p), true);
}

/* Stdio scope doesn't expose hardware register peek/poke. Return zero
 * so BASIC PEEK("...") / POKE paths error out cleanly via their
 * address-zero checks. */
unsigned int GetPeekAddr(unsigned char * p) {
    (void)p;
    return 0;
}
unsigned int GetPokeAddr(unsigned char * p) {
    (void)p;
    return 0;
}

/* xchg_byte — SPI bit-bang exchange function pointer used by SPI
 * comms over GP pins. Stdio scope has no software SPI exposed; the
 * no-op accepts but discards data, returning 0. */
typedef unsigned char BYTE;
static BYTE esp32_xchg_byte_noop(BYTE data_out) {
    (void)data_out;
    return 0;
}
BYTE (*xchg_byte)(BYTE data_out) = esp32_xchg_byte_noop;

/* Regex stubs — MMBasic's REGEX function. Real impl lives in
 * drivers/regex on Pico, not yet wired on ESP32. Return -1 (failure)
 * so REGEX("...") returns the convention "no match" without crashing. */
int xregcomp(void * preg, const char * pattern, int cflags) {
    (void)preg;
    (void)pattern;
    (void)cflags;
    return -1;
}
int xregexec(void * preg, const char * string, int nmatch, void * pmatch, int eflags) {
    (void)preg;
    (void)string;
    (void)nmatch;
    (void)pmatch;
    (void)eflags;
    return -1;
}
void xregfree(void * preg) {
    (void)preg;
}

/* rp2350a chip-detect bool. True only on rp2350a hardware (a Pico-SDK
 * runtime detect); false everywhere else, including ESP32. */
bool rp2350a = false;

/* MMBasic interpreter cursor / column accounting — incremented by
 * MMputchar on visible glyphs, reset on \r/\n. Used by PRINT to know
 * how many spaces to emit for tab alignment. */
int MMCharPos = 0;

/* Option / state globals MMBasic.h declares as extern but doesn't
 * define. Each port owns its definition; here they're zero-init defaults. */
MMFLOAT optionangle = 0;
bool useoptionangle = 0;
bool optionfastaudio = 0;
bool optionfulltime = 0;
bool optionlogging = 0;

/* Sensor / timer globals — feature areas not wired in stdio scope. */
volatile unsigned int AHRSTimer = 0;
int64_t * ds18b20Timers = NULL;
int last_adc = 0;
volatile int day_of_week = 0;
volatile unsigned int diskchecktimer = 0;
volatile unsigned int SecondsTimer = 0;
volatile unsigned int WDTimer = 0;
volatile int64_t mSecTimer = 0;

/* MMBasic interpreter abort flag. Set to 1 by Ctrl-C handlers; checked
 * by routinechecks / CheckAbort and the prompt loop's setjmp landing. */
volatile int MMAbort = 0;

/* SETTICK timer state. NBRSETTICKS slots; each independently programmed
 * by SETTICK n,handler. Stdio scope doesn't drive a 1 ms tick yet — the
 * counters stay at zero and SETTICK handlers won't fire. */
int TickPeriod[NBRSETTICKS] = {0};
volatile int TickTimer[NBRSETTICKS] = {0};

/* Watchdog state (BASIC-level — distinct from ESP-IDF's task watchdog,
 * which sdkconfig.defaults disables). 0 = no BASIC WATCHDOG configured. */
unsigned char WatchdogSet = 0;

/* MMBasic FUN_TILDE / fun_pin / fun_battery references. Real device
 * builds wire these to ADC; stdio scope reports a fixed nominal value. */
MMFLOAT VCC = 3.3;

/* Pico-SDK __uninitialized_ram persistent storage — used by crash-trail
 * code on Pico to survive a soft reset. ESP32 has no equivalent
 * persistent-RAM region; provide a regular global so the symbol resolves. */
uint64_t _persistent = 0;

/* Pico-SDK exception-code register save area, populated by the M0+
 * fault handler before the watchdog fires. ESP32 has its own panic
 * frame; this stays at 0. */
uint32_t _excep_code = 0;

/* SETTICK callback bookkeeping. NBRSETTICKS slots, each independently
 * armed by SETTICK n,handler_label. Stdio scope doesn't drive a 1 ms
 * tick yet, so handlers stay disarmed. */
volatile unsigned char TickActive[NBRSETTICKS] = {0};
unsigned char * TickInt[NBRSETTICKS] = {NULL};

/* Console behaviour state. BreakKey = ASCII code that triggers MMAbort
 * (default Ctrl-C = 0x03). EchoOption = 1 to echo received chars back
 * to the console; the prompt loop sets/clears it dynamically. */
unsigned char BreakKey = 3;
unsigned char EchoOption = 0;
