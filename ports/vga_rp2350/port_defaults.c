/*
 * ports/vga_rp2350/port_defaults.c — COMPILE=VGARP2350 / VGAUSBRP2350.
 * PICOMITEVGA on RP2350 — same defaults as ports/vga/.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

extern int checkslice(int pin1, int pin2, int ignore);
extern void port_picocalc_factory_reset_options(void);
void port_set_default_options(void)
{
    Option.DISPLAY_CONSOLE = 1;
    Option.DISPLAY_TYPE = SCREENMODE1;
    Option.X_TILE = 80;
    Option.Y_TILE = 40;
    Option.CPU_Speed = Freq252P;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
    Option.VGA_HSYNC = 21;
    Option.VGA_BLUE = 24;
    Option.KEYBOARD_CLOCK = KEYBOARDCLOCK;
    Option.KEYBOARD_DATA = KEYBOARDDATA;
    Option.KeyboardConfig = CONFIG_US;
#endif
}

/* Boards advertised by `CONFIGURE LIST`. */
#include "MMBasic.h"
void port_print_supported_boards(void)
{
#ifdef USBKEYBOARD
    MMPrintString("CMM1.5\r\n");
#else
    MMPrintString("PICOMITEVGA V1.1\r\n");
    MMPrintString("PICOMITEVGA V1.0\r\n");
    MMPrintString("VGA Design 1\r\n");
    MMPrintString("VGA Design 2\r\n");
    MMPrintString("SWEETIEPI\r\n");
    MMPrintString("VGA Basic\r\n");
#endif
}
/* OPTION RESET <BOARD> factory profiles. Lifted out of MM_Misc.c so its
 * configure() function stays preprocessor-clean; the per-board #ifdef
 * dispatch lives here in the port impl file (allowed). Returns 1 if a
 * board name matched (call doesn't return — SoftReset). Returns 0 if
 * no name matched, so MM_Misc.c can fall through to the error. */
int port_factory_reset_board(unsigned char *p)
{
#ifdef USBKEYBOARD
    if(checkstring(p,(unsigned char *) "CMM1.5"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.AllPins = 1;
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[14];
        Option.SYSTEM_I2C_SCL=PINMAP[15];
        Option.RTC = true;
        Option.SD_CS=PINMAP[13];
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.VGA_HSYNC=PINMAP[23];
        Option.VGA_BLUE=PINMAP[18];
        Option.AUDIO_L=PINMAP[16];
        Option.AUDIO_R=PINMAP[17];
        Option.modbuffsize=512;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[16],PINMAP[17], 0);
        strcpy((char *)Option.platform,"CMM1.5");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#else
    if(checkstring(p,(unsigned char *) "PICOMITEVGA V1.1"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.AllPins = 1;
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[14];
        Option.SYSTEM_I2C_SCL=PINMAP[15];
        Option.RTC = true;
        Option.SD_CS=PINMAP[13];
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.VGA_HSYNC=PINMAP[16];
        Option.VGA_BLUE=PINMAP[18];
        Option.AUDIO_CS_PIN=PINMAP[24];
        Option.AUDIO_CLK_PIN=PINMAP[22];
        Option.AUDIO_MOSI_PIN=PINMAP[23];
        Option.AUDIO_SLICE=checkslice(PINMAP[22],PINMAP[22], 1);
        Option.modbuffsize=512;
        Option.modbuff = true;
        strcpy((char *)Option.platform,"PICOMITEVGA V1.1");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "PICOMITEVGA V1.0"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.AllPins = 1;
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[14];
        Option.SYSTEM_I2C_SCL=PINMAP[15];
        Option.RTC = true;
        Option.SD_CS=PINMAP[13];
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.VGA_HSYNC=PINMAP[16];
        Option.VGA_BLUE=PINMAP[18];
        Option.AUDIO_L=PINMAP[22];
        Option.AUDIO_R=PINMAP[23];
        Option.modbuffsize=512;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[22],PINMAP[23], 0);
        strcpy((char *)Option.platform,"PICOMITEVGA V1.0");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "VGA DESIGN 1"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.SD_CS=PINMAP[13];
        Option.VGA_HSYNC=PINMAP[16];
        Option.VGA_BLUE=PINMAP[18];
        strcpy((char *)Option.platform,"VGA Design 1");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "VGA DESIGN 2"))  {
        ResetOptions(false);
        Option.CPU_Speed=252000;
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[14];
        Option.SYSTEM_I2C_SCL=PINMAP[15];
        Option.RTC = true;
        Option.SYSTEM_CLK=PINMAP[10];
        Option.SYSTEM_MOSI=PINMAP[11];
        Option.SYSTEM_MISO=PINMAP[12];
        Option.SD_CS=PINMAP[13];
        Option.VGA_HSYNC=PINMAP[16];
        Option.VGA_BLUE=PINMAP[18];
        Option.AUDIO_L=PINMAP[6];
        Option.AUDIO_R=PINMAP[7];
        Option.modbuffsize=192;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[7], 0);
        strcpy((char *)Option.platform,"VGA Design 2");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "SWEETIEPI"))  {
        Option.AllPins = 1;
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[0];
        Option.SYSTEM_I2C_SCL=PINMAP[1];
        Option.SD_CS=PINMAP[29];
        Option.SD_CLK_PIN=PINMAP[3];
        Option.SD_MOSI_PIN=PINMAP[4];
        Option.SD_MISO_PIN=PINMAP[2];
        Option.VGA_HSYNC=PINMAP[14];
        Option.VGA_BLUE=PINMAP[10];
        Option.AUDIO_CS_PIN=PINMAP[5];
        Option.AUDIO_CLK_PIN=PINMAP[6];
        Option.AUDIO_MOSI_PIN=PINMAP[7];
        Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[6], 1);
        Option.modbuffsize=192;
        Option.modbuff = true;
        strcpy((char *)Option.platform,"SWEETIEPI");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "VGA BASIC"))  {
        Option.ColourCode = 1;
        Option.SYSTEM_I2C_SDA=PINMAP[0];
        Option.SYSTEM_I2C_SCL=PINMAP[1];
        Option.SD_CS=PINMAP[14];
        Option.SD_CLK_PIN=PINMAP[13];
        Option.SD_MOSI_PIN=PINMAP[15];
        Option.SD_MISO_PIN=PINMAP[12];
        Option.VGA_HSYNC=PINMAP[16];
        Option.VGA_BLUE=PINMAP[18];
        Option.AUDIO_L=PINMAP[6];
        Option.AUDIO_R=PINMAP[7];
        Option.modbuffsize=192;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[6],PINMAP[7], 0);
        strcpy((char *)Option.platform,"VGA BASIC");
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    return 0;
}
