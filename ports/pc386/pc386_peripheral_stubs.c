/*
 * ports/pc386/pc386_peripheral_stubs.c — actual no-op stubs for the
 * peripherals an IBM-PC has none of (I2C, SPI, PWM, PIO, GPS, UART
 * specials, IR, DHT22, OneWire, etc.).
 *
 * Real drivers live in their own files (split by hardware distinction):
 *   pc386_cpu.c     — cmd_cpu
 *   pc386_rtc.c     — cmd_rtc + CMOS helpers
 *   pc386_memio.c   — cmd_poke, fun_peek (memory + I/O ports)
 *   pc386_lpt.c     — cmd_pin, cmd_setpin, fun_pin, Ext* / Pin* glue
 *   pc386_interp.c  — cmd_settick, cmd_ireturn, cmd_watchdog, MM.INFO,
 *                     GetIntAddress (interpreter glue, not a HW driver)
 *
 * Stubs that take parameters and would otherwise silently accept bad
 * input get pc386_panic so the gap surfaces. Most are silent so a
 * BASIC program that incidentally touches an unsupported feature
 * doesn't blow up.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"
#include "hal/hal_filesystem.h"
#include "OptionCommands.h"
#include "runtime/runtime.h"
#include "vm_sys_pin.h"
#include "drivers/vga_mode13h/vga_mode13h.h"

#include "pc386_panic.h"

#include <errno.h>

#ifdef PinRead
#undef PinRead
#endif

extern void pc386_audio_apply_options(void);
extern void printoptions(void);
extern int pc386_parse_lpt_pin(unsigned char * arg);

/* ------------------------------------------------------------------ */
/* cmd_* — most are silent no-ops on host, same here.                 */
/* ------------------------------------------------------------------ */

void cmd_adc(void) {}
void cmd_backlight(void) {}
void cmd_camera(void) {}
void cmd_cfunction(void) {}
void cmd_Classic(void) {}
void cmd_configure(void) {}
void cmd_csubinterrupt(void) {}
void cmd_device(void) {}
void cmd_DHT22(void) {}
void cmd_ds18b20(void) {}
void cmd_endprogram(void) {}
void cmd_fastgfx(void) {
    unsigned char * p;
    if ((p = checkstring(cmdline, (unsigned char *)"CREATE"))) {
        checkend(p);
        vga_mode13h_fastgfx_create();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        checkend(p);
        vga_mode13h_fastgfx_close();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SWAP"))) {
        checkend(p);
        vga_mode13h_fastgfx_swap();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        checkend(p);
        vga_mode13h_fastgfx_sync();
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"FPS"))) {
        vga_mode13h_fastgfx_set_fps(getint(p, 1, 1000));
        return;
    }
    error("Syntax");
}
void cmd_framebuffer(void) {
    error("FRAMEBUFFER not available on mode 13h display");
}
void cmd_i2c(void) {}
void cmd_i2c2(void) {}
void cmd_in(void) {}
void cmd_ir(void) {}
void cmd_irq(void) {}
void cmd_irqclear(void) {}
void cmd_irqnowait(void) {}
void cmd_irqset(void) {}
void cmd_irqwait(void) {}
void cmd_jmp(void) {}
void cmd_keypad(void) {}
void cmd_label(void) {}
void cmd_lcd(void) {}
void cmd_library(void) {}
void cmd_mouse(void) {}
void cmd_mov(void) {}
void cmd_nop(void) {}
void cmd_Nunchuck(void) {}
void cmd_onewire(void) {}
static int pc386_option_sb16(unsigned char * p) {
    p = checkstring(p, (unsigned char *)"SB16");
    if (!p) return 0;

    getargs(&p, 7, (unsigned char *)",");
    if (!(argc == 1 || argc == 3 || argc == 5 || argc == 7)) error("Argument count");
    int base = getint(argv[0], 0x200, 0x280);
    int irq = Option.pc386_sb_irq ? Option.pc386_sb_irq : 5;
    int dma = Option.pc386_sb_dma;
    int dma16 = Option.pc386_sb_dma16 ? Option.pc386_sb_dma16 : 5;
    if (argc >= 3) irq = getint(argv[2], 2, 15);
    if (argc >= 5) dma = getint(argv[4], 0, 3);
    if (argc >= 7) dma16 = getint(argv[6], 5, 7);
    if (dma == 2) error("DMA channel 2 is reserved for cascade");

    Option.pc386_sb_base = (uint16_t)base;
    Option.pc386_sb_irq = (uint8_t)irq;
    Option.pc386_sb_dma = (uint8_t)dma;
    Option.pc386_sb_dma16 = (uint8_t)dma16;
    SaveOptions();
    pc386_audio_apply_options();
    return 1;
}

