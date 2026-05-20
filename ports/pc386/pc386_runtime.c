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
#include "runtime/runtime.h"
#include "runtime/runtime_console_escdecode.h"
#include "hal/hal_time.h"

#include "../../drivers/i8042_kbd/i8042_kbd.h"
#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_mode13h/vga_mode13h.h"
#include "pc386_panic.h"

#ifdef PC386_BOOT_TRACE
extern void kputs(const char *s);
#define PC386_TRACE(s) kputs(s)
#else
#define PC386_TRACE(s) ((void)0)
#endif

extern jmp_buf mark;          /* defined by MMBasic.c */

/* Forward decls for the output hook contract host_runtime.c established. */
void (*host_output_hook)(const char *text, int len) = NULL;

extern uint64_t hal_time_us_64(void);
extern uint64_t timeroffset;
extern void flash_range_erase(uint32_t off, uint32_t count);
extern void pc386_options_defaults_ready(void);

#define PC386_DEFAULT_REPEAT_START_MS 350
#define PC386_DEFAULT_REPEAT_RATE_MS   75

/* =========================================================================
 *  Runtime lifecycle
 * ========================================================================= */

void pc386_apply_runtime_option_defaults(void)
{
    /* Sensible Option defaults for a freshly-booted pc386 kernel.
     * LoadOptions reads from a memset-zero options buffer on first
     * boot which leaves every field at 0. Override the few that have
     * meaningful non-zero defaults on every other port; leave the rest
     * (Listcase=TITLE, continuation=off, Refresh=off, etc.) at zero
     * so the user's OPTION X commands aren't silently overridden.
     *
     *   Tab = 4 — Editor TAB stop
     *   DefaultFont/FC/BC — fresh zeroed options would otherwise draw
     *                       black text on a black graphics console. */
    if (Option.Tab    == 0) Option.Tab    = 4;
    if (Option.RepeatStart == 0) Option.RepeatStart = PC386_DEFAULT_REPEAT_START_MS;
    if (Option.RepeatRate  == 0) Option.RepeatRate  = PC386_DEFAULT_REPEAT_RATE_MS;
    if (Option.DefaultFont == 0) Option.DefaultFont = 1;  /* font 1, 8x12 */
    if (Option.DefaultFC == 0 && Option.DefaultBC == 0) {
        Option.DefaultFC = WHITE;
        Option.DefaultBC = BLACK;
    }
}

void mmbasic_runtime_port_begin(void) {
    PC386_TRACE("RT:entry, ");
    timeroffset = hal_time_us_64();
    PC386_TRACE("RT:time, ");

    /* Tell Draw.c "a display is configured" so cmd_box / cmd_pixel
     * don't error. DISP_USER (28) skips all panel-specific code paths
     * in Draw.c. Same trick host_runtime.c uses. */
    Option.DISPLAY_TYPE = DISP_USER;
    PC386_TRACE("RT:display-type, ");

    /* Seed CFunctionFlash from the pre-erased buffer in pc386_state.c
     * so CFunction scan loops terminate immediately (mirrors host). */
    extern unsigned char pc386_cfunction_buf[];
    CFunctionFlash = pc386_cfunction_buf;
    PC386_TRACE("RT:cfunc, ");

    pc386_apply_runtime_option_defaults();
    PC386_TRACE("RT:defaults, ");
    pc386_options_defaults_ready();
    PC386_TRACE("RT:defaults-ready, ");

    /* Stage 5 display: VGA/VBE framebuffer is the primary console.
     * Keep runtime output screen-only on real hardware; boot diagnostics
     * still go through kputs/serial before the REPL starts. */
    vga_mode13h_init();
    PC386_TRACE("RT:vga, ");
    SetFont(Option.DefaultFont);
    PC386_TRACE("RT:font, ");
    gui_fcolour = PromptFC = Option.DefaultFC;
    gui_bcolour = PromptBC = Option.DefaultBC;
    PromptFont = Option.DefaultFont;
    Option.DISPLAY_CONSOLE = 1;
    OptionConsole = 2;                         /* display only */
    Option.Width = HRes / gui_font_width;
    Option.Height = VRes / gui_font_height;
    CurrentX = CurrentY = 0;
    ClearScreen(gui_bcolour);
    PC386_TRACE("RT:clear, ");

    /* Route file ops through FatFs (=1) rather than LFS (=0). Pc386 has
     * real FAT volumes mounted as A: (FDC), B: (second FDC if present),
     * and C: (primary IDE hard disk); LFS is
     * stubbed (panic on use). Without this, FILES / LOAD / SAVE etc.
     * end up at lfs_* and surface as "Error during device operation". */
    extern int FatFSFileSystem, FatFSFileSystemSave;
    FatFSFileSystem = FatFSFileSystemSave = 1;
    extern char filepath[][FF_MAX_LFN];
    strcpy(filepath[1], "C:/");
    PC386_TRACE("RT:fatfs, ");

    /* Persist the live Option struct back to flash_option_buf so the
     * LoadOptions inside error()'s reset path doesn't wipe these
     * defaults. SaveOptions → hal_flash_write_options →
     * pc386_options_snapshot copies live Option to the buffer. */
    SaveOptions();
    PC386_TRACE("RT:save-options, ");
}

