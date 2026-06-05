/*
 * ports/vga_wifi_rp2350/port_defaults.c — F2 port (VGA + WiFi).
 *
 * Stage-F validation port — defines the minimum set of port_*() entry
 * points so a clean firmware links. Factory-reset board profiles are
 * intentionally minimal (one generic VGA+WiFi profile). Real hardware
 * deployment can extend this.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "MMBasic.h"

extern int checkslice(int pin1, int pin2, int ignore);

void port_set_default_options(void) {
    Option.DISPLAY_CONSOLE = 1;
    Option.DISPLAY_TYPE = SCREENMODE1;
    Option.X_TILE = 80;
    Option.Y_TILE = 40;
    Option.CPU_Speed = Freq252P;
    Option.VGA_HSYNC = 21;
    Option.VGA_BLUE = 24;
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
    Option.ServerResponceTime = 5000;
    /* TOUCH_XSCALE/YSCALE don't exist on PICOMITEVGA (struct option_s
     * uses Height/Width/dummy in that slot). */
}

void port_print_supported_boards(void) {
    MMPrintString("VGA + WiFi (F2 validation)\r\n");
}

int port_factory_reset_board(unsigned char * p) {
    (void)p;
    return 0;
}

/* OPTION setters: VGA family RESOLUTION/VGA PINS/DEFAULT MODE. The
 * implementations live in vga_rp2350; F2 reuses the same logic via a
 * minimal proxy. CPUSPEED/AUTOREFRESH/LCDPANEL not relevant on a VGA
 * port. */
extern bool check_sys_clock_khz(uint32_t freq, uint * vco, uint * postdiv1, uint * postdiv2);
extern void ResetDisplay(void);
extern void ClearScreen(int colour);
extern short HRes;
extern short VRes;
extern short CurrentX;
extern short CurrentY;
extern int ScreenSize;
extern unsigned char * WriteBuf;
extern volatile int DISPLAY_TYPE;
extern void VGArecovery(int pin);

int port_display_option_setter(unsigned char * cmdline) {
    unsigned char * tp;
    tp = checkstring(cmdline, (unsigned char *)"RESOLUTION");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (checkstring(argv[0], (unsigned char *)"640") || checkstring(argv[0], (unsigned char *)"640x480")) {
            if (argc == 3) {
                int i = getint(argv[2], Freq252P, Freq378P);
                if (!(i == Freq252P || i == Freq480P || i == Freq378P)) error("Invalid speed");
                Option.CPU_Speed = i;
            } else
                Option.CPU_Speed = Freq252P;
            Option.DISPLAY_TYPE = SCREENMODE1;
            Option.DefaultFont = 1;
        } else
            error("Syntax");
        Option.X_TILE = 80;
        Option.Y_TILE = 40;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"DEFAULT MODE");
    if (tp) {
        int mode = getint(tp, 1, MAXMODES);
        if (mode == 3) {
            Option.DISPLAY_TYPE = SCREENMODE3;
            Option.DefaultFont = 1;
        } else if (mode == 2) {
            Option.DISPLAY_TYPE = SCREENMODE2;
            Option.DefaultFont = (6 << 4) | 1;
        } else {
            Option.DISPLAY_TYPE = SCREENMODE1;
            Option.DefaultFont = 1;
        }
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

/* VGArecovery — needed by drivers/sd_spi/mmc_stm32.c when SD pin
 * reassignment requires reclaiming a VGA-PIO pin. Same body as
 * vga_rp2350. */
extern uint64_t piomap[];
void VGArecovery(int pin) {
    ExtCurrentConfig[Option.VGA_BLUE] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[Option.VGA_HSYNC] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno + 1]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno + 2]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno + 3]] = EXT_BOOT_RESERVED;
    ExtCurrentConfig[PINMAP[PinDef[Option.VGA_HSYNC].GPno + 1]] = EXT_BOOT_RESERVED;
    if (pin) error("Pin %/| is in use", pin, pin);
#ifdef rp2350
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.VGA_BLUE].GPno);
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 1));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 2));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 3));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.VGA_HSYNC].GPno);
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_HSYNC].GPno + 1));
#endif
}

/* SSD1963 data-bus base GPIO. No SSD1963 on this port; stub. */
int SSD1963data = 0;

void port_clear_lcd_spi_if_shares_system(void) {}

/* CYW43-reserved virtual-pin aliases — same as web_rp2350. */
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

/* OPTION LCDPANEL CONSOLE: pre-fill the tile-color arrays. Same as
 * vga_rp2350. */
void port_apply_default_console_colors(int default_fc, int default_bc) {
    int fcolour = RGB121pack(default_fc);
    int bcolour = RGB121pack(default_bc);
    for (int xp = 0; xp < X_TILE; xp++) {
        for (int yp = 0; yp < Y_TILE; yp++) {
            if (fcolour != 0xFFFFFFFF) tilefcols[yp * X_TILE + xp] = (uint16_t)fcolour;
            if (bcolour != 0xFFFFFFFF) tilebcols[yp * X_TILE + xp] = (uint16_t)bcolour;
        }
    }
}
