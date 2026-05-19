/*
 * host_runtime.c — host-build runtime lifecycle + console I/O.
 *
 * Contains: host_runtime_configure/finish/timed_out, the runtime
 * timeout/slowdown poll hooks called from CheckAbort/routinechecks, the
 * stdin/stdout console routing (MMInkey, MMgetchar, putConsole,
 * MMputchar, MMPrintString, SerialConsolePutC), REPL state flags
 * (host_repl_mode), the hardware-world global backing store (Option,
 * FontTable, PinDef, inttbl, dma_hw, watchdog_hw, etc. — zero-initialised
 * because nothing drives them on host), and the calendar shim wrappers
 * shims that preserve UTC semantics around host_platform.h's rename.
 *
 * Peripheral cmd_XXX / fun_XXX no-ops live in host_peripheral_stubs.c.
 * FatFS/POSIX/flash/LFS shims live in host_fs_shims.c.
 * FASTGFX + cmd_framebuffer live in host_fastgfx.c.
 */

#include <setjmp.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"
#include "shared/gfx/gfx_pixel_shared.h"
#include "vm_sys_pin.h"
#include "vm_sys_file.h"
#include "vm_host_fat.h"
/* assets/fonts/font1.h defines `font1[]` inline — pulled in exclusively by Draw.c
 * now that FontTable initialisation lives there. */
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "host_terminal.h"
#include "host_fs.h"
#include "host_sim_audio.h"
#include "host_sim_server.h"
#include "host_time.h"
#include "host_fb.h"
#include "host_keys.h"
#include "runtime/runtime.h"
#include "shared/net/mm_net_interrupts.h"

/* Forward declarations for output capture */
extern void (*host_output_hook)(const char *text, int len);
static void host_print(const char *s, int len);
static void host_prints(const char *s);

/* host_sd_root, host_resolve_sd_path, host_append_default_ext, and all
 * POSIX filesystem shims live in host_fs_shims.c. */
extern const char *host_sd_root;

static void host_runtime_check_timeout(void);
static int host_parse_pin_arg(unsigned char *arg);
static const mm_runtime_console_adapter host_console_adapter;

/* Framebuffer state (host_framebuffer, dimensions, fastgfx_back) and the
 * FRAMEBUFFER/LAYER backend live in host_fb.c; see host_fb.h. */
static int host_runtime_timeout_ms = 0;
static uint64_t host_runtime_deadline_us = 0;
static int host_runtime_timed_out_flag = 0;
static int host_screenshot_written = 0;
static char host_screenshot_path[1024] = {0};
static int host_save_option_error_skip = 0;
static char host_save_error_message[MAXERRMSG] = {0};
static int host_save_errno = 0;
static char host_interrupt_return_token[3] = {0};
/* Scripted-key state (host_key_script / host_config_key_*) moved to
 * host_keys.c; see host_keys.h for the consume/peek API. */
/* FastGFX state and cmd_framebuffer/cmd_fastgfx live in host_fastgfx.c. */
extern void host_fastgfx_reset_state(void);

/* =========================================================================
 *  Global Variables — definitions (declared as extern in headers)
 * ========================================================================= */

/* Hardware / system state */
uint32_t _excep_code = 0;
uint64_t _persistent = 0;
uint32_t ADC_dma_chan = 0;
uint32_t ADC_dma_chan2 = 0;
bool ADCDualBuffering = 0;
volatile unsigned int AHRSTimer = 0;
volatile int ConsoleTxBufHead = 0;
volatile int ConsoleTxBufTail = 0;
uint32_t core1stack[256] = {[0] = 0x12345678};
volatile int DISPLAY_TYPE = 0;
/* DisplayNotSet is a function - see function stubs below */
uint32_t dma_rx_chan = 0;
uint32_t dma_rx_chan2 = 0;
uint32_t dma_tx_chan = 0;
uint32_t dma_tx_chan2 = 0;
bool dmarunning = 0;
int64_t *ds18b20Timers = NULL;
const uint8_t *flash_progmemory = NULL;
int GPSchannel = 0;
/* Display, pin, and filesystem globals live in shared state/FileIO modules. */
uint8_t I2C0locked = 0;
uint8_t I2C1locked = 0;
unsigned char IgnorePIN = 0;
unsigned char *InterruptReturn = NULL;
int InterruptUsed = 0;
int last_adc = 0;
lfs_t lfs;
int MMCharPos = 0;
int mmI2Cvalue = 0;
int mmOWvalue = 0;
bool mouse0 = 0;
unsigned char *OnKeyGOSUB = NULL;
unsigned char *OnPS2GOSUB = NULL;
MMFLOAT optionangle = 0;
bool optionfastaudio = 0;
bool optionfulltime = 0;
bool optionlogging = 0;
int PromptFont = 1;
int PromptFC = 0xFFFFFF;
int PromptBC = 0;
volatile int  PS2code = 0;
volatile bool PS2int  = false;
/* ReadBuffer is a function pointer - defined in function pointers section below */
/* Simulated erased-flash regions so Memory.c's scan loops terminate on the
 * first iteration instead of segfaulting on NULL. */