void host_runtime_finish(void) { }
int  host_runtime_timed_out(void) { return 0; }

/* =========================================================================
 *  CheckAbort + interrupt poll hooks (all no-ops for stage 3 — no IRQs).
 * ========================================================================= */

void CheckAbort(void)         { mmbasic_runtime_checkabort(NULL); }
int  check_interrupt(void)    { return 0; }
void ClearExternalIO(void)    { }
void closeframebuffer(char l) { (void)l; }
void clear320(void)           { }
void initMouse0(int s)        { (void)s; }
void restorepanel(void)       { WriteBuf = NULL; }
void routinechecks(void)      { mmbasic_runtime_routinechecks(NULL); }
void SoftReset(void)          { pc386_panic("SoftReset not yet implemented"); }
void uSec(int us)             { extern void hal_time_sleep_us(uint32_t); hal_time_sleep_us((uint32_t)us); }

/* MMBasic.c uses __get_MSP for stack-overflow protection. PC has no MSP
 * concept (and we have plenty of stack); return a value that always
 * passes the comparison. */
uint32_t __get_MSP(void) { return 0xFFFFFFFFu; }

/* DisplayPutC lives in gfx_console_shared.c — don't redefine. */
void Display_Refresh(void) { }
void DisplayNotSet(void) { /* no error — mmbasic_runtime_port_begin sets DISPLAY_TYPE */ }
void ScrollLCDSPISCR(int s) { (void)s; }

/* =========================================================================
 *  Console I/O — fork every byte to BOTH the graphics console (primary,
 *  visible on the QEMU window / real-hardware monitor) AND COM1 serial
 *  (backup / remote terminal / test harness). Mirrors the PicoMite
 *  shape: display is the local console, serial carries an ANSI-capable
 *  mirror.
 * ========================================================================= */

char SerialConsolePutC(char c, int flush) {
    (void)flush;
    if (host_output_hook) {
        /* Test-harness output-capture path (host_native uses this).
         * pc386 leaves host_output_hook NULL so this branch never
         * fires here, but keep it for symmetry with host's contract. */
        host_output_hook(&c, 1);
        return c;
    }
    serial_putc(c);
    return c;
}

void putConsole(int c, int flush) {
    /* Honour the OPTION CONSOLE routing bits (1 = serial, 2 = display). */
    if (OptionConsole & 2) DisplayPutC((char)c);
#ifdef PORT_PC386
    (void)flush;
#else
    if (OptionConsole & 1) SerialConsolePutC((char)c, flush);
#endif
}

// MMputchar lives in runtime/runtime_console_putchar.c — shared across every port.
// MMPrintString / SSPrintString live in runtime/runtime_console_printstring.c.
// pc386's bulk_flush fallback ends up calling pc386_libc.c::fflush which is
// a no-op shim, so the original "no trailing flush" behaviour is preserved.

/* PS/2 is IRQ-driven (drivers/i8042_kbd) and produces MMBasic-shaped key
 * codes (UP, BKSP, F1, etc.) directly; COM1 serial goes through the
 * shared escape decoder + mmbasic_console_normalise_byte. */

/* Per-call byte reader for the shared escape decoder. Used ONLY for
 * COM1 serial input; PS/2 already pre-decodes to MMBasic key codes. */
