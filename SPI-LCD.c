/***********************************************************************************************************************
PicoMite MMBasic

SPI-LCD.c

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

#include <stdarg.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/dma.h"
int CurrentSPIDevice=NONE_SPI_DEVICE;
const struct Displays display_details[]={
		{0,"", SDCARD_SPI_SPEED, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{1,"", SDCARD_SPI_SPEED, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{2,"SSD1306I2C", 400, 128, 64, 1, 1, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{3,"SSD1306I2C32", 400, 128, 32, 1, 1, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{4,"ILI9163", LCD_SPI_SPEED, 128, 128, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{5,"ILI9341", 50000000, 320, 240, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{6,"ST7735", LCD_SPI_SPEED, 160, 128, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{7,"ST7735S", LCD_SPI_SPEED, 160, 80, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{8,"SSD1331", LCD_SPI_SPEED, 96, 64, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{9,"ST7789", LCD_SPI_SPEED, 240, 240, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{10,"ILI9481", LCD_SPI_SPEED, 480, 320, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{11,"ILI9488", LCD_SPI_SPEED, 480, 320, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{12,"ST7789_135", LCD_SPI_SPEED, 240, 135, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{13,"ST7789_320", 50000000, 320, 240, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{14,"ILI9488W", 40000000, 480, 320, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{15,"ST7735S_W", LCD_SPI_SPEED, 128, 128, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{16,"GC9A01", LCD_SPI_SPEED, 240, 240, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{17,"ILI9481IPS", 12000000, 480, 320, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{18,"N5110", NOKIA_SPI_SPEED, 84, 48, 1, 1, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{19,"SSD1306SPI", LCD_SPI_SPEED, 128, 64, 1, 1, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{20,"ST7920", ST7920_SPI_SPEED, 128, 64, 1, 1, SPI_POLARITY_HIGH, SPI_PHASE_2EDGE},
		{21,"", TOUCH_SPI_SPEED, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{22,"SPIReadSpeed", 12000000, 480, 320, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{23,"ST7789RSpeed", 6000000, 320, 240, 16, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{24,"", SLOW_TOUCH_SPEED, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{25,"User", 0, 0, 0, 0, 0, 0 ,0},
		{26,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{27,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{28,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{29,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{30,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{31,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{32,"Dummy", 0, 0, 0, 0, 0, 0 ,0},
		{33,"SSD1963_4", 0, 0, 0, 0, 0, 0 ,0},
		{34,"SSD1963_5", 0, 0, 0, 0, 0, 0 ,0},
		{35,"SSD1963_5A", 0, 0, 0, 0, 0, 0 ,0},
		{36,"SSD1963_7", 0, 0, 0, 0, 0, 0 ,0},
		{37,"SSD1963_7A", 0, 0, 0, 0, 0, 0 ,0},
		{38,"SSD1963_8", 0, 0, 0, 0, 0, 0 ,0},
		{39,"ILI9341_8", 0, 0, 0, 0, 0, 0 ,0},
		{40,"SSD1963_4_16", 0, 0, 0, 0, 0, 0 ,0},
		{41,"SSD1963_5_16", 0, 0, 0, 0, 0, 0 ,0},
		{42,"SSD1963_5A_16" , 0, 0, 0, 0, 0, 0 ,0},
		{43,"SSD1963_7_16", 0, 0, 0, 0, 0, 0 ,0},
		{44,"SSD1963_7A_16", 0, 0, 0, 0, 0, 0 ,0},
		{45,"SSD1963_8_16", 0, 0, 0, 0, 0, 0 ,0},
		{46,"ILI9341_16", 0, 0, 0, 0, 0, 0 ,0},
		{47,"IPS_4_16", 0, 0, 0, 0, 0, 0 ,0},
		{48,"SSD1963_5E_16", 0, 0, 0, 0, 0, 0 ,0},
		{49,"SSD1963_7E_16", 0, 0, 0, 0, 0, 0 ,0},
		{50,"ILI9486_16", 0, 0, 0, 0, 0, 0 ,0},
		{51,"VIRTUAL_C", 0, 320, 240, 0, 0, 0, 0},
		{52,"VIRTUAL_M", 0, 640, 480, 0, 0, 0, 0},
		{53,"VS1053slow", 200000, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},
		{54,"VS1053fast", 4000000, 0, 0, 0, 0, SPI_POLARITY_LOW, SPI_PHASE_1EDGE},

};
void __not_in_flash_func(spi_write_fast)(spi_inst_t *spi, const uint8_t *src, size_t len) {
    // Write to TX FIFO whilst ignoring RX, then clean up afterward. When RX
    // is full, PL022 inhibits RX pushes, and sets a sticky flag on
    // push-on-full, but continues shifting. Safe if SSPIMSC_RORIM is not set.
    for (size_t i = 0; i < len; ++i) {
        while (!spi_is_writable(spi))
            tight_loop_contents();
        spi_get_hw(spi)->dr = (uint32_t)src[i];
    }
}
void __not_in_flash_func(spi_finish)(spi_inst_t *spi){
    // Drain RX FIFO, then wait for shifting to finish (which may be *after*
    // TX FIFO drains), then drain RX FIFO again
    while (spi_is_readable(spi))
        (void)spi_get_hw(spi)->dr;
    while (spi_get_hw(spi)->sr & SPI_SSPSR_BSY_BITS)
        tight_loop_contents();
    while (spi_is_readable(spi))
        (void)spi_get_hw(spi)->dr;

    // Don't leave overrun flag set
    spi_get_hw(spi)->icr = SPI_SSPICR_RORIC_BITS;
}
#ifndef PICOMITEVGA
int LCD_CS_PIN=0;
int LCD_CD_PIN=0;
int LCD_Reset_PIN=0;

//#ifndef PICOMITEVGA
unsigned char LCDBuffer[1440]={0};
//#endif

void DefineRegionSPI(int xstart, int ystart, int xend, int yend, int rw);
void DrawBitmapSPI(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern const int SPISpeeds[];
extern void spi_write_command(unsigned char command);
extern void I2C_Send_Data(unsigned char* data, int n);
void I2C_Send_Command(char command);
extern int mmI2Cvalue;												// value of MM.I2C
void waitwhilebusy(void);
#define SPIsend(a) {uint8_t b=a;xmit_byte_multi(&b,1);}
#define SPIqueue(a) {(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS) ? xmit_byte_multi(a,3) : xmit_byte_multi(a,2) ;}
#define SPIsend2(a) {SPIsend(0);SPIsend(a);}
int PackHorizontal=0;
int fullrefreshcount=0;
void DrawRectangleMEM(int x1, int y1, int x2, int y2, int c);
void DrawBitmapMEM(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
void DrawBufferMEM(int x1, int y1, int x2, int y2, unsigned char* p) ;
void ReadBufferMEM(int x1, int y1, int x2, int y2, unsigned char* buff);
void spi_write_CommandData(const uint8_t* pCommandData, uint8_t datalen);
void ST7920command(unsigned char data);
// utility function for routines that want to reserve a pin for special I/O
// this ignores any previous settings and forces the pin to its new state
// pin is the pin number
// inp is true if an input or false if an output
// init is the value used to initialise the pin if it is an output (hi or lo)
// type is the final tag for the pin in ExtCurrentConfig[]
void SetAndReserve(int pin, int inp, int init, int type) {
    if(pin == 0) return;                                            // do nothing if not set
}


void MIPS16 ConfigDisplaySPI(unsigned char *p) {
	char code,CD,RESET,CS=0;
	uint8_t BACKLIGHT=0;
	int DISPLAY_TYPE=0;
    getargs(&p, 13, (unsigned char *)",");
    if(checkstring(argv[0], (unsigned char *)"ILI9163")) {
        DISPLAY_TYPE = ILI9163;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1331")) {
        DISPLAY_TYPE = SSD1331;
    } else if(checkstring(argv[0], (unsigned char *)"ST7735S")) {
        DISPLAY_TYPE = ST7735S;
    } else if(checkstring(argv[0], (unsigned char *)"ST7735")) {
        DISPLAY_TYPE = ST7735;
    } else if(checkstring(argv[0], (unsigned char *)"ST7789")) {
        DISPLAY_TYPE = ST7789;
    } else if(checkstring(argv[0], (unsigned char *)"ST7789_135")) {
        DISPLAY_TYPE = ST7789A;
    } else if(checkstring(argv[0], (unsigned char *)"ST7789_320")) {
        DISPLAY_TYPE = ST7789B;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9481IPS")) {
        DISPLAY_TYPE = ILI9481IPS;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9481")) {
        DISPLAY_TYPE = ILI9481;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9488")) {
        DISPLAY_TYPE = ILI9488;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9488W")) {
        DISPLAY_TYPE = ILI9488W;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9341")) {
        DISPLAY_TYPE = ILI9341;
    } else if(checkstring(argv[0], (unsigned char *)"ST7735S_W")) {
        DISPLAY_TYPE = ST7735S_W;
    } else if(checkstring(argv[0], (unsigned char *)"GC9A01")) {
        DISPLAY_TYPE = GC9A01;
    } else if(checkstring(argv[0], (unsigned char *)"N5110")) {
        DISPLAY_TYPE = N5110;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1306SPI")) {
        DISPLAY_TYPE = SSD1306SPI;
    } else if(checkstring(argv[0], (unsigned char *)"ST7920")) {
        DISPLAY_TYPE = ST7920;
	} else return;
	if(!Option.SYSTEM_CLK)error("System SPI not configured");
    if(!(argc == 7 || argc == 9 || argc==11 || argc==13)) error("Argument count");
    if(checkstring(argv[2], (unsigned char *)"L") || checkstring(argv[2], (unsigned char *)"LANDSCAPE"))
        Option.DISPLAY_ORIENTATION = LANDSCAPE;
    else if(checkstring(argv[2], (unsigned char *)"P") || checkstring(argv[2], (unsigned char *)"PORTRAIT"))
        Option.DISPLAY_ORIENTATION = PORTRAIT;
    else if(checkstring(argv[2], (unsigned char *)"RL") || checkstring(argv[2], (unsigned char *)"RLANDSCAPE"))
        Option.DISPLAY_ORIENTATION = RLANDSCAPE;
    else if(checkstring(argv[2], (unsigned char *)"RP") || checkstring(argv[2], (unsigned char *)"RPORTRAIT"))
        Option.DISPLAY_ORIENTATION = RPORTRAIT;
    else error("Orientation");
    if(DISPLAY_TYPE==ST7789 || DISPLAY_TYPE == ST7789A|| DISPLAY_TYPE == ST7789A)Option.DISPLAY_ORIENTATION=(Option.DISPLAY_ORIENTATION+2) % 4;
	if(!(code=codecheck(argv[4])))argv[4]+=2;
	CD = getinteger(argv[4]);
	if(!code)CD=codemap(CD);
	if(!(code=codecheck(argv[6])))argv[6]+=2;
	RESET = getinteger(argv[6]);
	if(!code)RESET=codemap(RESET);
	if(DISPLAY_TYPE!=ST7920){
		if(!(code=codecheck(argv[8])))argv[8]+=2;
		CS = getinteger(argv[8]);
		if(!code)CS=codemap(CS);
		Option.LCDVOP=0xB1;
		Option.I2Coffset=0;
		if(argc>=11 && *argv[10]){
			if(DISPLAY_TYPE == N5110)Option.LCDVOP=getint(argv[10],0,255);
			else if(DISPLAY_TYPE == SSD1306SPI)Option.I2Coffset=getint(argv[10],0,10);
			else  {
				if(!(code=codecheck(argv[10])))argv[10]+=2;
				BACKLIGHT = getinteger(argv[10]);
				if(!code)BACKLIGHT=codemap(BACKLIGHT);
				CheckPin(BACKLIGHT, CP_IGNORE_INUSE);
        		if((PinDef[BACKLIGHT].slice & 0x7f) == Option.AUDIO_SLICE) error("Channel in use for Audio");
			}
		} 
		CheckPin(CS, CP_IGNORE_INUSE);
		Option.LCD_CS = CS;
		if(argc==13){
			if(checkstring(argv[12],(unsigned char *)"INVERT"))Option.BGR=1;
		} else Option.BGR=0;
	}
	CheckPin(CD, CP_IGNORE_INUSE);
    CheckPin(RESET, CP_IGNORE_INUSE);
	if(CS==CD || CS==RESET || (CS==BACKLIGHT && DISPLAY_TYPE!=ST7920) || CD==RESET || CD==BACKLIGHT || RESET==BACKLIGHT)error("Duplicated pin");
	Option.LCD_CD = CD;
	Option.LCD_Reset = RESET;
	Option.DISPLAY_BL = BACKLIGHT;
	Option.DISPLAY_TYPE=DISPLAY_TYPE;
    if(!(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel)) Option.Refresh = 1;
}



// initialise the display controller
// this is used in the initial boot sequence of the Micromite
void MIPS16 InitDisplaySPI(int InitOnly) {
    if(Option.DISPLAY_TYPE==0 || Option.DISPLAY_TYPE >= DISP_USER || Option.DISPLAY_TYPE <= I2C_PANEL) return;
    DisplayHRes = display_details[Option.DISPLAY_TYPE].horizontal;
    DisplayVRes = display_details[Option.DISPLAY_TYPE].vertical;

    if(!InitOnly) {
//        SPI2on();
        // open the SPI port and reserve the I/O pins

        // setup the pointers to the drawing primitives
        if(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel){
        	DrawRectangle = DrawRectangleSPI;
        	DrawBitmap = DrawBitmapSPI;
        	DrawBuffer = DrawBufferSPI;
        	DrawBLITBuffer = DrawBufferSPI;
        	if(Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ILI9488 || Option.DISPLAY_TYPE == ST7789B){
				ReadBuffer = ReadBufferSPI;
				ReadBLITBuffer = ReadBufferSPI;
				ScrollLCD = ScrollLCDSPI;
			}
        } else {
            DrawRectangle = DrawRectangleMEM;
            DrawBitmap = DrawBitmapMEM;
        	DrawBuffer = DrawBufferMEM;
			ReadBuffer = ReadBufferMEM;
        	DrawBLITBuffer = DrawBufferMEM;
			ReadBLITBuffer = ReadBufferMEM;
        }
		DrawPixel=DrawPixelNormal;
    }
    // the parameters for the display panel are set here
    // the initialisation sequences and the SPI driver code was written by Peter Mather (matherp on The Back Shed forum)
    switch(Option.DISPLAY_TYPE) {
		case ILI9488:
		case ILI9488W:
			DisplayHRes = 480;
			DisplayVRes = 320;
			ResetController();
			if(Option.DISPLAY_TYPE==ILI9488){
				spi_write_command(0xE0); // Positive Gamma Control
				spi_write_data(0x00);
				spi_write_data(0x03);
				spi_write_data(0x09);
				spi_write_data(0x08);
				spi_write_data(0x16);
				spi_write_data(0x0A);
				spi_write_data(0x3F);
				spi_write_data(0x78);
				spi_write_data(0x4C);
				spi_write_data(0x09);
				spi_write_data(0x0A);
				spi_write_data(0x08);
				spi_write_data(0x16);
				spi_write_data(0x1A);
				spi_write_data(0x0F);

				spi_write_command(0XE1); // Negative Gamma Control
				spi_write_data(0x00);
				spi_write_data(0x16);
				spi_write_data(0x19);
				spi_write_data(0x03);
				spi_write_data(0x0F);
				spi_write_data(0x05);
				spi_write_data(0x32);
				spi_write_data(0x45);
				spi_write_data(0x46);
				spi_write_data(0x04);
				spi_write_data(0x0E);
				spi_write_data(0x0D);
				spi_write_data(0x35);
				spi_write_data(0x37);
				spi_write_data(0x0F);

				spi_write_command(0XC0); // Power Control 1
				spi_write_data(0x17);
				spi_write_data(0x15);

				spi_write_command(0xC1); // Power Control 2
				spi_write_data(0x41);

				spi_write_command(0xC5); // VCOM Control
				spi_write_data(0x00);
				spi_write_data(0x12);
				spi_write_data(0x80);

				spi_write_command(TFT_MADCTL); // Memory Access Control
				spi_write_data(0x48); // MX, BGR

				spi_write_command(0x3A); // Pixel Interface Format
				spi_write_data(0x66); // 18 bit colour for SPI

				spi_write_command(0xB0); // Interface Mode Control
				spi_write_data(0x00);

				spi_write_command(0xB1); // Frame Rate Control
				spi_write_data(0xA0);
				if(Option.BGR)spi_write_command(0x21); 
				spi_write_command(0xB4); // Display Inversion Control
				spi_write_data(0x02);

				spi_write_command(0xB6); // Display Function Control
				spi_write_data(0x02);
				spi_write_data(0x02);
				spi_write_data(0x3B);

				spi_write_command(0xB7); // Entry Mode Set
				spi_write_data(0xC6);

				spi_write_command(0xF7); // Adjust Control 3
				spi_write_data(0xA9);
				spi_write_data(0x51);
				spi_write_data(0x2C);
				spi_write_data(0x82);

				spi_write_command(TFT_SLPOUT); //Exit Sleep
				uSec(120000);

				spi_write_command(TFT_DISPON); //Display on
				uSec(25000);
			} else {
				if(Option.BGR)spi_write_command(0x20); 
				else spi_write_command(0x21); 
				spi_write_command(0xC2);	//Normal mode, increase can change the display quality, while increasing power consumption
				spi_write_data(0x33);
				spi_write_command(0XC5);
				spi_write_data(0x00);
				spi_write_data(0x1e);//VCM_REG[7:0]. <=0X80.
				spi_write_data(0x80);
				spi_write_command(0xB1);//Sets the frame frequency of full color normal mode
				spi_write_data(0xB0);//0XB0 =70HZ, <=0XB0.0xA0=62HZ
				spi_write_command(0x36);
				spi_write_data(0x28); //2 DOT FRAME MODE,F<=70HZ.
				spi_write_command(0XE0);
				spi_write_data(0x0);
				spi_write_data(0x13);
				spi_write_data(0x18);
				spi_write_data(0x04);
				spi_write_data(0x0F);
				spi_write_data(0x06);
				spi_write_data(0x3a);
				spi_write_data(0x56);
				spi_write_data(0x4d);
				spi_write_data(0x03);
				spi_write_data(0x0a);
				spi_write_data(0x06);
				spi_write_data(0x30);
				spi_write_data(0x3e);
				spi_write_data(0x0f);		
				spi_write_command(0XE1);
				spi_write_data(0x0);
				spi_write_data(0x13);
				spi_write_data(0x18);
				spi_write_data(0x01);
				spi_write_data(0x11);
				spi_write_data(0x06);
				spi_write_data(0x38);
				spi_write_data(0x34);
				spi_write_data(0x4d);
				spi_write_data(0x06);
				spi_write_data(0x0d);
				spi_write_data(0x0b);
				spi_write_data(0x31);
				spi_write_data(0x37);
				spi_write_data(0x0f);
				spi_write_command(0X3A);	//Set Interface Pixel Format
				spi_write_data(0x55);
				spi_write_command(0x11);//sleep out
				uSec(120000);
				spi_write_command(0x29);//Turn on the LCD display
			}
			spi_write_command(TFT_MADCTL);
			switch(Option.DISPLAY_ORIENTATION) {
				case LANDSCAPE:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape); break;
				case PORTRAIT:      spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait); break;
				case RLANDSCAPE:    spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape180); break;
				case RPORTRAIT:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait180); break;
			}
 			break;
		case ILI9481IPS:
			DisplayHRes = 480;
			DisplayVRes = 320;
			ResetController();
			//3.5IPS ILI9481+CMI	
			spi_write_command(0x01); //Soft_rese
			uSec(220000);

			spi_write_command(0x11);
			uSec(280000);

			spi_write_command(0xd0); //Power_Setting
			spi_write_data(0x07);//07  VC[2:0] Sets the ratio factor of Vci to generate the reference voltages Vci1
			spi_write_data(0x44);//41  BT[2:0] Sets the Step up factor and output voltage level from the reference voltages Vci1
			spi_write_data(0x1E);//1f  17   1C  VRH[3:0]: Sets the factor to generate VREG1OUT from VCILVL
			uSec(220000);

			spi_write_command(0xd1); //VCOM Control
			spi_write_data(0x00);//00
			spi_write_data(0x0C);//1A   VCM [6:0] is used to set factor to generate VCOMH voltage from the reference voltage VREG1OUT  15    09
			spi_write_data(0x1A);//1F   VDV[4:0] is used to set the VCOM alternating amplitude in the range of VREG1OUT x 0.70 to VREG1OUT   1F   18

			spi_write_command(0xC5);  //Frame Rate
			spi_write_data(0x03); // 03   02

			spi_write_command(0xd2);  //Power_Setting for Normal Mode 
			spi_write_data(0x01);  //01
			spi_write_data(0x11);  //11

			spi_write_command(0xE4);  //?
			spi_write_data(0xa0);
			spi_write_command(0xf3);
			spi_write_data(0x00);
			spi_write_data(0x2a);

			//1  OK
			spi_write_command(0xc8);
			spi_write_data(0x00);
			spi_write_data(0x26);
			spi_write_data(0x21);
			spi_write_data(0x00);
			spi_write_data(0x00);
			spi_write_data(0x1f);
			spi_write_data(0x65);
			spi_write_data(0x23);
			spi_write_data(0x77);
			spi_write_data(0x00);
			spi_write_data(0x0f);
			spi_write_data(0x00);
			//GAMMA SETTING

			spi_write_command(0xC0);	//Panel Driving Setting																          
			spi_write_data(0x00); //1//00  REV  SM  GS
			spi_write_data(0x3B); //2//NL[5:0]: Sets the number of lines to drive the LCD at an interval of 8 lines. 
			spi_write_data(0x00); //3//SCN[6:0]
			spi_write_data(0x02); //4//PTV: Sets the Vcom output in non-display area drive period
			spi_write_data(0x11); //5//NDL: Sets the source output level in non-display area.  PTG: Sets the scan mode in non-display area.

			spi_write_command(0xc6); //Interface Control 
			spi_write_data(0x83);
			//GAMMA SETTING 

			spi_write_command(0xf0); //?
			spi_write_data(0x01);

			spi_write_command(0xE4);//?
			spi_write_data(0xa0);

			spi_write_command(0x3a);
			spi_write_data(0x66);

			uSec(280000);

			switch(Option.DISPLAY_ORIENTATION) {
            	case LANDSCAPE:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9481_Landscape); break;
            	case PORTRAIT:      spi_write_cd(ILI9341_MEMCONTROL,1,ILI9481_Portrait); break;
            	case RLANDSCAPE:    spi_write_cd(ILI9341_MEMCONTROL,1,ILI9481_Landscape180); break;
            	case RPORTRAIT:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9481_Portrait180); break;
			}
			spi_write_command(0x2a);
			spi_write_data(0x00);
			spi_write_data(0x00);
			spi_write_data(0x01);
			spi_write_data(0x3F); //3F

			spi_write_command(0x2b);
			spi_write_data(0x00);
			spi_write_data(0x00);
			spi_write_data(0x01);
			spi_write_data(0xDf); //DF

			if(Option.BGR) spi_write_command(0x21);
			spi_write_command(0x29);	
			break;
		case ILI9481:
			DisplayHRes = 480;
			DisplayVRes = 320;
			ResetController();
			spi_write_command(0x11);
			uSec(20000);
			spi_write_cd(0xD0,3,0x07,0x42,0x18);
			spi_write_cd(0xD1,3,0x00,0x07,0x10);
			spi_write_cd(0xD2,2,0x01,0x02);
			spi_write_cd(0xC0,5,0x10,0x3B,0x00,0x02,0x11);
//            spi_write_cd(0xC1, 3,0x10, 0x12, 0xC8);
//            spi_write_cd(0xC5,1,0x01);
			spi_write_cd(0xB3,4,0x00,0x00,0x00,0x10);
			spi_write_cd(0xC8,12,0x00,0x32,0x36,0x45,0x06,0x16,0x37,0x75,0x77,0x54,0x0C,0x00);
			spi_write_cd(0xE0,15,0x0f,0x24,0x1c,0x0a,0x0f,0x08,0x43,0x88,0x03,0x0f,0x10,0x06,0x0f,0x07,0x00);
			spi_write_cd(0xE1,15,0x0F,0x38,0x30,0x09,0x0f,0x0f,0x4e,0x77,0x3c,0x07,0x10,0x05,0x23,0x1b,0x00);
			spi_write_cd(0x36,0x0A);
			spi_write_cd(0x3A,1,0x55);
			spi_write_cd(0x2A,4,0x00,0x00,0x01,0x3F);
			spi_write_cd(0x2B,4,0x00,0x00,0x01,0xE0);
			if(Option.BGR) spi_write_command(0x21);
			uSec(120000);
			spi_write_command(0x29);
			switch(Option.DISPLAY_ORIENTATION) {
            	case LANDSCAPE:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape); break;
            	case PORTRAIT:      spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait); break;
            	case RLANDSCAPE:    spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape180); break;
            	case RPORTRAIT:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait180); break;
			}
			break;
        case SSD1331:
        	ResetController();
        	spi_write_command(SSD1331_CMD_DISPLAYOFF);  	// 0xAE
        	spi_write_command(SSD1331_CMD_SETREMAP); 	// 0xA0
        	if(Option.DISPLAY_ORIENTATION==1) spi_write_command(0x72);
        	else if(Option.DISPLAY_ORIENTATION==2) spi_write_command(0x63);
        	else if(Option.DISPLAY_ORIENTATION==3) spi_write_command(0x60);
        	else spi_write_command(0x71);
        	spi_write_command(SSD1331_CMD_STARTLINE); 	// 0xA1
        	spi_write_command(0x0);
        	spi_write_command(SSD1331_CMD_DISPLAYOFFSET); 	// 0xA2
        	spi_write_command(0x0);
        	spi_write_command(SSD1331_CMD_NORMALDISPLAY);  	// 0xA4
        	spi_write_command(SSD1331_CMD_SETMULTIPLEX); 	// 0xA8
        	spi_write_command(0x3F);  			// 0x3F 1/64 duty
        	spi_write_command(SSD1331_CMD_SETMASTER);  	// 0xAD
        	spi_write_command(0x8E);
        	spi_write_command(SSD1331_CMD_POWERMODE);  	// 0xB0
        	spi_write_command(0x0B);
        	spi_write_command(SSD1331_CMD_PRECHARGE);  	// 0xB1
        	spi_write_command(0x31);
        	spi_write_command(SSD1331_CMD_CLOCKDIV);  	// 0xB3
        	spi_write_command(0xF0);  // 7:4 = Oscillator Frequency, 3:0 = CLK Div Ratio (A[3:0]+1 = 1..16)
        	spi_write_command(SSD1331_CMD_PRECHARGEA);  	// 0x8A
        	spi_write_command(0x64);
        	spi_write_command(SSD1331_CMD_PRECHARGEB);  	// 0x8B
        	spi_write_command(0x78);
        	spi_write_command(SSD1331_CMD_PRECHARGEA);  	// 0x8C
        	spi_write_command(0x64);
        	spi_write_command(SSD1331_CMD_PRECHARGELEVEL);  	// 0xBB
        	spi_write_command(0x3A);
        	spi_write_command(SSD1331_CMD_VCOMH);  		// 0xBE
        	spi_write_command(0x3E);
        	spi_write_command(SSD1331_CMD_MASTERCURRENT);  	// 0x87
        	spi_write_command(0x06);
        	spi_write_command(SSD1331_CMD_CONTRASTA);  	// 0x81
        	spi_write_command(0x91);
        	spi_write_command(SSD1331_CMD_CONTRASTB);  	// 0x82
        	spi_write_command(0x50);
        	spi_write_command(SSD1331_CMD_CONTRASTC);  	// 0x83
        	spi_write_command(0x7D);
        	spi_write_command(SSD1331_CMD_DISPLAYON);	//--turn on oled panel
    		break;
    case ILI9341:
            ResetController();
            spi_write_command(ILI9341_SOFTRESET);                           //software reset
            uSec(20000);
			spi_write_command(ILI9341_DISPLAYOFF);
			spi_write_cd(ILI9341_POWERCONTROL1,1,0x23);
			spi_write_cd(ILI9341_POWERCONTROL2,1,0x10);
			spi_write_cd(ILI9341_VCOMCONTROL1,2,0x2B,0x2B);
			spi_write_cd(ILI9341_VCOMCONTROL2,1,0xC0);
			spi_write_cd(ILI9341_PIXELFORMAT,1,0x55);
			spi_write_cd(ILI9341_FRAMECONTROL,2,0x00,0x1B);
			spi_write_cd(ILI9341_ENTRYMODE,1,0x07);
			spi_write_cd(ILI9341_SLEEPOUT,1,0);
			uSec(50000);
			spi_write_command(ILI9341_NORMALDISP);
			if(Option.BGR) spi_write_command(ILI9341_INVERTON);
  			spi_write_command(ILI9341_DISPLAYON);
			uSec(100000);
         switch(Option.DISPLAY_ORIENTATION) {
             case LANDSCAPE:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape); break;
             case PORTRAIT:      spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait); break;
             case RLANDSCAPE:    spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Landscape180); break;
             case RPORTRAIT:     spi_write_cd(ILI9341_MEMCONTROL,1,ILI9341_Portrait180); break;
         }
         break;

    case GC9A01:
            ResetController();
			spi_write_command(0xEF);
			spi_write_cd(0xEB,1,0x14);
			spi_write_command(0xFE);
			spi_write_command(0xEF);
			spi_write_cd(0xEB,1,0x14);
			spi_write_cd(0x84,1,0x40);
			spi_write_cd(0x85,1,0xFF);
			spi_write_cd(0x86,1,0xFF);
			spi_write_cd(0x87,1,0xFF);
			spi_write_cd(0x88,1,0x0A);
			spi_write_cd(0x89,1,0x21);
			spi_write_cd(0x8A,1,0x00);
			spi_write_cd(0x8B,1,0x80);
			spi_write_cd(0x8C,1,0x01);
			spi_write_cd(0x8D,1,0x01);
			spi_write_cd(0x8E,1,0xFF);
			spi_write_cd(0x8F,1,0xFF);
			spi_write_cd(0xB6,2,0x00,0x20);
			spi_write_cd(0x3A,1,0x05);
			spi_write_cd(0x90,4,0x08, 0x08, 0x08, 0x08);
			spi_write_cd(0xBD,1,0x06);
			spi_write_cd(0xBC,1,0x00);
			spi_write_cd(0xFF,3,0x60, 0x01, 0x04);
			spi_write_cd(0xC3,1,0x13);
			spi_write_cd(0xC4,1,0x13);
			spi_write_cd(0xC9,1,0x22);
			spi_write_cd(0xBE,1,0x11);
			spi_write_cd(0xE1,2,0x10,0x0E);
			spi_write_cd(0xDF,3,0x21, 0x0c, 0x02);
			spi_write_cd(0xF0,6,0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);
			spi_write_cd(0xF1,6,0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);
			spi_write_cd(0xF2,6,0x45, 0x09, 0x08, 0x08, 0x26, 0x2A);
			spi_write_cd(0xF3,6,0x43, 0x70, 0x72, 0x36, 0x37, 0x6F);
			spi_write_cd(0xED,2,0x1B, 0x0B);
			spi_write_cd(0xAE,1,0x77);
			spi_write_cd(0xCD,1,0x63);
			spi_write_cd(0x70,9, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03);
			spi_write_cd(0xE8,1,0x34);
			spi_write_cd(0x62,12, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70);
			spi_write_cd(0x63,12, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70);
			spi_write_cd(0x64,7, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07);
			spi_write_cd(0x66,10, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00);
			spi_write_cd(0x67,10, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98);
			spi_write_cd(0x74,7, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00);
			spi_write_cd(0x98,2,0x3e, 0x07);
			spi_write_command(0x35);
			spi_write_command(GC9A01_SLPOUT);
			uSec(10000);
			spi_write_command(GC9A01_DISPON);
         switch(Option.DISPLAY_ORIENTATION) {
             case LANDSCAPE:     spi_write_cd(GC9A01_MADCTL,1,0x08); break;
             case PORTRAIT:      spi_write_cd(GC9A01_MADCTL,1,0x68); break;
             case RLANDSCAPE:    spi_write_cd(GC9A01_MADCTL,1,0xc8); break;
             case RPORTRAIT:     spi_write_cd(GC9A01_MADCTL,1,0xa8); break;
         }
         break;
       case ILI9163:
            ResetController();
            spi_write_command(ILI9341_SOFTRESET);                           //software reset
            uSec(20000);
			spi_write_command(ILI9163_SLPOUT);                      //exit sleep
  			uSec(5000);
  			spi_write_cd(ILI9163_PIXFMT,1,0x05);
  			uSec(5000);
  			spi_write_cd(ILI9163_GAMMASET,1,0x04);                  //0x04
  			uSec(1000);
  			spi_write_cd(ILI9163_GAMRSEL,1,0x01);
  			uSec(1000);
			if(Option.BGR) spi_write_command(ILI9163_DINVON);
  			spi_write_command(ILI9163_NORML);
  			spi_write_cd(ILI9163_DFUNCTR,2,0b11111111,0b00000110);  //
  			spi_write_cd(ILI9163_PGAMMAC,15,0x36,0x29,0x12,0x22,0x1C,0x15,0x42,0xB7,0x2F,0x13,0x12,0x0A,0x11,0x0B,0x06);//Positive Gamma Correction Setting
  			spi_write_cd(ILI9163_NGAMMAC,15,0x09,0x16,0x2D,0x0D,0x13,0x15,0x40,0x48,0x53,0x0C,0x1D,0x25,0x2E,0x34,0x39);//Negative Gamma Correction Setting
  			spi_write_cd(ILI9163_FRMCTR1,2,0x08,0x02);              //0x0C//0x08
 			uSec(1000);
  			spi_write_cd(ILI9163_DINVCTR,1,0x07);
  			uSec(1000);
  			spi_write_cd(ILI9163_PWCTR1,2,0x0A,0x02);               //4.30 - 0x0A
  			uSec(1000);
  			spi_write_cd(ILI9163_PWCTR2,1,0x02);
  			uSec(1000);
  			spi_write_cd(ILI9163_VCOMCTR1,2,0x50,99);               //0x50
  			uSec(1000);
  			spi_write_cd(ILI9163_VCOMOFFS,1,0);                     //0x40
  			uSec(1000);
  			spi_write_cd(ILI9163_VSCLLDEF,5,0,0,DisplayVRes,0,0);
 			spi_write_command(ILI9163_DISPON);                      //display ON
  			uSec(1000);
            switch(Option.DISPLAY_ORIENTATION) {
                case LANDSCAPE:     spi_write_cd(ILI9163_MADCTL,1,ILI9163_Landscape); break;
                case PORTRAIT:      spi_write_cd(ILI9163_MADCTL,1,ILI9163_Portrait); break;
                case RLANDSCAPE:    spi_write_cd(ILI9163_MADCTL,1,ILI9163_Landscape180); break;
                case RPORTRAIT:     spi_write_cd(ILI9163_MADCTL,1,ILI9163_Portrait180); break;
            }
  			uSec(1000);
            break;
        case ST7735:
        case ST7735S:
		case ST7735S_W:
            ResetController();
            spi_write_command(ILI9341_SOFTRESET);                           //software reset
            uSec(20000);
			spi_write_command(ST7735_SLPOUT);                       //out of sleep mode
			uSec(500000);
			spi_write_cd(ST7735_FRMCTR1,3,0x01,0x2C,0x2d);          //frame rate control - normal mode
			spi_write_cd(ST7735_FRMCTR2,3,0x01,0x2C,0x2D);          //frame rate control - idle mode
			spi_write_cd(ST7735_FRMCTR3,6,0x01,0x2c,0x2D,0x01,0x2C,0x2D);//frame rate control - partial mode
			spi_write_cd(ST7735_INVCTR,1,0x07);                     //display inversion control
			spi_write_cd(ST7735_PWCTR1,3,0xA2,0x02,0x84);           //power control
			spi_write_cd(ST7735_PWCTR2,1,0xC5);                     //power control
			spi_write_cd(ST7735_PWCTR3,2,0x0A,0x00);                //power control
			spi_write_cd(ST7735_PWCTR4,2,0x8A,0x2A);                //power control
			spi_write_cd(ST7735_PWCTR5,2,0x8A,0xEE);                //power control
			spi_write_cd(ST7735_VMCTR1,1,0x0E);                     //power control
			if(Option.DISPLAY_TYPE==ST7735 || Option.DISPLAY_TYPE==ST7735S_W)Option.BGR ? spi_write_command(ST7735_INVON): spi_write_command(ST7735_INVOFF);                       //don't invert display
			else Option.BGR ? spi_write_command(ST7735_INVOFF): spi_write_command(ST7735_INVON);  
			spi_write_cd(ST7735_COLMOD,1,0x05);                     //set color mode
			spi_write_cd(ST7735_CASET,4,0,0,0,0x7F);                //column addr set
			spi_write_cd(ST7735_RASET,4,0,0,0,0x9F);                //row addr set
			spi_write_cd(ST7735_GMCTRP1,16,0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2D,0x25,0x29,0x2B,0x39,0x00,0x01,0x03,0x10);
			spi_write_cd(ST7735_GMCTRN1,16,0x03,0x1d,0x07,0x06,0x2E,0x2c,0x29,0x2d,0x2E,0x2E,0x37,0x3f,0x00,0x00,0x02,0x10);
			spi_write_command(ST7735_NORON);                        //normal display on
			uSec(10000);
			spi_write_command(ST7735_DISPON);
            switch(Option.DISPLAY_ORIENTATION) {
                case LANDSCAPE:     spi_write_cd(ST7735_MADCTL, 1, ST7735_Landscape | (Option.DISPLAY_TYPE==ST7735 ? 0 : 8)); break;
                case PORTRAIT:      spi_write_cd(ST7735_MADCTL, 1, ST7735_Portrait | (Option.DISPLAY_TYPE==ST7735 ? 0 : 8)); break;
                case RLANDSCAPE:    spi_write_cd(ST7735_MADCTL, 1, ST7735_Landscape180 | (Option.DISPLAY_TYPE==ST7735 ? 0 : 8)); break;
                case RPORTRAIT:     spi_write_cd(ST7735_MADCTL, 1, ST7735_Portrait180 | (Option.DISPLAY_TYPE==ST7735 ? 0 : 8)); break;
            }
            break;
        case ST7789:
		case ST7789A:
		case ST7789B:
            ResetController();
            spi_write_command(ST77XX_SWRESET);    uSec(150000);
            spi_write_command(ST77XX_SLPOUT);    uSec(500000);
            spi_write_command(ST77XX_COLMOD);    spi_write_data(0x55);uSec(10000);
//            if(Option.DISPLAY_TYPE==ST7789){spi_write_command(ST77XX_CASET); spi_write_data(0x0); spi_write_data(0x0); spi_write_data(0x0); spi_write_data(239);}
//			else if(Option.DISPLAY_ORIENTATION & 1){spi_write_command(ST77XX_CASET); spi_write_data(0x0); spi_write_data(40); spi_write_data(0x1); spi_write_data(23);}
//				 else {spi_write_command(ST77XX_CASET); spi_write_data(0x0); spi_write_data(52); spi_write_data(0x0); spi_write_data(186);}
//            if(Option.DISPLAY_TYPE==ST7789){spi_write_command(ST77XX_RASET); spi_write_data(0x0); spi_write_data(0); spi_write_data(0); spi_write_data(239);}
//			else if(Option.DISPLAY_ORIENTATION & 1){spi_write_command(ST77XX_RASET); spi_write_data(0x0); spi_write_data(53); spi_write_data(0); spi_write_data(187);}
//				 else {spi_write_command(ST77XX_RASET); spi_write_data(0x0); spi_write_data(40); spi_write_data(1); spi_write_data(23);}
            if(Option.BGR)spi_write_command(ST77XX_INVOFF);
			else spi_write_command(ST77XX_INVON);
			uSec(10000);
            spi_write_command(ST77XX_NORON);    uSec(10000);
            spi_write_command(ST77XX_DISPON);    uSec(500000);
            switch(Option.DISPLAY_ORIENTATION) {
                case LANDSCAPE:     spi_write_cd(ST7735_MADCTL, 1, ST7735_Landscape); break;
                case PORTRAIT:      spi_write_cd(ST7735_MADCTL, 1, ST7735_Portrait); break;
                case RLANDSCAPE:    spi_write_cd(ST7735_MADCTL, 1, ST7735_Landscape180); break;
                case RPORTRAIT:     spi_write_cd(ST7735_MADCTL, 1, ST7735_Portrait180); break;
            }
            break;
        case N5110:
            ResetController();
            spi_write_command(0x21);   // LCD Extended Commands.
            uSec(20000);
            spi_write_command(Option.LCDVOP);  // Set LCD Vop (Contrast). //0xB0 for 5V, 0XB1 for 3.3v, 0XBF if screen too dark
            uSec(20000);
            spi_write_command(0x04); // Set Temp coefficient. //0x04
            uSec(20000);
            spi_write_command(0x14); // LCD bias mode 1:48. //0x13 or 0X14
            uSec(20000);
            spi_write_command(0x20); //We must send 0x20 before modifying the display control mode
            uSec(20000);
            spi_write_command(0x0C); // Set display control, normal mode. 0x0D for inverse, 0x0C for normal
            uSec(20000);
            break;
        case SSD1306SPI:
            ResetController();
            spi_write_command(0xAE);//DISPLAYOFF
            spi_write_command(0xD5);//DISPLAYCLOCKDIV
            spi_write_command(0x80);//the suggested ratio &H80
            spi_write_command(0xA8);//MULTIPLEX
            spi_write_command(0x3F);//
            spi_write_command(0xD3);//DISPLAYOFFSET
            spi_write_command(0x0);//no offset
            spi_write_command(0x40);//STARTLINE
            spi_write_command(0x8D);//CHARGEPUMP
            spi_write_command(0x14);
            spi_write_command(0x20);//MEMORYMODE
            spi_write_command(0x00);//&H0 act like ks0108
            spi_write_command(0xA1);//SEGREMAP OR 1
            spi_write_command(0xC8);//COMSCANDEC
            spi_write_command(0xDA);//COMPINS
            spi_write_command(0x12);
            spi_write_command(0x81);//SETCONTRAST
            spi_write_command(0xCF);
            spi_write_command(0xd9);//SETPRECHARGE
            spi_write_command(0xF1);
            spi_write_command(0xDB);//VCOMDETECT
            spi_write_command(0x40);
            spi_write_command(0xA4);//DISPLAYALLON_RESUME
            spi_write_command(0xA6);//NORMALDISPLAY
            spi_write_command(0xAF);//DISPLAYON
        	break;
        case ST7920:
            PackHorizontal=1;
            gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
            uSec(40000);
            SetCS();
            ResetController();
            ST7920command(1);
            uSec(20000);
            ST7920command(0b00001100);//display on
            uSec(20000);
            ST7920command(1);//DISPLAY CLEAR
            uSec(20000);
            ST7920command(0b00100110);//graphic mode
            uSec(20000);
			ClearCS(Option.LCD_CD);
        	break;
    }
    if(Option.DISPLAY_ORIENTATION & 1) {
        VRes=DisplayVRes;
        HRes=DisplayHRes;
    } else {
        VRes=DisplayHRes;
        HRes=DisplayVRes;
    }
    if(!InitOnly) {
    	ResetDisplay();
    	ClearScreen(Option.DISPLAY_CONSOLE ? Option.DefaultBC : 0);
    	    if(Option.Refresh)Display_Refresh();
    }
}


// set Chip Select for the LCD low
// this also checks the configuration of the SPI channel and if required reconfigures it to suit the LCD controller
void SetCS(void) {
	SPISpeedSet(Option.DISPLAY_TYPE);
    if(Option.DISPLAY_TYPE != ST7920)gpio_put(LCD_CS_PIN,GPIO_PIN_RESET);  // set CS low
    else gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
}



void spi_write_data(unsigned char data){
    gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
    SetCS();
    if(Option.DISPLAY_TYPE == ILI9481 || Option.DISPLAY_TYPE == ILI9488W)	{SPIsend2(data);}
    else {SPIsend(data);}
     ClearCS(Option.LCD_CS);
}


void spi_write_command(unsigned char data){
    gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
    SetCS();
    if(Option.DISPLAY_TYPE == ILI9481 || Option.DISPLAY_TYPE == ILI9488W)	{SPIsend2(data);}
    else {SPIsend(data);}
    ClearCS(Option.LCD_CS);
}
void ST7920command(unsigned char data){
	unsigned char a[3];
	a[0]=ST7920setcommand;
	a[1]=data & 0xF0;
	a[2]=((data & 0x0F)<<4) & 0xF0;
    SetCS();
	xmit_byte_multi(a,3);
	ClearCS(Option.LCD_CD);
}

void spi_write_cd(unsigned char command, int data, ...){
   int i;
   va_list ap;
   va_start(ap, data);
   spi_write_command(command);
   for(i = 0; i < data; i++) spi_write_data((char)va_arg(ap, int));
   va_end(ap);
}

void spi_write_CommandData(const uint8_t* pCommandData, uint8_t datalen){
	int i;
	spi_write_command(*pCommandData++);
    gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
    for(i=1;i<datalen;i++){
    	spi_write_data(*pCommandData++);
    }
}

void MIPS16 ResetController(void){
    PinSetBit(Option.LCD_Reset, LATSET);
    uSec(10000);
    PinSetBit(Option.LCD_Reset, LATCLR);
    uSec(10000);
    PinSetBit(Option.LCD_Reset, LATSET);
    uSec(200000);
}


void DefineRegionSPI(int xstart, int ystart, int xend, int yend, int rw) {
	unsigned char coord[4];
    if(Option.DISPLAY_TYPE == ILI9481 || Option.DISPLAY_TYPE == ILI9488W){
		SetCS();
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
    	SPIsend2(ILI9341_COLADDRSET);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
    	SPIsend2(xstart >> 8);
    	SPIsend2(xstart);
    	SPIsend2(xend >> 8);
    	SPIsend2(xend);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
    	SPIsend2(ILI9341_PAGEADDRSET);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
    	SPIsend2(ystart >> 8);
    	SPIsend2(ystart);
    	SPIsend2(yend >> 8);
    	SPIsend2(yend);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
    	if(rw) {
    		SPIsend2(ILI9341_MEMORYWRITE);
    	} else {
    		SPIsend2(ILI9341_RAMRD);
    	}
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
    } else 	if(Option.DISPLAY_TYPE==SSD1331){
		if(Option.DISPLAY_ORIENTATION&1){
		  spi_write_command(0x15); // Column addr set
		  spi_write_command(xstart);
		  spi_write_command(xend);

		  spi_write_command(0x75); // Row addr set
		  spi_write_command(ystart);
		  spi_write_command(yend);
		} else {
		  spi_write_command(0x75); // Row addr set
		  spi_write_command(xstart);
		  spi_write_command(xend);

		  spi_write_command(0x15); // Column addr set
		  spi_write_command(ystart);
		  spi_write_command(yend);
		}
		SetCS();
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
	} else {
		if(Option.DISPLAY_TYPE == 0) error("Display not configured");
		if(Option.DISPLAY_TYPE==ST7789){
			if(Option.DISPLAY_ORIENTATION==2){
				ystart+=80;
				yend+=80;
			}
			if(Option.DISPLAY_ORIENTATION==1){
				xstart+=80;
				xend+=80;
			}
		}
		if(Option.DISPLAY_TYPE==ST7789A){
			if(Option.DISPLAY_ORIENTATION ==1 ){
				xstart+=40;
				xend+=40;
				ystart+=52;
				yend+=52;
			}
			else if(Option.DISPLAY_ORIENTATION ==3 ){
				xstart+=40;
				xend+=40;
				ystart+=53;
				yend+=53;
			}
			else if(Option.DISPLAY_ORIENTATION==0){
				ystart+=40;
				yend+=40;
				xstart+=52;
				xend+=52;
			}
			else if(Option.DISPLAY_ORIENTATION==2){
				ystart+=40;
				yend+=40;
				xstart+=53;
				xend+=53;
			}
		}
		if(Option.DISPLAY_TYPE==ST7735S){
			if(Option.DISPLAY_ORIENTATION & 1){
				ystart+=26;
				yend+=26;
				xstart++;
				xend++;
			} else {
				xstart+=26;
				xend+=26;
				ystart++;
				yend++;
			}
		}
		if(Option.DISPLAY_TYPE==ST7735S_W){
			switch(Option.DISPLAY_ORIENTATION) {
				case LANDSCAPE:
				ystart+=2;
				yend+=2;
				xstart+=3;
				xend+=3;
				break;
				case PORTRAIT: 
				xstart+=2;
				xend+=2;
				ystart+=3;
				yend+=3;
				break;
				case RLANDSCAPE:
				ystart+=2;
				yend+=2;
				xstart+=1;
				xend+=1;
				break;
				case RPORTRAIT:
				xstart+=2;
				xend+=2;
				ystart+=1;
				yend+=1;
				break;
			}
		}
		SetCS();
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);//gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
		SPIsend(ILI9341_COLADDRSET);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
		coord[0]=xstart >> 8;
		coord[1]=xstart;
		coord[2]=xend >> 8;
		coord[3]=xend;
		xmit_byte_multi(coord,4);//		HAL_SPI_Transmit(&hspi3,coord,4,500);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
		SPIsend(ILI9341_PAGEADDRSET);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
		coord[0]=ystart >> 8;
		coord[1]=ystart;
		coord[2]=yend >> 8;
		coord[3]=yend;
		xmit_byte_multi(coord,4);//		HAL_SPI_Transmit(&hspi3,coord,4,500);
    	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
		if(rw) {
			SPIsend(ILI9341_MEMORYWRITE);
		} else {
			SPIsend(ILI9341_RAMRD);
		}
    	gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
	}
}

/****************************************************************************************************
 ****************************************************************************************************

 Basic drawing primitives
 all drawing on the LCD is done using either one of these two functions

 ****************************************************************************************************
****************************************************************************************************/
void spisendfast(unsigned char *n, int i){
		xmit_byte_multi(n,i);//		HAL_SPI_Transmit(&hspi3,coord,4,500);
}
// Draw a filled rectangle
// this is the basic drawing promitive used by most drawing routines
//    x1, y1, x2, y2 - the coordinates
//    c - the colour
void DrawRectangleSPI(int x1, int y1, int x2, int y2, int c){
    // convert the colours to 565 format
	unsigned char col[3];
	if(x1==x2 && y1==y2){
		if(x1 < 0) return;
	    if(x1 >= HRes) return;
	    if(y1 < 0) return;
	    if(y1 >= VRes) return;
		DefineRegionSPI(x1, y1, x2, y2, 1);
		if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS ){
			col[0]=(c>>16);
			col[1]=(c>>8) & 0xFF;
			col[2]=(c & 0xFF);
		} else {
			col[0]= ((c >> 16) & 0b11111000) | ((c >> 13) & 0b00000111);
			col[1] = ((c >>  5) & 0b11100000) | ((c >>  3) & 0b00011111);
		}
		if(Option.DISPLAY_TYPE == GC9A01){
			col[0]=~col[0];
			col[1]=~col[1];
		}
		SPIqueue(col);
	} else {
		int i,t,y;
		unsigned char *p;
		// make sure the coordinates are kept within the display area
		if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
		if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
		if(x1 < 0) x1 = 0;
		if(x1 >= HRes) x1 = HRes - 1;
		if(x2 < 0) x2 = 0;
		if(x2 >= HRes) x2 = HRes - 1;
		if(y1 < 0) y1 = 0;
		if(y1 >= VRes) y1 = VRes - 1;
		if(y2 < 0) y2 = 0;
		if(y2 >= VRes) y2 = VRes - 1;
		DefineRegionSPI(x1, y1, x2, y2, 1);
		if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS ){
			i = x2 - x1 + 1;
			i*=3;
			p=LCDBuffer;
			col[0]=(c>>16);
			col[1]=(c>>8) & 0xFF;
			col[2]=(c & 0xFF);
			for(t=0;t<i;t+=3){p[t]=col[0];p[t+1]=col[1];p[t+2]=col[2];}
			for(y=y1;y<=y2;y++){
				if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK)spi_write_fast(spi0,p,i);
				else spi_write_fast(spi1,p,i);
			}
		} else {
			i = x2 - x1 + 1;
			i*=2;
			p=LCDBuffer;
			col[0]= ((c >> 16) & 0b11111000) | ((c >> 13) & 0b00000111);
			col[1] = ((c >>  5) & 0b11100000) | ((c >>  3) & 0b00011111);
			if(Option.DISPLAY_TYPE == GC9A01){
				col[0]=~col[0];
				col[1]=~col[1];
			}
			for(t=0;t<i;t+=2){p[t]=col[0];p[t+1]=col[1];}
			if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK){
				for(t=y1;t<=y2;t++){
					spi_write_fast(spi0,p,i);
				} 
			} else {
				for(t=y1;t<=y2;t++){
					spi_write_fast(spi1,p,i);
				} 
			}
		}
	}
	if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK)spi_finish(spi0);
	else spi_finish(spi1);
	ClearCS(Option.LCD_CS);                                       //set CS high
}

