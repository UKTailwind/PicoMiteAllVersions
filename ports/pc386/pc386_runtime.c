/*
 * ports/pc386/pc386_runtime.c — runtime lifecycle + console + simple
 * peripheral hooks.
 *
 * Equivalent of host_runtime.c minus the host-specific bits: no
 * SDL framebuffer, no sim-server WebSocket, no termios raw-mode
 * juggling, no clock_gettime — pc386 uses serial_16550 for I/O,
 * hal_time for the clock, and the bare hardware for everything else.
 *
 * The error()/longjmp dance + the hooks the MMBasic core calls
 * unconditionally (CheckAbort, routinechecks, ClearExternalIO, etc.)
 * land here. Peripheral cmd_X / fun_X stubs that don't apply on PC
 * live in pc386_peripheral_stubs.c. BSS state arrays live in
 * pc386_state.c.
 */

#include <setjmp.h>
#include <string.h>
#include <stdio.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "../../drivers/serial_16550/serial_16550.h"
#include "pc386_panic.h"

extern jmp_buf mark;          /* defined by MMBasic.c */

/* Forward decls for the output hook contract host_runtime.c established. */
void (*host_output_hook)(const char *text, int len) = NULL;

extern uint64_t hal_time_us_64(void);
extern uint64_t timeroffset;
extern void flash_range_erase(uint32_t off, uint32_t count);

/* =========================================================================
 *  Runtime lifecycle
 * ========================================================================= */

void host_runtime_begin(void) {
    timeroffset = hal_time_us_64();

    /* Tell Draw.c "a display is configured" so cmd_box / cmd_pixel
     * don't error. DISP_USER (28) skips all panel-specific code paths
     * in Draw.c. Same trick host_runtime.c uses. */
    Option.DISPLAY_TYPE = DISP_USER;

    /* Seed CFunctionFlash from the pre-erased buffer in pc386_state.c
     * so CFunction scan loops terminate immediately (mirrors host). */
    extern unsigned char pc386_cfunction_buf[];
    CFunctionFlash = pc386_cfunction_buf;

    /* Pagination defaults to "off" until we have a way to ask the
     * console its row count. Host uses 1000; same here. */
    if (Option.Height == 0) Option.Height = 1000;
}

void host_runtime_finish(void) { }
int  host_runtime_timed_out(void) { return 0; }

/* =========================================================================
 *  CheckAbort + interrupt poll hooks (all no-ops for stage 3 — no IRQs).
 * ========================================================================= */

void CheckAbort(void)         { }
int  check_interrupt(void)    { return 0; }
void ClearExternalIO(void)    { }
void closeframebuffer(char l) { (void)l; }
void clear320(void)           { }
void initMouse0(int s)        { (void)s; }
void restorepanel(void)       { WriteBuf = NULL; }
void routinechecks(void)      { }
void SoftReset(void)          { pc386_panic("SoftReset not yet implemented"); }
void uSec(int us)             { extern void hal_time_sleep_us(uint32_t); hal_time_sleep_us((uint32_t)us); }

/* MMBasic.c uses __get_MSP for stack-overflow protection. PC has no MSP
 * concept (and we have plenty of stack); return a value that always
 * passes the comparison. */
uint32_t __get_MSP(void) { return 0xFFFFFFFFu; }

/* DisplayPutC lives in gfx_console_shared.c — don't redefine. */
void Display_Refresh(void) { }
void DisplayNotSet(void) { /* no error — host_runtime_begin sets DISPLAY_TYPE */ }
void ScrollLCDSPISCR(int s) { (void)s; }

/* =========================================================================
 *  Console I/O — output to COM1, input deferred to stage 3e.
 * ========================================================================= */

char SerialConsolePutC(char c, int flush) {
    (void)flush;
    if (host_output_hook) {
        host_output_hook(&c, 1);
    } else {
        serial_putc(c);
    }
    return c;
}

void putConsole(int c, int flush) {
    /* Honour the OPTION CONSOLE routing bits (1 = serial, 2 = display). */
    if (OptionConsole & 2) DisplayPutC((char)c);
    if (OptionConsole & 1) SerialConsolePutC((char)c, flush);
}

char MMputchar(char c, int flush) {
    putConsole(c, flush);
    /* Track column position for TAB() etc. */
    extern int isprint(int);
    if (isprint((unsigned char)c)) MMCharPos++;
    if (c == '\r') MMCharPos = 1;
    return c;
}

