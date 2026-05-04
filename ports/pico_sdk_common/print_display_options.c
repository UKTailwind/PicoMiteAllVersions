/*
 * ports/pico_sdk_common/print_display_options.c — OPTION LIST display
 * section. Runs `printoptions()`'s ~150-line middle block that used to
 * live in MM_Misc.c inside `#ifdef PICOMITEVGA` … `#else` … `#endif`.
 *
 * Compiled on every device build (WEB/non-WEB, VGA/non-VGA). The
 * top-level branch uses `HAL_PORT_IS_VGA` to pick VGA vs non-VGA output.
 * With -O2 the dead branch folds to zero code per port, so each
 * variant only carries its own output path.
 *
 * Host stub lives in host_runtime.c — host has no display hardware.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_i2c_keypad.h"
#include "hal/hal_print_options.h"

/* The Freq* / SCREENMODE* / NEXTGEN / VIRTUAL / SSDPANEL constants are
 * defined in configuration.h only on PICOMITEVGA builds. On non-VGA the
 * `if (HAL_PORT_IS_VGA)` block is dead, but the symbols still need to be
 * declared so the file compiles. Define them as impossible values
 * (-1 / 32767) so the inner comparisons would all fail even if the
 * dead branch ran. */
#ifndef Freq720P
#define Freq720P  -1
#define Freq480P  -2
#define Freq252P  -3
#define Freq378P  -4
#define FreqXGA   -5
#define FreqSVGA  -6
#define Freq848   -7
#define Freq400   -8
#define FreqY     -9
#define FreqX     -10
#define SCREENMODE1 32766
#define SCREENMODE2 32765
#define ST7789      32764
#define ST7789A     32763
#define ST7920      32762
#define DISP_USER   32761
#define I2C_PANEL   32760
#define BufferedPanel 32759
#define VIRTUAL     32758
#define SSDPANEL    32757
#define SSD1306I2C  32756
#define SSD1306SPI  32755
#define SSD_PANEL_8 32754
#define N5110       32753
#endif
#ifndef NEXTGEN
#define NEXTGEN     32752
#endif

