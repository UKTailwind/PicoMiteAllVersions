/*
 * host_peripheral_stubs.c — no-op stubs for commands/functions tied to
 * hardware the host build doesn't carry (I2C, SPI, PWM, PIO, GPS, UART,
 * CPU-specific pins, IR, watchdog, AES, xregex, etc.).
 *
 * Every symbol either returns a safe zero, silently no-ops, or errors
 * out if the user invokes a clearly-unsupported command on host.
 *
 * cmd_pwm / cmd_Servo / cmd_setpin / fun_pin / fun_keydown are NOT
 * no-ops — they route through the VM's pin HAL (vm_sys_pin.c) so that
 * a host program can still address virtual "pins" for testing. The
 * real peripheral-driving branches compile out under MMBASIC_HOST.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "vm_sys_pin.h"
#include "host_fb.h"
#include "host_keys.h"
#include "host_time.h"
#include "SPI-LCD.h"

/* host_parse_pin_arg converts a GPn textual pin argument (or a raw pin
 * number) to the VM's internal pin index. Used by cmd_setpin / fun_pin. */
static int host_parse_pin_arg(unsigned char *arg) {
    unsigned char *p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p'))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

/* =========================================================================
 * cmd_* no-op stubs
 *
 * cmd_3D, cmd_arc, cmd_blit, cmd_blitmemory, cmd_box come from Draw.c.
 * cmd_autosave, cmd_chdir, cmd_close, cmd_copy, cmd_disk, cmd_edit,
 * cmd_editfile, cmd_files, cmd_flash, cmd_flush, cmd_kill, cmd_load,
 * cmd_mkdir, cmd_name, cmd_open, cmd_rmdir, cmd_save, cmd_seek, cmd_var
 * come from FileIO.c. cmd_date / cmd_time / cmd_timer / cmd_pause /
 * cmd_sort / cmd_longString come from mm_misc_shared.c.
 * ======================================================================= */
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
void cmd_endprogram(void) {}
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

void cmd_pwm(void) {
    unsigned char *tp;
    if ((tp = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        MMFLOAT counts[12];
        uint16_t present = 0;
        int i;
        for (i = 0; i < 12; i++) counts[i] = -1.0;
#ifdef rp2350
        getargs(&tp, 23, (unsigned char *)",");
#else
        getargs(&tp, 15, (unsigned char *)",");
#endif
        for (i = 0; i < argc / 2 + 1 && i < 12; i++) {
            if (i * 2 < argc && *argv[i * 2]) {
                counts[i] = getnumber(argv[i * 2]);
                if ((counts[i] < 0.0 || counts[i] > 100.0) && counts[i] != -1.0)
                    error("Syntax");
                present |= (uint16_t)(1u << i);
            }
        }
        vm_sys_pwm_sync(present, counts);
        return;
    }

    getargs(&cmdline, 11, (unsigned char *)",");
    if (argc < 3) error("Syntax");
    {
        int slice = getint(argv[0], 0, 11);
        int phase = 0;
        int defer = 0;
        int has_duty1 = 0, has_duty2 = 0;
        MMFLOAT frequency, duty1 = 0, duty2 = 0;
        if (checkstring(argv[2], (unsigned char *)"OFF")) {
            vm_sys_pwm_off(slice);
            return;
        }
        if (argc < 5) error("Syntax");
        frequency = getnumber(argv[2]);
        if (*argv[4]) {
            duty1 = getnumber(argv[4]);
            has_duty1 = 1;
        }
        if (argc >= 7 && *argv[6]) {
            duty2 = getnumber(argv[6]);
            has_duty2 = 1;
        }
        if (argc >= 9 && *argv[8]) phase = getint(argv[8], 0, 1);
        if (argc == 11 && *argv[10]) defer = getint(argv[10], 0, 1);
        vm_sys_pwm_configure(slice, frequency, has_duty1, duty1, has_duty2, duty2, phase, defer);
    }
}

void cmd_rtc(void) {}

void cmd_Servo(void) {
    getargs(&cmdline, 5, (unsigned char *)",");
    if (argc < 3) error("Syntax");
    {
        int slice = getint(argv[0], 0, 11);
        int has_pos1 = 0, has_pos2 = 0;
        MMFLOAT pos1 = 0, pos2 = 0;
        if (checkstring(argv[2], (unsigned char *)"OFF")) {
            vm_sys_pwm_off(slice);
            return;
        }
        if (*argv[2]) {
            pos1 = getnumber(argv[2]);
            has_pos1 = 1;
        }
        if (argc >= 5 && *argv[4]) {
            pos2 = getnumber(argv[4]);
            has_pos2 = 1;
        }
        vm_sys_servo_configure(slice, has_pos1, pos1, has_pos2, pos2);
    }
}

void cmd_set(void) {}

void cmd_setpin(void) {
    int pin;
    int mode = -1;
    int option = VM_PIN_OPT_NONE;

    getargs(&cmdline, 7, (unsigned char *)",");
    if (argc % 2 == 0 || argc < 3) error("Argument count");
    pin = host_parse_pin_arg(argv[0]);

    if (checkstring(argv[2], (unsigned char *)"OFF") || checkstring(argv[2], (unsigned char *)"0"))
        mode = VM_PIN_MODE_OFF;
    else if (checkstring(argv[2], (unsigned char *)"DIN"))
        mode = VM_PIN_MODE_DIN;
    else if (checkstring(argv[2], (unsigned char *)"DOUT"))
        mode = VM_PIN_MODE_DOUT;
    else if (checkstring(argv[2], (unsigned char *)"ARAW"))
        mode = VM_PIN_MODE_ARAW;
    else if (checkstring(argv[2], (unsigned char *)"PWM"))
        mode = VM_PIN_MODE_PWM_AUTO;
    else if (checkstring(argv[2], (unsigned char *)"PWM0A"))
        mode = VM_PIN_MODE_PWM0A;
    else if (checkstring(argv[2], (unsigned char *)"PWM0B"))
        mode = VM_PIN_MODE_PWM0B;
    else if (checkstring(argv[2], (unsigned char *)"PWM1A"))
        mode = VM_PIN_MODE_PWM1A;
    else if (checkstring(argv[2], (unsigned char *)"PWM1B"))
        mode = VM_PIN_MODE_PWM1B;
    else if (checkstring(argv[2], (unsigned char *)"PWM2A"))
        mode = VM_PIN_MODE_PWM2A;
    else if (checkstring(argv[2], (unsigned char *)"PWM2B"))
        mode = VM_PIN_MODE_PWM2B;
    else if (checkstring(argv[2], (unsigned char *)"PWM3A"))
        mode = VM_PIN_MODE_PWM3A;
    else if (checkstring(argv[2], (unsigned char *)"PWM3B"))
        mode = VM_PIN_MODE_PWM3B;
    else if (checkstring(argv[2], (unsigned char *)"PWM4A"))
        mode = VM_PIN_MODE_PWM4A;
    else if (checkstring(argv[2], (unsigned char *)"PWM4B"))
        mode = VM_PIN_MODE_PWM4B;
    else if (checkstring(argv[2], (unsigned char *)"PWM5A"))
        mode = VM_PIN_MODE_PWM5A;
    else if (checkstring(argv[2], (unsigned char *)"PWM5B"))
        mode = VM_PIN_MODE_PWM5B;
    else if (checkstring(argv[2], (unsigned char *)"PWM6A"))
        mode = VM_PIN_MODE_PWM6A;
    else if (checkstring(argv[2], (unsigned char *)"PWM6B"))
        mode = VM_PIN_MODE_PWM6B;
    else if (checkstring(argv[2], (unsigned char *)"PWM7A"))
        mode = VM_PIN_MODE_PWM7A;
    else if (checkstring(argv[2], (unsigned char *)"PWM7B"))
        mode = VM_PIN_MODE_PWM7B;
#ifdef rp2350
    else if (checkstring(argv[2], (unsigned char *)"PWM8A"))
        mode = VM_PIN_MODE_PWM8A;
    else if (checkstring(argv[2], (unsigned char *)"PWM8B"))
        mode = VM_PIN_MODE_PWM8B;
    else if (checkstring(argv[2], (unsigned char *)"PWM9A"))
        mode = VM_PIN_MODE_PWM9A;
    else if (checkstring(argv[2], (unsigned char *)"PWM9B"))
        mode = VM_PIN_MODE_PWM9B;
    else if (checkstring(argv[2], (unsigned char *)"PWM10A"))
        mode = VM_PIN_MODE_PWM10A;
    else if (checkstring(argv[2], (unsigned char *)"PWM10B"))
        mode = VM_PIN_MODE_PWM10B;
    else if (checkstring(argv[2], (unsigned char *)"PWM11A"))
        mode = VM_PIN_MODE_PWM11A;
    else if (checkstring(argv[2], (unsigned char *)"PWM11B"))
        mode = VM_PIN_MODE_PWM11B;
#endif
    else
        error("Unsupported SETPIN mode");

    if (argc >= 5 && *argv[4]) {
        if (checkstring(argv[4], (unsigned char *)"PULLUP"))
            option = VM_PIN_OPT_PULLUP;
        else if (checkstring(argv[4], (unsigned char *)"PULLDOWN"))
            option = VM_PIN_OPT_PULLDOWN;
        else
            error("Unsupported SETPIN option");
    }
    vm_sys_pin_setpin(pin, mode, option);
}

void cmd_settick(void) {}
void cmd_sideset(void) {}
void cmd_spi(void) {}
void cmd_spi2(void) {}
void cmd_sync(void) {}
void cmd_update(void) {}
void cmd_wait(void) {}
void cmd_watchdog(void) {}
void cmd_wrap(void) {}
void cmd_wraptarget(void) {}
void cmd_WS2812(void) {}
void cmd_xmodem(void) {}

/* =========================================================================
 * fun_* no-op stubs
 *
 * fun_cwd / fun_dir / fun_eof / fun_inputstr / fun_loc / fun_lof come
 * from FileIO.c. fun_date / fun_datetime / fun_day / fun_epoch /
 * fun_format / fun_time / fun_timer / fun_LCompare / fun_LGetByte /
 * fun_LGetStr / fun_LInstr / fun_LLen come from mm_misc_shared.c.
 * ======================================================================= */
void fun_dev(void) {}
void fun_device(void) {}
void fun_distance(void) {}
void fun_ds18b20(void) {}
void fun_GPS(void) {}

/* Host stub for MM.INFO(...). Handles the subset device programs reach for
 * on a screen-only build: geometry, current font metrics, and foreground/
 * background colour. Anything unsupported returns 0 so callers don't crash
 * — matrix.bas's `Const CHR_W = mm.info(fontwidth)` etc. needed real font
 * numbers instead of 0 (Const CHR_W = 0 meant every `print @(col_x, y)`
 * landed at column 0). */
void fun_info(void) {
    extern short gui_font_width, gui_font_height;
    extern int gui_fcolour, gui_bcolour;
    extern const uint8_t *flash_target_contents;
    unsigned char *tp;
    if (checkstring(ep, (unsigned char *)"HRES")) {
        iret = HRes; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"VRES")) {
        iret = VRes; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"FONTWIDTH")) {
        iret = gui_font_width; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"FONTHEIGHT")) {
        iret = gui_font_height; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"FONT")) {
        iret = (gui_font >> 4) + 1; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"FCOLOUR") ||
        checkstring(ep, (unsigned char *)"FCOLOR")) {
        iret = gui_fcolour; targ = T_INT; return;
    }
    if (checkstring(ep, (unsigned char *)"BCOLOUR") ||
        checkstring(ep, (unsigned char *)"BCOLOR")) {
        iret = gui_bcolour; targ = T_INT; return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"FLASH ADDRESS"))) {
        /* uintptr_t round-trip — `(unsigned int)` would truncate the
         * pointer on 64-bit hosts and silently zero the upper bits. */
        iret = (int64_t)(uintptr_t)(flash_target_contents +
                                    (getint(tp, 1, MAXFLASHSLOTS) - 1) * MAX_PROG_SIZE);
        targ = T_INT;
        return;
    }
    iret = 0;
    targ = T_INT;
}