//Print the bitmap of a char on the video output
//    x, y - the top left of the char
//    width, height - size of the char's bitmap
//    scale - how much to scale the bitmap
//	  fc, bc - foreground and background colour
//    bitmap - pointer to the bitmap
void DrawBitmapSPI(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m, n;
    char f[3],b[3];
      int vertCoord, horizCoord, XStart, XEnd, YEnd;
    char *p=0;
    union colourmap {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
    if(bc == -1 && (void *)ReadBuffer == (void *)DisplayNotSet) bc = 0x0;
    if(x1>=HRes || y1>=VRes || x1+width*scale<0 || y1+height*scale<0)return;
    // adjust when part of the bitmap is outside the displayable coordinates
    vertCoord = y1; if(y1 < 0) y1 = 0;                                 // the y coord is above the top of the screen
    XStart = x1; if(XStart < 0) XStart = 0;                            // the x coord is to the left of the left marginn
    XEnd = x1 + (width * scale) - 1; if(XEnd >= HRes) XEnd = HRes - 1; // the width of the bitmap will extend beyond the right margin
    YEnd = y1 + (height * scale) - 1; if(YEnd >= VRes) YEnd = VRes - 1;// the height of the bitmap will extend beyond the bottom margin
    if(bc == -1) {                                                     //special case of overlay text
        i = 0;
        j = width * height * scale * scale * 3;
        p = GetMemory(j);                                              //allocate some temporary memory
        ReadBuffer(XStart, y1, XEnd, YEnd, (unsigned char *)p);
    }
    // convert the colours to 565 format
	if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS ){
		f[0]=(fc>>16);
		f[1]=(fc>>8) & 0xFF;
		f[2]=(fc & 0xFF);
		b[0]=(bc>>16);
		b[1]=(bc>>8) & 0xFF;
		b[2]=(bc & 0xFF);
	} else {
		f[0]= ((fc >> 16) & 0b11111000) | ((fc >> 13) & 0b00000111);
		f[1] = ((fc >>  5) & 0b11100000) | ((fc >>  3) & 0b00011111);
		b[0] = ((bc >> 16) & 0b11111000) | ((bc >> 13) & 0b00000111);
		b[1] = ((bc >>  5) & 0b11100000) | ((bc >>  3) & 0b00011111);
	}
		if(Option.DISPLAY_TYPE == GC9A01){
			f[0]=~f[0];
			b[0]=~b[0];
			f[1]=~f[1];
			b[1]=~b[1];
		}

    DefineRegionSPI(XStart, y1, XEnd, YEnd, 1);


    n = 0;
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            if(vertCoord++ < 0) continue;                           // we are above the top of the screen
            if(vertCoord > VRes) {                                  // we have extended beyond the bottom of the screen
    			ClearCS(Option.LCD_CS);                                       //set CS high
                if(p != NULL) FreeMemory((unsigned char *)p);
                return;
            }
            horizCoord = x1;
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    if(horizCoord++ < 0) continue;                  // we have not reached the left margin
                    if(horizCoord > HRes) continue;                 // we are beyond the right margin
                    if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
                        SPIqueue((uint8_t *)&f);
                    } else {
                        if(bc == -1){
                            c.rgbbytes[0] = p[n];
                            c.rgbbytes[1] = p[n+1];
                            c.rgbbytes[2] = p[n+2];
							if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS ){
								b[0]=c.rgbbytes[2];
								b[1]=c.rgbbytes[1];
								b[2]=c.rgbbytes[0];
							} else {
								b[0] = ((c.rgb >> 16) & 0b11111000) | ((c.rgb >> 13) & 0b00000111);
								b[1] = ((c.rgb >>  5) & 0b11100000) | ((c.rgb >>  3) & 0b00011111);
							}
						} 
                        SPIqueue((uint8_t *)&b);

                    }
                    n += 3;
                }
            }
        }
    }

    ClearCS(Option.LCD_CS);                                       //set CS high

    // revert to non enhanced SPI mode
    if(p != NULL) FreeMemory((unsigned char *)p);

}
const unsigned char map32[256];
void ReadBufferSPI(int x1, int y1, int x2, int y2, unsigned char* p) {
    int r, N, t;
    unsigned char h,l;
//	PInt(x1);PIntComma(y1);PIntComma(x2);PIntComma(y2);PRet();
    // make sure the coordinates are kept within the display area
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
    N=(x2- x1+1) * (y2- y1+1) * 3;
    if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE==ST7789B )spi_write_cd(ILI9341_PIXELFORMAT,1,0x66); //change to RDB666 for read
    DefineRegionSPI(x1, y1, x2, y2, 0);
	SPISpeedSet( (Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9481IPS) ? ST7789RSpeed : SPIReadSpeed); //need to slow SPI for read on this display
	rcvr_byte_multi((uint8_t *)p, 1);
    r=0;
	rcvr_byte_multi((uint8_t *)p,N);
	gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
    ClearCS(Option.LCD_CS);                  //set CS high
	SPISpeedSet(Option.DISPLAY_TYPE);
    // revert to non enhanced SPI mode
    if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE==ST7789B )spi_write_cd(ILI9341_PIXELFORMAT,1,0x55); //change back to rdb565
    r=0;
    while(N) {
		h=(uint8_t)p[r+2];
		l=(uint8_t)p[r];
		p[r]=h;//(h & 0xF8);
		p[r+2]=l;//(l & 0xF8);
        r+=3;
        N-=3;
    }
}