static int pc386_escdecode_read_byte_ms(int timeout_ms) {
    /* Busy-poll up to timeout_ms in ~1ms slices. hal_time_sleep_us
     * yields the CPU between checks. */
    int waited = 0;
    while (waited <= timeout_ms) {
        int s = serial_getc_nonblock();
        if (s >= 0) return s;
        hal_time_sleep_us(1000);
        waited++;
    }
    return -1;
}

int MMInkey(void) {
    /* Drain any chars left over from an earlier unrecognised escape
     * sequence before consulting the input sources. */
    {
        int pb = mmbasic_escdecode_pop_pushback();
        if (pb >= 0) return pb;
    }
    int c = kbd_get_key();
    if (c >= 0) return c;
    int s = serial_getc_nonblock();
    if (s < 0) return -1;
    if (s == 0x1b) return mmbasic_escdecode_run(pc386_escdecode_read_byte_ms);
    return mmbasic_console_normalise_byte(s);
}

int pc386_keyboard_repeat_start_ms(void) {
    return Option.RepeatStart > 0 ? Option.RepeatStart : PC386_DEFAULT_REPEAT_START_MS;
}

int pc386_keyboard_repeat_rate_ms(void) {
    return Option.RepeatRate > 0 ? Option.RepeatRate : PC386_DEFAULT_REPEAT_RATE_MS;
}

int MMgetchar(void) {
    /* Both PS/2 (IRQ1) and COM1 RX (IRQ4) are interrupt-driven, so we
     * can hlt between checks — the next IRQ wakes us. The check-then-
     * hlt order matters: cli/sti would race with the IRQ posting, so
     * we accept a possible spurious wake and re-check on every loop. */
    for (;;) {
        ShowCursor(1);
        int c = kbd_get_key();
        if (c >= 0) {
            ShowCursor(0);
            return c;
        }
        int s = serial_getc_nonblock();
        if (s >= 0) {
            ShowCursor(0);
            if (s == 0x1b) return mmbasic_escdecode_run(pc386_escdecode_read_byte_ms);
            return mmbasic_console_normalise_byte(s);
        }
        __asm__ volatile("sti" : : : "memory");
        hal_time_sleep_us(1000);
    }
}

// getConsole / kbhitConsole live in runtime/runtime_console_input_noop.c — shared.
void myprintf(char *s)     { MMPrintString(s); }

/* MMfopen/MMfclose/MMgetline — file I/O routes through hal_filesystem;
 * this layer is the dispatch handle. Stage 3f wires real bodies. */
void MMfopen(unsigned char *fname, unsigned char *mode, int fnbr) {
    (void)fname; (void)mode; (void)fnbr;
}

void MMfclose(int fnbr) { FileClose(fnbr); }

// MMgetline lives in runtime/runtime_getline.c — shared across every port.

void printoptions(void) {
    char line[96];
    snprintf(line, sizeof(line), "OPTION TAB %d\r\n", Option.Tab);
    MMPrintString(line);
    if (Option.RepeatStart != PC386_DEFAULT_REPEAT_START_MS ||
        Option.RepeatRate != PC386_DEFAULT_REPEAT_RATE_MS) {
        snprintf(line, sizeof(line), "OPTION KEYBOARD REPEAT %d,%d\r\n",
                 Option.RepeatStart, Option.RepeatRate);
        MMPrintString(line);
    }
    snprintf(line, sizeof(line), "OPTION DEFAULT COLOURS RGB(%d), RGB(%d)\r\n",
             Option.DefaultFC, Option.DefaultBC);
    MMPrintString(line);
    unsigned sb_dma = Option.pc386_sb_dma;
    if (Option.pc386_sb_base == 0) sb_dma = 1;
    snprintf(line, sizeof(line), "OPTION SB16 &H%X, %u, %u, %u\r\n",
             (unsigned)(Option.pc386_sb_base ? Option.pc386_sb_base : 0x220),
             (unsigned)(Option.pc386_sb_irq ? Option.pc386_sb_irq : 5),
             sb_dma,
             (unsigned)(Option.pc386_sb_dma16 ? Option.pc386_sb_dma16 : 5));
    MMPrintString(line);
    MMPrintString("OPTIONS.INI C:/OPTIONS.INI\r\n");
}

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
 * default-maps A: -> FLASHFILE -> LFS path; remap to FATFSFILE so all
 * DOS drive letters go through the hal_ff_* / FatFs route. */
int port_drivecheck_remap(int t) {
    return (t == FLASHFILE) ? FATFSFILE : t;
}