void MMPrintString(char *s) {
    while (*s) MMputchar(*s++, 0);
}

void SSPrintString(char *s) {
    while (*s) SerialConsolePutC(*s++, 0);
}

/* Stage 3e input: poll COM1 RX with normalisation.
 *
 * Terminal byte → MMBasic key code:
 *   0x0A (LF)         → ENTER (0x0D)
 *   0x7F (DEL, macOS) → BKSP  (0x08)
 *   anything else     → as-is
 * Real Editor.c-driven REPL with arrow keys / history lands in stage 4
 * once we have IRQ-driven input + a proper VT100 escape decoder. */
static int pc386_normalise_key(int c) {
    if (c == '\n') return ENTER;
    if (c == 0x7F) return BKSP;
    return c;
}

int MMInkey(void) {
    int c = serial_getc_nonblock();
    if (c < 0) return -1;
    return pc386_normalise_key(c);
}

int MMgetchar(void) {
    int c = serial_getc_blocking();
    return pc386_normalise_key(c);
}

int  getConsole(void)      { return -1; }
int  kbhitConsole(void)    { return 0; }
void myprintf(char *s)     { MMPrintString(s); }

/* MMfopen/MMfclose/MMgetline — file I/O routes through hal_filesystem;
 * this layer is the dispatch handle. Stage 3f wires real bodies. */
void MMfopen(unsigned char *fname, unsigned char *mode, int fnbr) {
    (void)fname; (void)mode; (void)fnbr;
}

void MMfclose(int fnbr) { FileClose(fnbr); }

void MMgetline(int filenbr, char *p) {
    int c, n = 0;
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
                if (++n > MAXSTRLEN) error("Line is too long");
                *p++ = ' ';
            } while (n % 4);
            continue;
        }
        if (++n > MAXSTRLEN) error("Line is too long");
        *p++ = (char)c;
    }
    *p = 0;
}

void printoptions(void) { }

/* =========================================================================
 *  REPL hooks — the actual prompt loop is MMBasic_RunPromptLoop in
 *  MMBasic_REPL.c (same one host_main.c + PicoMite.c use). Pc386 just
 *  has to provide the small port surface it pokes at.
 * ========================================================================= */

/* port_repl_post_clear_display_refresh comes from display_merge_stub.c
 * (which we link via CORE_DRV_SRCS). ApplyPromptConsoleColours comes
 * from Draw.c. Both already resolve. Only the WiFi hook needs a
 * pc386-local stub. */
void port_repl_wifi_arch_init_and_connect(void) { /* no WiFi on pc386 */ }

/* =========================================================================
 *  Cmd_files / cmd_load lifecycle hooks.
 * ========================================================================= */

void cmd_files_save_program_context(void) { }
void cmd_files_restore_program_context(void) { }
void cmd_files_pump_console_key(int *c)   { (void)c; }
void cmd_load_post_cleanup(void)          { }

/* CallCFunction / CallExecuteProgram — pc386 has no CFunction support. */
void CallCFunction(unsigned char *p, unsigned char *args, int *t, unsigned char **s) {
    (void)p; (void)args; (void)t; (void)s;
}
void CallExecuteProgram(char *p) { (void)p; }

/* =========================================================================
 *  port_* hooks — every "what does this board look like" question.
 *  PC has no MCU peripherals, so each is a no-op or returns "no".
 * ========================================================================= */

#include "bytecode.h"

int  port_usb_count(void) { return 0; }
int  port_usb_hid_field(int n, int field) { (void)n; (void)field; return 0; }

int  port_mount_sd_drive(void) {
    /* A: + C: are mounted from kmain via FatFs over ATA-PIO. Nothing
     * to do here — already mounted. SDCardStat-clear matches host. */
    extern volatile BYTE SDCardStat;
    SDCardStat = 0;
    return 2;
}

void port_apply_load_overrides(void) { }
void port_drive_check(char drive)    { (void)drive; }

void port_picocalc_set_keyboard_backlight(int level) { (void)level; }
int  port_picocalc_battery_pct(void) { return 0; }
int  port_picocalc_is_charging(void) { return 0; }
void port_picocalc_factory_reset_options(void) { }

void port_print_supported_boards(void) { }
int  port_factory_reset_board(unsigned char *p) { (void)p; return 0; }