void DrawBufferSPI(int x1, int y1, int x2, int y2, unsigned char* p) {
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
	unsigned char q[3];
	int i,t;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
	i=(x2-x1+1) * (y2-y1+1);
    DefineRegionSPI(x1, y1, x2, y2, 1);
    while(i--){
		c.rgbbytes[0]=*p++; //this order swaps the bytes to match the .BMP file
		c.rgbbytes[1]=*p++;
		c.rgbbytes[2]=*p++;
	// convert the colours to 565 format
		// convert the colours to 565 format
		if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE==ILI9481IPS ){
			q[0]=c.rgbbytes[2];
			q[1]=c.rgbbytes[1];
			q[2]=c.rgbbytes[0];
		} else {
			q[0]= ((c.rgb >> 16) & 0b11111000) | ((c.rgb >> 13) & 0b00000111);
			q[1] = ((c.rgb >>  5) & 0b11100000) | ((c.rgb >>  3) & 0b00011111);
		}
		if(Option.DISPLAY_TYPE == GC9A01){
			q[0]=~q[0];
			q[1]=~q[1];
		}
		SPIqueue(q);
    }
    ClearCS(Option.LCD_CS);                  //set CS high
}
void DrawBufferMEM(int x1, int y1, int x2, int y2, unsigned char* p) {
    int x,y; 
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
    for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
			c.rgbbytes[0]=*p++; //this order swaps the bytes to match the .BMP file
            if(c.rgbbytes[0]<0x40)c.rgbbytes[0]=0;
			c.rgbbytes[1]=*p++;
            if(c.rgbbytes[1]<0x40)c.rgbbytes[1]=0;
			c.rgbbytes[2]=*p++;
            if(c.rgbbytes[2]<0x40)c.rgbbytes[2]=0;
			c.rgbbytes[3]=0;
			DrawPixel(x,y,c.rgb);
		}
	}
}
void ReadBufferMEM(int x1, int y1, int x2, int y2, unsigned char* buff) {
    unsigned char* p=(void *)((unsigned int)LCDBuffer);
    int x,y,loc,t;
    unsigned char mask;
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(Option.DISPLAY_ORIENTATION==PORTRAIT){
        t=x1;
        x1=VRes-y2-1;
        y2=t;
        t=x2;
        x2=VRes-y1-1;
        y1=t;
    }
     if(Option.DISPLAY_ORIENTATION==RLANDSCAPE){
        x1=HRes-x1-1;
        x2=HRes-x2-1;
        y1=VRes-y1-1;
        y2=VRes-y2-1;
    }
   if(Option.DISPLAY_ORIENTATION==RPORTRAIT){
        t=y1;
        y1=HRes-x1-1;
        x1=t;
        t=y2;
        y2=HRes-x2-1;
        x2=t;
    }
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }

    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
    for(x=x1;x<=x2;x++){
        for(y=y1;y<=y2;y++){
           	if(!PackHorizontal){
        	   loc=x+(y/8)*DisplayHRes; //get the byte address for this bit
               mask=1<<(y % 8); //get the bit position for this bit
           	} else {
        	   loc=x/8+y*DisplayHRes/8; //get the byte address for this bit
               mask=1<<(7-(x % 8)); //get the bit position for this bit
           	}
		   	if(p[loc]&mask){
				*buff++=0xFF;
				*buff++=0xFF;
				*buff++=0xFF;
		   	} else {
				*buff++=0x0;
				*buff++=0x0;
				*buff++=0x0;
			}
       	}
    }
}

