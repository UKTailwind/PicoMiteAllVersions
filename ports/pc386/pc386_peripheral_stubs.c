/*
 * ports/pc386/pc386_peripheral_stubs.c — cmd_X / fun_X no-ops for the
 * peripherals an IBM-PC has none of (I2C, SPI, PWM, PIO, GPS, UART
 * specials, IR, DHT22, OneWire, etc.).
 *
 * Posture matches host_native: most stubs are silent no-ops so a
 * BASIC program that incidentally touches an unsupported feature
 * doesn't blow up. Stubs that take parameters and would otherwise
 * silently accept bad input get pc386_panic so the gap surfaces.
 *
 * Stage 6.5 (LPT1 GPIO) replaces the pin-related stubs; stage 6
 * replaces audio; stage 5 replaces framebuffer; stage 4 replaces
 * keyboard. Until then, this file resolves the symbols.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "pc386_panic.h"

/* ------------------------------------------------------------------ */
/* cmd_* — most are silent no-ops on host, same here.                 */
/* ------------------------------------------------------------------ */

void cmd_adc(void) {}
void cmd_backlight(void) {}
void cmd_camera(void) {}
void cmd_cfunction(void) {}
void cmd_Classic(void) {}
void cmd_configure(void) {}
void cmd_cpu(void) {}
void cmd_csubinterrupt(void) {}
void cmd_device(void) {}
void cmd_DHT22(void) {}
void cmd_ds18b20(void) {}
void cmd_edit(void) {}
void cmd_editfile(void) {}
void cmd_endprogram(void) {}
void cmd_fastgfx(void) {}
void cmd_framebuffer(void) {}
void cmd_i2c(void) {}
void cmd_i2c2(void) {}
void cmd_in(void) {}
void cmd_ir(void) {}
void cmd_ireturn(void) {}
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
void cmd_option(void) {}
void cmd_out(void) {}
void cmd_pin(void) {}
void cmd_pio(void) {}
void cmd_PIOline(void) {}
void cmd_poke(void) {}
void cmd_port(void) {}
void cmd_program(void) {}
void cmd_pull(void) {}
void cmd_pulse(void) {}
void cmd_push(void) {}
void cmd_pwm(void) { error("PWM not available on PC386"); }
void cmd_rtc(void) {}
void cmd_Servo(void) { error("Servo not available on PC386"); }
void cmd_set(void) {}
void cmd_setpin(void) { error("SETPIN not available until stage 6.5 (LPT1)"); }
void cmd_settick(void) {}
void cmd_spi(void) {}
void cmd_spi2(void) {}
void cmd_steppedstream(void) {}
void cmd_synth(void) {}
void cmd_temp(void) {}
void cmd_uart(void) {}
void cmd_watchdog(void) {}
void cmd_web(void) {}
void cmd_wii(void) {}
void cmd_ws2812(void) {}
void cmd_WS2812(void) {}
void cmd_sideset(void) {}
void cmd_sync(void) {}
void cmd_update(void) {}
void cmd_wait(void) {}
void cmd_wrap(void) {}
void cmd_wraptarget(void) {}
void cmd_xmodem(void) {}

/* ------------------------------------------------------------------ */
/* fun_* — return zero / empty-string posture.                        */
/* ------------------------------------------------------------------ */

