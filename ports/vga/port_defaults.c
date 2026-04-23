/*
 * ports/vga/port_defaults.c — COMPILE=VGA / VGAUSB board-specific
 * default Option.* values. PICOMITEVGA variant on RP2040.
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
    /* VGA has no touch — TOUCH_XSCALE / TOUCH_YSCALE stay at 0. */
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
        else if(checkstring(argv[0], (unsigned char *)"720") || checkstring(argv[0], (unsigned char *)"720x400")){
            Option.CPU_Speed = Freq400;
            Option.DISPLAY_TYPE=SCREENMODE1;
            Option.DefaultFont= 1 ;
        }
        else error("Syntax");
        Option.X_TILE=(Option.CPU_Speed==Freq400 ? 90 : 80);
        Option.Y_TILE=(Option.CPU_Speed==Freq400 ? 33 : 40);
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"VGA PINS");
    if(tp) {
        int pin1,pin2,testpin;
        getargs(&tp,3,(unsigned char *)",");
        if(CurrentLinePtr) error("Invalid in a program");
        char code;
        if(!(code=codecheck(argv[0])))argv[0]+=2;
        pin1 = getinteger(argv[0]);
        if(!code)pin1=codemap(pin1);
        if(!(code=codecheck(argv[2])))argv[2]+=2;
        pin2 = getinteger(argv[2]);
        if(!code)pin2=codemap(pin2);
        ExtCurrentConfig[Option.VGA_BLUE]=EXT_NOT_CONFIG;
        ExtCurrentConfig[Option.VGA_HSYNC]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+1]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+2]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+3]]=EXT_NOT_CONFIG;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_HSYNC].GPno+1]]=EXT_NOT_CONFIG;
        if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)VGArecovery(pin1);
        if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)VGArecovery(pin2);
        testpin=PINMAP[PinDef[pin1].GPno+1];
        if(ExtCurrentConfig[testpin] != EXT_NOT_CONFIG)VGArecovery(testpin);
        testpin=PINMAP[PinDef[pin2].GPno+1];
        if(ExtCurrentConfig[testpin] != EXT_NOT_CONFIG)VGArecovery(testpin);
        testpin=PINMAP[PinDef[pin2].GPno+2];
        if(ExtCurrentConfig[testpin] != EXT_NOT_CONFIG)VGArecovery(testpin);
        testpin=PINMAP[PinDef[pin2].GPno+3];
        if(ExtCurrentConfig[testpin] != EXT_NOT_CONFIG)VGArecovery(testpin);
        Option.VGA_HSYNC=pin1;
        Option.VGA_BLUE=pin2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"DEFAULT MODE");
    if(tp) {
        int mode=getint(tp,1,MAXMODES);
        if(mode==2){
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

/* VGA pin de-allocation helper. Called from OPTION VGA PINS (in this
 * file's port_factory_reset_board / port_display_option_setter) and
 * from drivers/sd_spi/mmc_stm32.c on non-HDMI VGA builds when SD-card
 * pin reassignment requires reclaiming a VGA pin. The body lives only
 * on VGA ports because it touches piomap[QVGA_PIO_NUM] which is
 * VGA-stack-only. */
extern uint64_t piomap[];
void VGArecovery(int pin){
        ExtCurrentConfig[Option.VGA_BLUE]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[Option.VGA_HSYNC]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+1]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+2]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_BLUE].GPno+3]]=EXT_BOOT_RESERVED;
        ExtCurrentConfig[PINMAP[PinDef[Option.VGA_HSYNC].GPno+1]]=EXT_BOOT_RESERVED;

        if(pin)error("Pin %/| is in use",pin,pin);
#ifdef rp2350
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.VGA_BLUE].GPno);
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+1));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+2));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_BLUE].GPno+3));
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.VGA_HSYNC].GPno);
        piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.VGA_HSYNC].GPno+1));
        if(Option.audio_i2s_bclk){
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_data].GPno);
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)PinDef[Option.audio_i2s_bclk].GPno);
            piomap[QVGA_PIO_NUM]|=(uint64_t)((uint64_t)1<<(uint64_t)(PinDef[Option.audio_i2s_bclk].GPno+1));
        }
#endif
}

/* SSD1963 data-bus base GPIO. Defined in SSD1963.c on non-VGA/HDMI
 * ports; stubbed here so MM_Misc.c can read it unconditionally for
 * the OPTION LCDPANEL DISABLE reset path. */
int SSD1963data = 0;

/* Port has no separate LCD SPI bus (LCD_CLK Option fields don't exist on
 * this build); the share-clear hook is a no-op. disable_lcdspi isn't
 * called from core on these ports. */
void port_clear_lcd_spi_if_shares_system(void) {}