void DrawRectangleMEM(int x1, int y1, int x2, int y2, int c){
    unsigned char* p=(void *)((unsigned int)LCDBuffer);
    int x,y,loc,t;
    unsigned char mask;
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(Option.DISPLAY_ORIENTATION==PORTRAIT){
        t=x1;
        x1=VRes-y2-1;
        y2=t;
        t=x2;
        x2=VRes-y1-1;
        y1=t;
    }
     if(Option.DISPLAY_ORIENTATION==RLANDSCAPE){
        x1=HRes-x1-1;
        x2=HRes-x2-1;
        y1=VRes-y1-1;
        y2=VRes-y2-1;
    }
   if(Option.DISPLAY_ORIENTATION==RPORTRAIT){
        t=y1;
        y1=HRes-x1-1;
        x1=t;
        t=y2;
        y2=HRes-x2-1;
        x2=t;
    }
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }

    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
     for(x=x1;x<=x2;x++){
        for(y=y1;y<=y2;y++){
           if(!PackHorizontal){
        	   loc=x+(y/8)*DisplayHRes; //get the byte address for this bit
               mask=1<<(y % 8); //get the bit position for this bit
           } else {
        	   loc=x/8+y*DisplayHRes/8; //get the byte address for this bit
               mask=1<<(7-(x % 8)); //get the bit position for this bit
           }
           if(c){
               p[loc]|=mask;
           } else {
               p[loc]&=(~mask);
           }
        }
    }
}
void DrawPixelMEM(int x1,int y1, int c){
	DrawRectangleMEM(x1,y1,x1,y1,c);
}
void DrawBitmapMEM(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m, x, y,t, loc;
    unsigned char omask, amask;
    unsigned char* p=(void *)((unsigned int)LCDBuffer);
    if(x1>=HRes || y1>=VRes || x1+width*scale<0 || y1+height*scale<0)return;
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    x=x1 + k * scale + m ;
                    y=y1 + i * scale + j ;
                    if(Option.DISPLAY_ORIENTATION==PORTRAIT){
                        t=x;
                        x=VRes-y-1;
                        y=t;
                    }
                    if(Option.DISPLAY_ORIENTATION==RLANDSCAPE){
                        x=HRes-x-1;
                        y=VRes-y-1;
                    }
                    if(Option.DISPLAY_ORIENTATION==RPORTRAIT){
                        t=y;
                        y=HRes-x-1;
                        x=t;
                    }
                    if(y<low_y)low_y=y;
                    if(y>high_y)high_y=y;
                    if(x<low_x)low_x=x;
                    if(x>high_x)high_x=x;
                    if(!PackHorizontal){
                 	   loc=x+(y/8)*DisplayHRes; //get the byte address for this bit
                       omask=1<<(y % 8); //get the bit position for this bit
                       amask=~omask;
                    } else {
                 	   loc=x/8+y*DisplayHRes/8; //get the byte address for this bit
                       omask=1<<(7-(x % 8)); //get the bit position for this bit
                       amask=~omask;
                    }
                    if(x >= 0 && x < DisplayHRes && y >= 0 && y < DisplayVRes) {  // if the coordinates are valid
                        if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
                            if(fc){
                                p[loc]|=omask;
                             } else {
                                p[loc]&=amask;
                             }
                       } else {
                            if(bc>0){
                                p[loc]|=omask;
                            } else if(bc==0) {
                                p[loc]&=amask;
                            }
                        }
                   }
                }
            }
        }
    }
}


