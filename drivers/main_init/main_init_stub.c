/*
 * drivers/main_init/main_init_stub.c — no-op port_main_launch_core1
 * for WEB-class ports (web, web_rp2350) that don't dedicate core1
 * to a scanout/merge worker. The CYW43 polled stack runs entirely on
 * core0 and the stdio console handles both serial + telnet, so there
 * is no second-core entry point on these builds.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_main_init.h"
#include "hal/hal_option_setters.h"

void port_main_launch_core1(void) { }

void port_video_validate_boot_options(void) { }

unsigned port_video_sys_clock_khz(unsigned cpu_khz) { return cpu_khz; }

void port_video_post_clock_init(void) { }

extern void disable_systemspi(void);

/* port_repl_post_clear_display_refresh is provided by MMsetwifi.c
 * on WEB ports (which always link the WiFi stack). */

/* OPTION LIST display section — Web is non-VGA. The real
 * port_print_display_panel_touch / port_print_system_spi /
 * port_setter_sdcard_combined_cs / port_mminfo_lcdpanel /
 * port_mminfo_lcd320 / port_setter_touch_status /
 * port_setter_poke_display impls are shared with the PicoMite
 * SPI-LCD ports — see drivers/spi_lcd/spi_lcd_options.c. The
 * VGA-only hooks stub out below. */
#include "hal/hal_print_options.h"

void port_print_display_resolution_hdmi(void) { /* Web: non-VGA. */ }

void port_print_sdcard_system_spi_share(void) { /* Web non-VGA: dedicated SD pins. */ }
void port_print_vga_pins(void)               { /* Web non-VGA: no VGA pins. */ }

void port_disable_sd_release_system_spi(void) { /* SD has dedicated pins. */ }

void port_setter_sdcard_argc_check(int argc) {
    if (!(argc == 1 || argc == 7)) error("Syntax");
}

int port_setter_sdcard_via_system_spi(int pin1, int pin2, int pin3) {
    (void)pin1; (void)pin2; (void)pin3;
    return 0;
}

int port_setter_hdmi_pins(unsigned char *cmdline)         { (void)cmdline; return 0; }
int port_setter_keyboard_backlight(unsigned char *cmdline){ (void)cmdline; return 0; }
int port_setter_scroll_start(int64_t *out_iret)           { (void)out_iret; return 0; }
int port_setter_screenbuff(int64_t *out_iret)             { (void)out_iret; return 0; }

int port_setter_system_lcd_spi(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"SYSTEM SPI");
    if (!tp) return 0;
    int pin1, pin2, pin3;
    if (checkstring(tp, (unsigned char *)"DISABLE")) {
        if (CurrentLinePtr) error("Invalid in a program");
        if ((Option.SD_CS && Option.SD_CLK_PIN == 0) || Option.TOUCH_CS || Option.LCD_CS || Option.CombinedCS)
            error("In use");
        disable_systemspi();
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    getargs(&tp, 5, (unsigned char *)",");
    if (CurrentLinePtr) error("Invalid in a program");
    if (argc != 5) error("Syntax");
    if (Option.SYSTEM_CLK) error("SYSTEM SPI already configured");
    unsigned char code;
    if (!(code = codecheck(argv[0]))) argv[0] += 2;
    pin1 = getinteger(argv[0]);
    if (!code) pin1 = codemap(pin1);
    if (IsInvalidPin(pin1)) error("Invalid pin");
    if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
    if (!(code = codecheck(argv[2]))) argv[2] += 2;
    pin2 = getinteger(argv[2]);
    if (!code) pin2 = codemap(pin2);
    if (IsInvalidPin(pin2)) error("Invalid pin");
    if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin2, pin2);
    if (!(code = codecheck(argv[4]))) argv[4] += 2;
    pin3 = getinteger(argv[4]);
    if (!code) pin3 = codemap(pin3);
    if (IsInvalidPin(pin3)) error("Invalid pin");
    if (ExtCurrentConfig[pin3] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin3, pin3);
    if (!(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) &&
        !(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX))
        error("Not valid SPI pins");
    if (PinDef[pin1].mode & SPI0SCK && SPI0locked) error("SPI channel already configured");
    if (PinDef[pin1].mode & SPI1SCK && SPI1locked) error("SPI channel already configured");
    Option.SYSTEM_CLK = pin1;
    Option.SYSTEM_MOSI = pin2;
    Option.SYSTEM_MISO = pin3;
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return 1;
}

/* port_setter_touch_status, port_setter_poke_display: shared with
 * PicoMite SPI-LCD ports — see drivers/spi_lcd/spi_lcd_options.c. */