extern int port_keyboard_option_setter(unsigned char * cmdline);

void cmd_option(void) {
    if (checkstring(cmdline, (unsigned char *)"LIST")) {
        printoptions();
        return;
    }
    if (checkstring(cmdline, (unsigned char *)"RESET")) {
        ResetOptions(false);
        return;
    }
    if (option_command_handle_common(cmdline, true)) return;
    if (port_keyboard_option_setter(cmdline)) return;
    if (pc386_option_sb16(cmdline)) return;
    error("Unknown option");
}
void cmd_out(void) {}
void cmd_pio(void) {}
void cmd_PIOline(void) {}
void cmd_port(void) {}
void cmd_program(void) {}
void cmd_pull(void) {}
void cmd_pulse(void) {}
void cmd_push(void) {}
void cmd_pwm(void) {
    error("PWM not available on PC386");
}
void cmd_Servo(void) {
    error("Servo not available on PC386");
}
void cmd_set(void) {}
void cmd_spi(void) {}
void cmd_spi2(void) {}
void cmd_steppedstream(void) {}
void cmd_synth(void) {}
void cmd_temp(void) {}
void cmd_uart(void) {}
void cmd_web(void) {}
void cmd_wii(void) {}
void cmd_ws2812(void) {}
void cmd_WS2812(void) {}
void cmd_sideset(void) {}
void cmd_sync(void) {}
static int pc386_sys_copy_file(const char * src, const char * dst) {
    hal_fs_fd_t in = 0, out = 0;
    int rc = hal_fs_open(src, HAL_FS_O_RDONLY, &in);
    if (rc < 0) return rc;
    hal_fs_unlink(dst);
    rc = hal_fs_open(dst, HAL_FS_O_WRONLY | HAL_FS_O_CREAT | HAL_FS_O_TRUNC, &out);
    if (rc < 0) {
        hal_fs_close(in);
        return rc;
    }

    char buf[512];
    for (;;) {
        ssize_t r = hal_fs_read(in, buf, sizeof(buf));
        if (r < 0) {
            rc = (int)r;
            break;
        }
        if (r == 0) break;
        ssize_t w = hal_fs_write(out, buf, (size_t)r);
        if (w != r) {
            rc = (w < 0) ? (int)w : -EIO;
            break;
        }
    }

    if (rc == 0) rc = hal_fs_sync(out);
    int close_out = hal_fs_close(out);
    int close_in = hal_fs_close(in);
    if (rc == 0 && close_out < 0) rc = close_out;
    if (rc == 0 && close_in < 0) rc = close_in;
    return rc;
}

static void pc386_sys_check(int rc, const char * what) {
    if (rc == 0 || rc == -EEXIST) return;
    error("SYS failed: $", (char *)what);
}

void cmd_sys(void) {
    unsigned char * p = cmdline;
    skipspace(p);
    if (*p == '"') {
        p = getFstring(p);
    }
    skipspace(p);
    if (!((p[0] == 'C' || p[0] == 'c') && (p[1] == '\0' || p[1] == ':' || p[1] == '/' || p[1] == '\\'))) {
        error("SYS supports C: only");
    }

    pc386_sys_check(hal_fs_mkdir("C:/BOOT"), "mkdir C:/BOOT");
    pc386_sys_check(pc386_sys_copy_file("A:/BOOT/MMBASIC.ELF", "C:/BOOT/MMBASIC.ELF"), "copy MMBASIC.ELF");
    pc386_sys_check(pc386_sys_copy_file("A:/BOOT/LIMINE.CNF", "C:/BOOT/LIMINE.CONF"), "copy LIMINE.CONF");
    pc386_sys_check(pc386_sys_copy_file("A:/BOOT/LIMINE.SYS", "C:/BOOT/LIMINE-BIOS.SYS"), "copy LIMINE-BIOS.SYS");
    MMPrintString("SYS C: boot files updated\r\n");
}
void cmd_update(void) {}
void cmd_wait(void) {}
void cmd_wrap(void) {}
void cmd_wraptarget(void) {}
/* cmd_xmodem now comes from core/mmbasic/XModem.c (which we add to
 * CORE_SRCS). It calls _outbyte/_inbyte which dispatch via getConsole()
 * (real impl in pc386_runtime.c) and SerialConsolePutC. The Pico-SDK
 * uart_* references in XModem.c are gated by Option.SerialConsole != 0,
 * which is never set on pc386, so the stubs below are link-only. */
