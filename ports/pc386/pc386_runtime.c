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

#include "../../drivers/i8042_kbd/i8042_kbd.h"
#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"
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

    /* Sensible Option defaults for a freshly-booted pc386 kernel.
     * LoadOptions reads from a memset-zero options buffer on first
     * boot which leaves every field at 0. Override the few that have
     * meaningful non-zero defaults on every other port; leave the rest
     * (Listcase=TITLE, continuation=off, Refresh=off, etc.) at zero
     * so the user's OPTION X commands aren't silently overridden.
     *
     *   Width = 80 — without this, MMputchar wraps after every char
     *   Height = 24 — pagination "PRESS ANY KEY" prompt
     *   Tab = 4 — Editor TAB stop
     *   OptionConsole bit 0 = serial (no graphics until stage 5) */
    if (Option.Width  == 0) Option.Width  = 80;
    if (Option.Height == 0) Option.Height = 1000;  /* effectively disable pagination —
                                                    * "PRESS ANY KEY" needs IRQ-driven
                                                    * input which lands in stage 4 */
    if (Option.Tab    == 0) Option.Tab    = 4;
    if ((OptionConsole & 0x03) == 0) OptionConsole = 1;

    /* Sane VRes + gui_font defaults so the `overlap` macro in FileIO.c
     *   (VRes % (FontTable[gui_font >> 4][1] * (gui_font & 0b1111)) ? 0 : 1)
     * doesn't divide by zero. With both at 0 the modulo is undefined
     * (x86 raises #DE which traps the kernel; gcc may also fold it to
     * an unpredictable value) and overlap returns garbage, making
     * Option.Height-overlap negative, which in turn fires cmd_files'
     * "PRESS ANY KEY ..." pagination after the first dir entry —
     * blocking on input we have no way to deliver yet. Stage 5 will
     * set HRes/VRes to real VGA dimensions. */
    extern short DisplayHRes, DisplayVRes;
    extern short HRes, VRes;
    extern short gui_font;
    if (gui_font == 0) gui_font = 0x11;        /* font 1, scale 1 */
    if (VRes == 0)     { VRes = 200; HRes = 320; }
    if (DisplayVRes == 0) { DisplayVRes = 200; DisplayHRes = 320; }

    /* Route file ops through FatFs (=1) rather than LFS (=0). Pc386 has
     * real FAT volumes from Stage 2 mounted on B: and C:; LFS is
     * stubbed (panic on use). Without this, FILES / LOAD / SAVE etc.
     * end up at lfs_* and surface as "Error during device operation". */
    extern int FatFSFileSystem, FatFSFileSystemSave;
    FatFSFileSystem = FatFSFileSystemSave = 1;

    /* Persist the live Option struct back to flash_option_buf so the
     * LoadOptions inside error()'s reset path doesn't wipe these
     * defaults. SaveOptions → hal_flash_write_options →
     * pc386_options_snapshot copies live Option to the buffer. */
    SaveOptions();
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
 *  Console I/O — fork every byte to BOTH the VGA-text console (primary,
 *  visible on the QEMU window / real-hardware monitor) AND COM1 serial
 *  (backup / remote terminal / test harness). Mirrors the standard
 *  IBM-PC convention: the user-facing console is whichever one's
 *  connected. The two outputs are byte-identical; vga_text_putc handles
 *  \r \n \b \t natively and scrolls; serial_putc writes raw.
 * ========================================================================= */

/* MMBasic's REPL emits VT100 escapes (cursor positioning, show/hide
 * cursor, clear-to-EOL, colour SGR) that a serial terminal interprets
 * but VGA text mode 03h renders as literal bytes. Strip them on the
 * VGA-bound path; keep them on serial so a connected terminal still
 * sees a properly-formatted prompt. State machine handles the only
 * three sequence shapes MMBasic emits:
 *   ESC [ ... <final>          CSI       — covers \[K, \[?25h, \[1A
 *   ESC O <final>              SS3       — F1..F4 sometimes
 *   ESC <one byte>             two-char  — bell-flash, charset switch
 */