void fun_adc(void)            { iret = 0; targ = T_INT; }
void fun_classic(void)        { iret = 0; targ = T_INT; }
void fun_cpuid(void)          { iret = 0; targ = T_INT; }
void fun_device(void)         { sret[0] = 0; targ = T_STR; }
void fun_DHT22(void)          { fret = 0; targ = T_NBR; }
void fun_ds18b20(void)        { fret = 0; targ = T_NBR; }
/* fun_keydown lives in vm_sys_input.c — don't redefine. */
void fun_keypad(void)         { iret = 0; targ = T_INT; }
void fun_mmcmdline(void)      { sret[0] = 0; targ = T_STR; }
void fun_pin(void)            { iret = 0; targ = T_INT; }
void fun_porta(void)          { iret = 0; targ = T_INT; }
void fun_temp(void)           { fret = 0; targ = T_NBR; }
void fun_touch(void)          { iret = 0; targ = T_INT; }
void fun_tpadlast(void)       { iret = 0; targ = T_INT; }
void fun_wii(void)            { iret = 0; targ = T_INT; }
void fun_ws2812(void)         { iret = 0; targ = T_INT; }
void fun_dev(void)            { sret[0] = 0; targ = T_STR; }
void fun_distance(void)       { fret = 0; targ = T_NBR; }
void fun_GPS(void)            { sret[0] = 0; targ = T_STR; }
void fun_info(void)           { sret[0] = 0; targ = T_STR; }
void fun_peek(void)           { iret = 0; targ = T_INT; }
void fun_pio(void)            { iret = 0; targ = T_INT; }
void fun_port(void)           { iret = 0; targ = T_INT; }
void fun_pulsin(void)         { iret = 0; targ = T_INT; }
void fun_spi(void)            { iret = 0; targ = T_INT; }
void fun_spi2(void)           { iret = 0; targ = T_INT; }

/* ------------------------------------------------------------------ */
/* Misc helpers MMBasic core calls unconditionally.                   */
/* ------------------------------------------------------------------ */

