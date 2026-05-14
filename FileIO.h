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
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/#ifndef __FILEIO_H
#define __FILEIO_H

#ifdef __cplusplus
extern "C" {
#endif
#include "port_config.h"   /* HAL_PORT_HAS_WIFI / HAL_PORT_HAS_GUICONTROLS
                              gates inside FileIO.h read from this. */
#include "ff.h"
#include "hal/hal_filesystem.h"

/* DEFINES dictionary state — used by the rp2350 compile-time
 * DEFINES feature. Declared here so the extracted loader in
 * ports/pico_sdk_common/defines_loader.c and the legacy callers in
 * FileIO.c share a single definition. */
typedef struct sa_dlist {
    char from[32];
    char to[32];
} a_dlist;

/* Flash/PSRAM-write scratch word. Defined in FileIO.c; the extracted
 * MemWriteBlock / MemWriteByte helpers in
 * ports/pico_sdk_common/mem_writeblock.c need it declared here. */
union u_flash {
    uint64_t i64[32];
    uint8_t  i8[256];
    uint32_t i32[64];
};
extern volatile union u_flash MemWord;
extern volatile int mi8p;
#ifdef rp2350
#include "upng.h"
#endif
// File related I/O
unsigned char MMfputc(unsigned char c, int fnbr);
int MMfgetc(int filenbr);
void MMfopen(unsigned char *fname, unsigned char *mode, int fnbr);
int MMfeof(int filenbr);
void MMfclose(int fnbr);
int FindFreeFileNbr(void);
void CloseAllFiles(void);
void MMgetline(int filenbr, char *p);
void MMPrintString(char *s);
void CheckAbort(void);
char FileGetChar(int fnbr);
void FilePutStr(int count, char *c, int fnbr);
char FilePutChar(char c, int fnbr);
void CheckSDCard(void);
void LoadOptions(void);
void CrunchData(unsigned char **p, int c);
int FileEOF(int fnbr);
void ClearSavedVars(void);
int FileLoadProgram(unsigned char *fname, bool chain);
int FileLoadSourceProgram(unsigned char *fname, char **source_out);
int FileLoadCMM2Program(char *fname, bool message);
void SaveOptions(void);
void ResetAllFlash(void);
void fileio_flash_write_begin(void);
void fileio_flash_write_end(void);
int ForceFileClose(int fnbr);
void ErrorCheck(int fnbr);
extern int OptionFileErrorAbort;
extern unsigned char filesource[MAXOPENFILES + 1];
extern int FatFSFileSystemSave;
extern void positionfile(int fnbr, int idx);
/* Stage P1 — flat layout. Every port sees the same struct layout
 * regardless of which HAL_PORT_HAS_* flags are set, so no feature
 * combination can silently corrupt persisted Option blobs by
 * misaligning fields. Adds ~19 bytes per port over the old gated
 * layout (negligible against the ~900-byte struct + 8 KB save area).
 *
 * Field meaning:
 *   Height/Width         — console rows/cols (was char on non-VGA,
 *                          short on VGA; now always short for both)
 *   KEYBOARDBL           — PicoCalc keypad backlight (other ports
 *                          ignore the field at runtime)
 *   LCD_CLK/MOSI/MISO    — rp2350 PICOMITE LCD-on-its-own-SPI bus
 *                          fields (other ports ignore)
 *   TCP_PORT, ServerResponceTime, UDP_PORT, UDPServerResponceTime,
 *   hostname, ipaddress, mask, gateway — WiFi networking fields
 *   X_TILE, Y_TILE       — VGA tile-grid dimensions
 *   TOUCH_XZERO/YZERO/XSCALE/YSCALE — SPI-LCD touch panel calibration
 *   MaxCtrls             — GUICONTROLS widget table cap
 *   HDMIclock/HDMId0..d2 — HDMI clock + 3 data PIO pair indices
 */
struct option_s {
    int  Magic;
    char Autorun;
    char Tab;
    char Invert;
    char Listcase; //8
  //
    unsigned int PROG_FLASH_SIZE;
    unsigned int HEAP_SIZE;
    short Height;        // console rows (was char on non-VGA, short on VGA)
    short Width;         // console cols
    unsigned char DISPLAY_TYPE;
    char DISPLAY_ORIENTATION; //20
//
    int  PIN;
    int  Baudrate;
    int8_t ColourCode;
    unsigned char MOUSE_CLOCK;
    unsigned char MOUSE_DATA;
    char spare;
    int CPU_Speed;
    unsigned int Telnet;    // used to store the size of the program flash (also start of the LIBRARY code)
    int DefaultFC, DefaultBC;      // the default colours
    short D3;         // default backlight brightness //48
    unsigned char KEYBOARD_CLOCK;
    unsigned char KEYBOARD_DATA;
    unsigned char continuation;
    unsigned char LOCAL_KEYBOARD;
    unsigned char KeyboardBrightness;
    uint8_t  D2;     //56
//
    // display related
    unsigned char DefaultFont;
    unsigned char KeyboardConfig;
    unsigned char RTC_Clock;
    unsigned char RTC_Data; //60
//
    /* PicoCalc keypad backlight. Always present so layout is
     * port-independent; non-PicoCalc ports ignore the value at
     * runtime (HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C=0 in port_config.h
     * gates the read-side branch). */
    uint8_t KEYBOARDBL;
    /* rp2350 PICOMITE second-SPI-bus LCD pin fields. Always present;
     * non-PICOMITE ports ignore them at runtime. */
    unsigned char LCD_CLK;
    unsigned char LCD_MOSI;
    unsigned char LCD_MISO; //64
    /* WiFi network-port + response-timeout. Always present; non-WiFi
     * ports ignore. */
    uint16_t TCP_PORT;
    uint16_t ServerResponceTime; //68
    /* VGA tile grid. Always present; non-VGA ports leave at 0. */
    int16_t X_TILE;
    int16_t Y_TILE; //72
    // for the SPI LCDs
    unsigned char LCD_CD;
    unsigned char LCD_CS;
    unsigned char LCD_Reset;
    // touch related
    unsigned char TOUCH_CS;
    unsigned char TOUCH_IRQ;
    char TOUCH_SWAPXY;
    unsigned char repeat;
    char disabletftp; //80
    /* SPI-LCD touch panel calibration. Always present; VGA/HDMI
     * ports leave at 0 (no touch panel on a scanout-display port). */
    int  TOUCH_XZERO;
    int  TOUCH_YZERO;
    float TOUCH_XSCALE;
    float TOUCH_YSCALE; //96
    /* GUICONTROLS widget cap. Always present; non-GUICONTROLS ports
     * leave at 0 — code that reads MaxCtrls is gated on
     * HAL_PORT_HAS_GUICONTROLS. */
    uint8_t MaxCtrls;
    /* HDMI clock + 3 data PIO pair indices (0..7). Always present;
     * non-HDMI ports ignore. */
    uint8_t HDMIclock;
    uint8_t HDMId0;
    uint8_t HDMId1;
    uint8_t HDMId2;
    unsigned char spare3[3]; //104
    unsigned int FlashSize; //108
    unsigned char SD_CS;
    unsigned char SYSTEM_MOSI;
    unsigned char SYSTEM_MISO;
    unsigned char SYSTEM_CLK;
    unsigned char DISPLAY_BL;
    unsigned char DISPLAY_CONSOLE;
    unsigned char TOUCH_Click;
    char LCD_RD;                   // used for the RD pin for SSD1963  //8=104
    unsigned char AUDIO_L;
    unsigned char AUDIO_R;
    unsigned char AUDIO_SLICE; 
    unsigned char SDspeed;
    unsigned char pins[3];  //20=116                // general use storage for CFunctions written by PeterM //86
    unsigned char TOUCH_CAP;
    unsigned char SSD_DATA;
    unsigned char THRESHOLD_CAP;
    unsigned char audio_i2s_data;
    unsigned char audio_i2s_bclk;
    char LCDVOP;
    char I2Coffset;
    unsigned char NoHeartbeat; 
    char Refresh;
    unsigned char SYSTEM_I2C_SDA;
    unsigned char SYSTEM_I2C_SCL;
    unsigned char RTC;
    char PWM;  //8=124
    unsigned char INT1pin;
    unsigned char INT2pin;
    unsigned char INT3pin; 
    unsigned char INT4pin;
    unsigned char SD_CLK_PIN;
    unsigned char SD_MOSI_PIN;
    unsigned char SD_MISO_PIN;
    unsigned char SerialConsole; //8=132
    unsigned char SerialTX;
    unsigned char SerialRX;
    unsigned char numlock; 
    unsigned char capslock; //4=136
    unsigned int  LIBRARY_FLASH_SIZE; // 4=140
    unsigned char AUDIO_CLK_PIN;
    unsigned char AUDIO_MOSI_PIN;
    unsigned char SYSTEM_I2C_SLOW;
    unsigned char AUDIO_CS_PIN; //4=144
    /* WiFi UDP + DHCP-style network config. Always present; non-WiFi
     * ports leave the strings empty / ports at 0. */
    uint16_t UDP_PORT;
    uint16_t UDPServerResponceTime;
    char hostname[32];
    char ipaddress[16];
    char mask[16];
    char gateway[16]; //84 bytes
    unsigned char heartbeatpin;
    unsigned char PSRAM_CS_PIN;
    unsigned char BGR;
    unsigned char NoScroll;
    unsigned char CombinedCS;
    unsigned char USBKeyboard;
    unsigned char VGA_HSYNC;
    unsigned char VGA_BLUE; //7=236
    unsigned char AUDIO_MISO_PIN;
    unsigned char AUDIO_DCS_PIN;
    unsigned char AUDIO_DREQ_PIN;
    unsigned char AUDIO_RESET_PIN;
    unsigned char SSD_DC;
    unsigned char SSD_WR;
    unsigned char SSD_RD;
    signed char SSD_RESET; //8=244
    unsigned char BackLightLevel;
    unsigned char NoReset;
    unsigned char AllPins;
    unsigned char modbuff; //4=248
	short RepeatStart;
	short RepeatRate;
    int modbuffsize; //8=256
    unsigned char F1key[MAXKEYLEN]; 
    unsigned char F5key[MAXKEYLEN]; 
    unsigned char F6key[MAXKEYLEN]; 
    unsigned char F7key[MAXKEYLEN]; 
    unsigned char F8key[MAXKEYLEN]; 
    unsigned char F9key[MAXKEYLEN]; 
    unsigned char SSID[MAXKEYLEN];  
    unsigned char PASSWORD[MAXKEYLEN]; //512=768
    unsigned char platform[32]; 
    unsigned char extensions[96]; //128=896 == 7 XMODEM blocks
    uint16_t pc386_sb_base;
    uint8_t pc386_sb_irq;
    uint8_t pc386_sb_dma;
    uint8_t pc386_sb_dma16;
    unsigned char WebConsole;          // ESP32 web console enable (0=off,1=on)
    // To enable older CFunctions to run any new options *MUST* be added at the end of the list
} __attribute__((packed));
extern unsigned char *CFunctionFlash, *CFunctionLibrary;
extern struct option_s Option;
extern int FlashLoad;
extern void ResetOptions(bool startup);
extern void FlashWriteBlock(void);
extern void FlashWriteWord(unsigned int i);
extern void FlashWriteByte(unsigned char b);
extern void FlashWriteAlign(void);
extern void FlashWriteClose(void);
extern void FlashWriteInit(int region);
void FlashSetAddress(int address);  //new
extern void FlashWriteAlignWord(void);  //new
extern void ResetFlashStorage(int umount);
extern volatile uint32_t realflashpointer;
extern int drivecheck(char *p, int *waste);
extern void getfullfilename(char *fname, char *q);
extern char *GetCWD(void);
extern int FSerror;
extern int lfs_FileFnbr;
extern struct lfs_config pico_lfs_cfg;
#define SAVED_OPTIONS_FLASH 5
#define LIBRARY_FLASH 6
#define SAVED_VARS_FLASH 7
#define PROGRAM_FLASH 8
/* FileTable slot. `com` is the allocation marker:
 *     0              → slot free
 *     1..MAXCOMPORTS → slot holds a serial port
 *     MAXCOMPORTS+1  → slot holds an open file (hal_fds[fnbr] has the fd)
 * Pre-real-hal the union also held the raw FatFS/LFS backend pointer so
 * callers could peek into it; every such caller now goes through
 * hal_fs_* on hal_fds[fnbr] and the shadow retired. */
typedef struct uFileTable
{
    unsigned int com;
}u_file;
enum {
    NONEFILE,
    FLASHFILE,
    FATFSFILE
};
#define FILE_SLOT_MARKER (MAXCOMPORTS + 1)
extern struct uFileTable FileTable[MAXOPENFILES + 1];
/* Per-fnbr HAL fd (see FileIO.c for ownership semantics). Indexed by
 * MMBasic file number (1..MAXOPENFILES); slot 0 is the console sentinel.
 * Callers that used to read FileTable[fnbr].fptr / .lfsptr should use
 * hal_fs_* on hal_fds[fnbr] instead. */
extern hal_fs_fd_t hal_fds[MAXOPENFILES + 1];

#ifdef __cplusplus
}
#endif
#endif /* __FILEIO_H */
/*  @endcond */