/* fun_keydown moved to MM_Misc.c — routes through hal_keyboard_* now. */

/* Minimal PEEK on host — enough to support flash-slot inspection from
 * test programs (PEEK(BYTE addr) / PEEK(INT8 addr) / WORD / SHORT /
 * INTEGER / FLOAT). On device the full PEEK lives in MM_Misc.c with
 * peripheral-bus subkeys (PIO, ADC, etc.); none of those are reachable
 * on host, so we keep this surface narrow. */
void fun_peek(void) {
    unsigned char *p;
    getargs(&ep, 3, (unsigned char *)",");
    if ((p = checkstring(argv[0], (unsigned char *)"INT8")) ||
        (p = checkstring(argv[0], (unsigned char *)"BYTE"))) {
        if (argc != 1) error("Syntax");
        iret = *(unsigned char *)(uintptr_t)getinteger(p);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"WORD"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint32_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)3);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"SHORT"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint16_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)1);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"INTEGER"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint64_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)7);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"FLOAT"))) {
        if (argc != 1) error("Syntax");
        fret = *(MMFLOAT *)((uintptr_t)getinteger(p) & ~(uintptr_t)7);
        targ = T_NBR;
        return;
    }
    /* PEEK(addr) shorthand → byte read */
    if (argc == 1) {
        iret = *(unsigned char *)(uintptr_t)getinteger(argv[0]);
        targ = T_INT;
        return;
    }
    error("Syntax");
}