static unsigned char host_saved_vars_flash_buf[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static unsigned char host_cfunction_flash_buf[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
unsigned char *SavedVarsFlash = host_saved_vars_flash_buf;
volatile unsigned int ScrewUpTimer = 0;
/* ScrollLCDSPISCR is a function - see function stubs below */
/* ScrollStart now defined in core/state/display_state.c. */
/* StartEditChar / StartEditPoint are defined in Editor.c now that it is
 * compiled into the host build. */
unsigned char *TickInt[NBRSETTICKS] = {NULL};
volatile int TickTimer[NBRSETTICKS] = {0};
int TickPeriod[NBRSETTICKS] = {0};
volatile unsigned char TickActive[NBRSETTICKS] = {0};
MMFLOAT VCC = 3.3;
bool useoptionangle = 0;
unsigned char WatchdogSet = 0;
/* Break-key state, normally owned by PicoMite.c. Editor.c saves/restores it
 * around the editing session. CTRL-C (0x03) is the MMBasic default. */
unsigned char BreakKey = 3;
/* editactive is now always defined in Editor.c (outside PICOMITEVGA)
 * because FullScreenEditor uses it unconditionally. Keep this comment
 * as a landmark for anyone re-investigating the symbol. */
/* MMAbort is toggled by interrupt handlers on device; on host nothing
 * flips it, but the REPL loop and ExecuteProgram both read it. */
volatile int MMAbort = 0;
/* WAVcomplete / WAVInterrupt / CurrentlyPlaying defined in Audio.c host body. */
volatile unsigned int WDTimer = 0;
/* Display_Refresh is a function - see function stubs below */

/* struct option_s Option lives in core/state/option_state.c. */

/* PinDef array */
const struct s_PinDef PinDef[NBRPINS + 1] = {{0}};

/* CFunctionFlash / CFunctionLibrary are defined by FileIO.c now. We seed
 * CFunctionFlash from mmbasic_runtime_port_begin below so scan loops terminate
 * immediately (host_cfunction_flash_buf is pre-filled with 0xFF to match
 * erased flash). */

/* Timer/system variables */
volatile int64_t mSecTimer = 0;
/* timeroffset + TimeOffsetToUptime are defined in mm_misc_shared.c now. */
extern uint64_t timeroffset;
extern int64_t TimeOffsetToUptime;
volatile unsigned int PauseTimer = 0;
volatile unsigned int IntPauseTimer = 0;
volatile unsigned int Timer1 = 0, Timer2 = 0, Timer3 = 0, Timer4 = 0, Timer5 = 0;
volatile unsigned int diskchecktimer = 0;
volatile unsigned int clocktimer = 0;
volatile int ds18b20Timer = 0;
/* CursorTimer moved to core/state/display_state.c. */
volatile unsigned int I2CTimer = 0;
volatile unsigned int MouseTimer = 0;
volatile unsigned int SecondsTimer = 0;
volatile int day_of_week = 0;
unsigned char PulsePin[NBR_PULSE_SLOTS] = {0};
unsigned char PulseDirection[NBR_PULSE_SLOTS] = {0};
int PulseCnt[NBR_PULSE_SLOTS] = {0};
int PulseActive = 0;
volatile int ClickTimer = 0;
int calibrate = 0;
volatile unsigned int InkeyTimer = 0;
volatile char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE] = {0};
volatile int ConsoleRxBufHead = 0;
volatile int ConsoleRxBufTail = 0;
volatile char ConsoleTxBuf[CONSOLE_TX_BUF_SIZE] = {0};
unsigned char SPIatRisk = 0;
int ExitMMBasicFlag = 0;
unsigned int _excep_peek = 0;
int OptionErrorCheck = 0;
unsigned int CurrentCpuSpeed = 0;
unsigned int PeripheralBusSpeed = 0;
unsigned char EchoOption = 0;
/* OptionFileErrorAbort / filesource / FatFSFileSystem{,Save} / FlashLoad
 * are now defined by FileIO.c (the host build links the shared file). */
volatile unsigned int GPSTimer = 0;
uint16_t AUDIO_L_PIN = 0, AUDIO_R_PIN = 0, AUDIO_SLICE = 0;
uint16_t AUDIO_WRAP = 0;
int ticks_per_second = 1000;
lfs_dir_t lfs_dir;
struct lfs_info lfs_info;
/* lfs_FileFnbr is now defined by FileIO.c. */
int ScreenSize = 0;
unsigned char *DisplayBuf = NULL;
unsigned char *SecondLayer = NULL;
unsigned char *SecondFrame = NULL;
char LCDAttrib = 0;
/* Interrupt-related */
volatile int INT0Value = 0, INT0InitTimer = 0, INT0Timer = 0;
volatile int INT1Value = 0, INT1InitTimer = 0, INT1Timer = 0;
volatile int INT2Value = 0, INT2InitTimer = 0, INT2Timer = 0;
volatile int INT3Value = 0, INT3InitTimer = 0, INT3Timer = 0;
volatile int INT4Value = 0, INT4InitTimer = 0, INT4Timer = 0;
volatile int64_t INT1Count = 0, INT2Count = 0, INT3Count = 0, INT4Count = 0;
volatile uint64_t INT5Count = 0, INT5Value = 0, INT5InitTimer = 0, INT5Timer = 0;
struct s_inttbl inttbl[NBRINTERRUPTS] = {{0}};

/* PWM pin vars */
uint8_t PWM0Apin = 0, PWM0Bpin = 0;
uint8_t PWM1Apin = 0, PWM1Bpin = 0;
uint8_t PWM2Apin = 0, PWM2Bpin = 0;
uint8_t PWM3Apin = 0, PWM3Bpin = 0;
uint8_t PWM4Apin = 0, PWM4Bpin = 0;
uint8_t PWM5Apin = 0, PWM5Bpin = 0;
uint8_t PWM6Apin = 0, PWM6Bpin = 0;
uint8_t PWM7Apin = 0, PWM7Bpin = 0;

/* UART/SPI/I2C pin vars */
uint8_t UART1RXpin = 0, UART1TXpin = 0;
uint8_t UART0TXpin = 0, UART0RXpin = 0;
uint8_t SPI1TXpin = 0, SPI1RXpin = 0, SPI1SCKpin = 0;
uint8_t SPI0TXpin = 0, SPI0RXpin = 0, SPI0SCKpin = 0;
uint8_t I2C1SDApin = 0, I2C1SCLpin = 0;
uint8_t I2C0SDApin = 0, I2C0SCLpin = 0;
uint8_t slice0 = 0, slice1 = 0, slice2 = 0, slice3 = 0;
uint8_t slice4 = 0, slice5 = 0, slice6 = 0, slice7 = 0;
uint8_t SPI0locked = 0, SPI1locked = 0;
volatile int CallBackEnabled = 0;
int ADCopen = 0;
volatile MMFLOAT *volatile a1float = NULL, *volatile a2float = NULL;
volatile MMFLOAT *volatile a3float = NULL, *volatile a4float = NULL;
uint32_t ADCmax = 0;
char *ADCInterrupt = NULL;
short *ADCbuffer = NULL;
volatile uint8_t *adcint = NULL;
uint8_t *adcint1 = NULL, *adcint2 = NULL;
unsigned char *KeypadInterrupt = NULL;
MMFLOAT ADCscale[4] = {0}, ADCbottom[4] = {0};

/* IR related */
void *IrDev = NULL, *IrCmd = NULL;
volatile char IrVarType = 0, IrState = 0, IrGotMsg = 0;
int IrBits = 0, IrCount = 0;
unsigned char *IrInterrupt = NULL;
unsigned int CFuncInt1 = 0, CFuncInt2 = 0, CFuncInt3 = 0, CFuncInt4 = 0;

int p100interrupts[NBRPINS + 1] = {0};

int BacklightSlice = 0, BacklightChannel = 0;

/* QVGA / display */
int QVGA_CLKDIV = 0;
/* ytileheight defined in Memory.c (which builds on host) under #ifndef PICOMITEVGA. */
volatile int X_TILE = 0, Y_TILE = 0;
int CameraSlice = 0, CameraChannel = 0;
char id_out[256] = {0};
uint8_t *buff320 = NULL;
uint16_t SD_CLK_PIN = 0, SD_MOSI_PIN = 0, SD_MISO_PIN = 0, SD_CS_PIN = 0;
bool screen320 = 0;

/* g_myrand provided by MATHS.c */

/* Function pointers for draw */

/* PINMAP */
#ifdef rp2350
const uint8_t PINMAP[48] = {0};
#else
const uint8_t PINMAP[30] = {0};
#endif

/* PinFunction */
const char *PinFunction[64] = {NULL};

/* Host has no USB host stack — USB hooks return 0 (no devices). The
 * full HID[4] array (≈336 B BSS) lives only in USBKeyboard.c on USB
 * device builds. */
int port_usb_count(void) { return 0; }
int port_usb_hid_field(int n, int field) { (void)n; (void)field; return 0; }

/* LFS config is defined by FileIO.c. */

/* Tile color arrays */
uint8_t map16[16] = {0};
uint16_t tilefcols[80*40] = {0};
uint16_t tilebcols[80*40] = {0};

/* MOUSE_CLOCK, MOUSE_DATA */
int MOUSE_CLOCK = 0, MOUSE_DATA = 0;

volatile uint64_t IRoffset = 0;

/* dma_hw and watchdog_hw - dummy storage for host build.
 * Commands.c accesses dma_hw->intf0 etc, so provide enough storage. */
#include "hardware/dma.h"
#include "hardware/structs/watchdog.h"
static dma_hw_t _dma_hw_store = {0};
static watchdog_hw_t _wdog_hw_store = {0};
dma_hw_t *dma_hw = &_dma_hw_store;
watchdog_hw_t *watchdog_hw = &_wdog_hw_store;

/* Pixel primitive used by the rest of this file (draw_line, glyph, etc.).
 * Keeps the same signature as the old static inline so call sites are
 * unchanged. Will be deleted in Phase 2 along with the drawing helpers. */
static void host_draw_pixel_ptr(int x, int y, int c) {
    host_fb_put_pixel(x, y, c);
}

/* host_sim_active: set to 1 when --sim mode is active (see
 * host_sim_server_start). Read by host_sim_audio.c to know whether to
 * record events for the WS stream.
 *
 * The tick thread, key queue, cmd stream, and emit_* recorders all live
 * in host_sim_server.c; this file just declares the flag. */
int host_sim_active = 0;

/* Thin wrapper around host_fb_write_screenshot that honors the once-per-
 * runtime-session guard; host_runtime_configure resets the flag. */
static void host_write_screenshot(const char *path) {
    if (!path || !*path || host_screenshot_written) return;
    host_fb_write_screenshot(path);
    host_screenshot_written = 1;
}

/* host_parse_escaped_char, host_load_key_script, host_keys_ready,
 * host_keydown, host_runtime_configure_keys all moved to host_keys.c. */

void host_runtime_configure(int timeout_ms, const char *screenshot_path) {
    host_runtime_timeout_ms = timeout_ms;
    host_screenshot_written = 0;
    host_runtime_timed_out_flag = 0;
    host_screenshot_path[0] = '\0';
    if (screenshot_path && *screenshot_path) {
        snprintf(host_screenshot_path, sizeof(host_screenshot_path), "%s", screenshot_path);
    }
}

void host_options_snapshot(void);

void mmbasic_runtime_port_begin(void) {
    mmbasic_runtime_console_set_adapter(&host_console_adapter);
    host_runtime_timed_out_flag = 0;
    host_screenshot_written = 0;
    host_fastgfx_reset_state();
    host_runtime_keys_load();
    timeroffset = host_time_us_64();
    host_runtime_deadline_us = 0;
    if (host_runtime_timeout_ms > 0) {
        host_runtime_deadline_us = timeroffset + (uint64_t)host_runtime_timeout_ms * 1000ULL;
    }
    if ((gui_font & 0x0F) == 0) gui_font = 0x01;
    if (gui_font_width <= 0) gui_font_width = 8;
    if (gui_font_height <= 0) gui_font_height = 12;
    if (gui_fcolour == 0) gui_fcolour = 0xFFFFFF;
    host_framebuffer_reset_runtime(gui_bcolour);
    /* FontTable[] is initialised in Draw.c now (static initialiser).
     *
     * Tell Draw.c "a display IS configured" so the 16 `DISPLAY_TYPE == 0
     * → error "Display not configured"` checks in cmd_box / cmd_pixel /
     * cmd_cls / … pass. DISP_USER (28) is the generic user-defined
     * display type that relies entirely on the DrawPixel / DrawRectangle
     * function pointers below — which is exactly the host model. It
     * falls outside every specific-panel range in Draw.c (I2C_PANEL+1 …
     * BufferedPanel, SSDPANEL … VIRTUAL, NEXTGEN+), so none of the
     * hardware-specific code paths fire.
     *
     * HRes/VRes are now defined in Draw.c (initialised to 0 there for
     * device boot); re-sync them to the host framebuffer dimensions so
     * MMBasic's geometry math + the VM's HRes/VRes checks see the same
     * values as the framebuffer plane. */
    Option.DISPLAY_TYPE = DISP_USER;
    HRes = (short)host_fb_width;
    VRes = (short)host_fb_height;
    DrawPixel = host_draw_pixel_ptr;
    DrawRectangle = host_fb_draw_rectangle;
    DrawBitmap = host_fb_draw_bitmap;
    ScrollLCD = host_fb_scroll_lcd;
    ReadBuffer = host_fb_read_buffer;
    /* CFunctionFlash is now defined in FileIO.c (initialised to NULL). Seed
     * it here with a pre-erased 0xFF buffer so the CFunction scan loops
     * terminate immediately — matches the device state after cold boot. */
    CFunctionFlash = host_cfunction_flash_buf;
    /* FatFSFileSystem default: 1 = use B: (vm_host_fat RAM disk or POSIX
     * via host_fs_posix). Keeps BasicFileOpen out of the LFS branch. */
    FatFSFileSystem = FatFSFileSystemSave = 1;
    /* Option.Height is the terminal row count that cmd_files / LIST use
     * for "PRESS ANY KEY" pagination. Zero (LoadOptions' default on
     * host) makes `ListCnt >= Option.Height - overlap` fire immediately,
     * hanging the test harness on stdin it never gets. 1000 effectively
     * disables pagination under batch runs; real REPL users can
     * OPTION HEIGHT to set their own. */
    if (Option.Height == 0) Option.Height = 1000;
    /* Snapshot Option back into flash_option_buf so the reset path in
     * error() (MMBasic.c:2835 calls LoadOptions) restores the *current*
     * host configuration, not a zero-filled default. Without this,
     * every error wipes Option.Width / Height / DISPLAY_CONSOLE / etc.
     * — the symptom is: browser console stops echoing, cmd_files wraps
     * at column 0, ListCnt pagination fires immediately. */
    host_options_snapshot();
}

void host_runtime_finish(void) {
    host_framebuffer_service();
    if (host_screenshot_path[0]) {
        host_write_screenshot(host_screenshot_path);
    }
}

int host_runtime_timed_out(void) {
    return host_runtime_timed_out_flag;
}

/*
 * --slowdown throttle. host_sim_apply_slowdown is implemented per-port
 * (ports/host_native/host_sim_slowdown.c, ports/host_wasm/host_sim_slowdown.c)
 * because the two ports need different timing primitives — native sleeps
 * directly, WASM accumulates to ms-boundaries around ASYNCIFY's 1 ms floor.
 * Storage for host_sim_slowdown_us lives alongside each impl.
 */
extern int host_sim_slowdown_us;
extern void host_sim_apply_slowdown(void);

static void host_runtime_check_timeout(void) {
    host_framebuffer_service();
    host_sim_apply_slowdown();
    /* Always refresh the msec/CursorTimer so code that polls without
     * going through host_sleep_us (e.g. the Editor's ShowCursor+MMInkey
     * loop) still sees time advance. On device the 1ms timer IRQ does
     * this; here we piggy-back on every MMInkey/routinechecks call. */
    uint64_t now = host_time_us_64();
    if (!host_runtime_deadline_us || host_runtime_timed_out_flag) return;
    if (now < host_runtime_deadline_us) return;

    host_runtime_timed_out_flag = 1;
    if (host_screenshot_path[0]) {
        host_write_screenshot(host_screenshot_path);
    }
    longjmp(mark, 1);
}

extern void ProcessWeb(int mode);
extern int host_tcp_interrupt_pending(void);

static inline CommandToken host_commandtbl_decode(const unsigned char *p) {
    return ((CommandToken)(p[0] & 0x7f)) | ((CommandToken)(p[1] & 0x7f) << 7);
}

static void host_runtime_network_service(void) {
    ProcessWeb(0);
}

static void host_runtime_service(void) {
    static int in_service;
    host_runtime_check_timeout();
    mmbasic_runtime_poll_service_once(&in_service, host_runtime_network_service);
}

static const mmbasic_runtime_abort_adapter host_abort_adapter = {
    .service = host_runtime_service,
    .abort_flag = &MMAbort,
    .flags = MMBASIC_RUNTIME_ABORT_FLAG_CHECK_ABORT |
             MMBASIC_RUNTIME_ABORT_FLAG_DO_END_LONGJMP,
};

/* Hardware interaction */
void CheckAbort(void) { mmbasic_runtime_checkabort(&host_abort_adapter); }

static const mmbasic_runtime_interrupt_dispatch_adapter host_interrupt_dispatch = {
    .service = host_runtime_service,
    .tcp_pending = host_tcp_interrupt_pending,
    .udp_pending = NULL,  /* host's UDPreceive flag is driven by shared/net */
    .commandtbl_decode = host_commandtbl_decode,
    .save_option_error_skip = &host_save_option_error_skip,
    .save_error_message = host_save_error_message,
    .save_error_message_size = sizeof(host_save_error_message),
    .save_errno = &host_save_errno,
    .interrupt_return_token = host_interrupt_return_token,
    .interrupt_return_token_size = sizeof(host_interrupt_return_token),
};

void cmd_ireturn(void) {
    mmbasic_runtime_cmd_ireturn(&host_interrupt_dispatch);
}

int check_interrupt(void) {
    return mmbasic_runtime_check_interrupt(&host_interrupt_dispatch);
}
void ClearExternalIO(void) {}
/* CloseAllFiles is provided by FileIO.c. */
/* CloseAudio is provided by Audio.c host body. */
void closeframebuffer(char layer) { host_framebuffer_close(layer); }
void clear320(void) {}
/* DisplayPutC is now the real one from gfx_console_shared.c. It gates on
 * Option.DISPLAY_CONSOLE and calls through the DrawBitmap / DrawRectangle
 * function pointers set up in mmbasic_runtime_port_begin. */
/* Flash write-batch hooks are provided by FileIO.c. */
void initMouse0(int sensitivity) { (void)sensitivity; }
void restorepanel(void) { WriteBuf = NULL; }
void routinechecks(void) { mmbasic_runtime_routinechecks(&host_abort_adapter); }
void SoftReset(void) {}
void uSec(int us) { (void)us; }
uint32_t __get_MSP(void) { return 0xFFFFFFFF; }  /* always pass stack overflow check */

/* Console I/O -- hooks into host_output_hook for output capture */
extern void (*host_output_hook)(const char *text, int len);

/* The bespoke --sim console emulator that once lived here has been
 * removed. Console output now flows through the real device path:
 *   MMputchar → putConsole → DisplayPutC → GUIPrintChar → DrawBitmap
 * where DisplayPutC / GUIPrintChar are the shared functions in
 * gfx_console_shared.c and DrawBitmap points at host_draw_bitmap_fn. */

static void host_print(const char *s, int len) {
    /* Bypass the console-routing machinery — this is only used by
     * MMfputs(stdout) and output-capture, which want raw stdout only. */
    if (host_output_hook) host_output_hook(s, len);
    else fwrite(s, 1, len, stdout);
}

static void host_prints(const char *s) {
    if (s) host_print(s, strlen(s));
}

extern int host_repl_mode;
extern void host_telnet_putc(int c, int flush);
char SerialConsolePutC(char c, int flush);

static int host_console_repl_mode(void) {
#ifdef MMBASIC_STDIO
    return 1;
#else
    return host_repl_mode;
#endif
}

static void host_console_stdout_flush(void) {
    fflush(stdout);
}

static void host_console_ctrl_d(void) {
    MMPrintString("\r\n");
    exit(0);
}

static void host_console_sleep_us(uint32_t us) {
    host_sleep_us(us);
}

static int host_console_sim_key(void) {
#ifdef MMBASIC_SIM
    if (host_sim_active) {
        int c = host_sim_pop_key();
        if (c >= 0) return c;
        if (!host_raw_mode_is_active()) {
            host_sleep_us(1000);
            return -1;
        }
    }
#endif
    return -2;
}

static const mm_runtime_console_adapter host_console_adapter = {
    .name = "host",
#ifdef MMBASIC_STDIO
    .flags = MM_RUNTIME_CONSOLE_FLAG_KEEP_STDIN_LF,
#endif
    .service = host_runtime_service,
    .scripted_key = host_runtime_keys_consume,
    .sim_key = host_console_sim_key,
    .raw_mode_active = host_raw_mode_is_active,
    .read_byte_nonblock = host_read_byte_nonblock,
    .read_byte_blocking_ms = host_read_byte_blocking_ms,
    .push_back_byte = host_push_back_byte,
    .sleep_us = host_console_sleep_us,
    .repl_mode = host_console_repl_mode,
    .on_ctrl_d = host_console_ctrl_d,
    .display_putc = DisplayPutC,
    .serial_putc = SerialConsolePutC,
    .telnet_putc = host_telnet_putc,
    .stdout_flush = host_console_stdout_flush,
    .raw_write = host_print,
};

/* PRet/PInt/PFlt/SRet/SInt/SIntComma/PIntComma/PIntH/PIntB/PIntHC/PIntBC/
 * PFltComma are in MMBasic_Print.c (shared). MMfputs/MMfeof/MMfputc/MMfgetc
 * are now provided by FileIO.c: PRINT and INPUT on host land in those
 * functions, which then dispatch to the SerialPutchar path for COM#s (no-op
 * on host) or to FilePutStr/FileGetChar — which themselves shunt through
 * host_fs_posix_* for POSIX-backed files. Console (fnbr==0) still reaches
 * putConsole → MMputchar via the same dispatch, untouched. */
void MMfopen(unsigned char *fname, unsigned char *mode, int fnbr) { (void)fname; (void)mode; (void)fnbr; }
void MMfclose(int fnbr) { FileClose(fnbr); }
void printoptions(void) {
    extern void port_web_print_options(void);
    port_web_print_options();
}
// getConsole lives in runtime/runtime_console_input_noop.c — shared.
void myprintf(char *s) { host_prints(s); }
char SerialConsolePutC(char c, int flush) {
    /* Host's "serial port" is stdout. Route through host_output_hook
     * when one is installed: SSPrintString (do_end's cursor/SGR reset,
     * Editor VT100 escapes, Prompt cursor nudges) otherwise bypasses
     * the hook and writes raw stdout. Ports that own stdout for other
     * purposes (mmbasic_ansi's render thread) or capture all BASIC
     * output (test harness strip_ansi_sequences) need the SSPrintString
     * stream too.
     *
     * In raw mode OPOST is disabled, so '\n' on its own no longer returns
     * the cursor to column 0 — without this translation, every prompt and
     * error message stair-steps down the terminal. */
    if (host_output_hook) {
        host_output_hook(&c, 1);
        host_telnet_putc(c, flush);
        if (flush) host_telnet_putc(0, -1);
        return c;
    }
    if (Option.Telnet != -1) {
        if (c == '\n' && host_raw_mode_is_active()) fputc('\r', stdout);
        fputc(c, stdout);
        if (flush) fflush(stdout);
    }
    host_telnet_putc(c, flush);
    if (flush) host_telnet_putc(0, -1);
    return c;
}
// kbhitConsole lives in runtime/runtime_console_input_noop.c — shared.


/* FlashWrite*, FlashSetAddress, LoadOptions, SaveOptions, ResetAllFlash,
 * ResetOptions, ResetFlashStorage, CheckSDCard, CrunchData, ClearSavedVars,
 * ForceFileClose, ErrorCheck, positionfile, drivecheck, getfullfilename,
 * GetCWD, InitSDCard are now provided by FileIO.c. Flash writes go through
 * flash_range_* (RAM-backed flash_prog_buf); InitSDCard just needs
 * vm_host_fat to be mounted. */
int64_t CallCFunction(unsigned char *cmd, unsigned char *args, unsigned char *def, unsigned char *caller) {
    (void)cmd; (void)args; (void)def; (void)caller;
    return 0;
}
void CallExecuteProgram(char *p) { (void)p; }


/* host_repl_mode is still used by other host stubs to branch on "are we
 * running the interactive REPL?". The bespoke EditInputLine previously
 * here was replaced by the shared device implementation now in
 * MMBasic_Prompt.c. */
int host_repl_mode = 0;

/* timegm / gmtime shim wrappers retired; every caller now routes
 * through drivers/calendar/calendar_bare.c via hal/hal_calendar.h.
 * Removed the host_platform.h rename trick at the same time. */

/* FileIO.c command-level lifecycle hooks. The device implementations live
 * in ports/pico_sdk_common/cmd_files_hooks.c. */

void cmd_files_save_program_context(void)
{
    /* Host can't SaveContext + InitHeap mid-FRUN — bc_alloc backs both
     * the heap and the live VMState. The 76 KB FILES sort buffer fits
     * fine in host RAM without the dance. */
}

void cmd_files_restore_program_context(void) {}

void cmd_files_pump_console_key(int *c)
{
    /* Host has no interrupt-driven ConsoleRxBuf filler — the REPL reads
     * keys via MMInkey (stdin / scripted key queue / --sim websocket).
     * Poll it here so the "PRESS ANY KEY" prompt unblocks on any
     * keypress instead of hanging forever. */
    if (*c == -1) {
        int k = MMInkey();
        if (k != -1) *c = k;
        else host_sleep_us(10000);  /* 10ms — don't peg a core */
    }
}

/* cmd_load_post_cleanup — shared default body in
 * runtime/runtime_cmd_load_post_cleanup.c (Finding 9). */

extern volatile BYTE SDCardStat;

int port_mount_sd_drive(void)
{
    /* Host FatFS is runtime/vm/vm_host_fat.c's in-memory disk (or the POSIX dir
     * walker when host_sd_root is set). No SPI/SD pins to validate —
     * just make sure the RAM disk is mounted. Also clear SDCardStat's
     * "no disk / not initialised" bits: they block fun_dir and other
     * callers that test them after a successful init (`SDCardStat &
     * STA_NOINIT` → "SD card not found" on host). */
    if (vm_host_fat_mount() != FR_OK) error("Host FAT init failed");
    SDCardStat = 0;
    return 2;
}

void port_apply_load_overrides(void)
{
    /* Host has no display/SD/audio pins to override; LoadOptions's flash
     * read into Option already wins. Real per-board overrides for device
     * builds live in ports/pico_sdk_common/port_load_overrides.c. */
}

/* port_drivecheck_remap + port_filesystem_prefix live in
 * runtime/runtime_filesystem_defaults.c — shared with Pico + ESP32.
 * pc386 provides real overrides (DOS drive-letter routing). */

void port_drive_check(char drive)
{
    /* Host has only one logical disk (B:, backed by POSIX under
     * host_sd_root or the vm_host_fat RAM disk). The A: drive is the
     * device's LittleFS-on-flash filesystem; LFS is stubbed on host,
     * so switching to A: lands on a broken branch and subsequent
     * FILES / COPY / etc. trip on the stubbed lfs_* calls (one
     * side-effect being console-routing corruption). Treat A: as an
     * error; B: is always available — no SD_CS pin to check. */
    if (drive == 'A') error("A: drive not available on host");
    if (drive != 'B') error("Invalid disk");
}

/* PicoCalc HW hooks — error stubs on host. Real impls in
 * ports/pico_sdk_common/picocalc_features_real.c on PicoCalc ports. */
void port_picocalc_set_keyboard_backlight(int level) { (void)level; error("Not supported on host"); }
int  port_picocalc_battery_pct(void)                 { error("Not supported on host"); return 0; }
int  port_picocalc_is_charging(void)                 { error("Not supported on host"); return 0; }
void port_picocalc_factory_reset_options(void)       { error("Not supported on host"); }

/* CONFIGURE LIST entries — host advertises no factory board profiles. */
void port_print_supported_boards(void) {}
int  port_factory_reset_board(unsigned char *p) { (void)p; return 0; }

/* Display-related OPTION setters (CPUSPEED, AUTOREFRESH, LCDPANEL, TOUCH,
 * RESOLUTION, VGA PINS, DEFAULT MODE) — host has no display hardware. */
int  port_display_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION LIST display-related lines — host has no display hardware. */
void port_print_display_options(void) {}
void port_print_lcd_spi(void) {}
void port_print_keyboard_heartbeat(void) {}
void port_print_usb_kb_repeat(void) {}
void port_clear_lcd_spi_if_shares_system(void) {}
int port_pinno_alias_for_name(const char *name) { (void)name; return 0; }
int port_pin_is_reserved_alias(int pin) { (void)pin; return 0; }
const char *port_pin_reserved_label(int pin) { (void)pin; return NULL; }
int port_lcd320_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION KEYBOARD setter — host has no keyboard config to set. */
int port_keyboard_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION HDMI PINS / KEYBOARD BACKLIGHT / PSRAM PIN / KEYBOARD REPEAT /
 * PS2 PINS / MOUSE — peripheral pin/feature setters. Host has none. */
int port_misc_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION PICO ON/OFF (CYW43-shadow pin gating) and OPTION HEARTBEAT
 * (pin selection). Host has neither. */
int port_pico_pins_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }
int port_heartbeat_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION LCDPANEL CONSOLE color reset — host has no tile-mode console. */
void port_apply_default_console_colors(int default_fc, int default_bc)
{ (void)default_fc; (void)default_bc; }

