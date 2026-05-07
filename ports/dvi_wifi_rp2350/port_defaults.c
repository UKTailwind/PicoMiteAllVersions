/*
 * ports/dvi_wifi_rp2350/port_defaults.c — single-board F1 port:
 * RP2350B + DVI/HDMI + RM2 (CYW43) WiFi + I²S audio + QSPI PSRAM.
 *
 * Single-board ports don't carry a multi-board factory-reset menu —
 * one board, one default-options block, no `OPTION RESET <BOARD>`
 * profile picker. Pin assignments (HDMI lanes, I²S clock/data, SD CS,
 * audio slice, etc.) live in the user's board-specific configuration
 * applied at the MMBasic prompt via OPTION HDMI PINS / OPTION I2S PINS /
 * OPTION SDCARD ... and persisted with SaveOptions.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "MMBasic.h"

/* First-boot defaults. Sets only board-independent state (display
 * mode, console enabled, CPU speed appropriate for HDMI, WiFi-server
 * timeout). Hardware pin assignments are left zero so the user
 * configures them once via OPTION at the prompt. */
void port_set_default_options(void)
{
    Option.DISPLAY_CONSOLE    = 1;
    Option.DISPLAY_TYPE       = SCREENMODE1;
    Option.X_TILE             = 80;
    Option.Y_TILE             = 40;
    Option.CPU_Speed          = Freq252P;     /* 252 MHz — HDMI 640x480 default */
    /* HSTX lane → physical-pin mapping. Without these the HSTX peripheral
     * sends every lane to bit 0, producing no valid DVI signal. Same
     * defaults as hdmi_rp2350 / pimoroni_pga2350 boards. */
    Option.HDMIclock          = 2;
    Option.HDMId0             = 0;
    Option.HDMId1             = 6;
    Option.HDMId2             = 4;
    /* No physical keyboard — REPL input comes from USB-CDC. SerialConsole
     * must be 0 (or >4) for the CDC pump in
     * drivers/console_cdc/hal_keyboard_cdc_only.c to drain bytes into
     * ConsoleRxBuf. */
    Option.KeyboardConfig     = NO_KEYBOARD;
    Option.SerialConsole      = 0;
    Option.capslock           = 0;
    Option.numlock            = 1;
    Option.ColourCode         = 1;
    Option.SSD_RESET          = -1;
    Option.ServerResponceTime = 5000;
}

/* Single-board port — `OPTION RESET <name>` has no menu of profiles
 * to choose from. Returns 0 so MM_Misc.c falls through to "Unknown
 * board". User reconfigures from the prompt. */
int port_factory_reset_board(unsigned char *p) { (void)p; return 0; }

/* Empty — `CONFIGURE LIST` shows nothing for this port. */
void port_print_supported_boards(void) { }

/* OPTION setters for the HDMI display family (RESOLUTION / DEFAULT MODE).
 * These configure display output mode, not board identity, so they're
 * still relevant on a single-board port. */
extern void ResetDisplay(void);
extern void ClearScreen(int colour);
extern short HRes;
extern short VRes;
extern short CurrentX;
extern short CurrentY;
extern int   ScreenSize;
extern unsigned char *WriteBuf;
extern volatile int DISPLAY_TYPE;

