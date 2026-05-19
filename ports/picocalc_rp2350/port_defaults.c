/*
 * ports/picocalc_rp2350/port_defaults.c -- ClockworkPi PicoCalc RP2350
 * default Option.* values.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int checkslice(int pin1, int pin2, int ignore);
void port_set_default_options(void)
{
    Option.CPU_Speed = FreqDefault;
#if HAL_PORT_KEYBOARD_USB_HOST
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

void port_print_supported_boards(void)
{
}

/* Single-board port: no selectable CONFIGURE profiles. */
int port_factory_reset_board(unsigned char *p)
{
    (void)p;
    return 0;
}

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

/* No tile-mode console — OPTION LCDPANEL CONSOLE color reset is a no-op. */
void port_apply_default_console_colors(int default_fc, int default_bc)
{ (void)default_fc; (void)default_bc; }