void fun_pin(void) {
    int pin;
    pin = host_parse_pin_arg(ep);
    iret = vm_sys_pin_read(pin);
    targ = T_INT;
}

void fun_pio(void) {}
void fun_port(void) {}
void fun_pulsin(void) {}
void fun_spi(void) {}
void fun_spi2(void) {}
void fun_touch(void) {}

/* =========================================================================
 * Drawing stubs — Draw.c still references these transitively for
 * legacy device code paths that never fire on host.
 * ======================================================================= */
void UnloadFont(int f) { (void)f; }
void setmode(int mode, bool clear) { (void)mode; (void)clear; }
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd) { (void)s; (void)xstart; (void)xend; (void)ystart; (void)yend; (void)odd; }
void copybuffertoscreen(unsigned char *s, int lx, int ly, int hx, int hy) { (void)s; (void)lx; (void)ly; (void)hx; (void)hy; }
void merge(uint8_t colour) { (void)colour; }
void blitmerge(int x0, int y0, int w, int h, uint8_t colour) { (void)x0; (void)y0; (void)w; (void)h; (void)colour; }
/* cmd_blit MERGE calls setframebuffer + hal_display_merge_post_blit_bg;
 * host gets here via the dead branch after has_pipeline() returns 0. */
void setframebuffer(void) { }

