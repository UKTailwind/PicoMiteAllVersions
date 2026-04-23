/*
 * ports/web_rp2350/port_defaults.c — COMPILE=WEBRP2350 defaults.
 * Same as ports/web/ but on RP2350.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int checkslice(int pin1, int pin2, int ignore);
extern void port_picocalc_factory_reset_options(void);
void port_set_default_options(void)
{
    Option.CPU_Speed = FreqDefault;
    Option.KeyboardConfig = NO_KEYBOARD;
    Option.SSD_RESET = -1;
    Option.ServerResponceTime = 5000;
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
    MMPrintString("Game*Mite\r\n");
#  ifdef PICOCALC
    MMPrintString("PicoCalc\r\n");
#  endif
    MMPrintString("Pico-ResTouch-LCD-3.5\r\n");
    MMPrintString("Pico-ResTouch-LCD-2.8\r\n");
    MMPrintString("PICO BACKPACK\r\n");
}
/* OPTION RESET <BOARD> factory profiles for PICOMITEWEB rp2040. WEB
 * doesn't get PALM PICO (PICOMITE-only) or RP2040LCD-* boards (#ifndef
 * PICOMITEWEB excludes them in the original block). */
int port_factory_reset_board(unsigned char *p)
{
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
    return 0;
}