#include "hardware/uart.h"
static uart_inst_t pc386_uart0_dummy, pc386_uart1_dummy;
uart_inst_t * uart0 = &pc386_uart0_dummy;
uart_inst_t * uart1 = &pc386_uart1_dummy;
void uart_set_irq_enables(uart_inst_t * u, bool rx, bool tx) {
    (void)u;
    (void)rx;
    (void)tx;
}
void uart_putc_raw(uart_inst_t * u, char c) {
    (void)u;
    (void)c;
}
bool uart_is_readable(uart_inst_t * u) {
    (void)u;
    return false;
}
char uart_getc(uart_inst_t * u) {
    (void)u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* fun_* — return zero / empty-string posture.                        */
/* ------------------------------------------------------------------ */

void fun_adc(void) {
    iret = 0;
    targ = T_INT;
}
void fun_classic(void) {
    iret = 0;
    targ = T_INT;
}
void fun_device(void) {
    sret[0] = 0;
    targ = T_STR;
}
void fun_DHT22(void) {
    fret = 0;
    targ = T_NBR;
}
void fun_ds18b20(void) {
    fret = 0;
    targ = T_NBR;
}
/* fun_keydown lives in vm_sys_input.c — don't redefine. */
void fun_keypad(void) {
    iret = 0;
    targ = T_INT;
}
void fun_mmcmdline(void) {
    sret[0] = 0;
    targ = T_STR;
}
void fun_porta(void) {
    iret = 0;
    targ = T_INT;
}
void fun_temp(void) {
    fret = 0;
    targ = T_NBR;
}
void fun_touch(void) {
    iret = 0;
    targ = T_INT;
}
void fun_tpadlast(void) {
    iret = 0;
    targ = T_INT;
}
void fun_wii(void) {
    iret = 0;
    targ = T_INT;
}
void fun_ws2812(void) {
    iret = 0;
    targ = T_INT;
}
void fun_dev(void) {
    sret[0] = 0;
    targ = T_STR;
}
void fun_distance(void) {
    fret = 0;
    targ = T_NBR;
}
void fun_GPS(void) {
    sret[0] = 0;
    targ = T_STR;
}

void fun_pio(void) {
    iret = 0;
    targ = T_INT;
}
void fun_port(void) {
    iret = 0;
    targ = T_INT;
}
void fun_pulsin(void) {
    iret = 0;
    targ = T_INT;
}
void fun_spi(void) {
    iret = 0;
    targ = T_INT;
}
void fun_spi2(void) {
    iret = 0;
    targ = T_INT;
}

/* ------------------------------------------------------------------ */
/* Misc helpers MMBasic core calls unconditionally.                   */
/* ------------------------------------------------------------------ */

void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    (void)x0;
    (void)y0;
    (void)w;
    (void)h;
    (void)colour;
}
void cleanserver(void) {}
void close_tcpclient(void) {}
void close_udpclient(void) {}

/* ------------------------------------------------------------------ */
/* AES — drivers/aes provides real impls; if we don't link aes.c the
 * symbols stay undef. Pc386 has no security need for AES yet, so
 * stub them out. (Vendoring tiny-AES adds ~700 LOC for no benefit
 * until a BASIC program actually uses cmd_aes.)                       */
/* ------------------------------------------------------------------ */

