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

/* Pin-definition table — zero-initialized so any code that probes
 * PinDef[].name == NULL on this port sees "no pin function configured".
 * Sized via NBRPINS, which is HAL_PORT_NBR_PINS = 49 on this port
 * (ESP32-S3 has GPIO 0..48). */
const struct s_PinDef PinDef[NBRPINS + 1] = {{0}};

/* Prompt rendering attributes. Match host_runtime.c's defaults: white
 * foreground, black background, font index 1 (the standard 8x16). */
int PromptFC   = 0xFFFFFF;
int PromptBC   = 0;
int PromptFont = 1;

/* External-interrupt cleanup helper. ESP32 stdio scope has no GPIO
 * BASIC interrupts wired; nothing to clear. */
void ClearExternalIO(void) {}

/* SoftReset — called from cmd_cpu RESTART on some ports. ESP32 uses
 * esp_restart() directly from esp32_system.c::cmd_cpu, so this is
 * never reached; the symbol exists only because Commands.c references
 * it from non-ESP32 paths. */
void SoftReset(void) {}

/* WiFi-startup completion flag. Hardware_Includes.h declares it as
 * extern int and warns that "WEB sets it true when the wifi stack is
 * up". Stdio scope has no WiFi → leave at 0. */
int startupcomplete = 0;

/* PEEK / POKE / interrupt-source address resolvers. Stdio scope
 * doesn't expose hardware register peek/poke. Return zero/NULL so any
 * BASIC PEEK("...") / POKE / SETTICK INTERRUPT path errors-out cleanly
 * via the address-zero check in External.c. */
unsigned char *GetIntAddress(unsigned char *p) { (void)p; return NULL; }
unsigned int   GetPeekAddr(unsigned char *p)   { (void)p; return 0; }
unsigned int   GetPokeAddr(unsigned char *p)   { (void)p; return 0; }

/* xchg_byte — SPI bit-bang exchange function pointer used by SPI
 * comms over GP pins. Stdio scope has no software SPI exposed; the
 * no-op accepts but discards data, returning 0. */
typedef unsigned char BYTE;
static BYTE esp32_xchg_byte_noop(BYTE data_out) { (void)data_out; return 0; }
BYTE (*xchg_byte)(BYTE data_out) = esp32_xchg_byte_noop;

/* Regex stubs — MMBasic's REGEX function. Real impl lives in
 * drivers/regex on Pico, not yet wired on ESP32. Return -1 (failure)
 * so REGEX("...") returns the convention "no match" without crashing. */
int xregcomp(void *preg, const char *pattern, int cflags) {
    (void)preg; (void)pattern; (void)cflags; return -1;
}
int xregexec(void *preg, const char *string, int nmatch, void *pmatch, int eflags) {
    (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags; return -1;
}
void xregfree(void *preg) { (void)preg; }
