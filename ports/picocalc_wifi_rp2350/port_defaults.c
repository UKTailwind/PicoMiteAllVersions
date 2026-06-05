/*
 * ports/picocalc_wifi_rp2350/port_defaults.c -- ClockworkPi PicoCalc
 * Pico 2 W default Option.* values.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int checkslice(int pin1, int pin2, int ignore);
void port_set_default_options(void) {
    Option.CPU_Speed = FreqDefault;
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
    Option.ServerResponceTime = 5000;
    Option.TOUCH_XSCALE = 1.0f;
    Option.TOUCH_YSCALE = 1.0f;
}

void port_print_supported_boards(void) {
}

/* Single-board port: no selectable CONFIGURE profiles. */
int port_factory_reset_board(unsigned char * p) {
    (void)p;
    return 0;
}

extern bool check_sys_clock_khz(uint32_t freq, uint * vco, uint * postdiv1, uint * postdiv2);
extern void Display_Refresh(void);
extern void DisplayNotSet(void);
extern void setterminal(int height, int width);
extern void ConfigDisplayUser(unsigned char * p);
extern void ConfigDisplaySPI(unsigned char * p);
extern void ConfigDisplayVirtual(unsigned char * p);
extern void ConfigDisplaySSD(unsigned char * p);
extern void ConfigDisplayI2C(unsigned char * p);
extern void ConfigTouch(unsigned char * p);
extern void ClearScreen(int colour);
extern short HRes;
extern short VRes;
extern short CurrentX;
extern short CurrentY;
extern int SSD1963data;
extern volatile int DISPLAY_TYPE;

int port_display_option_setter(unsigned char * cmdline) {
    unsigned char * tp;
    tp = checkstring(cmdline, (unsigned char *)"CPUSPEED");
    if (tp) {
        uint32_t speed = 0;
        if (CurrentLinePtr) error("Invalid in a program");
        speed = getint(tp, MIN_CPU, MAX_CPU);
        uint vco, postdiv1, postdiv2;
        if (!check_sys_clock_khz(speed, &vco, &postdiv1, &postdiv2)) error("Invalid clock speed");
        Option.CPU_Speed = speed;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"AUTOREFRESH");
    if (tp) {
        if ((Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ILI9163 || Option.DISPLAY_TYPE == ST7735 || Option.DISPLAY_TYPE == ST7789 || Option.DISPLAY_TYPE == ST7789A)) error("Not valid for this display");
        if (checkstring(tp, (unsigned char *)"ON")) {
            Option.Refresh = 1;
            Display_Refresh();
            return 1;
        }
        if (checkstring(tp, (unsigned char *)"OFF")) {
            Option.Refresh = 0;
            return 1;
        }
        error("Syntax");
    }
    tp = checkstring(cmdline, (unsigned char *)"LCDPANEL");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            if (CurrentLinePtr) error("Invalid in a program");
            Option.LCD_CD = Option.LCD_CS = Option.LCD_Reset = Option.DISPLAY_TYPE = Option.SSD_DATA = HRes = VRes = 0;
            Option.SSD_DC = Option.SSD_WR = Option.SSD_RD = SSD1963data = 0;
            Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;
            Option.SSD_RESET = -1;
            if (Option.DISPLAY_CONSOLE) {
                Option.Height = SCREENHEIGHT;
                Option.Width = SCREENWIDTH;
                setterminal(Option.Height, Option.Width);
            }
            DrawRectangle = (void (*)(int, int, int, int, int))DisplayNotSet;
            DrawBitmap = (void (*)(int, int, int, int, int, int, int, unsigned char *))DisplayNotSet;
            ScrollLCD = (void (*)(int))DisplayNotSet;
            DrawBuffer = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
            ReadBuffer = (void (*)(int, int, int, int, unsigned char *))DisplayNotSet;
            Option.DISPLAY_CONSOLE = false;
        } else {
            if (Option.DISPLAY_TYPE && !CurrentLinePtr) error("Display already configured");
            ConfigDisplayUser(tp);
            if (Option.DISPLAY_TYPE) return 1;
            if (CurrentLinePtr) error("Invalid in a program");
            if (!Option.DISPLAY_TYPE) ConfigDisplaySPI(tp);
            if (!Option.DISPLAY_TYPE) ConfigDisplayVirtual(tp);
            if (!Option.DISPLAY_TYPE) ConfigDisplaySSD(tp);
            if (!Option.DISPLAY_TYPE) ConfigDisplayI2C(tp);
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"TOUCH");
    if (tp) {
        if (CurrentLinePtr) error("Invalid in a program");
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            if (Option.CombinedCS) error("Touch CS in use for SDcard");
            Option.TOUCH_Click = Option.TOUCH_CS = Option.TOUCH_IRQ = false;
        } else {
            if (Option.TOUCH_CS) error("Touch already configured");
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

/* PICOMITEWEB exposes GP23/24/25/29 as virtual pins 41-44 (the CYW43
 * radio claims those GPIOs; MMBasic still wants them addressable). */
static int starts_with_gp(const char * s, char d1, char d2) {
    return (s[0] == 'G' || s[0] == 'g') && (s[1] == 'P' || s[1] == 'p') && s[2] == d1 && s[3] == d2;
}
int port_pinno_alias_for_name(const char * name) {
    if (starts_with_gp(name, '2', '3')) return 41;
    if (starts_with_gp(name, '2', '4')) return 42;
    if (starts_with_gp(name, '2', '5')) return 43;
    if (starts_with_gp(name, '2', '9')) return 44;
    return 0;
}
int port_pin_is_reserved_alias(int pin) {
    return pin >= 41 && pin <= 44;
}
const char * port_pin_reserved_label(int pin) {
    if (pin >= 41 && pin <= 44) return "Boot Reserved : CYW43";
    return NULL;
}

/* No tile-mode console — OPTION LCDPANEL CONSOLE color reset is a no-op. */
void port_apply_default_console_colors(int default_fc, int default_bc) {
    (void)default_fc;
    (void)default_bc;
}