struct AES_ctx;
void AES_init_ctx(struct AES_ctx * c, const uint8_t * k) {
    (void)c;
    (void)k;
}
void AES_init_ctx_iv(struct AES_ctx * c, const uint8_t * k, const uint8_t * iv) {
    (void)c;
    (void)k;
    (void)iv;
}
void AES_ECB_encrypt(const struct AES_ctx * c, uint8_t * b) {
    (void)c;
    (void)b;
}
void AES_ECB_decrypt(const struct AES_ctx * c, uint8_t * b) {
    (void)c;
    (void)b;
}
void AES_CBC_encrypt_buffer(struct AES_ctx * c, uint8_t * b, uint32_t l) {
    (void)c;
    (void)b;
    (void)l;
}
void AES_CBC_decrypt_buffer(struct AES_ctx * c, uint8_t * b, uint32_t l) {
    (void)c;
    (void)b;
    (void)l;
}
void AES_CTR_xcrypt_buffer(struct AES_ctx * c, uint8_t * b, uint32_t l) {
    (void)c;
    (void)b;
    (void)l;
}

/* ------------------------------------------------------------------ */
/* GPS — globals + cmd/fun stubs. PC has no GPS hat. Signatures match */
/* the extern decls in GPS.h / Hardware_Includes.h verbatim.           */
/* ------------------------------------------------------------------ */
int GPSadjust = 0; /* extern int — not MMFLOAT */
MMFLOAT GPSaltitude = 0;
char GPSdate[11] = {0};
MMFLOAT GPSdop = 0;
int GPSfix = 0;
MMFLOAT GPSlatitude = 0;
MMFLOAT GPSlongitude = 0;
int GPSsatellites = 0;
MMFLOAT GPSspeed = 0;
char GPStime[9] = {0};
MMFLOAT GPStrack = 0;
int GPSvalid = 0;
static volatile char pc386_gpsbuf_storage[128] = {0};
volatile char * volatile gpsbuf = pc386_gpsbuf_storage;
volatile char gpsbuf1[128] = {0};
volatile char gpscount = 0;
volatile int gpscurrent = 0;
volatile int gpsmonitor = 0;

/* ------------------------------------------------------------------ */
/* Serial peripheral cmds (PC has hardware UART but exposed via       */
/* hal_keyboard for COM1; the BASIC PRINT #/INPUT # path doesn't use  */
/* these symbols on a port without the cmd_uart hardware.) Stub.      */
/* ------------------------------------------------------------------ */
void SerialOpen(unsigned char * spec) {
    (void)spec;
}
void SerialClose(int comnbr) {
    (void)comnbr;
}
int SerialGetchar(int comnbr) {
    (void)comnbr;
    return -1;
}
unsigned char SerialPutchar(int comnbr, unsigned char c) {
    (void)comnbr;
    (void)c;
    return c;
}
int SerialRxStatus(int comnbr) {
    (void)comnbr;
    return 0;
}
int SerialTxStatus(int comnbr) {
    (void)comnbr;
    return 0;
}

/* ------------------------------------------------------------------ */
/* littlefs — pc386 file persistence is FatFs only; LFS unused. Every */
/* lfs_X returns LFS_ERR_IO. Use lfs.h's real types so signatures     */
/* match exactly.                                                      */
/* ------------------------------------------------------------------ */
#include "lfs.h"

struct lfs_config pico_lfs_cfg; /* tentative-def storage */