/* =========================================================================
 * SPI / Serial / Audio stubs
 * ======================================================================= */
void spi_write_command(unsigned char data) { (void)data; }
void spi_write_data(unsigned char data) { (void)data; }
unsigned char SerialPutchar(int comnbr, unsigned char c) { (void)comnbr; (void)c; return c; }
void WriteComand(int cmd) { (void)cmd; }
void WriteData(int data) { (void)data; }
void SPIClose(void) {}
void SPI2Close(void) {}

void SerialOpen(unsigned char *spec) { (void)spec; error("COM: not supported on host"); }
void SerialClose(int comnbr) { (void)comnbr; }
int SerialGetchar(int comnbr) { (void)comnbr; return -1; }
int SerialRxStatus(int comnbr) { (void)comnbr; return 0; }
int SerialTxStatus(int comnbr) { (void)comnbr; return 0; }
void disable_audio(void) {}

/* =========================================================================
 * GPS globals — FileIO.c externs them for DATE$/TIME$ with GPS=N.
 * ======================================================================= */
volatile char gpsbuf1[128] = {0};
volatile char gpsbuf2[128] = {0};
volatile char * volatile gpsbuf = NULL;
volatile char gpscount = 0;
volatile int gpscurrent = 0;
volatile int gpsmonitor = 0;
MMFLOAT GPSlatitude = 0, GPSlongitude = 0, GPSspeed = 0, GPStrack = 0;
MMFLOAT GPSdop = 0, GPSaltitude = 0;
int GPSvalid = 0, GPSfix = 0, GPSadjust = 0, GPSsatellites = 0;
char GPStime[9] = {0};
char GPSdate[11] = {0};