static char pc386_fatfs_drive = 'C';
static char pc386_fatfs_cwd[3][FF_MAX_LFN] = { "A:/", "B:/", "C:/" };
extern char filepath[][FF_MAX_LFN];

static int pc386_fatfs_drive_index(char drive)
{
    if (drive == 'A') return 0;
    if (drive == 'B') return 1;
    return 2;
}

static void pc386_store_fatfs_cwd(char drive)
{
    int idx = pc386_fatfs_drive_index(drive);
    strncpy(pc386_fatfs_cwd[idx], filepath[1], sizeof(pc386_fatfs_cwd[idx]) - 1);
    pc386_fatfs_cwd[idx][sizeof(pc386_fatfs_cwd[idx]) - 1] = 0;
}

static void pc386_load_fatfs_cwd(char drive)
{
    int idx = pc386_fatfs_drive_index(drive);
    if (pc386_fatfs_cwd[idx][0] == 0) {
        snprintf(pc386_fatfs_cwd[idx], sizeof(pc386_fatfs_cwd[idx]), "%c:/", drive);
    }
    strcpy(filepath[1], pc386_fatfs_cwd[idx]);
}

const char *port_filesystem_prefix(int filesystem) {
    static char prefix[3] = "C:";
    if (!filesystem) return "A:";
    prefix[0] = pc386_fatfs_drive;
    return prefix;
}

/* =========================================================================
 *  Cmd_files / cmd_load lifecycle hooks.
 * ========================================================================= */

void cmd_files_save_program_context(void) { }
void cmd_files_restore_program_context(void) { }
void cmd_files_pump_console_key(int *c)   { (void)c; }

/* cmd_load_post_cleanup — shared default body in
 * runtime/runtime_cmd_load_post_cleanup.c (Finding 9). pc386's
 * SaveProgramToFlash uses inpbuf/tknbuf so the longjmp default is
 * exactly what we want here. */

/* CallCFunction / CallExecuteProgram — pc386 has no CFunction support. */
int64_t CallCFunction(unsigned char *cmd, unsigned char *args, unsigned char *def, unsigned char *caller) {
    (void)cmd; (void)args; (void)def; (void)caller;
    return 0;
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
    /* A:/B:/C: are mounted from kmain via FatFs over FDC/ATA. Nothing
     * to do here — already mounted. SDCardStat-clear matches host. */
    extern volatile BYTE SDCardStat;
    SDCardStat = 0;
    return 2;
}

void port_apply_load_overrides(void) {
    if (Option.pc386_sb_base == 0) {
        Option.pc386_sb_base = 0x220;
        Option.pc386_sb_irq = 5;
        Option.pc386_sb_dma = 1;
        Option.pc386_sb_dma16 = 5;
        return;
    }
    if (Option.pc386_sb_irq == 0) Option.pc386_sb_irq = 5;
    if (Option.pc386_sb_dma == 0 || Option.pc386_sb_dma > 3 || Option.pc386_sb_dma == 2) {
        Option.pc386_sb_dma = 1;
    }
    if (Option.pc386_sb_dma16 == 0) Option.pc386_sb_dma16 = 5;
}
void port_drive_check(char drive) {
    if (drive >= 'a' && drive <= 'z') drive = (char)(drive - 'a' + 'A');
    if (drive != 'A' && drive != 'B' && drive != 'C') error("Invalid disk");
    pc386_store_fatfs_cwd(pc386_fatfs_drive);
    pc386_fatfs_drive = drive;
    pc386_load_fatfs_cwd(pc386_fatfs_drive);
}

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
int  port_keyboard_option_setter(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
    if (!tp) return 0;
    getargs(&tp, 3, (unsigned char *)",");
    Option.RepeatStart = getint(argv[0], 100, 2000);
    Option.RepeatRate = getint(argv[2], 25, 2000);
    SaveOptions();
    return 1;
}
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

/* Draw.c terminal hooks live in runtime/runtime_terminal_hooks_noop.c —
 * shared across every port with a real framebuffer. */

/* mmbasic_timegm / mmbasic_gmtime shim wrappers retired — the
 * pc386_platform.h header never had a rename macro pointing at them,
 * so the bodies were dead code. All BASIC datetime paths now go
 * through hal_calendar (drivers/calendar/calendar_bare.c). */
