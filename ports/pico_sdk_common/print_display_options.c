/*
 * ports/pico_sdk_common/print_display_options.c — OPTION LIST display
 * section. Runs `printoptions()`'s ~150-line middle block that used to
 * live in MM_Misc.c inside `#ifdef PICOMITEVGA` … `#else` … `#endif`.
 *
 * Compiled on every device build (WEB/non-WEB, VGA/non-VGA). The
 * VGA vs non-VGA split is delegated to the per-port-shape print
 * hooks (port_print_display_resolution_hdmi / port_print_display_panel_touch
 * / port_print_sdcard_system_spi_share / port_print_vga_pins). Each is
 * real on one port shape and a no-op on the other.
 *
 * Host stub lives in host_runtime.c — host has no display hardware.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_i2c_keypad.h"
#include "hal/hal_print_options.h"

extern void PO(char *s1);
extern void PO2Int(char *s1, int n1);
extern void PO2Str(char *s1, char *s2);
extern void PRet(void);
extern void port_web_print_options(void);

extern bool rp2350a;
extern const char *KBrdList[];

void port_print_keyboard_heartbeat(void)
{
    /* Keyboard layout / pins / mouse / REPEAT lines are emitted by
     * the per-keyboard-driver port_print_kb_layout hook (USB-host
     * driver vs PS/2 driver). */
    port_print_kb_layout();
    if (Option.KeyboardConfig == CONFIG_I2C) PO2Str("KEYBOARD", "I2C");
#ifdef rp2350
    if (Option.NoHeartbeat && rp2350a) PO2Str("HEARTBEAT", "OFF");
    /* LOCAL_KEYBOARD / KeyboardBrightness exist in struct option_s on
     * every port (FileIO.h); the runtime guard makes the print
     * inert on ports that never set LOCAL_KEYBOARD. */
    if (Option.LOCAL_KEYBOARD) PO2Str("KEYBOARD", "LOCAL");
    if (Option.LOCAL_KEYBOARD) PO2Int("KEYBOARD BACKLIGHT", Option.KeyboardBrightness);
#else
    if (Option.NoHeartbeat) PO2Str("HEARTBEAT", "OFF");
#endif
}

void port_print_usb_kb_repeat(void)
{
    /* USB-host driver emits OPTION KEYBOARD REPEAT here; PS/2 driver
     * provides a no-op stub (the PS/2 REPEAT line, if any, is
     * emitted earlier inside port_print_kb_layout). */
    port_print_kb_repeat();
}

void port_print_lcd_spi(void)
{
    /* LCD_CLK / LCD_MOSI / LCD_MISO exist in struct option_s on every
     * port (FileIO.h). The runtime guard makes the print inert on
     * ports that never configure a separate LCD SPI bus. */
    if (Option.LCD_CLK && !(Option.SYSTEM_CLK == Option.LCD_CLK)) {
        PO("LCD SPI");
        MMPrintString((char *)PinDef[Option.LCD_CLK].pinname);  MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.LCD_MOSI].pinname); MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.LCD_MISO].pinname); MMPrintString("\r\n");
    }
}

void port_print_display_options(void)
{
    /* VGA-family ports print RESOLUTION + DEFAULT MODE + DISPLAY +
     * HDMI PINS; non-VGA ports print CPUSPEED + LCDPANEL + TOUCH.
     * Each hook is real on its own port shape and a no-op on the
     * other (driver-pair linkage). */
    port_print_display_resolution_hdmi();
    port_print_display_panel_touch();
    /* SDCARD print — VGA shares system SPI with SD when SD_CLK_PIN==0,
     * prints SYSTEM_CLK/MOSI/MISO in that case; non-VGA always uses
     * dedicated SD pins. */
    if(Option.SD_CS){
        PO("SDCARD");
        MMPrintString((char *)PinDef[Option.SD_CS].pinname);
        if(Option.SD_CLK_PIN){
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_CLK_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MOSI_PIN].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SD_MISO_PIN].pinname);
        } else {
            port_print_sdcard_system_spi_share();
        }
        MMPrintString("\r\n");
    }
    /* VGA PINS print — pure-VGA only (HDMI ports stub). */
    port_print_vga_pins();
    /* OPTION WIFI / TCP SERVER PORT / UDP SERVER PORT / TELNET / TFTP
     * lines — real impl in MMsetwifi.c on WiFi ports, stub no-op in
     * MMweb_stubs.c on non-WiFi. Called here so VGA-family WiFi ports
     * (vga_wifi_rp2350, dvi_wifi_rp2350) print the WIFI line; non-VGA
     * WiFi ports (web, web_rp2350) used to call it inside
     * port_print_display_panel_touch but that hook is a stub on VGA. */
    port_web_print_options();
}
