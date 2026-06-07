/*
 * esp32_peripheral_stubs.c — cmd_, fun_, AES_, GPS_, TCP_ symbols core
 * code references but ESP32 stdio scope doesn't expose. Most are no-op
 * bodies; a handful (cmd_pwm, cmd_Servo, cmd_setpin, fun_pin) route
 * through the VM's pin HAL (vm_sys_pin_) so virtual pin ops still work.
 *
 * Generated from ports/host_native/host_peripheral_stubs.c +
 * host_runtime.c via Step A inventory: 117 unique decls covering 124
 * sole-provider symbols. Verbatim copy with two rp2350 ifdefs collapsed
 * to the rp2040 form (cmd_pwm SYNC argc, cmd_setpin PWM mode list).
 *
 * Per the D-decouple plan: ESP32 owns its full port surface. This file
 * is the replacement for the cmd_/fun_/state symbols host_native used
 * to provide during early bring-up.
 */

#include <limits.h>
#include <setjmp.h>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_private/esp_clk.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lfs.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "OptionCommands.h"
#include "runtime/runtime.h"
#include "vm_sys_pin.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_time.h"
#include "esp32_board_profile.h"
#include "esp32_audio_options.h"

extern void port_print_supported_boards(void);
extern int port_factory_reset_board(unsigned char * p);
extern const char * port_pin_reserved_label(int pin);

/* esp32_parse_pin_arg — converts a "GPn" textual pin argument (or raw pin
 * number) to the VM's internal pin index. Used by cmd_setpin / fun_pin. */
static int esp32_parse_pin_arg(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p') && isdigit(p[2]))
        return codemap(getinteger(p + 2));
    return getinteger(p);
}

static const char * esp32_pin_state_label(int cfg) {
    switch (cfg) {
    case EXT_NOT_CONFIG: return "OFF";
    case EXT_DIG_IN: return "DIN";
    case EXT_DIG_OUT: return "DOUT";
    case EXT_ADCRAW: return "ARAW";
    default: return "Reserved";
    }
}

static int esp32_parse_gpio_info_pin_arg(unsigned char * arg) {
    unsigned char * p = arg;
    skipspace(p);
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p') && isdigit(p[2]))
        return codemap(getint(p + 2, 0, 48));
    return codemap(getint(p, 0, 48));
}

/* PNG decoder + CMM2-program loader stubs. PNG support comes from
 * drivers/upng_sprite/ on Pico; not wired on ESP32 stdio. CMM2 program
 * loading is a different format from .bas — also out of scope. */
void LoadPNG(unsigned char * p) {
    (void)p;
    error("PNG not supported on this port");
}
int FileLoadCMM2Program(char * fname, bool message) {
    (void)fname;
    (void)message;
    return 0;
}

/* `RAM FILE LOAD <slot>, "name.bas"` reaches MemLoadProgram via the
 * shared cmd_psram. The real implementation lives in pico_sdk_common's
 * mem_writeblock.c for RP2350. ESP32 doesn't ship the legacy XIP flash
 * walker yet; stub it so the link resolves and surface the limit at
 * runtime if the user tries to use it. */
int MemLoadProgram(unsigned char * fname, unsigned char * ram) {
    (void)fname;
    (void)ram;
    error("RAM FILE LOAD not supported on this port");
    return 0;
}

/* OPTION LIST diagnostic dump — limited to the ESP32 stdio option
 * surface currently owned by this port. */
void printoptions(void) {
    extern void esp32_wifi_print_options(void);
    extern void esp32_vga_print_options(void);
    extern void esp32_usb_role_print_options(void);
    esp32_board_profile_print_option();
    esp32_wifi_print_options();
    esp32_usb_role_print_options();
    esp32_audio_print_options();
    esp32_vga_print_options();
    hal_gui_controls_print_options();
    /* PSRAM presence: the slab is set up by hal_psram_init() at boot
     * from heap_caps_aligned_alloc(MALLOC_CAP_SPIRAM). PSRAM_CS_PIN
     * is the rp2350 channel and stays at 0 on ESP32, so emit a
     * dedicated `OPTION PSRAM SIZE <MB>` line instead. Operators
     * read this to confirm the slab made it up; the realign smoke
     * harness pattern-matches on `OPTION PSRAM`. */
    if (PSRAMsize != 0) {
        unsigned mb = (unsigned)((uint32_t)PSRAMsize / (1024u * 1024u));
        if (mb == 0) mb = 1;
        char line[40];
        snprintf(line, sizeof(line), "OPTION PSRAM SIZE %u\r\n", mb);
        MMPrintString(line);
    }
}