extern const char *OrientList[];
extern void PO(char *s1);
extern void PO2Int(char *s1, int n1);
extern void PO3Int(char *s1, int n1, int n2);
extern void PO2Str(char *s1, char *s2);
extern void PO2StrInt(char *s1, char *s2, int n1);
extern void PRet(void);
extern short HRes;
extern short VRes;
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
    if (HAL_PORT_IS_VGA) {
        if(Option.CPU_Speed==Freq720P)PO2StrInt("RESOLUTION", "1280x720",Option.CPU_Speed);
        if(Option.CPU_Speed==FreqXGA)PO2StrInt("RESOLUTION", "1024x768",Option.CPU_Speed);
        if(Option.CPU_Speed==FreqSVGA)PO2StrInt("RESOLUTION", "800x600",Option.CPU_Speed);
        if(Option.CPU_Speed==Freq848)PO2StrInt("RESOLUTION", "848x480",Option.CPU_Speed);
        if(Option.CPU_Speed==Freq400)PO2StrInt("RESOLUTION", "720x400",Option.CPU_Speed);
        if(Option.CPU_Speed==FreqX)PO2StrInt("RESOLUTION", "1024x600",Option.CPU_Speed);
        if(Option.CPU_Speed==FreqY)PO2StrInt("RESOLUTION", "800x480",Option.CPU_Speed);
        if(Option.CPU_Speed==Freq480P || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq378P )PO2StrInt("RESOLUTION", "640x480",Option.CPU_Speed);
        if(Option.DISPLAY_TYPE!=SCREENMODE1)PO2Int("DEFAULT MODE", Option.DISPLAY_TYPE-SCREENMODE1+1);
        if(Option.Height != 40 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
        /* HDMIclock / HDMId0..d2 are universal struct option_s fields
         * (FileIO.h). Default values 2/0/6/4 make the runtime guard
         * skip on non-HDMI ports where they're never reassigned. */
        if (Option.HDMIclock != 2 || Option.HDMId0 != 0 ||
            Option.HDMId1   != 6 || Option.HDMId2 != 4) {
            PO("HDMI PINS ");
            PInt(Option.HDMIclock); PIntComma(Option.HDMId0);
            PIntComma(Option.HDMId1); PIntComma(Option.HDMId2); PRet();
        }
    } else {
        int i = 0;
        PO2Int("CPUSPEED (KHz)", Option.CPU_Speed);
        if(Option.DISPLAY_CONSOLE == true) {
            PO("LCDPANEL CONSOLE");
            if(Option.DefaultFont != (Option.DISPLAY_TYPE==SCREENMODE2? (6<<4) | 1 : 0x01 ))PInt((Option.DefaultFont>>4) +1);
            else if(!(Option.DefaultFC==WHITE && Option.DefaultBC==BLACK && Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
            if(Option.DefaultFC!=WHITE)PIntHC(Option.DefaultFC);
            else if(!(Option.DefaultBC==BLACK && Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
            if(Option.DefaultBC!=BLACK)PIntHC(Option.DefaultBC);
            else if(!(Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
            if(Option.BackLightLevel != 100)PIntComma(Option.BackLightLevel);
            else if(!(Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
            if(Option.NoScroll!=0)MMPrintString(",NOSCROLL");
            PRet();
        }
        if(Option.Height != 24 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
        if(Option.DISPLAY_TYPE == DISP_USER) PO3Int("LCDPANEL USER", HRes, VRes);
        {
            int in_range;
            if (HAL_PORT_HAS_NEXTGEN_DISPLAY)
                in_range = (Option.DISPLAY_TYPE > I2C_PANEL && (Option.DISPLAY_TYPE < DISP_USER || Option.DISPLAY_TYPE>=NEXTGEN));
            else
                in_range = (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < DISP_USER);
            if (in_range) {
                i=Option.DISPLAY_ORIENTATION;
                if(Option.DISPLAY_TYPE==ST7789 || Option.DISPLAY_TYPE == ST7789A)i=(i+2) % 4;
                PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CD].pinname);
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_Reset].pinname);
                if(Option.DISPLAY_TYPE!=ST7920){
                    MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CS].pinname);
                }
                int buffered_range;
                if (HAL_PORT_HAS_NEXTGEN_DISPLAY)
                    buffered_range = (Option.DISPLAY_TYPE<=I2C_PANEL || (Option.DISPLAY_TYPE>=BufferedPanel && Option.DISPLAY_TYPE<NEXTGEN));
                else
                    buffered_range = (Option.DISPLAY_TYPE<=I2C_PANEL || Option.DISPLAY_TYPE>=BufferedPanel);
                if(!buffered_range && Option.DISPLAY_BL){
                    MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
                } else if(Option.BGR)MMputchar(',',1);
                if(!buffered_range && Option.BGR){
                    MMputchar(',',1);MMPrintString((char *)"INVERT");
                }
                if(Option.DISPLAY_TYPE==SSD1306SPI && Option.I2Coffset)PIntComma(Option.I2Coffset);
                if(Option.DISPLAY_TYPE==N5110 && Option.LCDVOP!=0xC8)PIntComma(Option.LCDVOP);
                MMPrintString("\r\n");
            }
        }
        if(Option.DISPLAY_TYPE > 0 && Option.DISPLAY_TYPE <= I2C_PANEL) {
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
            if(Option.DISPLAY_TYPE==SSD1306I2C && Option.I2Coffset)PIntComma(Option.I2Coffset);
            MMPrintString("\r\n");
        }
        if(Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL) {
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", ");
            MMPrintString((char *)OrientList[(int)i - 1]);
            if(Option.DISPLAY_BL){
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
            } else if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13) || Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
            if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13)){
                MMputchar(',',1);MMPrintString((char *)PinDef[PINMAP[Option.SSD_DC]].pinname);
            } else if(Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
            if(Option.SSD_RESET==-1){
                MMputchar(',',1);MMPrintString("NORESET");
            } else if( (Option.SSD_DATA!=1))MMputchar(',',1);
            if(Option.SSD_DATA!=1){
                MMputchar(',',1);
                MMPrintString((char *)PinDef[Option.SSD_DATA].pinname);
            }
            PRet();
        }
        {
            int is_virtual;
            if (HAL_PORT_HAS_NEXTGEN_DISPLAY)
                is_virtual = (Option.DISPLAY_TYPE >= VIRTUAL && Option.DISPLAY_TYPE<NEXTGEN);
            else
                is_virtual = (Option.DISPLAY_TYPE >= VIRTUAL);
            if (is_virtual) {
                PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); PRet();
            }
        }
        if(Option.BackLightLevel!=100)PO2Int("LCD BACKLIGHT", Option.BackLightLevel);
        hal_gui_controls_print_options();
        hal_i2c_keypad_print_options();
        port_web_print_options();
        /* TOUCH_CS / TOUCH_XZERO / TOUCH_YZERO / TOUCH_XSCALE /
         * TOUCH_YSCALE are universal struct option_s fields
         * (FileIO.h). The TOUCH_CS == 0 default makes the runtime
         * guard skip on VGA ports where touch isn't configured. */
        if(Option.TOUCH_CS) {
            PO("TOUCH");
            if(Option.TOUCH_CAP==1)(MMPrintString("FT6336 "));
            MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_IRQ : Option.TOUCH_CS].pinname);MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_CS : Option.TOUCH_IRQ].pinname);
            if(Option.TOUCH_Click) {
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.TOUCH_Click].pinname);
            } else if(Option.TOUCH_CAP)MMputchar(',',1);
            if(Option.TOUCH_CAP){
                MMputchar(',',1);PInt(Option.THRESHOLD_CAP);
            }
            MMPrintString("\r\n");
            if(Option.TOUCH_XZERO != 0 || Option.TOUCH_YZERO != 0) {
                MMPrintString("GUI CALIBRATE "); PInt(Option.TOUCH_SWAPXY); PIntComma(Option.TOUCH_XZERO); PIntComma(Option.TOUCH_YZERO);
                PIntComma(Option.TOUCH_XSCALE * 10000); PIntComma(Option.TOUCH_YSCALE * 10000); MMPrintString("\r\n");
            }
        }
    }
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
        } else if (HAL_PORT_IS_VGA) {
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname);
            MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname);
        }
        MMPrintString("\r\n");
    }
    /* VGA PINS print — VGA family non-HDMI only. */
    if (HAL_PORT_IS_VGA && !HAL_PORT_HAS_HDMI) {
        if(Option.VGA_BLUE!=24 || Option.VGA_HSYNC!=21 ){
            PO("VGA PINS"); MMPrintString((char *)PinDef[Option.VGA_HSYNC].pinname);
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.VGA_BLUE].pinname);PRet();
        }
    }
}