/* OPTION SYSTEM SPI / OPTION LCD SPI — host has no SPI peripheral. */
int port_system_lcd_spi_option_setter(unsigned char *cmdline) { (void)cmdline; return 0; }

/* OPTION AUDIO I2S — host has no I2S peripheral. */
int port_audio_i2s_pio_slice(int pin1, int pin2) { (void)pin1; (void)pin2; return 0; }

/* MM.INFO INTERRUPTS — host has no NVIC. */
int port_mminfo_interrupts(int64_t *out_iret) { (void)out_iret; return 0; }

/* MM.INFO TOUCH / SCROLL / SCREENBUFF — host has none. */
int port_mminfo_touch_status(unsigned char *out_sret) { (void)out_sret; return 0; }
int port_mminfo_scroll_start(int64_t *out_iret) { (void)out_iret; return 0; }
int port_mminfo_screenbuff(int64_t *out_iret) { (void)out_iret; return 0; }

/* PIO interrupt-poll lookup — host has no PIO. */
#include "hardware/pio.h"
PIO port_pio_for_index(int pio_idx) { (void)pio_idx; return NULL; }

/* POKE DISPLAY raw panel write — host has no display panel. */
int port_poke_display_panel(unsigned char *p) { (void)p; return 0; }

/* str_replace/STR_REPLACE provided by MATHS.c */