/* OPEN COM:N peripheral stubs — UART comms not wired in stdio scope.
 * SerialOpen errors so user code gets feedback; the rest stay silent
 * because SerialRxStatus / SerialTxStatus are polled from FileIO.c
 * regardless of whether a COM port was opened. */
void SerialOpen(unsigned char * spec) {
    (void)spec;
    error("COM: not supported on this port");
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

void AES_CBC_decrypt_buffer(void * ctx, uint8_t * buf, int len) {
    (void)ctx;
    (void)buf;
    (void)len;
}

void AES_CBC_encrypt_buffer(void * ctx, uint8_t * buf, int len) {
    (void)ctx;
    (void)buf;
    (void)len;
}

void AES_CTR_xcrypt_buffer(void * ctx, uint8_t * buf, int len) {
    (void)ctx;
    (void)buf;
    (void)len;
}

void AES_ECB_decrypt(void * ctx, uint8_t * buf) {
    (void)ctx;
    (void)buf;
}

void AES_ECB_encrypt(void * ctx, uint8_t * buf) {
    (void)ctx;
    (void)buf;
}

void AES_init_ctx(void * ctx, const uint8_t * key) {
    (void)ctx;
    (void)key;
}

void AES_init_ctx_iv(void * ctx, const uint8_t * key, const uint8_t * iv) {
    (void)ctx;
    (void)key;
    (void)iv;
}

void cleanserver(void) {
    extern void port_web_clear_runtime_state(void);
    port_web_clear_runtime_state();
}

void close_tcpclient(void) {
    extern void esp32_tcp_client_close(void);
    esp32_tcp_client_close();
}

void cmd_adc(void) {}

void cmd_camera(void) {}

void cmd_cfunction(void) {}

void cmd_Classic(void) {}

static void esp32_configure(unsigned char * p) {
    if (!*p) {
        ResetOptions(false);
        SaveOptions();
        printoptions();
        uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
    } else {
        if (checkstring(p, (unsigned char *)"LIST")) {
            port_print_supported_boards();
            return;
        }
        if (port_factory_reset_board(p)) return;
        error("Invalid board for this firmware");
    }
}

void cmd_configure(void) {
    esp32_configure(cmdline);
}

void cmd_csubinterrupt(void) {}

void cmd_device(void) {}

void cmd_DHT22(void) {}

void cmd_ds18b20(void) {}

void cmd_endprogram(void) {}

void cmd_files_pump_console_key(int * c) {
    extern int esp32_console_read_byte_nonblock(void);
    extern int esp32_web_console_pop_key(void);
    extern int esp32_usb_keyboard_pop_key(void);

    /* FILES already calls ProcessWeb() before this hook. Consume the
     * resulting web/serial key directly instead of re-entering MMInkey()
     * and its own network poll from inside the pager loop. */
    if (*c == -1) {
        int k = esp32_web_console_pop_key();
        if (k == -1) k = esp32_usb_keyboard_pop_key();
        if (k == -1) k = esp32_console_read_byte_nonblock();
        if (k != -1)
            *c = k;
        else
            hal_time_sleep_us(10000); /* 10ms — don't peg a core */
    }
}

void cmd_files_restore_program_context(void) {}

void cmd_files_save_program_context(void) {}

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

void cmd_load_post_cleanup(void) {
    /* ESP32's source loader tokenises through the shared tknbuf, so a
     * plain LOAD cannot safely resume the immediate command buffer after
     * FileLoadProgram returns. Autorun has already redirected nextstmt to
     * ProgMemory; let that path fall through so LOAD "file",R starts the
     * freshly loaded program. */
    extern unsigned char * nextstmt;
    extern unsigned char * ProgMemory;
    if (nextstmt == ProgMemory) return;
    extern unsigned char inpbuf[];
    extern jmp_buf mark;
    mmbasic_runtime_post_load_longjmp(inpbuf, STRINGSIZE, mark);
}

void cmd_mouse(void) {}

void cmd_mov(void) {}

void cmd_nop(void) {}

void cmd_Nunchuck(void) {}

void cmd_onewire(void) {}

void cmd_option(void) {
    extern int esp32_wifi_option_setter(unsigned char * cmdline);
    extern int esp32_vga_option_setter(unsigned char * cmdline);
    extern int esp32_usb_role_option_setter(unsigned char * cmdline);
    unsigned char * tp;
    if (checkstring(cmdline, (unsigned char *)"LIST")) {
        printoptions();
        return;
    }
    tp = checkstring(cmdline, (unsigned char *)"RESET");
    if (tp) {
        if (CurrentLinePtr) error("Invalid in a program");
        esp32_configure(tp);
        return;
    }
    if (esp32_usb_role_option_setter(cmdline)) return;
    if (esp32_board_profile_option_setter(cmdline)) return;
    if (esp32_audio_option_setter(cmdline)) return;
    if (esp32_vga_option_setter(cmdline)) return;
    if (hal_gui_controls_option_set(cmdline)) return;
    if (option_command_handle_common(cmdline, false)) {
        if (Option.DISPLAY_TYPE == ILI9341) Option.Refresh = 0;
        return;
    }
    if (esp32_wifi_option_setter(cmdline)) return;
    error("Option not supported on this port");
}

void cmd_out(void) {}

void cmd_pin(void) {
    int pin = esp32_parse_pin_arg(cmdline);
    int value;
    while (*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
    if (!*cmdline) error("Invalid syntax");
    cmdline++;
    if (!*cmdline) error("Invalid syntax");
    value = getinteger(cmdline);
    vm_sys_pin_write(pin, value);
}

void cmd_pio(void) {}

void cmd_PIOline(void) {}

void cmd_poke(void) {}

void cmd_port(void) {}

void cmd_program(void) {}

void cmd_pull(void) {}

void cmd_pulse(void) {}

void cmd_push(void) {}

void cmd_pwm(void) {
    unsigned char * tp;
    if ((tp = checkstring(cmdline, (unsigned char *)"SYNC"))) {
        MMFLOAT counts[12];
        uint16_t present = 0;
        int i;
        for (i = 0; i < 12; i++) counts[i] = -1.0;
        getargs(&tp, 15, (unsigned char *)",");
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
    pin = esp32_parse_pin_arg(argv[0]);

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

void cmd_xmodem(void) {}

void disable_sd(void) {}

void disable_systemi2c(void) {}

void disable_systemspi(void) {}

void fun_dev(void) {}

void fun_device(void) {
    sret = GetTempMemory(STRINGSIZE);
    strcpy((char *)sret, esp32_board_profile_device_name());
    CtoM(sret);
    targ = T_STR;
}

void fun_distance(void) {}

void fun_ds18b20(void) {}

void fun_GPS(void) {}

void fun_info(void) {
    extern short gui_font_width, gui_font_height;
    extern int gui_fcolour, gui_bcolour;
    extern const uint8_t * flash_target_contents;
    extern lfs_t lfs;
    extern struct lfs_config pico_lfs_cfg;
    extern int ExistsFile(char * p);
    extern int ExistsDir(char * p, char * q, int * filesystem);
    extern int FileSize(char * p);
    extern int esp32_wifi_mminfo(unsigned char * ep, int64_t * out_iret,
                                 unsigned char * out_sret, int * out_targ);
    unsigned char * tp;
    if (sret == NULL) sret = GetTempMemory(STRINGSIZE);
    if (checkstring(ep, (unsigned char *)"DEVICE")) {
        strcpy((char *)sret, esp32_board_profile_device_name());
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"PLATFORM") ||
        checkstring(ep, (unsigned char *)"PROFILE")) {
        strcpy((char *)sret, esp32_board_profile_current()->platform_name);
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"HRES")) {
        iret = HRes;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"VRES")) {
        iret = VRes;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONTWIDTH")) {
        iret = gui_font_width;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONTHEIGHT")) {
        iret = gui_font_height;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FONT")) {
        iret = (gui_font >> 4) + 1;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FCOLOUR") ||
        checkstring(ep, (unsigned char *)"FCOLOR")) {
        iret = gui_fcolour;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"BCOLOUR") ||
        checkstring(ep, (unsigned char *)"BCOLOR")) {
        iret = gui_bcolour;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"PLAYING") ||
        checkstring(ep, (unsigned char *)"SOUND")) {
        strcpy((char *)sret, PlayingStr[CurrentlyPlaying]);
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"BOOT COUNT")) {
        iret = 0;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"CPUSPEED")) {
        iret = esp_clk_cpu_freq() / 1000;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"HEAP")) {
        iret = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"PSRAM SIZE")) {
        iret = PSRAMsize;
        targ = T_INT;
        return;
    }
    if (esp32_audio_mminfo(ep, sret, &targ)) return;
    if (checkstring(ep, (unsigned char *)"STACK")) {
        iret = (int64_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FLASHTOP")) {
        iret = TOP_OF_SYSTEM_FLASH;
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"FREE SPACE")) {
        lfs_ssize_t used = lfs_fs_size(&lfs);
        if (used < 0) used = 0;
        iret = ((int64_t)pico_lfs_cfg.block_count - used) * pico_lfs_cfg.block_size;
        if (iret < 0) iret = 0;
        targ = T_INT;
        return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"EXISTS FILE"))) {
        char * p = (char *)getFstring(tp);
        iret = ExistsFile(p);
        targ = T_INT;
        return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"EXISTS DIR"))) {
        char dir[FF_MAX_LFN] = {0};
        char * p = (char *)getFstring(tp);
        int filesystem = 0;
        iret = ExistsDir(p, dir, &filesystem);
        targ = T_INT;
        return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"FILESIZE"))) {
        char * p = (char *)getFstring(tp);
        iret = FileSize(p);
        targ = T_INT;
        return;
    }
    if (checkstring(ep, (unsigned char *)"UPTIME")) {
        fret = (MMFLOAT)hal_time_us_64() / 1000000.0;
        targ = T_NBR;
        return;
    }
    if (checkstring(ep, (unsigned char *)"ID")) {
        uint8_t mac[6] = {0};
        if (esp_efuse_mac_get_default(mac) == ESP_OK) {
            snprintf((char *)sret, STRINGSIZE, "ESP32-S3 %02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            strcpy((char *)sret, "ESP32-S3");
        }
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"PIN"))) {
        int pin = esp32_parse_gpio_info_pin_arg(tp);
        const char * reserved = port_pin_reserved_label(pin);
        if (reserved)
            strcpy((char *)sret, reserved);
        else if (pin < 1 || pin > NBRPINS || (PinDef[pin].mode & UNUSED))
            strcpy((char *)sret, "Invalid");
        else
            strcpy((char *)sret, esp32_pin_state_label(ExtCurrentConfig[pin] & 0xFF));
        CtoM(sret);
        targ = T_STR;
        return;
    }
    if ((tp = checkstring(ep, (unsigned char *)"FLASH ADDRESS"))) {
        /* uintptr_t round-trip — `(unsigned int)` would truncate the
         * pointer on 64-bit hosts and silently zero the upper bits. */
        iret = (int64_t)(uintptr_t)(flash_target_contents +
                                    (getint(tp, 1, MAXFLASHSLOTS) - 1) * MAX_PROG_SIZE);
        targ = T_INT;
        return;
    }
    if (esp32_wifi_mminfo(ep, &iret, sret, &targ)) return;
    iret = 0;
    targ = T_INT;
}

