/*
 * ports/pico_rp2350/port_defaults.c — COMPILE=PICORP2350 / PICOUSBRP2350
 * board-specific default Option.* values. See ports/pico/port_defaults.c
 * for the mechanism; this port shares the PicoMite (non-VGA) defaults.
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
    MMPrintString("Palm Pico");
#ifndef USBKEYBOARD
    MMPrintString("Game*Mite\r\n");
#  ifdef PICOCALC
    MMPrintString("PicoCalc\r\n");
#  endif
    MMPrintString("Pico-ResTouch-LCD-3.5\r\n");
    MMPrintString("Pico-ResTouch-LCD-2.8\r\n");
    MMPrintString("PICO BACKPACK\r\n");
#else
    MMPrintString("USB Edition V1.0\r\n");
#endif
}

/* OPTION RESET <BOARD> factory profiles for PICOMITE rp2350 (PICORP2350
 * + PICOUSBRP2350). Adds PALM PICO + LCD_CLK/MOSI/MISO doubled-up
 * setting that the rp2350 PICOMITE port uses. */
int port_factory_reset_board(unsigned char *p)
{
    if(checkstring(p,(unsigned char *) "PALM PICO"))  {
        ResetOptions(false);
        Option.CPU_Speed=360000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[6];
        Option.SYSTEM_MOSI=PINMAP[7];
        Option.SYSTEM_MISO=PINMAP[4];
        Option.LCD_CLK=PINMAP[10];
        Option.LCD_MOSI=PINMAP[11];
        Option.LCD_MISO=PINMAP[12];
        Option.AllPins = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[20];
        Option.SYSTEM_I2C_SCL=PINMAP[21];
        Option.RTC = true;
        Option.SerialTX=PINMAP[8];
        Option.SerialRX=PINMAP[9];
        Option.SerialConsole=2;
        Option.DISPLAY_ORIENTATION=2;
        Option.DISPLAY_TYPE=ST7796SPBUFF;
        Option.LCD_CD=PINMAP[1];
        Option.LCD_Reset=PINMAP[2];
        Option.LCD_CS=PINMAP[3];
        Option.DISPLAY_BL=PINMAP[18];
        Option.BGR=1;
        Option.SD_CS=PINMAP[5];
        Option.audio_i2s_bclk=PINMAP[13];
        Option.audio_i2s_data=PINMAP[15];
        Option.AUDIO_SLICE=11;
        Option.PSRAM_CS_PIN=PINMAP[0];
        Option.LOCAL_KEYBOARD=1;
        Option.NoHeartbeat=0;
        Option.heartbeatpin=PINMAP[25];
        Option.KeyboardBrightness=10;
        Option.BackLightLevel=60;
        Option.DISPLAY_CONSOLE = 1;
        strcpy((char *)Option.platform,"PALM PICO");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#ifndef USBKEYBOARD
    if(checkstring(p,(unsigned char *) "GAMEMITE"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.LCD_CLK=Option.SYSTEM_CLK=PINMAP[6];
        Option.LCD_MOSI=Option.SYSTEM_MOSI=PINMAP[3];
        Option.LCD_MISO=Option.SYSTEM_MISO=PINMAP[4];
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
        Option.LCD_CLK=Option.SYSTEM_CLK=PINMAP[10];
        Option.LCD_MOSI=Option.SYSTEM_MOSI=PINMAP[11];
        Option.LCD_MISO=Option.SYSTEM_MISO=PINMAP[12];
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
        Option.LCD_CLK=Option.SYSTEM_CLK=PINMAP[18];
        Option.LCD_MOSI=Option.SYSTEM_MOSI=PINMAP[19];
        Option.LCD_MISO=Option.SYSTEM_MISO=PINMAP[16];
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
        Option.LCD_CLK=Option.SYSTEM_CLK=PINMAP[10];
        Option.LCD_MOSI=Option.SYSTEM_MOSI=PINMAP[11];
        Option.LCD_MISO=Option.SYSTEM_MISO=PINMAP[12];
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

/* PICOMITE rp2350 has dedicated LCD_CLK/MOSI/MISO Option fields. */
void MIPS16 disable_lcdspi(void)
{
    if(!IsInvalidPin(Option.LCD_MOSI))ExtCurrentConfig[Option.LCD_MOSI] = EXT_DIG_IN;
    if(!IsInvalidPin(Option.LCD_MISO))ExtCurrentConfig[Option.LCD_MISO] = EXT_DIG_IN;
    if(!IsInvalidPin(Option.LCD_CLK))ExtCurrentConfig[Option.LCD_CLK] = EXT_DIG_IN;
    if(!IsInvalidPin(Option.LCD_MOSI))ExtCfg(Option.LCD_MOSI, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.LCD_MISO))ExtCfg(Option.LCD_MISO, EXT_NOT_CONFIG, 0);
    if(!IsInvalidPin(Option.LCD_CLK))ExtCfg(Option.LCD_CLK, EXT_NOT_CONFIG, 0);
    Option.LCD_MOSI=Option.SYSTEM_MOSI ? Option.SYSTEM_MOSI : 0;
    Option.LCD_MISO=Option.SYSTEM_MISO ? Option.SYSTEM_MISO : 0;
    Option.LCD_CLK=Option.SYSTEM_CLK ? Option.SYSTEM_CLK : 0;
}

/* Called from disable_systemspi: if LCD_CLK shares SYSTEM_CLK pin,
 * clear the LCD_* fields too. */
void port_clear_lcd_spi_if_shares_system(void)
{
    if (Option.LCD_CLK == Option.SYSTEM_CLK) {
        Option.LCD_MOSI = 0;
        Option.LCD_MISO = 0;
        Option.LCD_CLK  = 0;
    }
}

/* Port has no pin aliases for MM.PINNO / MM.PIN. */
int port_pinno_alias_for_name(const char *name) { (void)name; return 0; }
int port_pin_is_reserved_alias(int pin) { (void)pin; return 0; }
const char *port_pin_reserved_label(int pin) { (void)pin; return NULL; }
