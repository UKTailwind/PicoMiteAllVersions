/*
 * ports/pico/port_defaults.c — COMPILE=PICO / PICOUSB board-specific
 * default Option.* values set during factory reset. See FileIO.c's
 * ResetOptions(), which calls port_set_default_options() at the end
 * of its shared defaults.
 *
 * Port impl files may use target-macro #ifdef dispatch (this one has
 * USBKEYBOARD inside) per the fixup-plan rules — the macros don't
 * leak back into core because core only sees the function call.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int checkslice(int pin1, int pin2, int ignore);
extern void port_picocalc_factory_reset_options(void);
void port_set_default_options(void)
{
    Option.CPU_Speed = FreqDefault;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.RepeatStart = 600;
    Option.RepeatRate = 150;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
#endif
    /* Non-VGA targets default touch scale to 1:1. */
    Option.TOUCH_XSCALE = 1.0f;
    Option.TOUCH_YSCALE = 1.0f;
}

/* Boards advertised by `CONFIGURE LIST`. The body's #ifdef gates stay
 * inside this port impl file (port files are exempt from the purity
 * gate); MM_Misc.c calls port_print_supported_boards() unconditionally.
 */
#include "MMBasic.h"  /* for MMPrintString */
void port_print_supported_boards(void)
{
#ifndef USBKEYBOARD
    MMPrintString("Game*Mite\r\n");
#  ifdef PICOCALC
    MMPrintString("PicoCalc\r\n");
#  endif
    MMPrintString("Pico-ResTouch-LCD-3.5\r\n");
    MMPrintString("Pico-ResTouch-LCD-2.8\r\n");
    MMPrintString("PICO BACKPACK\r\n");
    MMPrintString("RP2040-LCD-1.28\r\n");
    MMPrintString("RP2040LCD-0.96\r\n");
    MMPrintString("RP2040-GEEK\r\n");
#else
    MMPrintString("USB Edition V1.0\r\n");
#endif
}

/* OPTION RESET <BOARD> factory profiles for PICOMITE / PICOMITEWEB
 * (rp2040). Internal #ifdefs handle USBKEYBOARD vs PS/2 +
 * PICOMITE-vs-PICOMITEWEB differences (RP2040LCD-* boards exclude WEB
 * to save flash). */