void fun_peek(void) {
    unsigned char * p;
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
    pin = esp32_parse_pin_arg(ep);
    iret = vm_sys_pin_read(pin);
    targ = T_INT;
}

void fun_pio(void) {}

void fun_port(void) {}

void fun_pulsin(void) {}

void fun_spi(void) {}

void fun_spi2(void) {}

int GPSvalid = 0, GPSfix = 0, GPSadjust = 0, GPSsatellites = 0;

MMFLOAT GPSdop = 0, GPSaltitude = 0;

volatile char * volatile gpsbuf = NULL;

volatile char gpsbuf1[128] = {0};

int GPSchannel = 0;

volatile char gpscount = 0;

volatile int gpscurrent = 0;

char GPSdate[11] = {0};

MMFLOAT GPSlatitude = 0, GPSlongitude = 0, GPSspeed = 0, GPStrack = 0;

volatile int gpsmonitor = 0;

char GPStime[9] = {0};

uint8_t I2C0locked = 0;

uint8_t I2C1locked = 0;

unsigned char IgnorePIN = 0;

void initMouse0(int sensitivity) {
    (void)sensitivity;
}

int mmI2Cvalue = 0;

int mmOWvalue = 0;

bool mouse0 = 0;

unsigned char * OnKeyGOSUB = NULL;

unsigned char * OnPS2GOSUB = NULL;

volatile int PS2code = 0;

unsigned char SPIatRisk = 0;

bool optionsuppressstatus = false;