/* =========================================================================
 * Memory / regex / AES stubs
 * ======================================================================= */
unsigned int GetPeekAddr(unsigned char *p) { (void)p; return 0; }
unsigned int GetPokeAddr(unsigned char *p) { (void)p; return 0; }
unsigned char *GetIntAddress(unsigned char *p) { (void)p; return NULL; }
long long int *GetReceiveDataBuffer(unsigned char *p, unsigned int *nbr) { (void)p; (void)nbr; return NULL; }
uint32_t getFreeHeap(void) { return 0; }

int xregcomp(void *preg, const char *pattern, int cflags) { (void)preg; (void)pattern; (void)cflags; return -1; }
int xregexec(void *preg, const char *string, int nmatch, void *pmatch, int eflags) { (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags; return -1; }
void xregfree(void *preg) { (void)preg; }

void AES_init_ctx(void *ctx, const uint8_t *key) { (void)ctx; (void)key; }
void AES_init_ctx_iv(void *ctx, const uint8_t *key, const uint8_t *iv) { (void)ctx; (void)key; (void)iv; }
void AES_ECB_encrypt(void *ctx, uint8_t *buf) { (void)ctx; (void)buf; }
void AES_ECB_decrypt(void *ctx, uint8_t *buf) { (void)ctx; (void)buf; }
void AES_CBC_encrypt_buffer(void *ctx, uint8_t *buf, int len) { (void)ctx; (void)buf; (void)len; }
void AES_CBC_decrypt_buffer(void *ctx, uint8_t *buf, int len) { (void)ctx; (void)buf; (void)len; }
void AES_CTR_xcrypt_buffer(void *ctx, uint8_t *buf, int len) { (void)ctx; (void)buf; (void)len; }

/* =========================================================================
 * External.c / GPIO / interrupt stubs
 * ======================================================================= */
void PinSetBit(int pin, unsigned int offset) { (void)pin; (void)offset; }
volatile unsigned int GetPinStatus(int pin) { (void)pin; return 0; }
int GetPinBit(int pin) { (void)pin; return 0; }
void WriteCoreTimer(unsigned long timeset) { (void)timeset; }
unsigned long ReadCoreTimer(void) { return 0; }
uint64_t readusclock(void) { return host_time_us_64(); }
void writeusclock(uint64_t timeset) { (void)timeset; }
uint64_t readIRclock(void) { return 0; }
void writeIRclock(uint64_t timeset) { (void)timeset; }
void initExtIO(void) {}

void ExtCfg(int pin, int cfg, int option) {
    if (cfg == EXT_NOT_CONFIG) {
        vm_sys_pin_setpin(pin, VM_PIN_MODE_OFF, VM_PIN_OPT_NONE);
    } else if (cfg == EXT_DIG_IN) {
        int vm_option = VM_PIN_OPT_NONE;
        if (option == CNPUSET) vm_option = VM_PIN_OPT_PULLUP;
        else if (option == CNPDSET) vm_option = VM_PIN_OPT_PULLDOWN;
        vm_sys_pin_setpin(pin, VM_PIN_MODE_DIN, vm_option);
    } else if (cfg == EXT_DIG_OUT) {
        vm_sys_pin_setpin(pin, VM_PIN_MODE_DOUT, VM_PIN_OPT_NONE);
    } else if (cfg == EXT_ADCRAW) {
        vm_sys_pin_setpin(pin, VM_PIN_MODE_ARAW, VM_PIN_OPT_NONE);
    }
}
void ExtSet(int pin, int val) { vm_sys_pin_write(pin, val); }
int64_t ExtInp(int pin) { return vm_sys_pin_read(pin); }
int IsInvalidPin(int pin) { (void)pin; return 1; }
unsigned long ReadCount5(void) { return 0; }
void WriteCount5(unsigned long timeset) { (void)timeset; }
void SetADCFreq(float frequency) { (void)frequency; }
#ifndef PICOCALC
void setBacklight(int level, int frequency) { (void)level; (void)frequency; }
#else
void setBacklight(int level) { (void)level; }
#endif
void gpio_callback(uint gpio, uint32_t events) { (void)gpio; (void)events; }
int CheckPin(int pin, int action) { (void)pin; (void)action; return 0; }
void CallCFuncInt1(void) {}
void CallCFuncInt2(void) {}
void CallCFuncInt3(void) {}
void CallCFuncInt4(void) {}
void IrInit(void) {}
void IrReset(void) {}
void IRSendSignal(int pin, int half_cycles) { (void)pin; (void)half_cycles; }
void TM_EXTI_Handler_5(char *buf, uint32_t events) { (void)buf; (void)events; }
int KeypadCheck(void) { return 0; }
int codemap(int pin) { (void)pin; return 0; }
int codecheck(unsigned char *line) { (void)line; return 0; }
int getslice(int pin) { (void)pin; return 0; }
void setpwm(int pin, int *PWMChannel, int *PWMSlice, MMFLOAT frequency, MMFLOAT duty) { (void)pin; (void)PWMChannel; (void)PWMSlice; (void)frequency; (void)duty; }

/* =========================================================================
 * Terminal / misc stubs
 * ======================================================================= */
void setterminal(int height, int width) { (void)height; (void)width; }
void OtherOptions(void) {}
void disable_sd(void) {}
void disable_systemspi(void) {}
void disable_systemi2c(void) {}
void mT4IntEnable(int status) { (void)status; }
void InitReservedIO(void) {}

void DisplayNotSet(void) {}
void ScrollLCDSPISCR(int lines) { (void)lines; }
void Display_Refresh(void) {}

void cmd_guiBasic(void) {}

/* =========================================================================
 * Display-table placeholder — DISPLAY_TYPE is DISP_USER on host so
 * display_details[] is never indexed in practice, but Draw.c references
 * the symbol. BmpDecoder.c (BMP_bDecode / BMP_bDecode_memory /
 * BDEC_bReadHeader) is now linked into the host build directly; the
 * earlier error-stubs were removed.
 * ======================================================================= */
const struct Displays display_details[1] = {{ .ref = 0, .name = {0}, .speed = 0,
    .horizontal = 0, .vertical = 0, .bits = 0, .buffered = 0,
    .CPOL = 0, .CPHASE = 0 }};

/* PSRAM-cache save/restore stubs — real implementation lives in
 * ports/pico_sdk_common/psram_cache.c for device builds; host has no
 * PSRAM and no XIP cache to manage. Lets FileIO.c's
 * disable_interrupts_pico / enable_interrupts_pico bodies run
 * unconditionally. */
void mmbasic_save_psram_settings(void) {}
void mmbasic_restore_psram_settings(void) {}

/* RP2350 chip-variant flag. On every device build PicoMite.c defines this
 * (true=RP2350A or RP2040, false=RP2350B); host has no chip so default to
 * the RP2040-equivalent value (true) — code paths that branch on this
 * pick the larger-pin-count behaviour, which is what RP2040 already used. */
bool rp2350a = true;

/* Per-port factory-default Option setup. Real implementations live in
 * ports/<board>/port_defaults.c; host doesn't pre-set hardware-tied pins
 * (LCD_CS, AUDIO_L, …) because none of them mean anything. */
void port_set_default_options(void) {}

/* Networking stubs — real implementations only link on PICOMITEWEB. The
 * non-WEB MM_Misc.c block that defines these doesn't compile on host
 * (host links mm_misc_shared.c instead of MM_Misc.c). */
void closeMQTT(void) {}
void ProcessWeb(int mode) { (void)mode; }
int  startupcomplete = 0;
void tcp_free_recv_buffers(void) {}
void tcp_realloc_recv_buffers(void) {}
