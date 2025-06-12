/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

SPI-LCD.h.c

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

************************************************************************************************************************/
#ifndef SPI_LCD_HEADER
#define SPI_LCD_HEADER
#include "hardware/spi.h"





extern void ConfigDisplaySPI(unsigned char *p);
extern void InitDisplaySPI(int InitOnly);
extern void SetAndReserve(int pin, int inp, int init, int type);
extern void OpenSpiChannel(void);
extern void DisplayNotSet(void);
extern void SPISpeedSet(int speed);
extern void DefineRegionSPI(int xstart, int ystart, int xend, int yend, int rw);
extern void ClearCS(int pin);
extern void ResetController(void);
extern void spi_write_command(unsigned char data);
extern void spi_write_cd(unsigned char command, int data, ...);
extern void spi_write_data(unsigned char data);
extern void DrawRectangleSPI(int x1, int y1, int x2, int y2, int c);
extern void DrawBufferSPI(int x1, int y1, int x2, int y2, unsigned char* p);
extern void DrawBitmapSPI(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void ReadBufferSPI(int x1, int y1, int x2, int y2, unsigned char* p) ;
extern void DrawRectangleSPISCR(int x1, int y1, int x2, int y2, int c);
extern void DrawBufferSPISCR(int x1, int y1, int x2, int y2, unsigned char* p);
extern void DrawBitmapSPISCR(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void ReadBufferSPISCR(int x1, int y1, int x2, int y2, unsigned char* p) ;
extern void ScrollLCDSPISCR(int lines);
extern void set_cs(void);
extern void __not_in_flash_func(spi_write_fast)(spi_inst_t *spi, const uint8_t *src, size_t len);
extern void __not_in_flash_func(spi_finish)(spi_inst_t *spi);
#define SSD1331_COLORORDER_RGB
#define SSD1331_CMD_DRAWLINE 		0x21
#define SSD1331_CMD_DRAWRECT 		0x22
#define SSD1331_CMD_FILL 			0x26
#define SSD1331_CMD_SETCOLUMN 		0x15
#define SSD1331_CMD_SETROW    		0x75
#define SSD1331_CMD_CONTRASTA 		0x81
#define SSD1331_CMD_CONTRASTB 		0x82
#define SSD1331_CMD_CONTRASTC		0x83
#define SSD1331_CMD_MASTERCURRENT 	0x87
#define SSD1331_CMD_SETREMAP 		0xA0
#define SSD1331_CMD_STARTLINE 		0xA1
#define SSD1331_CMD_DISPLAYOFFSET 	0xA2
#define SSD1331_CMD_NORMALDISPLAY 	0xA4
#define SSD1331_CMD_DISPLAYALLON  	0xA5
#define SSD1331_CMD_DISPLAYALLOFF 	0xA6
#define SSD1331_CMD_INVERTDISPLAY 	0xA7
#define SSD1331_CMD_SETMULTIPLEX  	0xA8
#define SSD1331_CMD_SETMASTER 		0xAD
#define SSD1331_CMD_DISPLAYDIM 		0xAC
#define SSD1331_CMD_DISPLAYOFF 		0xAE
#define SSD1331_CMD_DISPLAYON     	0xAF
#define SSD1331_CMD_POWERMODE 		0xB0
#define SSD1331_CMD_PRECHARGE 		0xB1
#define SSD1331_CMD_CLOCKDIV 		0xB3
#define SSD1331_CMD_PRECHARGEA 		0x8A
#define SSD1331_CMD_PRECHARGEB 		0x8B
#define SSD1331_CMD_PRECHARGEC 		0x8C
#define SSD1331_CMD_PRECHARGELEVEL 	0xBB
#define SSD1331_CMD_VCOMH 			0xBE

#define ST7735_NOP              0x0
#define ST7735_SWRESET          0x01
#define ST7735_RDDID            0x04
#define ST7735_RDDST            0x09
#define ST7735_SLPIN            0x10
#define ST7735_SLPOUT           0x11
#define ST7735_PTLON            0x12
#define ST7735_NORON            0x13
#define ST7735_INVOFF           0x20
#define ST7735_INVON            0x21
#define ST7735_DISPOFF          0x28
#define ST7735_DISPON           0x29
#define ST7735_CASET            0x2A
#define ST7735_RASET            0x2B
#define ST7735_RAMWR            0x2C
#define ST7735_RAMRD            0x2E
#define ST7735_PTLAR            0x30
#define ST7735_MADCTL           0x36
#define ST7735_COLMOD           0x3A
#define ST7735_FRMCTR1          0xB1
#define ST7735_FRMCTR2          0xB2
#define ST7735_FRMCTR3          0xB3
#define ST7735_INVCTR           0xB4
#define ST7735_DISSET5          0xB6
#define ST7735_PWCTR1           0xC0
#define ST7735_PWCTR2           0xC1
#define ST7735_PWCTR3           0xC2
#define ST7735_PWCTR4           0xC3
#define ST7735_PWCTR5           0xC4
#define ST7735_VMCTR1           0xC5
#define ST7735_RDID1            0xDA
#define ST7735_RDID2            0xDB
#define ST7735_RDID3            0xDC
#define ST7735_RDID4            0xDD
#define ST7735_PWCTR6           0xFC
#define ST7735_GMCTRP1          0xE0
#define ST7735_GMCTRN1          0xE1
#define ST7735_Portrait         0xC0
#define ST7735_Portrait180      0
#define ST7735_Landscape        0xA0
#define ST7735_Landscape180     0x60

#define ILI9341_SOFTRESET       0x01
#define ILI9341_SLEEPIN         0x10
#define ILI9341_SLEEPOUT        0x11
#define ILI9341_NORMALDISP      0x13
#define ILI9341_INVERTOFF       0x20
#define ILI9341_INVERTON        0x21
#define ILI9341_GAMMASET        0x26
#define ILI9341_DISPLAYOFF      0x28
#define ILI9341_DISPLAYON       0x29
#define ILI9341_COLADDRSET      0x2A
#define ILI9341_PAGEADDRSET     0x2B
#define ILI9341_MEMORYWRITE     0x2C
#define ILI9341_RAMRD           0x2E
#define ILI9341_PIXELFORMAT     0x3A
#define ILI9341_FRAMECONTROL    0xB1
#define ILI9341_DISPLAYFUNC     0xB6
#define ILI9341_ENTRYMODE       0xB7
#define ILI9341_POWERCONTROL1   0xC0
#define ILI9341_POWERCONTROL2   0xC1
#define ILI9341_VCOMCONTROL1    0xC5
#define ILI9341_VCOMCONTROL2    0xC7
#define ILI9341_MEMCONTROL 	0x36
#define ILI9341_MADCTL_MY  	0x80
#define ILI9341_MADCTL_MX  	0x40
#define ILI9341_MADCTL_MV  	0x20
#define ILI9341_MADCTL_ML  	0x10
#define ILI9341_MADCTL_RGB 	0x00
#define ILI9341_MADCTL_BGR 	0x08
#define ILI9341_MADCTL_MH  	0x04

#define ILI9341_Portrait        ILI9341_MADCTL_MX | ILI9341_MADCTL_BGR
#define ILI9341_Portrait180     ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR
#define ILI9341_Landscape       ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR
#define ILI9341_Landscape180    ILI9341_MADCTL_MY | ILI9341_MADCTL_MX | ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR

#define ILI9481_MADCTL_FH       0x02
#define ILI9481_MADCTL_FV       0x01

#define ILI9481_Portrait        ILI9481_MADCTL_FH | ILI9341_MADCTL_BGR
#define ILI9481_Portrait180     ILI9481_MADCTL_FV | ILI9341_MADCTL_BGR
#define ILI9481_Landscape       ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR
#define ILI9481_Landscape180    ILI9481_MADCTL_FV | ILI9481_MADCTL_FH | ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR
//
#define ILI9163_NOP             0x00//Non operation
#define ILI9163_SWRESET 	0x01//Soft Reset
#define ILI9163_SLPIN   	0x10//Sleep ON
#define ILI9163_SLPOUT  	0x11//Sleep OFF
#define ILI9163_PTLON   	0x12//Partial Mode ON
#define ILI9163_NORML   	0x13//Normal Display ON
#define ILI9163_DINVOF  	0x20//Display Inversion OFF
#define ILI9163_DINVON   	0x21//Display Inversion ON
#define ILI9163_GAMMASET 	0x26//Gamma Set (0x01[1],0x02[2],0x04[3],0x08[4])
#define ILI9163_DISPOFF 	0x28//Display OFF
#define ILI9163_DISPON  	0x29//Display ON
#define ILI9163_IDLEON  	0x39//Idle Mode ON
#define ILI9163_IDLEOF  	0x38//Idle Mode OFF
#define ILI9163_CLMADRS   	0x2A//Column Address Set
#define ILI9163_PGEADRS   	0x2B//Page Address Set
#define ILI9163_RAMWR   	0x2C//Memory Write
#define ILI9163_RAMRD   	0x2E//Memory Read
#define ILI9163_CLRSPACE   	0x2D//Color Space : 4K/65K/262K
#define ILI9163_PARTAREA	0x30//Partial Area
#define ILI9163_VSCLLDEF	0x33//Vertical Scroll Definition
#define ILI9163_TEFXLON		0x35//Tearing Effect Line ON
#define ILI9163_TEFXLOF		0x34//Tearing Effect Line OFF
#define ILI9163_MADCTL  	0x36//Memory Access Control
#define ILI9163_VSSTADRS	0x37//Vertical Scrolling Start address
#define ILI9163_PIXFMT  	0x3A//Interface Pixel Format
#define ILI9341_GETSCANLINE 0x45//read the current scanline
#define ILI9163_FRMCTR1 	0xB1//Frame Rate Control (In normal mode/Full colors)
#define ILI9163_FRMCTR2 	0xB2//Frame Rate Control(In Idle mode/8-colors)
#define ILI9163_FRMCTR3 	0xB3//Frame Rate Control(In Partial mode/full colors)
#define ILI9163_DINVCTR		0xB4//Display Inversion Control
#define ILI9163_RGBBLK		0xB5//RGB Interface Blanking Porch setting
#define ILI9163_DFUNCTR 	0xB6//Display Fuction set 5
#define ILI9163_SDRVDIR 	0xB7//Source Driver Direction Control
#define ILI9163_GDRVDIR 	0xB8//Gate Driver Direction Control
#define ILI9163_PWCTR1  	0xC0//Power_Control1
#define ILI9163_PWCTR2  	0xC1//Power_Control2
#define ILI9163_PWCTR3  	0xC2//Power_Control3
#define ILI9163_PWCTR4  	0xC3//Power_Control4
#define ILI9163_PWCTR5  	0xC4//Power_Control5
#define ILI9163_VCOMCTR1  	0xC5//VCOM_Control 1
#define ILI9163_VCOMCTR2  	0xC6//VCOM_Control 2
#define ILI9163_VCOMOFFS  	0xC7//VCOM Offset Control
#define ILI9163_PGAMMAC		0xE0//Positive Gamma Correction Setting
#define ILI9163_NGAMMAC		0xE1//Negative Gamma Correction Setting
#define ILI9163_GAMRSEL		0xF2//GAM_R_SEL
#define ILI9163_Portrait        0b00001000
#define ILI9163_Portrait180     0b11001000
#define ILI9163_Landscape       0b01101000
#define ILI9163_Landscape180    0b10101000
#define ST77XX_SWRESET    0x01
#define ST77XX_DISPON     0x29
#define ST77XX_CASET      0x2A
#define ST77XX_RASET      0x2B
#define ST77XX_INVON      0x21
#define ST77XX_INVOFF     0x20
#define ST77XX_NORON      0x13
#define ST77XX_SLPOUT     0x11
#define ST77XX_COLMOD     0x3A

#define GDEH029A1_PU_DELAY 300

#define GDEH029A1_X_PIXELS 128
#define GDEH029A1_Y_PIXELS 296
#define GDEH029A1_WIDTH GDEH029A1_X_PIXELS
#define GDEH029A1_HEIGHT GDEH029A1_Y_PIXELS
#define DRIVER_OUTPUT_CONTROL                       0x01
#define BOOSTER_SOFT_START_CONTROL                  0x0C
#define GATE_SCAN_START_POSITION                    0x0F
#define DEEP_SLEEP_MODE                             0x10
#define DATA_ENTRY_MODE_SETTING                     0x11
#define SW_RESET                                    0x12
#define TEMPERATURE_SENSOR_CONTROL                  0x1A
#define MASTER_ACTIVATION                           0x20
#define DISPLAY_UPDATE_CONTROL_1                    0x21
#define DISPLAY_UPDATE_CONTROL_2                    0x22
#define WRITE_RAM                                   0x24
#define WRITE_VCOM_REGISTER                         0x2C
#define WRITE_LUT_REGISTER                          0x32
#define SET_DUMMY_LINE_PERIOD                       0x3A
#define SET_GATE_TIME                               0x3B
#define BORDER_WAVEFORM_CONTROL                     0x3C
#define SET_RAM_X_ADD_START_END_POS                 0x44
#define SET_RAM_Y_ADD_START_END_POS                 0x45
#define SET_RAM_X_ADDRESS_COUNTER                   0x4E
#define SET_RAM_Y_ADDRESS_COUNTER                   0x4F
#define TERMINATE_FRAME_READ_WRITE                  0xFF
#define SPI_POLARITY_LOW false
#define SPI_PHASE_1EDGE false
#define SPI_POLARITY_HIGH true
#define SPI_PHASE_2EDGE true
#define ST7920setcommand 0b11111000
#define ST7920setata 0b11111010
#define SDFAST          0
#define SDSLOW          1
#define SSD1306I2C      2
#define SSD1306I2C32    3
#define I2C_PANEL       SSD1306I2C32    // anything less than or equal to I2C_PANEL is handled by the I2C driver
#define ILI9163         4
#define ILI9341         5
#define ST7735          6
#define ST7735S         7
#define SSD1331			8
#define ST7789          9
#define ILI9481         10
#define ILI9488         11
#define ILI9488P        12
#define ST7789A         13
#define ST7789B         14
#define ILI9488W        15
#define ST7796S         16
#define ST7796SP        17
#define ST7735S_W       18
#define GC9A01          19
#define ILI9481IPS      20
#define N5110			21
#define BufferedPanel	N5110
#define SSD1306SPI      22
#define ST7920			23
#define TOUCH           24
#define SPIReadSpeed    25
#define ST7789RSpeed    26
#define SLOWTOUCH       27
#define DISP_USER       28
#define SCREENMODE1     29
#define VGADISPLAY      SCREENMODE1  
#define SCREENMODE2     30
#define SCREENMODE3     31
#define SCREENMODE4     32
#define SCREENMODE5     33
#define SCREENMODE6     34
#define SCREENMODE7     35
#define SSD1963_4       36
#define SSDPANEL        SSD1963_4
#define SSD1963_5       37
#define SSD1963_5A      38
#define SSD1963_7       39
#define SSD1963_7A      40
#define SSD1963_8       41
#define ILI9341_8       42
#define SSD_PANEL_8 ILI9341_8 
#define SSD1963_4_16    43
#define SSD1963_5_16    44
#define SSD1963_5A_16   45
#define SSD1963_7_16    46
#define SSD1963_7A_16   47
#define SSD1963_8_16    48
#define ILI9341_16      49
#define IPS_4_16        50
#define SSD1963_5ER_16  51
#define SSD1963_7ER_16  52
#define ILI9486_16      53
#define VIRTUAL_C       54
#define VIRTUAL         VIRTUAL_C
#define VIRTUAL_M       55
#define VS1053slow      56
#define VS1053fast      57
#define NEXTGEN1        58
#define NEXTGEN          NEXTGEN1
#define TFT_NOP 0x00
#define TFT_SWRST 0x01
#define SSDTYPE (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL_C && !(Option.DISPLAY_TYPE==ILI9341_16 || Option.DISPLAY_TYPE==ILI9341_8 || Option.DISPLAY_TYPE==IPS_4_16 || Option.DISPLAY_TYPE==ILI9486_16))
#define SSD16TYPE (Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE<VIRTUAL_C && !(Option.DISPLAY_TYPE==ILI9341_16 || Option.DISPLAY_TYPE==IPS_4_16 || Option.DISPLAY_TYPE==ILI9486_16))
#define SPIREAD (Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ST7796SP  || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ST7789B)
#define FASTSCROLL (SSDTYPE || Option.DISPLAY_TYPE==SCREENMODE1 ||  Option.DISPLAY_TYPE == SCREENMODE2 || Option.DISPLAY_TYPE == VIRTUAL_C || Option.DISPLAY_ORIENTATION == VIRTUAL_M)
#define SPI480 (Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ST7796S || Option.DISPLAY_TYPE==ILI9488W || Option.DISPLAY_TYPE==ILI9481 || Option.DISPLAY_TYPE==ILI9481IPS) 
#define TFT_SLPIN 0x10
#define TFT_SLPOUT 0x11

#define TFT_INVOFF 0x20
#define TFT_INVON 0x21

#define TFT_DISPOFF 0x28
#define TFT_DISPON 0x29

#define TFT_CASET 0x2A
#define TFT_RASET 0x2B
#define TFT_RAMWR 0x2C

#define TFT_RAMRD 0x2E

#define TFT_MADCTL 0x36

#define TFT_MAD_MY 0x80
#define TFT_MAD_MX 0x40
#define TFT_MAD_MV 0x20
#define TFT_MAD_ML 0x10
#define TFT_MAD_RGB 0x00
#define TFT_MAD_BGR 0x08
#define TFT_MAD_MH 0x04
#define TFT_MAD_SS 0x02
#define TFT_MAD_GS 0x01

#define TFT_IDXRD 0x00 // ILI9341 only, indexed control register read
#define GC9A01_TFTWIDTH 240
#define GC9A01_TFTHEIGHT 240

#define GC9A01_RST_DELAY 120    ///< delay ms wait for reset finish
#define GC9A01_SLPIN_DELAY 120  ///< delay ms wait for sleep in finish
#define GC9A01_SLPOUT_DELAY 120 ///< delay ms wait for sleep out finish

#define GC9A01_NOP 0x00
#define GC9A01_SWRESET 0x01
#define GC9A01_RDDID 0x04
#define GC9A01_RDDST 0x09

#define GC9A01_SLPIN 0x10
#define GC9A01_SLPOUT 0x11
#define GC9A01_PTLON 0x12
#define GC9A01_NORON 0x13

#define GC9A01_INVOFF 0x20
#define GC9A01_INVON 0x21
#define GC9A01_DISPOFF 0x28
#define GC9A01_DISPON 0x29

#define GC9A01_CASET 0x2A
#define GC9A01_RASET 0x2B
#define GC9A01_RAMWR 0x2C
#define GC9A01_RAMRD 0x2E

#define GC9A01_PTLAR 0x30
#define GC9A01_COLMOD 0x3A
#define GC9A01_MADCTL 0x36

#define GC9A01_MADCTL_MY 0x80
#define GC9A01_MADCTL_MX 0x40
#define GC9A01_MADCTL_MV 0x20
#define GC9A01_MADCTL_ML 0x10
#define GC9A01_MADCTL_RGB 0x00

#define GC9A01_RDID1 0xDA
#define GC9A01_RDID2 0xDB
#define GC9A01_RDID3 0xDC
#define GC9A01_RDID4 0xDD

#define LANDSCAPE       1
#define PORTRAIT        2
#define RLANDSCAPE      3
#define RPORTRAIT       4
#define DISPLAY_LANDSCAPE   (Option.DISPLAY_ORIENTATION & 1)
#define TOUCH_NOT_CALIBRATED    -999999
#define RESET_COMMAND       9999                                // indicates that the reset was caused by the RESET command
#define WATCHDOG_TIMEOUT    9998                                // reset caused by the watchdog timer
#define PIN_RESTART         9997                                // reset caused by entering 0 at the PIN prompt
#define RESTART_NOAUTORUN   9996                                // reset required after changing the LCD or touch config
#define SCREWUP_TIMEOUT    	9994                                // reset caused by the execute timer
#define SOFT_RESET          9993
#define POSSIBLE_WATCHDOG   9992
#define INVALID_CLOCKSPEED  9991
#define RESET_CLOCKSPEED  9990

#define FLASH_SPI_SPEED 20000000
#define LCD_SPI_SPEED   25000000                                   // the speed of the SPI bus when talking to an SPI LCD display controller
#define TOUCH_SPI_SPEED 300000
#define SLOW_TOUCH_SPEED 120000
#define NOKIA_SPI_SPEED 4000000
#define ST7920_SPI_SPEED 600000
#define SDCARD_SPI_SPEED 12000000
#define NONE_SPI_DEVICE -1
#define P_INPUT				1						// for setting the TRIS on I/O bits
#define P_OUTPUT			0
#define P_ON				1
#define P_OFF				0
#define P_I2C_SCL            0
#define P_I2C_SDA            1
extern void Display_Refresh(void);
extern void waitwhilebusy(void);
struct Displays {
    unsigned char ref;
	char name [13];
    int speed;
    int horizontal;
    int vertical;
    int bits;
    unsigned char buffered;
    int CPOL;
    int CPHASE;
};
extern const struct Displays display_details[];
extern int LCD_CS_PIN;
extern int LCD_CD_PIN;
extern int LCD_Reset_PIN;
extern int LCD_E_INKbusy;
extern void (*xmit_byte_multi)(const BYTE *buff, int cnt);
extern void (*rcvr_byte_multi)(BYTE *buff, int cnt);
extern int (*SET_SPI_CLK)(int speed, int polarity, int edge);
extern void SPISpeedSet(int device);
extern BYTE (*xchg_byte)(BYTE data_out);
extern int SD_SPI_SPEED;
extern int __not_in_flash_func(HW0Clk)(int speed, int polarity, int edge);
extern int __not_in_flash_func(HW1Clk)(int speed, int polarity, int edge);
extern int __not_in_flash_func(BitBangSetClk)(int speed, int polarity, int edge);
extern BYTE __not_in_flash_func(HW0SwapSPI)(BYTE data_out);
extern BYTE __not_in_flash_func(HW1SwapSPI)(BYTE data_out);
extern BYTE BitBangSwapSPI(BYTE data_out);
extern void __not_in_flash_func(HW0SendSPI)(const BYTE *buff, int cnt);
extern void __not_in_flash_func(HW1SendSPI)(const BYTE *buff, int cnt);
extern void BitBangSendSPI(const BYTE *buff, int cnt);
extern void __not_in_flash_func(HW0ReadSPI)(BYTE *buff, int cnt);
extern void __not_in_flash_func(HW1ReadSPI)(BYTE *buff, int cnt);
extern void BitBangReadSPI(BYTE *buff, int cnt);
extern void ScrollLCDSPI(int lines);
extern void SetCS(void);
extern int GetLineILI9341(void);
extern void SPI111init(void);
#endif
/*  @endcond */
