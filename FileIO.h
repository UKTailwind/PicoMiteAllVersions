/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

FileIO.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

#ifndef __FILEIO_H
#define __FILEIO_H

#ifdef __cplusplus
extern "C"
{
#endif

/* ============================================================================
 * Includes
 * ============================================================================ */
#include "ff.h"

#ifdef rp2350
#include "upng.h"
#endif

/* ============================================================================
 * Constants - Flash storage regions
 * ============================================================================ */
#define SAVED_OPTIONS_FLASH 5
#define LIBRARY_FLASH 6
#define SAVED_VARS_FLASH 7
#define PROGRAM_FLASH 8

    /* ============================================================================
     * Constants - File types
     * ============================================================================ */
    enum
    {
        NONEFILE,
        FLASHFILE,
        FATFSFILE
    };

    /* ============================================================================
     * Type definitions - File table union
     * ============================================================================ */
    typedef union uFileTable
    {
        unsigned int com;
        FIL *fptr;
        lfs_file_t *lfsptr;
    } u_file;

    /* ============================================================================
     * Type definitions - Options structure
     * ============================================================================ */
    struct option_s
    {
        /* Basic settings */
        int Magic;
        char Autorun;
        char Tab;
        char Invert;
        char Listcase; // 8 bytes

        /* Memory configuration */
        unsigned int PROG_FLASH_SIZE;
        unsigned int HEAP_SIZE;

        /* Display dimensions */
#ifndef PICOMITEVGA
        char Height;
        char Width;
#else
    short d2;
#endif

        /* Display configuration */
        unsigned char DISPLAY_TYPE;
        char DISPLAY_ORIENTATION; // 12-20 bytes

        /* Security and communication */
        int PIN;
        int Baudrate;
        int8_t ColourCode;
        unsigned char MOUSE_CLOCK;
        unsigned char MOUSE_DATA;
        char spare;
        int CPU_Speed;
        unsigned int Telnet; // Also stores size of program flash (start of LIBRARY code)

        /* Color settings */
        int DefaultFC, DefaultBC; // Default colors
        short version;            // 40 bytes

        /* Keyboard configuration */
        unsigned char KEYBOARD_CLOCK;
        unsigned char KEYBOARD_DATA;
        unsigned char continuation;
        unsigned char LOCAL_KEYBOARD;
        unsigned char KeyboardBrightness;
        uint8_t D2;

        /* Font and RTC */
        unsigned char DefaultFont;
        unsigned char KeyboardConfig;
        unsigned char RTC_Clock;
        unsigned char RTC_Data; // 60 bytes

        /* Platform-specific configuration */
#if PICOMITERP2350
        unsigned char LCD_CLK;
        unsigned char LCD_MOSI;
        unsigned char LCD_MISO;
        char dummy; // 64 bytes
#endif

#if defined(PICOMITE) && !defined(rp2350)
        char dummy[4]; // 64 bytes
#endif

#ifdef PICOMITEWEB
        uint16_t TCP_PORT;
        uint16_t ServerResponceTime;
#endif

#ifdef PICOMITEVGA
        int16_t X_TILE;
        int16_t Y_TILE;
#endif

        /* SPI LCD pins */
        unsigned char LCD_CD;
        unsigned char LCD_CS;
        unsigned char LCD_Reset;

        /* Touch screen configuration */
        unsigned char TOUCH_CS;
        unsigned char TOUCH_IRQ;
        char TOUCH_SWAPXY;
        unsigned char repeat;
        char disabletftp; // 72 bytes

        /* Touch calibration */
#ifndef PICOMITEVGA
        int TOUCH_XZERO;
        int TOUCH_YZERO;
        float TOUCH_XSCALE;
        float TOUCH_YSCALE; // 88 bytes
#else
    short Height;
    short Width;
    char dummy[12];
#endif

        /* GUI or HDMI configuration */
#ifdef GUICONTROLS
        uint8_t MaxCtrls;
        unsigned char spare3[3];
#else
    uint8_t HDMIclock;
    uint8_t HDMId0;
    uint8_t HDMId1;
    uint8_t HDMId2;
#endif

        /* Flash and SD card */
        unsigned int FlashSize; // 96 bytes
        unsigned char SD_CS;
        unsigned char SYSTEM_MOSI;
        unsigned char SYSTEM_MISO;
        unsigned char SYSTEM_CLK;

        /* Display backlight and console */
        unsigned char DISPLAY_BL;
        unsigned char DISPLAY_CONSOLE;
        unsigned char TOUCH_Click;
        char LCD_RD; // Used for RD pin for SSD1963, 104 bytes

        /* Audio configuration */
        unsigned char AUDIO_L;
        unsigned char AUDIO_R;
        unsigned char AUDIO_SLICE;
        unsigned char SDspeed;
        unsigned char pins[3]; // General use storage for CFunctions

        /* Touch and display */
        unsigned char TOUCH_CAP;
        unsigned char SSD_DATA;
        unsigned char THRESHOLD_CAP;
        unsigned char audio_i2s_data;
        unsigned char audio_i2s_bclk;
        char LCDVOP;
        char I2Coffset;
        unsigned char NoHeartbeat;
        char Refresh;

        /* System I2C and RTC */
        unsigned char SYSTEM_I2C_SDA;
        unsigned char SYSTEM_I2C_SCL;
        unsigned char RTC;
        char PWM; // 124 bytes

        /* Interrupt pins */
        unsigned char INT1pin;
        unsigned char INT2pin;
        unsigned char INT3pin;
        unsigned char INT4pin;

        /* SD card pins */
        unsigned char SD_CLK_PIN;
        unsigned char SD_MOSI_PIN;
        unsigned char SD_MISO_PIN;

        /* Serial console */
        unsigned char SerialConsole; // 132 bytes
        unsigned char SerialTX;
        unsigned char SerialRX;

        /* Keyboard lock status */
        unsigned char numlock;
        unsigned char capslock; // 136 bytes

        /* Library flash size */
        unsigned int LIBRARY_FLASH_SIZE; // 140 bytes

        /* Audio pins */
        unsigned char AUDIO_CLK_PIN;
        unsigned char AUDIO_MOSI_PIN;
        unsigned char SYSTEM_I2C_SLOW;
        unsigned char AUDIO_CS_PIN; // 144 bytes

        /* Network configuration (PICOMITEWEB) */
#ifdef PICOMITEWEB
        uint16_t UDP_PORT;
        uint16_t UDPServerResponceTime;
        char hostname[32];
        char ipaddress[16];
        char mask[16];
        char gateway[16];
#else
    unsigned char x[84]; // 229 bytes
#endif

        /* Miscellaneous pins and settings */
        unsigned char heartbeatpin;
        unsigned char PSRAM_CS_PIN;
        unsigned char BGR;
        unsigned char NoScroll;
        unsigned char CombinedCS;
        unsigned char USBKeyboard;
        unsigned char VGA_HSYNC;
        unsigned char VGA_BLUE; // 236 bytes

        /* Additional audio pins */
        unsigned char AUDIO_MISO_PIN;
        unsigned char AUDIO_DCS_PIN;
        unsigned char AUDIO_DREQ_PIN;
        unsigned char AUDIO_RESET_PIN;

        /* SSD display pins */
        unsigned char SSD_DC;
        unsigned char SSD_WR;
        unsigned char SSD_RD;
        signed char SSD_RESET; // 244 bytes

        /* Display and reset settings */
        unsigned char BackLightLevel;
        unsigned char NoReset;
        unsigned char AllPins;
        unsigned char modbuff; // 248 bytes

        /* Keyboard repeat settings */
        short RepeatStart;
        short RepeatRate;
        int modbuffsize; // 256 bytes

        /* Function keys and network credentials */
        unsigned char F1key[MAXKEYLEN];
        unsigned char F5key[MAXKEYLEN];
        unsigned char F6key[MAXKEYLEN];
        unsigned char F7key[MAXKEYLEN];
        unsigned char F8key[MAXKEYLEN];
        unsigned char F9key[MAXKEYLEN];
        unsigned char SSID[MAXKEYLEN];
        unsigned char PASSWORD[MAXKEYLEN]; // 768 bytes

        /* Platform identification and extensions */
        unsigned char platform[32];
        unsigned char extensions[96]; // 896 bytes == 7 XMODEM blocks

        /* NOTE: To enable older CFunctions to run, any new options MUST be added at the end of the list */
    } __attribute__((packed));

    /* ============================================================================
     * External variables - File tables and options
     * ============================================================================ */
    extern union uFileTable FileTable[MAXOPENFILES + 1];
    extern struct option_s Option;
    extern unsigned char filesource[MAXOPENFILES + 1];

    /* ============================================================================
     * External variables - Flash and file system
     * ============================================================================ */
    extern unsigned char *CFunctionFlash, *CFunctionLibrary;
    extern const uint8_t *flash_option_contents;
    extern volatile uint32_t realflashpointer;
    extern int FlashLoad;
    extern int FatFSFileSystemSave;
    extern int FSerror;
    extern int lfs_FileFnbr;
    extern struct lfs_config pico_lfs_cfg;

    /* ============================================================================
     * External variables - Error handling
     * ============================================================================ */
    extern int OptionFileErrorAbort;

    /* ============================================================================
     * Function declarations - File operations
     * ============================================================================ */
    void MMfopen(unsigned char *fname, unsigned char *mode, int fnbr);
    void MMfclose(int fnbr);
    unsigned char MMfputc(unsigned char c, int fnbr);
    int MMfgetc(int filenbr);
    int MMfeof(int filenbr);
    void MMgetline(int filenbr, char *p);

    /* ============================================================================
     * Function declarations - File management
     * ============================================================================ */
    int FindFreeFileNbr(void);
    void CloseAllFiles(void);
    int ForceFileClose(int fnbr);
    void ErrorCheck(int fnbr);
    int FileEOF(int fnbr);

    /* ============================================================================
     * Function declarations - Character and string I/O
     * ============================================================================ */
    char FileGetChar(int fnbr);
    char FilePutChar(char c, int fnbr);
    void FilePutStr(int count, char *c, int fnbr);
    void FilePutdata(char *c, int fnbr, int n);
    int __not_in_flash_func(FileGetdata)(int fnbr, void *buff, int count, unsigned int *read);

    /* ============================================================================
     * Function declarations - File positioning
     * ============================================================================ */
    void positionfile(int fnbr, int idx);

    /* ============================================================================
     * Function declarations - Program loading
     * ============================================================================ */
    int FileLoadProgram(unsigned char *fname, bool chain);
    int FileLoadCMM2Program(char *fname, bool message);

    /* ============================================================================
     * Function declarations - Options and configuration
     * ============================================================================ */
    void LoadOptions(void);
    void SaveOptions(void);
    void ResetOptions(bool startup);

    /* ============================================================================
     * Function declarations - Flash operations
     * ============================================================================ */
    void FlashWriteInit(int region);
    void FlashWriteBlock(void);
    void FlashWriteWord(unsigned int i);
    void FlashWriteByte(unsigned char b);
    void FlashWriteAlign(void);
    void FlashWriteAlignWord(void);
    void FlashWriteClose(void);
    void FlashSetAddress(int address);
    void ResetAllFlash(void);
    void ResetFlashStorage(int umount);

    /* ============================================================================
     * Function declarations - Saved variables
     * ============================================================================ */
    void ClearSavedVars(void);

    /* ============================================================================
     * Function declarations - Data compression
     * ============================================================================ */
    void CrunchData(unsigned char **p, int c);

    /* ============================================================================
     * Function declarations - SD card
     * ============================================================================ */
    void CheckSDCard(void);

    /* ============================================================================
     * Function declarations - Interrupt control
     * ============================================================================ */
    void disable_interrupts_pico(void);
    void enable_interrupts_pico(void);

    /* ============================================================================
     * Function declarations - File system utilities
     * ============================================================================ */
    int drivecheck(char *p, int *waste);
    void getfullfilename(char *fname, char *q);
    char *GetCWD(void);
    unsigned short hashversion(void);

    /* ============================================================================
     * Function declarations - Output
     * ============================================================================ */
    void MMPrintString(char *s);
    void CheckAbort(void);

#ifdef __cplusplus
}
#endif

#endif /* __FILEIO_H */

/*  @endcond */