void N5110SetXY(int x, int y){
  int LcdData;
  LcdData=0b01000000 | y ; spi_write_command(LcdData);
  LcdData=0b10000000 | x ; spi_write_command(LcdData);
}
void SSD1306I2CSetXY(uint8_t x, uint8_t y){
	uint8_t xn=x;
	I2C_Send_Command(0xB0 | y);
	I2C_Send_Command(0x10 | ((xn>>4) & 0xF));
	I2C_Send_Command(0x00 | (xn & 0xF));
}
void SSD1306SPISetXY(uint8_t x, uint8_t y){
	uint8_t xn=x;
	spi_write_command(0xB0 | y);
	spi_write_command(0x10 | ((xn>>4) & 0xF));
	spi_write_command(0x00 | (xn & 0xF));
}
void ST7920SetXY(int x, int y){
	int xx=x, yy=y;
	if(yy>31){
		xx=xx+8;
		yy=yy-32;
	}
	unsigned char a[5];
	a[0]=ST7920setcommand;
	a[1]=(yy & 0x10) | 0x80;
	a[2]=(yy & 0x0F) <<4;
	a[3]=0x80;
	a[4]=xx<<4;
    SetCS();
	xmit_byte_multi(a,5);
//	HAL_SPI_Transmit(&hspi3,a,5,500);
	uSec(50);
	ClearCS(Option.LCD_CD);
}

