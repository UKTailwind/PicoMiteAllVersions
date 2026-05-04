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

/* MM_Misc.c batch-18 hooks — Web side (non-VGA). */
extern void PO(char *s1);
extern void PO2Int(char *s1, int n1);
extern void PO3Int(char *s1, int n1, int n2);
extern void PInt(int64_t n);
extern void PIntComma(int64_t n);
/* PIntHC declared in Hardware_Includes.h. */
extern void PRet(void);
extern const char *OrientList[];
extern void port_web_print_options(void);
extern short HRes;
extern short VRes;
#include "hal/hal_print_options.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_i2c_keypad.h"

/* OPTION LIST display section — Web is non-VGA, so it prints
 * CPUSPEED + LCDPANEL + TOUCH (mirrors display_merge_pico.c's
 * non-VGA real impl). VGA-only hooks stub out. */
void port_print_display_resolution_hdmi(void) { /* Web: non-VGA. */ }

void port_print_display_panel_touch(void) {
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
        int in_range = (Option.DISPLAY_TYPE > I2C_PANEL &&
                        (Option.DISPLAY_TYPE < DISP_USER ||
                         Option.DISPLAY_TYPE >= NEXTGEN));
        if (in_range) {
            i=Option.DISPLAY_ORIENTATION;
            if(Option.DISPLAY_TYPE==ST7789 || Option.DISPLAY_TYPE == ST7789A)i=(i+2) % 4;
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CD].pinname);
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_Reset].pinname);
            if(Option.DISPLAY_TYPE!=ST7920){
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CS].pinname);
            }
            int buffered_range = (Option.DISPLAY_TYPE <= I2C_PANEL ||
                                  (Option.DISPLAY_TYPE >= BufferedPanel &&
                                   Option.DISPLAY_TYPE <  NEXTGEN));
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
        int is_virtual = (Option.DISPLAY_TYPE >= VIRTUAL &&
                          Option.DISPLAY_TYPE <  NEXTGEN);
        if (is_virtual) {
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); PRet();
        }
    }
    if(Option.BackLightLevel!=100)PO2Int("LCD BACKLIGHT", Option.BackLightLevel);
    hal_gui_controls_print_options();
    hal_i2c_keypad_print_options();
    port_web_print_options();
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

void port_print_sdcard_system_spi_share(void) { /* Web non-VGA: dedicated SD pins. */ }
void port_print_vga_pins(void)               { /* Web non-VGA: no VGA pins. */ }

void port_print_system_spi(void) {
    if (Option.SYSTEM_CLK) {
        PO("SYSTEM SPI");
        MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);  MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname); MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname); MMPrintString("\r\n");
    }
}

void port_disable_sd_release_system_spi(void) { /* SD has dedicated pins. */ }

int port_setter_sdcard_combined_cs(unsigned char *tp) {
    if (!checkstring(tp, (unsigned char *)"COMBINED CS")) return 0;
    if (Option.SD_CS || Option.CombinedCS) error("SDcard already configured");
    if (!Option.SYSTEM_CLK) error("System SPI not configured");
    if (!Option.TOUCH_CS)   error("Touch CS pin not configured");
    Option.CombinedCS = 1;
    Option.SD_CS = 0;
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return 1;
}

void port_setter_sdcard_argc_check(int argc) {
    if (!(argc == 1 || argc == 7)) error("Syntax");
}

int port_setter_sdcard_via_system_spi(int pin1, int pin2, int pin3) {
    (void)pin1; (void)pin2; (void)pin3;
    return 0;
}

int port_mminfo_lcdpanel(unsigned char *ep, unsigned char *sret, int *out_targ) {
    if (!checkstring(ep, (unsigned char *)"LCDPANEL")) return 0;
    strcpy((char *)sret, display_details[Option.DISPLAY_TYPE].name);
    CtoM(sret);
    *out_targ = T_STR;
    return 1;
}

int port_mminfo_lcd320(unsigned char *ep, int64_t *out_iret, int *out_targ) {
    if (!checkstring(ep, (unsigned char *)"LCD320")) return 0;
    *out_iret = (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16);
    *out_targ = T_INT;
    return 1;
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

int port_setter_touch_status(unsigned char *out_sret) {
    if (Option.TOUCH_CS == false)                         strcpy((char *)out_sret, "Disabled");
    else if (Option.TOUCH_XZERO == TOUCH_NOT_CALIBRATED)  strcpy((char *)out_sret, "Not calibrated");
    else                                                   strcpy((char *)out_sret, "Ready");
    return 1;
}

int port_setter_poke_display(unsigned char *p) {
    getargs(&p, (MAX_ARG_COUNT * 2) - 3, (unsigned char *)",");
    if (!argc) return 1;
    if (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL) {
        WriteComand(getinteger(argv[0]));
        for (int i = 2; i < argc; i += 2) WriteData(getinteger(argv[i]));
        return 1;
    } else if (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < ST7920) {
        spi_write_command(getinteger(argv[0]));
        for (int i = 2; i < argc; i += 2) spi_write_data(getinteger(argv[i]));
        return 1;
    } else if (Option.DISPLAY_TYPE <= I2C_PANEL) {
        if (argc > 1) error("UNsupported command");
        I2C_Send_Command(getinteger(argv[0]));
        return 1;
    } else {
        error("Display not supported");
    }
    return 1;
}