int port_display_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"RESOLUTION");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if      (checkstring(argv[0], (unsigned char *)"640")     || checkstring(argv[0], (unsigned char *)"640x480"))  { Option.CPU_Speed = (argc == 3 ? getint(argv[2], Freq252P, Freq378P) : Freq252P); Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else if (checkstring(argv[0], (unsigned char *)"1280")    || checkstring(argv[0], (unsigned char *)"1280x720")) { Option.CPU_Speed = Freq720P;  Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = (2 << 4) | 1; }
        else if (checkstring(argv[0], (unsigned char *)"1024")    || checkstring(argv[0], (unsigned char *)"1024x768")) { Option.CPU_Speed = FreqXGA;   Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = (2 << 4) | 1; }
        else if (checkstring(argv[0], (unsigned char *)"1024x600"))                                                     { Option.CPU_Speed = FreqX;     Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else if (checkstring(argv[0], (unsigned char *)"800x480"))                                                      { Option.CPU_Speed = FreqY;     Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else if (checkstring(argv[0], (unsigned char *)"800")     || checkstring(argv[0], (unsigned char *)"800x600"))  { Option.CPU_Speed = FreqSVGA;  Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else if (checkstring(argv[0], (unsigned char *)"848")     || checkstring(argv[0], (unsigned char *)"848x480"))  { Option.CPU_Speed = Freq848;   Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else if (checkstring(argv[0], (unsigned char *)"720")     || checkstring(argv[0], (unsigned char *)"720x400"))  { Option.CPU_Speed = Freq400;   Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        else error("Syntax");
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"DEFAULT MODE");
    if (tp) {
        int mode = getint(tp, 1, MAXMODES);
        if      (mode == 5) { Option.DISPLAY_TYPE = SCREENMODE5; Option.DefaultFont = (6 << 4) | 1; }
        else if (mode == 4) { if (!FullColour) error("Mode not available in this resolution"); Option.DISPLAY_TYPE = SCREENMODE4; Option.DefaultFont = (6 << 4) | 1; }
        else if (mode == 3) { Option.DISPLAY_TYPE = SCREENMODE3; Option.DefaultFont = 1; }
        else if (mode == 2) { Option.DISPLAY_TYPE = SCREENMODE2; Option.DefaultFont = (6 << 4) | 1; }
        else                { Option.DISPLAY_TYPE = SCREENMODE1; Option.DefaultFont = 1; }
        SaveOptions();
        DISPLAY_TYPE = Option.DISPLAY_TYPE;
        memset((void *)WriteBuf, 0, ScreenSize);
        ResetDisplay();
        CurrentX = CurrentY = 0;
        if (Option.DISPLAY_TYPE != SCREENMODE1) ClearScreen(Option.DefaultBC);
        SetFont(Option.DefaultFont);
        return 1;
    }
    return 0;
}

/* SSD1963 data-bus base GPIO. Stubbed — this port has no SSD1963.
 * MM_Misc.c reads it unconditionally for OPTION LCDPANEL DISABLE. */
int SSD1963data = 0;

/* No SPI-LCD on this port — no shared SYSTEM-vs-LCD SPI clock to clear. */
void port_clear_lcd_spi_if_shares_system(void) { }

/* CYW43-reserved GPIO virtual aliases. The RM2 module wires CYW43 SPI
 * to GP23/24/25/29 (matches pico2_w / pimoroni_pico_plus2_w_rp2350
 * board file). Those GPIOs are boot-reserved and unavailable for
 * general I/O — MMBasic addresses them as virtual pins 41-44 so user
 * code can still reference them by name (e.g. for diagnostic prints). */
static int starts_with_gp(const char *s, char d1, char d2)
{
    return (s[0] == 'G' || s[0] == 'g') && (s[1] == 'P' || s[1] == 'p') && s[2] == d1 && s[3] == d2;
}

int port_pinno_alias_for_name(const char *name)
{
    if (starts_with_gp(name, '2', '3')) return 41;
    if (starts_with_gp(name, '2', '4')) return 42;
    if (starts_with_gp(name, '2', '5')) return 43;
    if (starts_with_gp(name, '2', '9')) return 44;
    return 0;
}

int port_pin_is_reserved_alias(int pin) { return pin >= 41 && pin <= 44; }

const char *port_pin_reserved_label(int pin)
{
    if (pin >= 41 && pin <= 44) return "Boot Reserved : CYW43";
    return NULL;
}

/* OPTION LCDPANEL CONSOLE colour reset — VGA-family tile-colour
 * helper. HDMI tiles come in two flavours selected at runtime by
 * FullColour: 16-bit (RGB555) or 8-bit (RGB332). */
void port_apply_default_console_colors(int default_fc, int default_bc)
{
    int fcolour = (FullColour ? RGB555(default_fc) : RGB332(default_fc));
    int bcolour = (FullColour ? RGB555(default_bc) : RGB332(default_bc));
    for (int xp = 0; xp < X_TILE; xp++) {
        for (int yp = 0; yp < Y_TILE; yp++) {
            if (FullColour) {
                if (fcolour != 0xFFFFFFFF) tilefcols[yp * X_TILE + xp]   = (uint16_t)fcolour;
                if (bcolour != 0xFFFFFFFF) tilebcols[yp * X_TILE + xp]   = (uint16_t)bcolour;
            } else {
                if (fcolour != 0xFFFFFFFF) tilefcols_w[yp * X_TILE + xp] = (uint8_t)fcolour;
                if (bcolour != 0xFFFFFFFF) tilebcols_w[yp * X_TILE + xp] = (uint8_t)bcolour;
            }
        }
    }
}

/* VGA-family pin-recovery hook — called from drivers/sd_spi/mmc_stm32.c
 * when SD-card pin reassignment needs to reclaim a VGA-PIO pin. Same
 * body as vga_rp2350 / hdmi_rp2350 since this port shares the
 * VGA-PIO scaffolding. */
extern uint64_t piomap[];
void VGArecovery(int pin)
{
    ExtCurrentConfig[Option.VGA_BLUE]  = EXT_BOOT_RESERVED;
    ExtCurrentConfig[Option.VGA_HSYNC] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno  + 1]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno  + 2]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno  + 3]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_HSYNC].GPno + 1]] = EXT_BOOT_RESERVED;
    if (pin) error("Pin %/| is in use", pin, pin);
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << PinDef[Option.VGA_BLUE].GPno;
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << (PinDef[Option.VGA_BLUE].GPno + 1);
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << (PinDef[Option.VGA_BLUE].GPno + 2);
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << (PinDef[Option.VGA_BLUE].GPno + 3);
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << PinDef[Option.VGA_HSYNC].GPno;
    piomap[QVGA_PIO_NUM] |= (uint64_t)1 << (PinDef[Option.VGA_HSYNC].GPno + 1);
}