/* bc_debug.c crash-dump port hooks — host has no ARM fault registers. */
#include "bytecode.h"  /* BCCrashInfo */
uint32_t port_bc_crash_get_sp(void) { return 0; }
void port_bc_crash_save_fault_regs(BCCrashInfo *info) { (void)info; }

/* MMBasic.c error-prompt-font hook — host has no SetFont / narrow
 * font switch, so the hook is a no-op. Real impls live in
 * ports/pico_sdk_common/mmbasic_port_pico.c and ports/hdmi_rp2350/
 * mmbasic_port_hdmi.c. */
void port_select_error_prompt_font(void) {}

/* MMBasic.c ClearRuntime display-reset hook — host has no SPI-LCD
 * panel to reset. Real impl in ports/pico_sdk_common/clear_runtime_port.c. */
void port_clear_runtime_display_reset(void) {}

/* MMBasic.c error() display hooks — host has no WriteBuf/DisplayBuf
 * or LCD panel, so both stub to no-ops. */
void port_error_restore_console_surface(void) {}
void port_error_show_lcd_banner(int line_num, const char *source_line, const char *err_msg) {
    (void)line_num;
    (void)source_line;
    (void)err_msg;
}

/* MMBasic.c FindSubFun hash-lookup hook — host has no funtbl hash
 * (rp2350-only). Return 0 to fall through to the linear scan. */