void blitmerge(int x0, int y0, int w, int h, uint8_t colour) {
    (void)x0; (void)y0; (void)w; (void)h; (void)colour;
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
void AES_init_ctx(struct AES_ctx *c, const uint8_t *k) { (void)c; (void)k; }
void AES_init_ctx_iv(struct AES_ctx *c, const uint8_t *k, const uint8_t *iv) { (void)c; (void)k; (void)iv; }
void AES_ECB_encrypt(const struct AES_ctx *c, uint8_t *b) { (void)c; (void)b; }
void AES_ECB_decrypt(const struct AES_ctx *c, uint8_t *b) { (void)c; (void)b; }
void AES_CBC_encrypt_buffer(struct AES_ctx *c, uint8_t *b, uint32_t l) { (void)c; (void)b; (void)l; }
void AES_CBC_decrypt_buffer(struct AES_ctx *c, uint8_t *b, uint32_t l) { (void)c; (void)b; (void)l; }
void AES_CTR_xcrypt_buffer(struct AES_ctx *c, uint8_t *b, uint32_t l) { (void)c; (void)b; (void)l; }

/* ------------------------------------------------------------------ */
/* GPS — globals + cmd/fun stubs. PC has no GPS hat. Signatures match */
/* the extern decls in GPS.h / Hardware_Includes.h verbatim.           */
/* ------------------------------------------------------------------ */
int     GPSadjust    = 0;     /* extern int — not MMFLOAT */
MMFLOAT GPSaltitude  = 0;
char    GPSdate[11]  = {0};
MMFLOAT GPSdop       = 0;
int     GPSfix       = 0;
MMFLOAT GPSlatitude  = 0;
MMFLOAT GPSlongitude = 0;
int     GPSsatellites = 0;
MMFLOAT GPSspeed     = 0;
char    GPStime[9]   = {0};
MMFLOAT GPStrack     = 0;
int     GPSvalid     = 0;
static volatile char  pc386_gpsbuf_storage[128] = {0};
volatile char * volatile gpsbuf = pc386_gpsbuf_storage;
volatile char gpsbuf1[128] = {0};
volatile char gpscount = 0;
volatile int  gpscurrent = 0;
volatile int  gpsmonitor = 0;

/* ------------------------------------------------------------------ */
/* Serial peripheral cmds (PC has hardware UART but exposed via       */
/* hal_keyboard for COM1; the BASIC PRINT #/INPUT # path doesn't use  */
/* these symbols on a port without the cmd_uart hardware.) Stub.      */
/* ------------------------------------------------------------------ */
void SerialOpen    (unsigned char *spec) { (void)spec; }
void SerialClose   (int comnbr) { (void)comnbr; }
int  SerialGetchar (int comnbr) { (void)comnbr; return -1; }
unsigned char SerialPutchar(int comnbr, unsigned char c) { (void)comnbr; (void)c; return c; }
int  SerialRxStatus(int comnbr) { (void)comnbr; return 0; }
int  SerialTxStatus(int comnbr) { (void)comnbr; return 0; }

/* ------------------------------------------------------------------ */
/* littlefs — pc386 file persistence is FatFs only; LFS unused. Every */
/* lfs_X returns LFS_ERR_IO. Use lfs.h's real types so signatures     */
/* match exactly.                                                      */
/* ------------------------------------------------------------------ */
#include "lfs.h"

struct lfs_config pico_lfs_cfg;  /* tentative-def storage */

int lfs_format   (lfs_t *l, const struct lfs_config *c) { (void)l; (void)c; return LFS_ERR_IO; }
int lfs_mount    (lfs_t *l, const struct lfs_config *c) { (void)l; (void)c; return LFS_ERR_IO; }
int lfs_unmount  (lfs_t *l)                              { (void)l; return LFS_ERR_IO; }
int lfs_remove   (lfs_t *l, const char *p)               { (void)l; (void)p; return LFS_ERR_IO; }
int lfs_stat     (lfs_t *l, const char *p, struct lfs_info *i) { (void)l; (void)p; (void)i; return LFS_ERR_IO; }
lfs_ssize_t lfs_getattr(lfs_t *l, const char *p, uint8_t t, void *b, lfs_size_t sz)
{ (void)l; (void)p; (void)t; (void)b; (void)sz; return LFS_ERR_IO; }
int lfs_file_open (lfs_t *l, lfs_file_t *f, const char *p, int flags)
{ (void)l; (void)f; (void)p; (void)flags; return LFS_ERR_IO; }
int lfs_file_close(lfs_t *l, lfs_file_t *f) { (void)l; (void)f; return LFS_ERR_IO; }
lfs_ssize_t lfs_file_read (lfs_t *l, lfs_file_t *f, void *b, lfs_size_t sz)
{ (void)l; (void)f; (void)b; (void)sz; return LFS_ERR_IO; }
lfs_ssize_t lfs_file_write(lfs_t *l, lfs_file_t *f, const void *b, lfs_size_t sz)
{ (void)l; (void)f; (void)b; (void)sz; return LFS_ERR_IO; }
int lfs_dir_open (lfs_t *l, lfs_dir_t *d, const char *p) { (void)l; (void)d; (void)p; return LFS_ERR_IO; }
int lfs_dir_close(lfs_t *l, lfs_dir_t *d)                 { (void)l; (void)d; return LFS_ERR_IO; }
int lfs_dir_read (lfs_t *l, lfs_dir_t *d, struct lfs_info *i) { (void)l; (void)d; (void)i; return LFS_ERR_IO; }
lfs_ssize_t lfs_fs_size(lfs_t *l) { (void)l; return 0; }

/* ------------------------------------------------------------------ */
/* Memory pokes — PC has no useful peek/poke target until LPT1.       */
/* ------------------------------------------------------------------ */
unsigned char *GetIntAddress(unsigned char *p) { (void)p; pc386_panic("GetIntAddress not supported"); }
unsigned int   GetPeekAddr  (unsigned char *p) { (void)p; pc386_panic("GetPeekAddr not supported"); }
unsigned int   GetPokeAddr  (unsigned char *p) { (void)p; pc386_panic("GetPokeAddr not supported"); }

/* ------------------------------------------------------------------ */
/* Web / TCP / MQTT — pc386 has no networking yet (Stage 9+).         */
/* ------------------------------------------------------------------ */
void ProcessWeb(int mode) { (void)mode; }
void port_web_clear_runtime_state(void) {}
void tcp_free_recv_buffers(void) {}
void tcp_realloc_recv_buffers(void) {}
void port_fun_mm_mqtt_copy(void) {}

/* ------------------------------------------------------------------ */
/* Misc symbols MMBasic core unconditionally references.              */
/* ------------------------------------------------------------------ */
int  editactive = 0;
int  StartEditChar = 0;
unsigned char *StartEditPoint = NULL;
int  startupcomplete = 1;        /* host-style: always treat boot as complete */
uint32_t PSRAMsize = 0;
bool rp2350a = false;            /* board flag — we are not RP2350 */

void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd) {
    (void)s; (void)xstart; (void)xend; (void)ystart; (void)yend; (void)odd;
}
void disable_audio(void) {}
void disable_sd(void) {}
void disable_systemi2c(void) {}
void disable_systemspi(void) {}

