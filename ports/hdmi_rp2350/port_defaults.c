/*
 * ports/hdmi_rp2350/port_defaults.c — COMPILE=HDMI / HDMIUSB defaults.
 * PICOMITEVGA + HDMI. Both variants set HDMI output pins regardless
 * of keyboard; HDMIUSB additionally routes USB-keyboard serial config.
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
    Option.HDMIclock = 2;
    Option.HDMId0 = 0;
    Option.HDMId1 = 6;
    Option.HDMId2 = 4;
#ifdef USBKEYBOARD
    Option.USBKeyboard = CONFIG_US;
    Option.SerialConsole = 2;
    Option.SerialTX = 11;
    Option.SerialRX = 12;
    Option.capslock = 0;
    Option.numlock = 1;
    Option.ColourCode = 1;
#else
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
    MMPrintString("HDMIUSB\r\n");
    MMPrintString("OLIMEX USB\r\n");
    MMPrintString("PICO COMPUTER\r\n");
    MMPrintString("HDMIUSBI2S\r\n");
#else
    MMPrintString("OLIMEX\r\n");
    MMPrintString("HDMIBasic\r\n");
#endif
}

/* OPTION RESET <BOARD> factory profiles for HDMI / HDMIUSB. */
int port_factory_reset_board(unsigned char *p)
{
#ifdef USBKEYBOARD
    if(checkstring(p,(unsigned char *) "HDMIUSB") || checkstring(p,(unsigned char *) "PICO COMPUTER") )  {
        ResetOptions(false);
        if(checkstring(p,(unsigned char *) "HDMIUSB") )strcpy((char *)Option.platform,"HDMIUSB");
        else strcpy((char *)Option.platform,"PICO COMPUTER");
        Option.ColourCode = 1;
        Option.CPU_Speed =Freq480P;
        Option.SD_CS=PINMAP[22];
        Option.SD_CLK_PIN=PINMAP[26];
        Option.SD_MOSI_PIN=PINMAP[27];
        Option.SD_MISO_PIN=PINMAP[28];
        Option.AUDIO_L=PINMAP[10];
        Option.AUDIO_R=PINMAP[11];
        Option.modbuffsize=192;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[10],PINMAP[11], 0);
        Option.SYSTEM_I2C_SDA=PINMAP[20];
        Option.SYSTEM_I2C_SCL=PINMAP[21];
        Option.RTC = true;
        Option.SerialTX=PINMAP[8];
        Option.SerialRX=PINMAP[9];
        Option.SerialConsole=2;
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "HDMIUSBI2S"))  {
        if(rp2350a)error("RP350B chips only");
        ResetOptions(false);
        strcpy((char *)Option.platform,"HDMIUSBI2S");
        Option.heartbeatpin=PINMAP[25];
        Option.NoHeartbeat=false;
        Option.ColourCode = 1;
        Option.modbuffsize=512;
        Option.modbuff = true;
        Option.audio_i2s_bclk=PINMAP[10];
        Option.audio_i2s_data=PINMAP[22];
        Option.AUDIO_SLICE=11;
        Option.SD_CS=PINMAP[29];
        Option.SD_CLK_PIN=PINMAP[30];
        Option.SD_MOSI_PIN=PINMAP[31];
        Option.SD_MISO_PIN=PINMAP[32];
        Option.SYSTEM_I2C_SDA=PINMAP[20];
        Option.SYSTEM_I2C_SCL=PINMAP[21];
        Option.RTC = true;
        Option.HDMIclock=1;
        Option.HDMId0=3;
        Option.HDMId1=5;
        Option.HDMId2=7;
        Option.SerialTX=PINMAP[8];
        Option.SerialRX=PINMAP[9];
        Option.SerialConsole=2;
        Option.INT1pin = PINMAP[0];
        Option.INT2pin = PINMAP[1];
        Option.INT3pin = PINMAP[2];
        Option.INT4pin = PINMAP[3];
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "OLIMEXUSB"))  {
        ResetOptions(false);
        strcpy((char *)Option.platform,"OLIMEX USB");
        Option.ColourCode = 1;
        Option.AUDIO_L=PINMAP[26];
        Option.AUDIO_R=PINMAP[27];
        Option.modbuffsize=192;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
        Option.SD_CS=PINMAP[22];
        Option.SD_CLK_PIN=PINMAP[6];
        Option.SD_MOSI_PIN=PINMAP[7];
        Option.SD_MISO_PIN=PINMAP[4];
        Option.HDMIclock=1;
        Option.HDMId0=3;
        Option.HDMId1=7;
        Option.HDMId2=5;
        Option.SerialTX=PINMAP[0];
        Option.SerialRX=PINMAP[1];
        Option.SerialConsole=1;
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#else
    if(checkstring(p,(unsigned char *) "HDMIBASIC"))  {
        ResetOptions(false);
        strcpy((char *)Option.platform,"HDMIbasic");
        Option.ColourCode = 1;
        Option.SD_CS=7;
        Option.SD_CLK_PIN=4;
        Option.SD_MOSI_PIN=5;
        Option.SD_MISO_PIN=6;
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    if(checkstring(p,(unsigned char *) "OLIMEX"))  {
        ResetOptions(false);
        strcpy((char *)Option.platform,"OLIMEX");
        Option.ColourCode = 1;
        Option.AUDIO_L=PINMAP[26];
        Option.AUDIO_R=PINMAP[27];
        Option.modbuffsize=192;
        Option.modbuff = true;
        Option.AUDIO_SLICE=checkslice(PINMAP[26],PINMAP[27], 0);
        Option.SD_CS=PINMAP[22];
        Option.SD_CLK_PIN=PINMAP[6];
        Option.SD_MOSI_PIN=PINMAP[7];
        Option.SD_MISO_PIN=PINMAP[4];
        Option.HDMIclock=1;
        Option.HDMId0=3;
        Option.HDMId1=7;
        Option.HDMId2=5;
        SaveOptions();
        printoptions();uSec(100000);
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    return 0;
}