int port_try_find_subfun_hash(unsigned char *p, int *out_index) {
    (void)p;
    (void)out_index;
    return 0;
}

/* MMBasic.c PrepareProgram finalize hook — rp2350 rebuilds the
 * funtbl[] hash from subfun[] and runs hashlabels() against
 * ProgMemory + LibMemory. Host has neither, so no-op. */
void port_prepare_program_finalize_subfun(int ErrAbort) { (void)ErrAbort; }

/* MMBasic.c findlabel hash-lookup hook — host has no funtbl label
 * hash (rp2350-only). Return 0 to fall through to the linear scan. */
int port_try_find_label_hash(unsigned char *labelptr, unsigned char **out_ptr) {
    (void)labelptr;
    (void)out_ptr;
    return 0;
}

/* MMBasic.c findvar / sub-fun collision check — rp2350 probes the
 * funtbl[] hash directly. Host has no hash; return 0 so findvar runs
 * its linear subfun[] scan fallback. */
int port_try_check_var_subfun_collision(const unsigned char *name, int namelen) {
    (void)name;
    (void)namelen;
    return 0;
}

/* bc_bridge.c subfun-hash hooks — rp2350 maintains a funtbl[] hash
 * alongside subfun[] for O(1) FindSubFun lookups; rp2040 + host use
 * the linear scan, so the hooks are no-ops. Real impl lives in
 * ports/pico_sdk_common/bc_bridge_pico.c. */