void Display_Refresh(void){
	if(!(Option.DISPLAY_TYPE<=I2C_PANEL || Option.DISPLAY_TYPE>=BufferedPanel)) return;
	unsigned char* p=(void *)((unsigned int)LCDBuffer);
	if(low_x<0)low_x=0;
	if(low_y<0)low_y=0;
	if(high_x>DisplayHRes)high_x=DisplayHRes-1;
	if(high_y>DisplayVRes)high_y=DisplayVRes-1;
	if(Option.DISPLAY_TYPE==N5110){
		int y;
		for(y=low_y/8;y<(high_y & 0xf8)/8+1;y++){
			N5110SetXY(low_x, y);
			SetCS();
			gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
			xmit_byte_multi(p+(y*DisplayHRes)+low_x,high_x-low_x+1);
//			HAL_SPI_Transmit(&hspi3,p+(y*DisplayHRes)+low_x,high_x-low_x+1,500);
			ClearCS(Option.LCD_CS);
		}
	}
	if(Option.DISPLAY_TYPE<=I2C_PANEL){
		int y;
		for(y=low_y/8;y<(high_y & 0xf8)/8+1;y++){
			SSD1306I2CSetXY(Option.I2Coffset+low_x,y);
			I2C_Send_Data(p+(y*DisplayHRes)+low_x,high_x-low_x+1);
		}
	}
	if(Option.DISPLAY_TYPE==SSD1306SPI){
		int y;
		for(y=low_y/8;y<(high_y & 0xf8)/8+1;y++){
			SSD1306SPISetXY(Option.I2Coffset+low_x,y);
			SetCS();
			gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
			xmit_byte_multi(p+(y*DisplayHRes)+low_x,high_x-low_x+1);
//			HAL_SPI_Transmit(&hspi3,p+(y*DisplayHRes)+low_x,high_x-low_x+1,500);
			ClearCS(Option.LCD_CS);
		}
	}
	if(Option.DISPLAY_TYPE==ST7920){
		int y,i;
		unsigned char x_array[33];
		unsigned char *q;
		for(y=low_y;y<=high_y;y++){
			q=p+y*16;
			x_array[0]=ST7920setata;
			for(i=1;i<33;i+=2){
				x_array[i]=*q & 0xF0;
				x_array[i+1]=((*q++)<<4) & 0xF0;
			}
			ST7920SetXY(0,y);
			SetCS();
			xmit_byte_multi(x_array,33);
//			HAL_SPI_Transmit(&hspi3,x_array,33,500);
			ClearCS(Option.LCD_CD);
		}
	}
    low_y=2000; high_y=0; low_x=2000; high_x=0;

}
#endif
void DisplayNotSet(void) {
    error("Display not configured");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// These three functions allow the SPI port to be used by multiple drivers (LCD/touch/SD card)
// The BASIC use of the SPI port does NOT use these functions
// The MX170 uses SPI channel 1 which is shared by the BASIC program
// The MX470 uses SPI channel 2 which it has exclusive control of (needed because touch can be used at any time)
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
extern uint16_t SPI_CLK_PIN;
// config the SPI port for output
// it will not touch the port if it has already been opened
void SPISpeedSet(int device){
    if(CurrentSPIDevice != device){
		if(device==SDSLOW || (device==SDFAST && SPI_CLK_PIN!= SD_CLK_PIN)) {
//			MMPrintString("Slow Bitbang\r\n");
			xchg_byte= BitBangSwapSPI;
			xmit_byte_multi=BitBangSendSPI;
			rcvr_byte_multi=BitBangReadSPI;
			SET_SPI_CLK=BitBangSetClk; 
			SET_SPI_CLK(SD_SPI_SPEED, false, false);
		} else {
			if(PinDef[Option.SYSTEM_CLK].mode & SPI0SCK && PinDef[Option.SYSTEM_MOSI].mode & SPI0TX  && PinDef[Option.SYSTEM_MISO].mode & SPI0RX  ) {
//				MMPrintString("SPI0\r\n");
				xchg_byte= HW0SwapSPI;
				xmit_byte_multi=HW0SendSPI;
				rcvr_byte_multi=HW0ReadSPI;
				SET_SPI_CLK=HW0Clk;
                gpio_set_input_enabled(PinDef[Option.SYSTEM_CLK].GPno,false);
                gpio_set_input_enabled(PinDef[Option.SYSTEM_MOSI].GPno,false);
                gpio_set_input_enabled(PinDef[Option.SYSTEM_MISO].GPno,false);
			} else if(PinDef[Option.SYSTEM_CLK].mode & SPI1SCK && PinDef[Option.SYSTEM_MOSI].mode & SPI1TX  && PinDef[Option.SYSTEM_MISO].mode & SPI1RX ){
//				MMPrintString("SPI1\r\n");
				xchg_byte= HW1SwapSPI;
				xmit_byte_multi=HW1SendSPI;
				rcvr_byte_multi=HW1ReadSPI;
				SET_SPI_CLK=HW1Clk;
			} else {
//				MMPrintString("Fast Bitbang\r\n");
				xchg_byte= BitBangSwapSPI;
				xmit_byte_multi=BitBangSendSPI;
				rcvr_byte_multi=BitBangReadSPI;
				SET_SPI_CLK=BitBangSetClk; 
			}
			SET_SPI_CLK(display_details[device].speed, display_details[device].CPOL, display_details[device].CPHASE);
		}
		CurrentSPIDevice=device;
    }
}

// set the chip select for SPI to high (disabled)
void ClearCS(int pin) {
    if(pin) {
    	if(Option.DISPLAY_TYPE != ST7920) gpio_put(PinDef[pin].GPno,GPIO_PIN_SET);
    	else gpio_put(PinDef[pin].GPno,GPIO_PIN_RESET);
    }
}
#ifndef PICOMITEVGA
int GetLineILI9341(void){
	unsigned char q;
    SetCS();
	SPISpeedSet(Option.DISPLAY_TYPE==ILI9341 ? SPIReadSpeed : ST7789RSpeed);
    gpio_put(LCD_CD_PIN,GPIO_PIN_RESET);
	SPIsend(ILI9341_GETSCANLINE);
    gpio_put(LCD_CD_PIN,GPIO_PIN_SET);
	uSec(3);
	xchg_byte(0);
	q=xchg_byte(0);
	xchg_byte(0);
	ClearCS(Option.LCD_CS);
	SPISpeedSet(Option.DISPLAY_TYPE);
	return (int)(q);
}
#endif
