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

/* OPTION setters for VGA / HDMI displays. Each VGA-family port
 * (vga, vga_rp2350, hdmi_rp2350) has its own version of this function
 * with the resolutions and screen modes its hardware supports. */
extern void ResetDisplay(void);
extern void ClearScreen(int colour);
extern short HRes;
extern short VRes;
extern short CurrentX;
extern short CurrentY;
extern int ScreenSize;
extern unsigned char *WriteBuf;
extern volatile int DISPLAY_TYPE;
extern void VGArecovery(int pin);

int port_display_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;
    tp = checkstring(cmdline, (unsigned char *)"RESOLUTION");
    if(tp) {
        getargs(&tp,3,(unsigned char *)",");
        if(CurrentLinePtr) error("Invalid in a program");
        if((checkstring(argv[0], (unsigned char *)"640")) || (checkstring(argv[0], (unsigned char *)"640x480"))){
            if(argc==3){
                int i=getint(argv[2],Freq252P,Freq378P);
                if(!(i==Freq252P || i==Freq480P || i==Freq378P))error("Invalid speed");
                Option.CPU_Speed = i;
            } else Option.CPU_Speed = Freq252P;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont = 1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"1280") || checkstring(argv[0], (unsigned char *)"1280x720")){
            Option.CPU_Speed = Freq720P;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=(2<<4) | 1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"1024") || checkstring(argv[0], (unsigned char *)"1024x768")){
            Option.CPU_Speed = FreqXGA;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=(2<<4) | 1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"1024x600")){
            Option.CPU_Speed = FreqX;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"800x480")){
            Option.CPU_Speed = FreqY;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont=1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"800") || checkstring(argv[0], (unsigned char *)"800x600")){
            Option.CPU_Speed = FreqSVGA;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"848") || checkstring(argv[0], (unsigned char *)"848x480")){
            Option.CPU_Speed = Freq848;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
        else if(checkstring(argv[0], (unsigned char *)"720") || checkstring(argv[0], (unsigned char *)"720x400")){
            Option.CPU_Speed = Freq400;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
        else error("Syntax");
        /* HDMI: no X_TILE/Y_TILE assignment. */
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    /* HDMI: no VGA PINS option. */
    tp = checkstring(cmdline, (unsigned char *)"DEFAULT MODE");
    if(tp) {
        int mode=getint(tp,1,MAXMODES);
        if(mode==3){
            Option.DISPLAY_TYPE=SCREENMODE3;
            Option.DefaultFont = 1 ;
        } else if(mode==4){
            if(!(FullColour))error("Mode not available in this resolution");
            Option.DISPLAY_TYPE=SCREENMODE4;
            Option.DefaultFont=(6<<4) | 1 ;
        } else if(mode==5){
            Option.DISPLAY_TYPE=SCREENMODE5;
            Option.DefaultFont=(6<<4) | 1 ;
        } else if(mode==2){
            Option.DISPLAY_TYPE=SCREENMODE2;
            Option.DefaultFont=(6<<4) | 1 ;
        } else {
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
        SaveOptions();
        DISPLAY_TYPE= Option.DISPLAY_TYPE;
        memset((void *)WriteBuf, 0, ScreenSize);
        ResetDisplay();
        CurrentX = CurrentY =0;
        if(Option.DISPLAY_TYPE!=SCREENMODE1)ClearScreen(Option.DefaultBC);
        SetFont(Option.DefaultFont);
        return 1;
    }
    return 0;
}