void port_bc_bridge_clear_subfun_hash(void) {}
void port_bc_bridge_rehash_subfun(unsigned char **subfun_arr) { (void)subfun_arr; }

/* vm_sys_time port hook — host picks up MMBASIC_HOST_DATE /
 * MMBASIC_HOST_TIME env-var overrides (tests pin deterministic values
 * so interpreter-vs-VM output comparison is stable) and falls back to
 * localtime(). vm_sys_time.c formats the result into MMBasic string
 * buffers. Device impl lives in ports/pico_sdk_common/vm_sys_time_pico.c. */
#include <time.h>
int port_vm_time_get_tm(struct tm *out) {
    /* Start from wall-clock localtime; env-var overrides apply per
     * field so MMBASIC_HOST_DATE affects only DATE$ and
     * MMBASIC_HOST_TIME affects only TIME$, matching the pre-refactor
     * semantics where the two BASIC functions had separate mocks. */
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (!lt) return 0;
    *out = *lt;
    const char *mock_date = getenv("MMBASIC_HOST_DATE");
    if (mock_date && *mock_date) {
        int d = 1, mo = 1, y = 2020;
        sscanf(mock_date, "%d-%d-%d", &d, &mo, &y);
        out->tm_mday = d;
        out->tm_mon  = mo - 1;
        out->tm_year = y - 1900;
    }
    const char *mock_time = getenv("MMBASIC_HOST_TIME");
    if (mock_time && *mock_time) {
        int h = 0, mi = 0, s = 0;
        sscanf(mock_time, "%d:%d:%d", &h, &mi, &s);
        out->tm_hour = h;
        out->tm_min  = mi;
        out->tm_sec  = s;
    }
    return 1;
}