int port_factory_reset_board(unsigned char *p)
{
#ifndef USBKEYBOARD
    if(checkstring(p,(unsigned char *) "GAMEMITE"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[6];
        Option.SYSTEM_MOSI=PINMAP[3];
        Option.SYSTEM_MISO=PINMAP[4];
        Option.AUDIO_L=PINMAP[20];
        Option.AUDIO_R=PINMAP[21];
        Option.modbuffsize=192;
        Option.DISPLAY_TYPE=ILI9341;
        Option.LCD_CD=PINMAP[2];
        Option.LCD_Reset=PINMAP[1];
        Option.LCD_CS=PINMAP[0];
        Option.TOUCH_CS=PINMAP[5];
        Option.TOUCH_IRQ=PINMAP[7];
        Option.SD_CS=PINMAP[22];
        Option.modbuff = true;
        Option.DISPLAY_ORIENTATION=3;
        Option.AUDIO_SLICE=checkslice(PINMAP[20],PINMAP[21], 0);
        Option.TOUCH_SWAPXY = 0;
        Option.TOUCH_XZERO = 407;
        Option.TOUCH_YZERO = 267;
        Option.TOUCH_XSCALE = 0.0897;
        Option.TOUCH_YSCALE = 0.0677;
        strcpy((char *)Option.platform,"Game*Mite");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "PICOCALC"))  {
        port_picocalc_factory_reset_options();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "PICORESTOUCHLCD3.5"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.modbuffsize=192;
        Option.DISPLAY_TYPE=ILI9488W;
        Option.LCD_CD=PINMAP[8];
        Option.LCD_Reset=PINMAP[15];
        Option.LCD_CS=PINMAP[9];
        Option.TOUCH_CS=PINMAP[16];
        Option.TOUCH_IRQ=PINMAP[17];
        Option.SD_CS=PINMAP[22];
        Option.DISPLAY_BL=PINMAP[13];
        Option.modbuff = true;
        Option.DISPLAY_ORIENTATION=1;
        Option.TOUCH_SWAPXY = 0;
        Option.TOUCH_XZERO = 3963;
        Option.TOUCH_YZERO = 216;
        Option.TOUCH_XSCALE = -0.1285;
        Option.TOUCH_YSCALE = 0.0859;
        strcpy((char *)Option.platform,"Pico-ResTouch-LCD-3.5");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "PICO BACKPACK"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[18];
        Option.SYSTEM_MOSI=PINMAP[19];
        Option.SYSTEM_MISO=PINMAP[16];
        Option.DISPLAY_TYPE=ILI9341;
        Option.LCD_CD=PINMAP[20];
        Option.LCD_Reset=PINMAP[21];
        Option.LCD_CS=PINMAP[17];
        Option.TOUCH_CS=PINMAP[14];
        Option.TOUCH_IRQ=PINMAP[15];
        Option.SD_CS=PINMAP[22];
        Option.DISPLAY_ORIENTATION=1;
        Option.TOUCH_SWAPXY = 0;
        Option.TOUCH_XZERO = 3963;
        Option.TOUCH_YZERO = 216;
        Option.TOUCH_XSCALE = -0.1285;
        Option.TOUCH_YSCALE = 0.0859;
        strcpy((char *)Option.platform,"Pico Backpack");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "PICORESTOUCHLCD2.8"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.modbuffsize=192;
        Option.DISPLAY_TYPE=ST7789B;
        Option.LCD_CD=PINMAP[8];
        Option.LCD_Reset=PINMAP[15];
        Option.LCD_CS=PINMAP[9];
        Option.TOUCH_CS=PINMAP[16];
        Option.TOUCH_IRQ=PINMAP[17];
        Option.SD_CS=PINMAP[22];
        Option.DISPLAY_BL=PINMAP[13];
        Option.modbuff = true;
        Option.DISPLAY_ORIENTATION=1;
        Option.TOUCH_SWAPXY = 0;
        Option.TOUCH_XZERO = 373;
        Option.TOUCH_YZERO = 3859;
        Option.TOUCH_XSCALE = 0.0894;
        Option.TOUCH_YSCALE = -0.0657;
        strcpy((char *)Option.platform,"Pico-ResTouch-LCD-2.8");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#  ifndef PICOMITEWEB
    if(checkstring(p,(unsigned char *) "RP2040LCD1.28"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.AllPins = 1;
        Option.ColourCode = 1;
        Option.NoHeartbeat = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[28];
        Option.DISPLAY_TYPE=GC9A01;
        Option.LCD_CD=PINMAP[8];
        Option.LCD_Reset=PINMAP[12];
        Option.LCD_CS=PINMAP[9];
        Option.DISPLAY_BL=PINMAP[25];
        Option.DISPLAY_ORIENTATION=1;
        Option.SYSTEM_I2C_SDA=PINMAP[6];
        Option.SYSTEM_I2C_SCL=PINMAP[7];
        strcpy((char *)Option.platform,"RP2040-LCD-1.28");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "RP2040LCD0.96"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.NoHeartbeat = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[28];
        Option.DISPLAY_TYPE=ST7735S;
        Option.LCD_CD=PINMAP[8];
        Option.LCD_Reset=PINMAP[12];
        Option.LCD_CS=PINMAP[9];
        Option.DISPLAY_BL=PINMAP[25];
        Option.DISPLAY_ORIENTATION=1;
        strcpy((char *)Option.platform,"RP2040-LCD-0.96");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "RP2040GEEK"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.NoHeartbeat = 1;
        Option.AllPins = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[24];
        Option.DISPLAY_TYPE=ST7789A;
        Option.LCD_CD=PINMAP[8];
        Option.LCD_Reset=PINMAP[12];
        Option.LCD_CS=PINMAP[9];
        Option.SD_CS=PINMAP[23];
        Option.SD_CLK_PIN=PINMAP[18];
        Option.SD_MOSI_PIN=PINMAP[19];
        Option.SD_MISO_PIN=PINMAP[20];
        Option.DISPLAY_BL=PINMAP[25];
        Option.DISPLAY_ORIENTATION=1;
        strcpy((char *)Option.platform,"RP2040-GEEK");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#  endif
#else
    if(checkstring(p,(unsigned char *) "USB Edition V1.0"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.NoHeartbeat = 1;
        Option.AllPins = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[24];
        Option.SYSTEM_I2C_SCL=PINMAP[25];
        Option.RTC = true;
        Option.TOUCH_CS=PINMAP[21];
        Option.TOUCH_IRQ=PINMAP[19];
        Option.SYSTEM_CLK=PINMAP[22];
        Option.SYSTEM_MOSI=PINMAP[23];
        Option.SYSTEM_MISO=PINMAP[20];
        Option.AUDIO_L=PINMAP[26];
        Option.AUDIO_R=PINMAP[27];
        Option.SerialTX=PINMAP[28];
        Option.SerialRX=PINMAP[29];
        Option.SerialConsole=1;
        Option.CombinedCS=1;
        Option.SD_CS=0;
        Option.modbuffsize=512;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
        strcpy((char *)Option.platform,"USB Edition V1.0");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    return 0;
}

/* OPTION setters for SPI-LCD displays + touch. Lifted out of MM_Misc.c.
 * Returns 1 if a known OPTION matched (the call typically never returns —
 * SoftReset). VGA / HDMI ports replace this whole function with a different
 * set (RESOLUTION / VGA PINS / DEFAULT MODE). */
extern bool check_sys_clock_khz(uint32_t freq, uint *vco, uint *postdiv1, uint *postdiv2);
extern void Display_Refresh(void);
extern void DisplayNotSet(void);
extern void setterminal(int height, int width);
extern void ConfigDisplayUser(unsigned char *p);
extern void ConfigDisplaySPI(unsigned char *p);
extern void ConfigDisplayVirtual(unsigned char *p);
extern void ConfigDisplaySSD(unsigned char *p);
extern void ConfigDisplayI2C(unsigned char *p);
extern void ConfigTouch(unsigned char *p);
extern void ResetDisplay(void);
extern void ClearScreen(int colour);
extern short HRes;
extern short VRes;
extern short CurrentX;
extern short CurrentY;
extern int  SSD1963data;
extern volatile int DISPLAY_TYPE;

int port_display_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"CPUSPEED");
    if(tp) {
        uint32_t speed=0;
        if(CurrentLinePtr) error("Invalid in a program");
        speed=getint(tp,MIN_CPU,MAX_CPU);
        uint vco, postdiv1, postdiv2;
        if (!check_sys_clock_khz(speed, &vco, &postdiv1, &postdiv2))error("Invalid clock speed");
        Option.CPU_Speed=speed;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"AUTOREFRESH");
    if(tp) {
        if((Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE == ILI9163 || Option.DISPLAY_TYPE == ST7735 || Option.DISPLAY_TYPE == ST7789 || Option.DISPLAY_TYPE == ST7789A)) error("Not valid for this display");
        if(checkstring(tp, (unsigned char *)"ON")) {
            Option.Refresh = 1;
            Display_Refresh();
            return 1;
        }
        if(checkstring(tp, (unsigned char *)"OFF")) { Option.Refresh = 0; return 1; }
        error("Syntax");
    }
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL");
    if(tp) {
        if(checkstring(tp, (unsigned char *)"DISABLE")) {
            if(CurrentLinePtr) error("Invalid in a program");
            Option.LCD_CD = Option.LCD_CS = Option.LCD_Reset = Option.DISPLAY_TYPE = Option.SSD_DATA= HRes = VRes = 0;
            Option.SSD_DC = Option.SSD_WR = Option.SSD_RD=SSD1963data=0;
            Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;
            Option.SSD_RESET = -1;
            if(Option.DISPLAY_CONSOLE){
                Option.Height = SCREENHEIGHT;
                Option.Width = SCREENWIDTH;
                setterminal(Option.Height,Option.Width);
            }
            DrawRectangle = (void (*)(int , int , int , int , int ))DisplayNotSet;
            DrawBitmap =  (void (*)(int , int , int , int , int , int , int , unsigned char *))DisplayNotSet;
            ScrollLCD = (void (*)(int ))DisplayNotSet;
            DrawBuffer = (void (*)(int , int , int , int , unsigned char * ))DisplayNotSet;
            ReadBuffer = (void (*)(int , int , int , int , unsigned char * ))DisplayNotSet;
            Option.DISPLAY_CONSOLE = false;
        } else {
            if(Option.DISPLAY_TYPE && !CurrentLinePtr) error("Display already configured");
            ConfigDisplayUser(tp);
            if(Option.DISPLAY_TYPE)return 1;
            if(CurrentLinePtr) error("Invalid in a program");
            if(!Option.DISPLAY_TYPE)ConfigDisplaySPI(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplayVirtual(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplaySSD(tp);
            if(!Option.DISPLAY_TYPE)ConfigDisplayI2C(tp);
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TOUCH");
    if(tp) {
        if(CurrentLinePtr) error("Invalid in a program");
        if(checkstring(tp, (unsigned char *)"DISABLE")) {
            if(Option.CombinedCS)error("Touch CS in use for SDcard");
            Option.TOUCH_Click = Option.TOUCH_CS = Option.TOUCH_IRQ = false;
        } else  {
            if(Option.TOUCH_CS) error("Touch already configured");
            ConfigTouch(tp);
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    return 0;
}

/* Port has no separate LCD SPI bus (LCD_CLK Option fields don't exist on
 * this build); the share-clear hook is a no-op. disable_lcdspi isn't
 * called from core on these ports. */
void port_clear_lcd_spi_if_shares_system(void) {}

/* Port has no pin aliases for MM.PINNO / MM.PIN. */
int port_pinno_alias_for_name(const char *name) { (void)name; return 0; }
int port_pin_is_reserved_alias(int pin) { (void)pin; return 0; }
const char *port_pin_reserved_label(int pin) { (void)pin; return NULL; }

/* No tile-mode console — OPTION LCDPANEL CONSOLE color reset is a no-op. */
void port_apply_default_console_colors(int default_fc, int default_bc)
{ (void)default_fc; (void)default_bc; }