int lfs_format(lfs_t * l, const struct lfs_config * c) {
    (void)l;
    (void)c;
    return LFS_ERR_IO;
}
int lfs_mount(lfs_t * l, const struct lfs_config * c) {
    (void)l;
    (void)c;
    return LFS_ERR_IO;
}
int lfs_unmount(lfs_t * l) {
    (void)l;
    return LFS_ERR_IO;
}
int lfs_remove(lfs_t * l, const char * p) {
    (void)l;
    (void)p;
    return LFS_ERR_IO;
}
int lfs_stat(lfs_t * l, const char * p, struct lfs_info * i) {
    (void)l;
    (void)p;
    (void)i;
    return LFS_ERR_IO;
}
lfs_ssize_t lfs_getattr(lfs_t * l, const char * p, uint8_t t, void * b, lfs_size_t sz) {
    (void)l;
    (void)p;
    (void)t;
    (void)b;
    (void)sz;
    return LFS_ERR_IO;
}
int lfs_file_open(lfs_t * l, lfs_file_t * f, const char * p, int flags) {
    (void)l;
    (void)f;
    (void)p;
    (void)flags;
    return LFS_ERR_IO;
}
int lfs_file_close(lfs_t * l, lfs_file_t * f) {
    (void)l;
    (void)f;
    return LFS_ERR_IO;
}
lfs_ssize_t lfs_file_read(lfs_t * l, lfs_file_t * f, void * b, lfs_size_t sz) {
    (void)l;
    (void)f;
    (void)b;
    (void)sz;
    return LFS_ERR_IO;
}
lfs_ssize_t lfs_file_write(lfs_t * l, lfs_file_t * f, const void * b, lfs_size_t sz) {
    (void)l;
    (void)f;
    (void)b;
    (void)sz;
    return LFS_ERR_IO;
}
int lfs_dir_open(lfs_t * l, lfs_dir_t * d, const char * p) {
    (void)l;
    (void)d;
    (void)p;
    return LFS_ERR_IO;
}
int lfs_dir_close(lfs_t * l, lfs_dir_t * d) {
    (void)l;
    (void)d;
    return LFS_ERR_IO;
}
int lfs_dir_read(lfs_t * l, lfs_dir_t * d, struct lfs_info * i) {
    (void)l;
    (void)d;
    (void)i;
    return LFS_ERR_IO;
}
lfs_ssize_t lfs_fs_size(lfs_t * l) {
    (void)l;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Memory pokes — PC has no useful peek/poke target until LPT1.       */
/* ------------------------------------------------------------------ */
unsigned int GetPeekAddr(unsigned char * p) {
    (void)p;
    pc386_panic("GetPeekAddr not supported");
}
unsigned int GetPokeAddr(unsigned char * p) {
    (void)p;
    pc386_panic("GetPokeAddr not supported");
}

/* ------------------------------------------------------------------ */
/* Web / TCP / MQTT — pc386 has no networking yet (Stage 9+).         */
/* ------------------------------------------------------------------ */
void ProcessWeb(int mode) {
    (void)mode;
}
void port_web_clear_runtime_state(void) {}
void tcp_free_recv_buffers(void) {}
void tcp_realloc_recv_buffers(void) {}
void port_fun_mm_mqtt_copy(void) {}

/* ------------------------------------------------------------------ */
/* Misc symbols MMBasic core unconditionally references.              */
/* ------------------------------------------------------------------ */
int startupcomplete = 1; /* host-style: always treat boot as complete */
uint32_t PSRAMsize = 0;
uintptr_t PSRAMbase = 0;
bool rp2350a = false; /* board flag — we are not RP2350 */

void copyframetoscreen(uint8_t * s, int xstart, int xend, int ystart, int yend, int odd) {
    (void)s;
    (void)xstart;
    (void)xend;
    (void)ystart;
    (void)yend;
    (void)odd;
}
void disable_audio(void) {}
void disable_sd(void) {}
void disable_systemi2c(void) {}
void disable_systemspi(void) {}

/* display_details is an array, not a function. Storage so the symbol
 * resolves; OPTION LIST that walks it terminates immediately. */
const struct Displays display_details[1] = {{0}};

void setframebuffer(void) {}
void setterminal(void) {}
void port_runtime_abort_dma(void) {}
void port_set_default_options(void) {
    Option.pc386_sb_base = 0x220;
    Option.pc386_sb_irq = 5;
    Option.pc386_sb_dma = 1;
    Option.pc386_sb_dma16 = 5;
}

extern int FatFSFileSystem;
extern int drivecheck(char * p, int * waste);
extern void getfullfilename(char * fname, char * q);
extern int InitSDCard(void);
extern const char * port_filesystem_prefix(int filesystem);

static int pc386_stat_path(char * path, struct hal_stat * st, char * stripped, int * filesystem) {
    if (!path || !*path) return -1;
    int save_fs = FatFSFileSystem;
    int waste = 0;
    int t = drivecheck(path, &waste);
    FatFSFileSystem = t - 1;
    if (filesystem) *filesystem = FatFSFileSystem;

    char q[FF_MAX_LFN] = {0};
    getfullfilename(path + waste, q);
    if (stripped) strcpy(stripped, q);

    char prefixed[FF_MAX_LFN + 4];
    snprintf(prefixed, sizeof(prefixed), "%s%s",
             port_filesystem_prefix(FatFSFileSystem), q);

    int rc = -1;
    if (InitSDCard()) rc = hal_fs_stat(prefixed, st);
    FatFSFileSystem = save_fs;
    return rc;
}

int ExistsFile(char * path) {
    struct hal_stat st;
    if (pc386_stat_path(path, &st, NULL, NULL) < 0) return 0;
    return (st.mode & HAL_FS_S_IFREG) ? 1 : 0;
}

int ExistsDir(char * path, char * q, int * fs) {
    struct hal_stat st;
    char stripped[FF_MAX_LFN] = {0};
    if (pc386_stat_path(path, &st, stripped, fs) < 0) return 0;
    if (q) strcpy(q, stripped);
    return (st.mode & HAL_FS_S_IFDIR) ? 1 : 0;
}
int FileLoadCMM2Program(char * fname, bool message) {
    (void)fname;
    (void)message;
    return 0;
}
void SaveProgramToFlash(unsigned char * pm, int msg) {
    (void)msg;
    if (!pm) return;
    mmbasic_save_loaded_source((const char *)pm, MMBASIC_SOURCE_FLAGS_BATCH_LOAD);
}
void LoadPNG(unsigned char * fname) {
    (void)fname;
}
uint64_t readusclock(void) {
    extern uint64_t hal_time_us_64(void);
    return hal_time_us_64();
}
/* xchg_byte is a function pointer (per Custom.h), not a function.
 * Provide storage that points at a default no-op. SPI swap defaults
 * to identity on a port without an SPI peripheral. */
static unsigned char pc386_xchg_byte_noop(unsigned char d) {
    return d;
}
unsigned char (*xchg_byte)(unsigned char) = pc386_xchg_byte_noop;

/* xregex — vendored regex engine. MMBasic uses it for OPTION REGEX
 * but BASIC programs rarely touch it. Stubbed; vendor xregex sources
 * in a future stage if needed. */
int xregcomp(void * p, const char * re, int opts) {
    (void)p;
    (void)re;
    (void)opts;
    return -1;
}
int xregexec(const void * p, const char * s, size_t n, void * m, int flags) {
    (void)p;
    (void)s;
    (void)n;
    (void)m;
    (void)flags;
    return -1;
}
void xregfree(void * p) {
    (void)p;
}

/* bc_fastgfx — bytecode VM hooks for FASTGFX. */
void bc_fastgfx_create(void) {
    vga_mode13h_fastgfx_create();
}
void bc_fastgfx_close(void) {
    vga_mode13h_fastgfx_close();
}
void bc_fastgfx_reset(void) {
    vga_mode13h_fastgfx_reset();
}
void bc_fastgfx_set_fps(int fps) {
    vga_mode13h_fastgfx_set_fps(fps);
}
void bc_fastgfx_swap(void) {
    vga_mode13h_fastgfx_swap();
}
void bc_fastgfx_sync(void) {
    vga_mode13h_fastgfx_sync();
}

/* hal_ff_* live in hal_filesystem_pc386.c (real FatFs forwarders). */

/* host_runtime_get_pixel — screenshot/test readback. */
uint32_t host_runtime_get_pixel(int x, int y) {
    return vga_mode13h_get_pixel(x, y);
}

/* vm_host_fat_* — these were the in-RAM FAT for B: in mmbasic_stdio.
 * Pc386 has real FatFs (A: + C:); stub the entry points so vm_sys_file
 * link references resolve, but they're never reached. */
#include "ff.h"
FRESULT vm_host_fat_mount(void) {
    return FR_OK;
}
const char * vm_host_fat_path(const char * p) {
    return p;
}
void vm_host_fat_reset(void) {}

/* flash_option_contents / flash_target_contents now live in
 * pc386_flash.c, which owns the real backing buffers + the
 * flash_range_erase/program shims hal_flash_pc386.c wraps. */

/* PSRAM controller save/restore hooks called from Commands.c
 * SaveContext / RestoreContext (and hal_flash_pico flush paths) when
 * PSRAMsize is nonzero. Pc386 has no PSRAM (PSRAMsize==0), so the
 * call sites are unreachable, but the symbols must resolve for link.
 * Mirrors drivers/psram_heap/hal_psram_stub.c. */
void hal_psram_save_settings(void) {}
void hal_psram_restore_settings(void) {}