static int vga_esc_state = 0;   /* 0=idle, 1=after-ESC, 2=in-CSI */

static void vga_text_filtered_putc(char c) {
    switch (vga_esc_state) {
    case 0:
        if (c == 0x1B) { vga_esc_state = 1; return; }
        vga_text_putc(c);
        return;
    case 1:
        if (c == '[' || c == 'O') { vga_esc_state = 2; return; }
        /* Two-char ESC sequence — swallow this final byte, back to idle. */
        vga_esc_state = 0;
        return;
    case 2:
        /* CSI body: keep eating until a final byte (0x40..0x7E). */
        if (c >= 0x40 && c <= 0x7E) { vga_esc_state = 0; return; }
        return;
    }
}

char SerialConsolePutC(char c, int flush) {
    (void)flush;
    if (host_output_hook) {
        /* Test-harness output-capture path (host_native uses this).
         * pc386 leaves host_output_hook NULL so this branch never
         * fires here, but keep it for symmetry with host's contract. */
        host_output_hook(&c, 1);
        return c;
    }
    vga_text_filtered_putc(c);
    serial_putc(c);
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

/* Stage 4e input: drain whichever of (PS/2 keyboard, COM1 serial) has
 * a character ready. PS/2 is IRQ-driven (drivers/i8042_kbd) and produces
 * MMBasic-shaped key codes (UP, BKSP, F1, etc.) directly; COM1 still
 * polls in stage 4 (4f turns it IRQ-driven) and goes through the same
 * terminal-byte normalisation as 3e.
 *
 * Both sources drain into the same return value here; we don't try to
 * coalesce into ConsoleRxBuf because nothing reads it until stage 4f
 * brings up the routinechecks-tier pump. */
static int pc386_normalise_serial(int c) {
    if (c == '\n') return ENTER;
    if (c == 0x7F) return BKSP;
    return c;
}

int MMInkey(void) {
    int c = kbd_get_key();
    if (c >= 0) return c;
    int s = serial_getc_nonblock();
    if (s < 0) return -1;
    return pc386_normalise_serial(s);
}

int MMgetchar(void) {
    /* Both PS/2 (IRQ1) and COM1 RX (IRQ4) are interrupt-driven, so we
     * can hlt between checks — the next IRQ wakes us. The check-then-
     * hlt order matters: cli/sti would race with the IRQ posting, so
     * we accept a possible spurious wake and re-check on every loop. */
    for (;;) {
        int c = kbd_get_key();
        if (c >= 0) return c;
        int s = serial_getc_nonblock();
        if (s >= 0) return pc386_normalise_serial(s);
        __asm__ volatile("hlt");
    }
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

/* Pc386 has FatFs on every mounted volume (no LFS). MMBasic's drivecheck
 * default-maps A: → FLASHFILE → LFS path; remap to FATFSFILE so A:/C:
 * both go through the (working) hal_ff_* / FatFs route. */
int port_drivecheck_remap(int t) {
    return (t == FLASHFILE) ? FATFSFILE : t;
}

/* =========================================================================
 *  Cmd_files / cmd_load lifecycle hooks.
 * ========================================================================= */

void cmd_files_save_program_context(void) { }
void cmd_files_restore_program_context(void) { }
void cmd_files_pump_console_key(int *c)   { (void)c; }

/* SaveProgramToFlash on pc386 calls tokenise() which writes through
 * inpbuf/tknbuf — the same buffers ExecuteProgram is currently
 * iterating over in cmd_load. Returning normally would have the
 * outer ExecuteProgram resume on stale token bytes and trip
 * "Unknown command". Bounce back to the prompt instead, like
 * host_native's host_runtime.c does. */
void cmd_load_post_cleanup(void) {
    extern unsigned char inpbuf[];
    memset(inpbuf, 0, STRINGSIZE);
    longjmp(mark, 1);
}

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