/* display_details is an array, not a function. Storage so the symbol
 * resolves; OPTION LIST that walks it terminates immediately. */
const struct Displays display_details[1] = {{ 0 }};

void setframebuffer(void) {}
void setterminal(void) {}
void port_runtime_abort_dma(void) {}
void port_set_default_options(void) {}

int  ExistsFile(char *path) { (void)path; return 0; }
int  ExistsDir (char *path, char *q, int *fs) { (void)path; (void)q; (void)fs; return 0; }
int  FileLoadCMM2Program(char *fname, bool message) { (void)fname; (void)message; return 0; }
void SaveProgramToFlash(unsigned char *p, int erase) { (void)p; (void)erase; }
void LoadPNG(unsigned char *fname) { (void)fname; }
uint64_t readusclock(void) { extern uint64_t hal_time_us_64(void); return hal_time_us_64(); }
/* xchg_byte is a function pointer (per Custom.h), not a function.
 * Provide storage that points at a default no-op. SPI swap defaults
 * to identity on a port without an SPI peripheral. */
static unsigned char pc386_xchg_byte_noop(unsigned char d) { return d; }
unsigned char (*xchg_byte)(unsigned char) = pc386_xchg_byte_noop;

/* xregex — vendored regex engine. MMBasic uses it for OPTION REGEX
 * but BASIC programs rarely touch it. Stubbed; vendor xregex sources
 * in a future stage if needed. */
int  xregcomp(void *p, const char *re, int opts) { (void)p; (void)re; (void)opts; return -1; }
int  xregexec(const void *p, const char *s, size_t n, void *m, int flags)
{ (void)p; (void)s; (void)n; (void)m; (void)flags; return -1; }
void xregfree(void *p) { (void)p; }

/* bc_fastgfx — bytecode VM hooks for FASTGFX. Pc386 has no fastgfx
 * until stage 5; stubs no-op until then. */
void bc_fastgfx_create (void) {}
void bc_fastgfx_close  (void) {}
void bc_fastgfx_reset  (void) {}
void bc_fastgfx_set_fps(int fps) { (void)fps; }
void bc_fastgfx_swap   (void) {}
void bc_fastgfx_sync   (void) {}

/* hal_ff_* live in hal_filesystem_pc386.c (real FatFs forwarders). */

/* host_runtime_get_pixel — fun_pixel readback. No framebuffer yet. */
uint32_t host_runtime_get_pixel(int x, int y) { (void)x; (void)y; return 0; }

/* MMBasic_Prompt.c references these display routines — stubbed since
 * pc386 has no LCD until stage 5. */
void MX470Display(int fn) { (void)fn; }
void DisplayPutS(char *s) { (void)s; }

/* vm_host_fat_* — these were the in-RAM FAT for B: in mmbasic_stdio.
 * Pc386 has real FatFs (A: + C:); stub the entry points so vm_sys_file
 * link references resolve, but they're never reached. */
#include "ff.h"
FRESULT vm_host_fat_mount(void) { return FR_OK; }
const char *vm_host_fat_path(const char *p) { return p; }
void vm_host_fat_reset(void) {}

/* flash_option_contents / flash_target_contents now live in
 * pc386_flash.c, which owns the real backing buffers + the
 * flash_range_erase/program shims hal_flash_pc386.c wraps. */