int  port_display_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }
void port_print_display_options(void) { }
void port_print_lcd_spi(void) { }
void port_print_keyboard_heartbeat(void) { }
void port_print_usb_kb_repeat(void) { }
void port_clear_lcd_spi_if_shares_system(void) { }
int  port_pinno_alias_for_name(const char *n) { (void)n; return 0; }
int  port_pin_is_reserved_alias(int p) { (void)p; return 0; }
const char *port_pin_reserved_label(int p) { (void)p; return NULL; }
int  port_lcd320_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_keyboard_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_misc_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_pico_pins_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_heartbeat_option_setter(unsigned char *c) { (void)c; return 0; }
void port_apply_default_console_colors(int fc, int bc) { (void)fc; (void)bc; }
int  port_system_lcd_spi_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_audio_i2s_pio_slice(int p1, int p2) { (void)p1; (void)p2; return 0; }

int  port_mminfo_interrupts(int64_t *o) { (void)o; return 0; }
int  port_mminfo_touch_status(unsigned char *o) { (void)o; return 0; }
int  port_mminfo_scroll_start(int64_t *o) { (void)o; return 0; }
int  port_mminfo_screenbuff(int64_t *o)   { (void)o; return 0; }

#include "hardware/pio.h"
PIO port_pio_for_index(int i) { (void)i; return NULL; }

int  port_poke_display_panel(unsigned char *p) { (void)p; return 0; }

void port_web_print_options(void) { }
int  port_web_option_setter(unsigned char *c) { (void)c; return 0; }
int  port_web_mminfo(unsigned char *ep, int64_t *iret,
                     unsigned char *sret, int *targ)
{ (void)ep; (void)iret; (void)sret; (void)targ; return 0; }
int  port_web_get_ssid(unsigned char *o, int *t) { (void)o; (void)t; return 0; }

/* bc_debug.c crash-dump hooks. */
uint32_t port_bc_crash_get_sp(void) { return 0; }
void port_bc_crash_save_fault_regs(BCCrashInfo *info) { (void)info; }

void port_select_error_prompt_font(void) { }
void port_clear_runtime_display_reset(void) { }
void port_error_restore_console_surface(void) { }
void port_error_show_lcd_banner(int line, const char *src, const char *err) {
    (void)line; (void)src; (void)err;
}

int  port_try_find_subfun_hash(unsigned char *p, int *out)
{ (void)p; (void)out; return 0; }
void port_prepare_program_finalize_subfun(int e) { (void)e; }
int  port_try_find_label_hash(unsigned char *l, unsigned char **o)
{ (void)l; (void)o; return 0; }
int  port_try_check_var_subfun_collision(const unsigned char *n, int len)
{ (void)n; (void)len; return 0; }

void port_bc_bridge_clear_subfun_hash(void) { }
void port_bc_bridge_rehash_subfun(unsigned char **a) { (void)a; }

/* vm_sys_time port hook — pc386 has no RTC yet (would come from CMOS
 * port 0x70/0x71 in a later stage). Return zero; vm_sys_time falls
 * back to the un-set value. */
#include <time.h>
int port_vm_time_get_tm(struct tm *out) {
    (void)out;
    return 0;
}

/* =========================================================================
 *  bc_runtime free-source hook (matches host_native — pc386 owns the
 *  source buffer the same way; never BC_FREE it).
 * ========================================================================= */
void port_bc_runtime_free_source(const char **source) { (void)source; }

/* =========================================================================
 *  Draw.c terminal hooks — pc386 has VGA text + serial, not ANSI; the
 *  framebuffer path covers cmd_cls / cmd_colour once stage 5 brings up
 *  graphics. No-op for stage 3.
 * ========================================================================= */
bool port_terminal_handle_cls(void) { return false; }
void port_terminal_emit_colour(int fg, int bg, int has_bg) {
    (void)fg; (void)bg; (void)has_bg;
}

/* =========================================================================
 *  mmbasic_timegm/gmtime — host_platform.h renames the libc names to
 *  these to dodge GPS.h's const-vs-non-const conflict. Our time.h
 *  shim doesn't apply that rename; provide the real symbols anyway in
 *  case some compiled object expects them.
 * ========================================================================= */
time_t mmbasic_timegm(const struct tm *tm) {
    return timegm(tm);
}
struct tm *mmbasic_gmtime(const time_t *t) {
    return gmtime(t);
}
