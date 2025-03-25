/***********************************************************************************************************************
PicoMite MMBasic

SSD1963.c

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
/**
* @file SSD1963.c
* @author Geoff Graham, Peter Mather
* @brief Source for getscanline MMBasic function
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
int ScrollStart;
int Has100Pins = 0;

// parameters for the SSD1963 display panel (refer to the glass data sheet)
int SSD1963HorizPulseWidth, SSD1963HorizBackPorch, SSD1963HorizFrontPorch;
int SSD1963VertPulseWidth, SSD1963VertBackPorch, SSD1963VertFrontPorch;
int SSD1963PClock1, SSD1963PClock2, SSD1963PClock3;
int SSD1963Mode1, SSD1963Mode2;
unsigned int RDpin, RDport;
int SSD1963PixelInterface, SSD1963PixelFormat;
#define DELAY 0x80  //Bit7 of the count indicates a delay is also added.
#define REPEAT 0x40  //Bit6 of the count indicates same data is repeated instead of reading next byte.
int SSD1963rgb,SSD1963data=0;
void WriteCmdDataIPS_4_16(int cmd,int n,int data);
unsigned int ReadData(void);
void ReadBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p);
void DrawBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p);
//#define dx(...) {char s[140];sprintf(s,  __VA_ARGS__); SerUSBPutS(s); SerUSBPutS("\r\n");}


////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Functions used by MMBasic to setup the display
//
////////////////////////////////////////////////////////////////////////////////////////////////////////

void MIPS16 ConfigDisplaySSD(unsigned char *p) {
    getargs(&p, 13, (unsigned char *)",");
    if((argc & 1) != 1 || argc < 3) error("Argument count");


    if(checkstring(argv[0], (unsigned char *)"SSD1963_4")) {                         // this is the 4" glass
        Option.DISPLAY_TYPE = SSD1963_4;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1963_5")) {                  // this is the 5" glass
        Option.DISPLAY_TYPE = SSD1963_5;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1963_5A")) {                 // this is the 5" glass alternative version
        Option.DISPLAY_TYPE = SSD1963_5A;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1963_7")) {                  // there appears to be two versions of the 7" glass in circulation, this is type 1
        Option.DISPLAY_TYPE = SSD1963_7;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1963_7A")) {                 // this is type 2 of the 7" glass (high luminosity version)
        Option.DISPLAY_TYPE = SSD1963_7A;
    } else if(checkstring(argv[0], (unsigned char *)"SSD1963_8")) {                  // this is the 8" glass (EastRising)
        Option.DISPLAY_TYPE = SSD1963_8;
    } else if(checkstring(argv[0], (unsigned char *)"ILI9341_8")) {                  // this is the 8" glass (EastRising)
        Option.DISPLAY_TYPE = ILI9341_8;
	} else if(checkstring(argv[0],  (unsigned char *)"SSD1963_4_16")) {                         // this is the 4" glass
		Option.DISPLAY_TYPE = SSD1963_4_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_5_16")) {                  // this is the 5" glass
    	Option.DISPLAY_TYPE = SSD1963_5_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_5A_16")) {                 // this is the 5" glass alternative version
    	Option.DISPLAY_TYPE = SSD1963_5A_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_5ER_16")) {                 // this is the 5" EastRising RGB is BGR
       	Option.DISPLAY_TYPE = SSD1963_5ER_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_7_16")) {                  // there appears to be two versions of the 7" glass in circulation, this is type 1
    	Option.DISPLAY_TYPE = SSD1963_7_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_7A_16")) {                 // this is type 2 of the 7" glass (high luminosity version)
       	Option.DISPLAY_TYPE = SSD1963_7A_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_7ER_16")) {                // this is the 7" EastRising RGB is BGR
       	Option.DISPLAY_TYPE = SSD1963_7ER_16;
    } else if(checkstring(argv[0],  (unsigned char *)"SSD1963_8_16")) {                  // this is the 8" and 9" glass (EastRising)
    	Option.DISPLAY_TYPE = SSD1963_8_16;
    } else if(checkstring(argv[0],  (unsigned char *)"ILI9341_16")) {
    	Option.DISPLAY_TYPE = ILI9341_16;
    } else if(checkstring(argv[0],  (unsigned char *)"ILI9486_16")) {
    	Option.DISPLAY_TYPE = ILI9486_16;
    } else if(checkstring(argv[0],  (unsigned char *)"IPS_4_16")) {
    	Option.DISPLAY_TYPE = IPS_4_16;	                      /***G.A***/
    } else return;
#ifdef rp2350
    if(!(argc == 3 || argc == 5 || argc == 7 || argc == 9 || (argc == 11 && !rp2350a))) error("Argument count");
#else
if(!(argc == 3 || argc == 5 || argc == 7 || argc == 9 )) error("Argument count");
#endif

    if(checkstring(argv[2], (unsigned char *)"L") || checkstring(argv[2], (unsigned char *)"LANDSCAPE"))
        Option.DISPLAY_ORIENTATION = LANDSCAPE;
    else if(checkstring(argv[2], (unsigned char *)"P") || checkstring(argv[2], (unsigned char *)"PORTRAIT"))
        Option.DISPLAY_ORIENTATION = PORTRAIT;
    else if(checkstring(argv[2], (unsigned char *)"RL") || checkstring(argv[2], (unsigned char *)"RLANDSCAPE"))
        Option.DISPLAY_ORIENTATION = RLANDSCAPE;
    else if(checkstring(argv[2], (unsigned char *)"RP") || checkstring(argv[2], (unsigned char *)"RPORTRAIT"))
        Option.DISPLAY_ORIENTATION = RPORTRAIT;
    else
        error("Orientation");
    Option.SSD_DATA=1;
    if(argc==11){ //only valid on rp2350b
        char code;
        if(!(code=codecheck(argv[10])))argv[10]+=2;
        int pin = getinteger(argv[10]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        Option.SSD_DATA=pin;
    }
    CheckPin(SSD1963_DAT1, OptionErrorCheck);
    CheckPin(SSD1963_DAT2, OptionErrorCheck);
    CheckPin(SSD1963_DAT3, OptionErrorCheck);
    CheckPin(SSD1963_DAT4, OptionErrorCheck);
    CheckPin(SSD1963_DAT5, OptionErrorCheck);
    CheckPin(SSD1963_DAT6, OptionErrorCheck);
    CheckPin(SSD1963_DAT7, OptionErrorCheck);
    CheckPin(SSD1963_DAT8, OptionErrorCheck);
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        CheckPin(SSD1963_DAT9, OptionErrorCheck);
        CheckPin(SSD1963_DAT10, OptionErrorCheck);
        CheckPin(SSD1963_DAT11, OptionErrorCheck);
        CheckPin(SSD1963_DAT12, OptionErrorCheck);
        CheckPin(SSD1963_DAT13, OptionErrorCheck);
        CheckPin(SSD1963_DAT14, OptionErrorCheck);
        CheckPin(SSD1963_DAT15, OptionErrorCheck);
        CheckPin(SSD1963_DAT16, OptionErrorCheck);
    }

    if(argc > 3 && *argv[4]) {
        char code;
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        int pin = getinteger(argv[4]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
        if(ExtCurrentConfig[pin] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin,pin);
        if((PinDef[pin].slice & 0x7f) == Option.AUDIO_SLICE) error("Channel in use for Audio");
        Option.DISPLAY_BL = pin;
    } else Option.DISPLAY_BL = 0;
    if(argc>=7 && *argv[6]){
        char code;
        if(!(code=codecheck(argv[6])))argv[6]+=2;
        int pin = getinteger(argv[6]);
        if(!code)pin=codemap(pin);
        if(IsInvalidPin(pin)) error("Invalid pin");
#ifdef rp2350
        if(Option.DISPLAY_TYPE>SSD_PANEL_8 && PinDef[pin].GPno!=16 && rp2350a)error("Must be GP16 for 16-bit displays on the RP2350A");
#else
        if(Option.DISPLAY_TYPE>SSD_PANEL_8 && PinDef[pin].GPno!=16)error("Must be GP16 for 16-bit displays");
#endif
        if(ExtCurrentConfig[pin] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin,pin);
        Option.SSD_DC = PinDef[pin].GPno;
        Option.SSD_WR=  Option.SSD_DC+1;
        Option.SSD_RD=  Option.SSD_DC+2;
        Option.SSD_RESET=  Option.SSD_DC+3;
    } else {
        if(Option.DISPLAY_TYPE>SSD_PANEL_8){
            Option.SSD_DC = 16;
            Option.SSD_WR = 17;
            Option.SSD_RD = 18;
            Option.SSD_RESET = 19;
        } else {
            Option.SSD_DC = 13;
            Option.SSD_WR = 14;
            Option.SSD_RD = 15;
            Option.SSD_RESET = 16;
        }

    }
    if(argc>=9 && *argv[8]){
        if(checkstring(argv[8],(unsigned char *)"NORESET"))Option.SSD_RESET=-1;
    }

    CheckPin(SSD1963_DC_PIN, OptionErrorCheck);
    if(Option.SSD_RESET!=-1) CheckPin(SSD1963_RESET_PIN, OptionErrorCheck);
    CheckPin(SSD1963_WR_PIN, OptionErrorCheck);
    CheckPin(SSD1963_RD_PIN, OptionErrorCheck);


    // disable the SPI LCD and touch
    Option.LCD_CD = Option.LCD_Reset = Option.LCD_CS = 0;
    Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;                    // record the touch feature as not calibrated
}
void Write16bitCommand(int cmd) {
    PinSetBit(SSD1963_DC_PIN, LATCLR);
    nop;nop;nop;nop;nop; nop; 
    gpio_put_masked64((0xFFFF<<SSD1963data),(cmd<<SSD1963data)); nop;nop;nop;nop;nop;nop;
    gpio_put(SSD1963_WR_GPPIN,0);nop;nop;nop;nop;nop; nop; ;gpio_put(SSD1963_WR_GPPIN,1);nop;nop;nop;nop;nop;nop;
    PinSetBit(SSD1963_DC_PIN, LATSET);
    nop;
}

// Write an 8 bit data word to the SSD1963
void WriteData16bit(int data) {
    gpio_put_masked64((0xFFFF<<SSD1963data),(data<<SSD1963data));  nop;nop;nop;nop;nop; nop; 
    gpio_put(SSD1963_WR_GPPIN,0);nop;nop;nop;nop;nop; nop; gpio_put(SSD1963_WR_GPPIN,1);nop;nop;nop;nop;nop;nop;
}
// Write sequential 16 bit command with the same 16 bit data word n times to the IPS_4_16
void WriteCmdDataIPS_4_16(int cmd,int n,int data) {
    while (n>0) {
        /* Write 16-bit Index, then write register */
        Write16bitCommand(cmd);
        /* Write 16-bit Reg */
        WriteData16bit(data);
        cmd++;
        n--;
    }
}

// Companion code to the above tables.  Reads and issues
// a series of LCD commands.
//command types
//   0= B7-B0 as 8bit command - command is not repeated for each subsequent byte of data. LCDPanel accepts multiple data bytes with each commande
//   1=single byte shifted to B15-B8 as 16bit command -command incremented and sent with each new data byte
//   2= two command bytes read to fill B15-B0 as 16bit command - command incremented and sent with each new data byte
void static SendCommand16Block(const uint8_t *addr) {
   uint8_t numCommands, numArgs,cmdType,numReps;
   uint16_t ms,cmd;
   numCommands = *(addr++);           // Number of commands to follow
   cmdType = *(addr++);               // Number of commands to follow
   while(numCommands--) {                 // For each command...
	 if (cmdType==2){
		 cmd  = (*(addr++)<<8);
         cmd |= *(addr++);
	 }else if (cmdType==1){
		 cmd  = (*(addr++)<<8);
	 }else{ //cmdType==0
		 cmd = *(addr++);
		 Write16bitCommand(cmd) ;        //   Read, issue command and increment address
	 }
	 numArgs  = *(addr++);                //   Number of args to follow
	 ms       = numArgs & DELAY;          //   If hibit set, delay follows args
     numArgs &= ~DELAY;                   //   Mask out delay bit
     numReps  = numArgs & REPEAT;         //   If B6 set then repeat same byte for numArgs
     numArgs &= ~REPEAT;                   //   Mask out repeat bit
     if (numArgs==0 && cmdType!=0){Write16bitCommand(cmd++) ;}
     while(numArgs--) { //   For each argument...
       if (numReps){
    	   if(cmdType!=0){Write16bitCommand(cmd++) ; }
    	   WriteData16bit(*(addr));         //     Read, issue argument DONT step address
       }else{
    	   if(cmdType!=0){Write16bitCommand(cmd++) ; }
    	   WriteData16bit(*(addr++));         //  Read, issue argument step address
       }
     }
     if (numReps){addr++;}                //move pointer to next value
     if(ms) {
       ms = *(addr++);                    // Read post-command delay time (ms)
       if(ms == 255) ms = 500;            // If 255, delay for 500 ms
         uSec(ms*1000);                   //convert to uS
     }
   }
}

static const uint8_t
ILI9341_16Init[] = {   // Initialization commands for ILI9486-16 screen
	    17,            // no of commands in list:
		0,             // 0= B7-B0 as 8bit command  8=single byte shifted to B15-B8 as 16bit command 2= two bytes needed

		0xCB,5,0x39,0x2c,0x00,0x34,0x02,
		0xCF,3,0x00,0xc1,0x30,
		0xE8,3,0x85,0x00,0x78,
		0xEA,2,0x00,0x00,
		0xED,4,0x64,0x03,0x12,0x81,
		0xF7,1,0x20,
		0xC0,1,0x23,            //Power control  //VRH[5:0]
		0xC1,1,0x10,            //Power control   //SAP[2:0];BT[3:0]
		0xC5,2,0x3e,0x28,       //VCM control //Contrast
		0xC7,1,0x86,            //VCM control2

		0x3A,1,0x55,            // 55= RGB565 66=RGB666
		0xB1,2,0x00,0x18,
		0xB6,3,0x08,0x82,0x27, // Display Function Control
		0xF2,1,0x00,           // 3Gamma Function Disable
		0x26,1,0x01,           //Gamma curve selected
		0xe0,15,0x0f,0x31,0x2b,0x0c,0x0e,0x08,0x4e,0xf1,0x37,0x07,0x10,0x03,0x0e,0x09,0x00, //Set Gamma
		0xe1,15,0x00,0x0e,0x14,0x03,0x11,0x07,0x31,0xc1,0x48,0x08,0x0f,0x0c,0x31,0x36,0x0f  //Set Gamma
};

// First byte is the number of commands
// Second byte is command type 0= B7-B0 as 8bit command  8=single byte shifted to B15-B8 as 16bit command 2= two bytes needed
static const uint8_t
ILI9486_16Init[] = {   // Initialization commands for ILI9486-16 screen
	    15,            // no of commands in list:
		0,             // 0= B7-B0 as 8bit command  1=single byte shifted to B15-B8 as 16bit command 2= two bytes needed

		0x01,DELAY,100,                           // uSec( 100000); // software reset 100ms pause
		0x28,0, // display off
        0xF1,6,0x36,0x04,0x00,0x3c,0x0f,0x8f,
        0xF2,9,0x18,0xa3,0x12,0x02,0xb2,0x12,0xff,0x10,0x00,
        0xf8,2,0x21,0x04,
        0xf9,2,0x00,0x08,
        0xb4,1,0x00,
        0xc1,1,0x47,
        0xC5,4,0x00,0xaf,0x80,0x00,
        0xe0,15,0x0f,0x1f,0x1c,0x0c,0x0f,0x08,0x48,0x98,0x37,0x0a,0x13,0x04,0x11,0x0d,0x00,

		0xe1,15,0x0f,0x32,0x2e,0x0b,0x0d,0x05,0x47,0x75,0x37,0x06,0x10,0x03,0x24,0x20,0x00,
        0x34,0,                                     //Tearing Effect Off
        0x3A,1,0x66,                                // Pixel Interface Format // 18 bit colour for SPI
        0x11,DELAY,150,                             //uSec( 150000);
        0x29,DELAY,255                              //uSec(500000);
};


static const uint8_t
NT35510_16Init[] = {   // Initialization commands for ILI9486-16 screen
	    36,            // no of commands in list:
		1,             // 0= B7-B0 as 8bit command  1=single byte shifted to B15-B8 as 16bit command 2= two bytes needed

		0xF0,5,0x55,0xAA,0x52,0x08,0x01,
		0xB6,REPEAT+3,0x34,//0x34,0x34,
		0xB0,REPEAT+3,0x0D,//0x0D,0x0D,
		0xB7,REPEAT+3,0x24,//0x24,0x24,
		0xB1,REPEAT+3,0x0D,//0x0D,0x0D,
		0xB8,REPEAT+3,0x24,//0x24,0x24,
		0xB2,1,0x00,
		0xB9,REPEAT+3,0x24,//0x24,0x24,
		0xB3,REPEAT+3,0x05,//0x05,0x05,
		0xBF,1,0x01,

		0xBA,REPEAT+3,0x34,//0x34,0x34,
		0xB5,REPEAT+3,0x0B,//0x0B,0x0B,
		0xBC,3,0x00,0xA3,0x00,
		0xBD,3,0x00,0xA3,0x00,
		0xBE,2,0x00,0x63,
		0xD1,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,

		0xD2,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,

		0xD3,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,

		0xD4,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,

		0xD5,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,


		0xD6,52,
			0x00,0x37,0x00,0x52,0x00,0x7B,0x00,0x99,0x00,0xB1,0x00,0xD2,0x00,0xF6,0x01,0x27,
			0x01,0x4E,0x01,0x8C,0x01,0xBE,0x02,0x0B,0x02,0x48,0x02,0x4A,0x02,0x7E,0x02,0xBC,
			0x02,0xE1,0x03,0x10,0x03,0x31,0x03,0x5A,0x03,0x73,0x03,0x94,0x03,0x9F,0x03,0xB3,
			0x03,0xB9,0x03,0xC1,


		0xF0,5,0x55,0xAA,0x52,0x08,0x00,
		0xB0,5,0x08,0x05,0x02,0x05,0x02,
		0xB6,1,0x08,
		0xB5,1,0x50,  //480x800
		0xB7,2,0x00,0x00,
		0xB8,4,0x01,0x05,0x05,0x05,
		0xBC,3,0x00,0x00,0x00,
		0xCC,3,0x03,0x00,0x00,
		0xBD,5,0x01,0x84,0x07,0x31,0x00,

		0xBA,1,0x01,
		0xFF,4,0xAA,0x55,0x25,0x01,
		0x35,1,0x00,
		0x3A,1,0x66,    // 55=Colour 565 66=Colour 666 77=Colour 888
		0x11,DELAY,100,   // uSec( 100000);//delay(100);
		0x29,DELAY,100    // uSec( 100000);//delay(50);
};

static const uint8_t
OTM8009A_16Init[] = {   // Initialization commands for OTM8009A_16 screen
	    51,            // no of commands in list:
		2,             // 0= B7-B0 as 8bit command  1=single byte shifted to B15-B8 as 16bit command 2= two bytes needed

		0xff,0x00,3,0x80,0x09,0x01, //enable access command2
		0xff,0x80,2,0x80,0x09, //enable access command2
		0xff,0x03,1,0x01,  //DON?T KNOW ???
		0xc5,0xb1,1,0xA9, //power control
		0xc5,0x91,1,0x0F, //power control
		0xc0,0xB4,1,0x50,
		0xE1,0x00,16,0x00,0x09,0x0F,0x0E,0x07,0x10,0x0B,0x0A,0x04,0x07,0x0B,0x08,0x0F,0x10,0x0A,0x01,
		0xE2,0x00,16,0x00,0x09,0x0F,0x0E,0x07,0x10,0x0B,0x0A,0x04,0x07,0x0B,0x08,0x0F,0x10,0x0A,0x01,
	    0xD9,0x00,1,0x4E,      /* VCOM Voltage Setting */
		0xc1,0x81,1,0x66,      //osc=65HZ

		0xc1,0xa1,1,0x08,      //RGB Video Mode Setting
		0xc5,0x92,1,0x01,      //power control
		0xc5,0x95,1,0x34,      //power control
		0xd8,0x00,2,0x79,0x79, //GVDD / NGVDD setting
		0xc5,0x94,1,0x33, //power control
		0xc0,0xa3,1,0x1B,//panel timing setting
		0xc5,0x82,1,0x83, //power control
		0xc4,0x81,1,0x83,  //source driver setting
		0xc1,0xa1,1,0x0E,
		0xb3,0xa6,2,0x20,0x01,

		0xce,0x80,6,0x85,0x01,0x00,0x84,0x01,0x00,  // GOA VST
		0xce,0xa0,7,0x18,0x04,0x03,0x39,0x00,0x00,0x00,
		0xce,0xa7,7,0x18,0x03,0x03,0x3a,0x00,0x00,0x00,
		0xce,0xb0,14,0x18,0x02,0x03,0x3b,0x00,0x00,0x00,0x18,0x01,0x03,0x3c,0x00,0x00,0x00,
		0xcf,0xc0,10,0x01,0x01,0x20,0x20,0x00,0x00,0x01,0x00,0x00,0x00,
		0xcf,0xd0,1,0x00,
		0xcb,0x80,REPEAT+10,0x00,
		0xcb,0x90,REPEAT+15,0x00,
		0xcb,0xa0,REPEAT+15,0x00,
		0xcb,0xb0,REPEAT+10,0x00,

		0xcb,0xc0,1,0x00,
		0xcb,0xc1,REPEAT+5,0x04,
		0xcb,0xc6,REPEAT+9,0x00,
		0xcb,0xd0,REPEAT+6,0x00,
		0xcb,0xd6,REPEAT+5,0x04,
		0xcb,0xdb,REPEAT+4,0x00,
		0xcb,0xe0,REPEAT+10,0x00,
		0xcb,0xf0,REPEAT+10,0xff,
		0xcc,0x80,10,0x00,0x26,0x09,0x0B,0x01,0x25,0x00,0x00,0x00,0x00,
		0xcc,0x90,REPEAT+11,0x00,

		0xcc,0x9b,4,0x26,0x0A,0x0C,0x02,
		0xcc,0xa0,1,0x25,
		0xcc,0xa1,REPEAT+14,0x00,
		0xcc,0xb0,10,0x00,0x25,0x0c,0x0a,0x02,0x26,0x00,0x00,0x00,0x00,
		0xcc,0xc0,REPEAT+11,0x00,
		0xcc,0xcb,4,0x25,0x0B,0x09,0x01,
		0xcc,0xd0,1,0x26,
		0xcc,0xd1,REPEAT+14,0x00,
		0x3A,0x00,1,0x55,
		0x11,0x00,DELAY,100,

		0x29,0x00,DELAY,100

};



void MIPS16 InitILI9341(void){

    if(Option.SSD_RESET){
        PinSetBit(SSD1963_RESET_PIN, LATCLR);                              // reset the SSD1963
        uSec(10000);
        PinSetBit(SSD1963_RESET_PIN, LATSET);                              // release from reset state to sleep state
        uSec(100000);
    }

     if(Option.DISPLAY_TYPE ==  ILI9486_16)	{
	        SendCommand16Block(ILI9486_16Init);
			//ILI9486 initialisation
	 } else {

		//ILI9341_16 Initialisation
		 SendCommand16Block(ILI9341_16Init);
	 }
		uSec(120000);
		int i=0;
		switch(Option.DISPLAY_ORIENTATION) {
        	case LANDSCAPE:     i=ILI9341_Landscape; break;
        	case PORTRAIT:      i=ILI9341_Portrait; break;
        	case RLANDSCAPE:    i=ILI9341_Landscape180; break;
        	case RPORTRAIT:     i=ILI9341_Portrait180; break;
		}
		Write16bitCommand(0x36);    // Memory Access Control
		WriteData16bit(i);
		Write16bitCommand(0x11);    //Exit Sleep
		uSec(120000);
		Write16bitCommand(0x29); //display on
		Write16bitCommand(0x2c); //display on
		ClearScreen(Option.DefaultBC);
}

void MIPS16 InitIPS_4_16(void){
    if(Option.SSD_RESET){
        PinSetBit(SSD1963_RESET_PIN, LATCLR);                              // reset the SSD1963
        uSec(10000);
        PinSetBit(SSD1963_RESET_PIN, LATSET);                              // release from reset state to sleep state
        uSec(100000);
    }
    int t=0;
    //read the id to see if OTM8009A or NT35510
    WriteComand(0xDA00);
    gpio_set_dir_in_masked64(Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data):(0xFF<<SSD1963data));
    t=ReadData() ; // dummy read
    t=ReadData() ; // dummy read
    gpio_set_dir_out_masked64(Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data):(0xFF<<SSD1963data));
//    MMPrintString("ID1=");PIntH(t);PRet();
    if ((t & 0x7F) == 0x55){   //was ((t & 0x7F) == 0x55) //((t & 0x71) == 0x51)
        // NT35510 IPS Display detected. Identified in code by (LCDAttrib==1)
        LCDAttrib = 1;
        SaveOptions();
        SendCommand16Block(NT35510_16Init);
    }else{
    //============ OTM8009A+HSD3.97 20140613 ===============================================//
        LCDAttrib = 0;
        SendCommand16Block(OTM8009A_16Init);
    }
    //It is a natural PORTrait LCD, but we will fudge it to make it
    //work for 1=landscape,2=portrait 3=RLandscape,4=RPortrait

    #define OTM8009A_MADCTL_MY  0x80
    #define OTM8009A_MADCTL_MX  0x40
    #define OTM8009A_MADCTL_MV  0x20

    int i=0;
    switch(Option.DISPLAY_ORIENTATION) {
        case LANDSCAPE:     i=OTM8009A_MADCTL_MX | OTM8009A_MADCTL_MV; break;
        case PORTRAIT:      i=0x0 ; break;
        case RLANDSCAPE:    i=OTM8009A_MADCTL_MY | OTM8009A_MADCTL_MV; break;
        case RPORTRAIT:     i=OTM8009A_MADCTL_MX | OTM8009A_MADCTL_MY; break;
    }

    WriteCmdDataIPS_4_16(0x3600,1,i);   //set Memory Access Control
    ClearScreen(Option.DefaultBC);

}

// initialise the display controller
// this is used in the initial boot sequence of the Micromite
void MIPS16 InitDisplaySSD(void) {
	SSD1963rgb=0b0;
	LCDAttrib=0;
    SSD1963data=PinDef[Option.SSD_DATA].GPno;
    if(Option.DISPLAY_TYPE<SSDPANEL || Option.DISPLAY_ORIENTATION>=VIRTUAL)return;

    // the parameters for the display panel are set here (refer to the data sheet for the glass)
    switch(Option.DISPLAY_TYPE) {
        case SSD1963_4_16:
        case SSD1963_4: DisplayHRes = 480;                          // this is the 4.3" glass
                        DisplayVRes = 272;
                        SSD1963HorizPulseWidth = 41;
                        SSD1963HorizBackPorch = 2;
                        SSD1963HorizFrontPorch = 2;
                        SSD1963VertPulseWidth = 10;
                        SSD1963VertBackPorch = 2;
                        SSD1963VertFrontPorch = 2;
                        //Set LSHIFT freq, i.e. the DCLK with PLL freq 120MHz set previously
                        //Typical DCLK is 9MHz.  9MHz = 120MHz*(LCDC_FPR+1)/2^20.  LCDC_FPR = 78642 (0x13332)
                        SSD1963PClock1 = 0x01;
                        SSD1963PClock2 = 0x33;
                        SSD1963PClock3 = 0x32;
                        SSD1963Mode1 = 0x20;                        // 24-bit for 4.3" panel, data latch in rising edge for LSHIFT
                        SSD1963Mode2 = 0;                           // Hsync+Vsync mode
                        break;
        case SSD1963_5ER_16:
        	           SSD1963rgb=0b1000;
        case SSD1963_5_16:
        case SSD1963_5: DisplayHRes = 800;                          // this is the 5" glass
                        DisplayVRes = 480;
                        SSD1963HorizPulseWidth = 128;
                        SSD1963HorizBackPorch = 88;
                        SSD1963HorizFrontPorch = 40;
                        SSD1963VertPulseWidth = 2;
                        SSD1963VertBackPorch = 25;
                        SSD1963VertFrontPorch = 18;
                        //Set LSHIFT freq, i.e. the DCLK with PLL freq 120MHz set previously
                        //Typical DCLK is 33MHz.  30MHz = 120MHz*(LCDC_FPR+1)/2^20.  LCDC_FPR = 262143 (0x3FFFF)
                        SSD1963PClock1 = 0x03;
                        SSD1963PClock2 = 0xff;
                        SSD1963PClock3 = 0xff;
                        SSD1963Mode1 = 0x24;                        // 24-bit for 5" panel, data latch in falling edge for LSHIFT
                        SSD1963Mode2 = 0;                           // Hsync+Vsync mode
                        break;
        case SSD1963_5A_16:
        case SSD1963_5A: DisplayHRes = 800;                         // this is a 5" glass alternative version
                        DisplayVRes = 480;
                        SSD1963HorizPulseWidth = 128;
                        SSD1963HorizBackPorch = 88;
                        SSD1963HorizFrontPorch = 40;
                        SSD1963VertPulseWidth = 2;
                        SSD1963VertBackPorch = 25;
                        SSD1963VertFrontPorch = 18;
                        //Set LSHIFT freq, i.e. the DCLK with PLL freq 120MHz set previously
                        //Typical DCLK is 33MHz.  30MHz = 120MHz*(LCDC_FPR+1)/2^20.  LCDC_FPR = 262143 (0x3FFFF)
                        SSD1963PClock1 = 0x04;
                        SSD1963PClock2 = 0x93;
                        SSD1963PClock3 = 0xe0;
                        SSD1963Mode1 = 0x24;                        // 24-bit for 5" panel, data latch in falling edge for LSHIFT
                        SSD1963Mode2 = 0;                           // Hsync+Vsync mode
                        break;
        case SSD1963_7ER_16:
                	    SSD1963rgb=0b1000;
        case SSD1963_7_16:
        case SSD1963_7: DisplayHRes = 800;                          // this is the 7" glass
                        DisplayVRes = 480;
                        SSD1963HorizPulseWidth = 1;
                        SSD1963HorizBackPorch = 210;
                        SSD1963HorizFrontPorch = 45;
                        SSD1963VertPulseWidth = 1;
                        SSD1963VertBackPorch = 34;
                        SSD1963VertFrontPorch = 10;
                        //Set LSHIFT freq, i.e. the DCLK with PLL freq 120MHz set previously
                        //Typical DCLK is 33.3MHz(datasheet), experiment shows 30MHz gives a stable result
                        //30MHz = 120MHz*(LCDC_FPR+1)/2^20.  LCDC_FPR = 262143 (0x3FFFF)
                        //Time per line = (DISP_HOR_RESOLUTION+DISP_HOR_PULSE_WIDTH+DISP_HOR_BACK_PORCH+DISP_HOR_FRONT_PORCH)/30 us = 1056/30 = 35.2us
                        SSD1963PClock1 = 0x03;
                        SSD1963PClock2 = 0xff;
                        SSD1963PClock3 = 0xff;
                        SSD1963Mode1 = 0x10;                        // 18-bit for 7" panel
                        SSD1963Mode2 = 0x80;                        // TTL mode
                        break;
        case SSD1963_7A_16:
        case SSD1963_7A: DisplayHRes = 800;                         // this is a 7" glass alternative version (high brightness)
                        DisplayVRes = 480;
                        SSD1963HorizPulseWidth = 3;
                        SSD1963HorizBackPorch = 88;
                        SSD1963HorizFrontPorch = 37;
                        SSD1963VertPulseWidth = 3;
                        SSD1963VertBackPorch = 32;
                        SSD1963VertFrontPorch = 10;
                        SSD1963PClock1 = 0x03;
                        SSD1963PClock2 = 0xff;
                        SSD1963PClock3 = 0xff;
                        SSD1963Mode1 = 0x10;                        // 18-bit for 7" panel
                        SSD1963Mode2 = 0x80;                        // TTL mode
                        break;
        case SSD1963_8_16:
        case SSD1963_8: DisplayHRes = 800;                          // this is the 8" glass (not documented because the 40 pin connector is non standard)
                        DisplayVRes = 480;
                        SSD1963HorizPulseWidth = 1;
                        SSD1963HorizBackPorch = 210;
                        SSD1963HorizFrontPorch = 45;
                        SSD1963VertPulseWidth = 1;
                        SSD1963VertBackPorch = 34;
                        SSD1963VertFrontPorch = 10;
                        //Set LSHIFT freq, i.e. the DCLK with PLL freq 120MHz set previously
                        //Typical DCLK is 33.3MHz(datasheet), experiment shows 30MHz gives a stable result
                        //30MHz = 120MHz*(LCDC_FPR+1)/2^20.  LCDC_FPR = 262143 (0x3FFFF)
                        //Time per line = (DISP_HOR_RESOLUTION+DISP_HOR_PULSE_WIDTH+DISP_HOR_BACK_PORCH+DISP_HOR_FRONT_PORCH)/30 us = 1056/30 = 35.2us
                        SSD1963PClock1 = 0x03;
                        SSD1963PClock2 = 0xff;
                        SSD1963PClock3 = 0xff;
                        SSD1963Mode1 = 0x20;
                        SSD1963Mode2 = 0x00;
                        break;
        case ILI9341_8: 
        case ILI9341_16:
                        DisplayHRes=320;
                        DisplayVRes=240;
                        break;
        case ILI9486_16:
                        DisplayHRes=480;
                        DisplayVRes=320;
                        break;
        case IPS_4_16:                                              /***G.A***/
                        DisplayHRes=800;
                        DisplayVRes=480;
    }

    if(DISPLAY_LANDSCAPE) {
        VRes=DisplayVRes;
        HRes=DisplayHRes;
    } else {
        VRes=DisplayHRes;
        HRes=DisplayVRes;
    }
    if(Option.DISPLAY_TYPE > SSD_PANEL_8){
        SSD1963PixelInterface=3; //PIXEL data interface - 16-bit RGB565
        SSD1963PixelFormat=0b01010000; //PIXEL data interface RGB565
    } else {
        SSD1963PixelInterface=0; //PIXEL data interface - 8-bit
        SSD1963PixelFormat=0b01110000;	//PIXEL data interface 24-bit
    }

    // setup the pointers to the drawing primitives
    DrawRectangle= DrawRectangleSSD1963;
    DrawBitmap = DrawBitmapSSD1963;
    if(!(Option.DISPLAY_TYPE == ILI9341_8 || Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == IPS_4_16 ))ScrollLCD = ScrollSSD1963;
    else ScrollLCD=ScrollLCDSPI;
    DrawBuffer = DrawBufferSSD1963;
    ReadBuffer = ReadBufferSSD1963;
    if(SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16){
        DrawBLITBuffer= DrawBLITBufferSSD1963;
        ReadBLITBuffer = ReadBLITBufferSSD1963;
    } else {
        DrawBLITBuffer= DrawBufferSSD1963;
        ReadBLITBuffer = ReadBufferSSD1963;
    }
    DrawPixel = DrawPixelNormal;
    if(Option.DISPLAY_TYPE==ILI9341_8)InitILI9341_8();
    else if(Option.DISPLAY_TYPE==ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16  )InitILI9341();
    else if(Option.DISPLAY_TYPE == IPS_4_16) InitIPS_4_16();
    else InitSSD1963();
    SetFont(Option.DefaultFont);
    PromptFont = gui_font;
    PromptFC = gui_fcolour = Option.DefaultFC;
    PromptBC = gui_bcolour = Option.DefaultBC;
    ResetDisplay();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// The SSD1963 driver
//
////////////////////////////////////////////////////////////////////////////////////////////////////////


// Write a command byte to the SSD1963
void WriteComand(int cmd) {
    gpio_put_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)),(cmd<<SSD1963data));
    gpio_put(SSD1963_DC_GPPIN,0);
    gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
    gpio_put(SSD1963_DC_GPPIN,1);
}


// Write an 8 bit data word to the SSD1963
void WriteData(int data) {
    gpio_put_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)),(data<<SSD1963data));
    gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
}


// For the 100 pin chip write RGB colour over an 8 bit bus
void WriteColor(unsigned int c) {
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        gpio_put_masked64((0xFFFF<<SSD1963data),(c<<SSD1963data));
        gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
    } else {
        gpio_put_masked64((0xFF<<SSD1963data),(((c >> 16) & 0xFF)<<SSD1963data));
        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
        gpio_put_masked64((0xFF<<SSD1963data),(((c >> 8) & 0xFF)<<SSD1963data));
        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
        nop;gpio_put_masked64((0xFF<<SSD1963data),((c & 0xFF)<<SSD1963data));
        gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
    }
}

// The next two functions are used in the initial setup where the SSD1963 cannot respond to fast signals

// Slowly write a command byte to the SSD1963
static void WriteComandSlow(int cmd) {
    gpio_put_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)),(cmd<<SSD1963data));
    gpio_put(SSD1963_DC_GPPIN,0);
    gpio_put(SSD1963_WR_GPPIN,0);uSec(5);gpio_put(SSD1963_WR_GPPIN,1);
    gpio_put(SSD1963_DC_GPPIN,1);
}


// Slowly write an 8 bit data word to the SSD1963
void WriteDataSlow(int data) {
    gpio_put_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)),(data<<SSD1963data));
    gpio_put(SSD1963_WR_GPPIN,0);uSec(5);gpio_put(SSD1963_WR_GPPIN,1);
}


// Read a byte from the SSD1963
inline __attribute((always_inline)) unsigned int ReadData(void) {
    gpio_put(SSD1963_RD_GPPIN,0);nop;nop;nop;nop;nop;nop;gpio_put(SSD1963_RD_GPPIN,1);
    return (gpio_get_all64() & (Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)))>>SSD1963data;
}

unsigned int ReadDataIPS(void) {
    gpio_put(SSD1963_RD_GPPIN,0);nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;gpio_put(SSD1963_RD_GPPIN,1);
    return (gpio_get_all64() & (Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)))>>SSD1963data;
}

// Slowly read a byte from the SSD1963
unsigned int ReadDataSlow(void) {
    gpio_put(SSD1963_RD_GPPIN,0);
    uSec(2);
    gpio_put(SSD1963_RD_GPPIN,1);
    uSec(2);
    return (gpio_get_all64() & (Option.DISPLAY_TYPE>SSD_PANEL_8 ?(0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)))>>SSD1963data;
}



/*********************************************************************
* defines start/end coordinates for memory access from host to SSD1963
* also maps the start and end points to suit the orientation
********************************************************************/
void SetAreaSSD1963(int x1, int y1, int x2, int y2) {
    int start_x, start_y, end_x, end_y;

    switch(Option.DISPLAY_ORIENTATION) {
        case LANDSCAPE:
        case RLANDSCAPE: start_x = x1;
                         end_x = x2;
                         start_y = y1;
                         end_y = y2;
                         break;
        case PORTRAIT:
        case RPORTRAIT:  start_x = y1;
                         end_x = y2;
                         start_y = (DisplayVRes - 1) - x2;
                         end_y = (DisplayVRes - 1) - x1;
                         break;
        default: return;
    }

  WriteComand(CMD_SET_COLUMN);
  WriteData(start_x>>8);
  WriteData(start_x);
  WriteData(end_x>>8);
  WriteData(end_x);
  WriteComand(CMD_SET_PAGE);
  WriteData(start_y>>8);
  WriteData(start_y);
  WriteData(end_y>>8);
  WriteData(end_y);
}


/*********************************************************************
* Set a GPIO pin to state high(1) or low(0)
*
* PreCondition: Set the GPIO pin an output prior using this function
*
* Arguments: BYTE pin   -     LCD_RESET
*                         LCD_SPENA
*                         LCD_SPCLK
*                         LCD_SPDAT
*
*          BOOL state -   0 for low
*                         1 for high
*********************************************************************/
static void GPIO_WR(int pin, int state) {
  int _gpioStatus = 0;

  if(state==1)
      _gpioStatus = _gpioStatus|pin;
  else
      _gpioStatus = _gpioStatus&(~pin);

  WriteComand(CMD_SET_GPIO_VAL);                                 // Set GPIO value
  WriteData(_gpioStatus);
}


/*********************************************************************
* SetBacklight(BYTE intensity)
* Some boards may use of PWM feature of ssd1963 to adjust the backlight
* intensity and this function supports that.  However, most boards have
* a separate PWM input pin and that is also supported by using the variable
*  display_backlight intimer.c
*
* Input:    intensity = 0 (off) to 100 (full on)
*
* Note: The base frequency of PWM set to around 300Hz with PLL set to 120MHz.
*     This parameter is hardware dependent
********************************************************************/
void MIPS16 SetBacklightSSD1963(int intensity) {
  WriteComand(CMD_SET_PWM_CONF);                                   // Set PWM configuration for backlight control

  WriteData(0x0E);                                                  // PWMF[7:0] = 2, PWM base freq = PLL/(256*(1+5))/256 = 300Hz for a PLL freq = 120MHz
  WriteData((intensity * 255)/100);                                 // Set duty cycle, from 0x00 (total pull-down) to 0xFF (99% pull-up , 255/256)
  WriteData(0x01);                                                  // PWM enabled and controlled by host (mcu)
  WriteData(0x00);
  WriteData(0x00);
  WriteData(0x00);

    display_backlight = intensity/5;                                // this is used in timer.c
}

/*********************************************************************
* SetTearingCfg(BOOL state, BOOL mode)
* This function enable/disable tearing effect
*
* Input:    BOOL state -  1 to enable
*                         0 to disable
*         BOOL mode -     0:  the tearing effect output line consists
*                             of V-blanking information only
*                         1:  the tearing effect output line consists
*                             of both V-blanking and H-blanking info.
********************************************************************/
void SetTearingCfg(int state, int mode)
{
  if(state == 1) {
      WriteComand(CMD_SET_TEAR_ON);
      WriteData(mode&0x01);
  } else {
      WriteComand(0x34);
  }

}


/***********************************************************************************************************************************
* Function:  void InitSSD1963()
* Initialise SSD1963 for PCLK,    HSYNC, VSYNC etc
***********************************************************************************************************************************/
void MIPS16 InitILI9341_8(void){
    if(Option.SSD_RESET){
        PinSetBit(SSD1963_RESET_PIN, LATCLR);                              // reset the SSD1963
        uSec(10000);
        PinSetBit(SSD1963_RESET_PIN, LATSET);                              // release from reset state to sleep state
        uSec(100000);
    }
    WriteComand(0xEF);
    WriteData(0x03);
    WriteData(0x80);
    WriteData(0x02);

    WriteComand(0xCF);
    WriteData(0x00);
    WriteData(0XC1);
    WriteData(0X30);

    WriteComand(0xED);
    WriteData(0x64);
    WriteData(0x03);
    WriteData(0X12);
    WriteData(0X81);

    WriteComand(0xE8);
    WriteData(0x85);
    WriteData(0x00);
    WriteData(0x78);

    WriteComand(0xCB);
    WriteData(0x39);
    WriteData(0x2C);
    WriteData(0x00);
    WriteData(0x34);
    WriteData(0x02);

    WriteComand(0xF7);
    WriteData(0x20);

    WriteComand(0xEA);
    WriteData(0x00);
    WriteData(0x00);

    WriteComand(ILI9341_PWCTR1);    //Power control
    WriteData(0x23);   //VRH[5:0]

    WriteComand(ILI9341_PWCTR2);    //Power control
    WriteData(0x10);   //SAP[2:0];BT[3:0]

    WriteComand(ILI9341_VMCTR1);    //VCM control
    WriteData(0x3e);
    WriteData(0x28);

    WriteComand(ILI9341_VMCTR2);    //VCM control2
    WriteData(0x86);  //--

    WriteComand(ILI9341_PIXFMT);
    WriteData(0x66);

    WriteComand(ILI9341_FRMCTR1);
    WriteData(0x00);
    WriteData(0x13); // 0x18 79Hz, 0x1B default 70Hz, 0x13 100Hz

    WriteComand(ILI9341_DFUNCTR);    // Display Function Control
    WriteData(0x08);
    WriteData(0x82);
    WriteData(0x27);

    WriteComand(0xF2);    // 3Gamma Function Disable
    WriteData(0x00);

    WriteComand(ILI9341_GAMMASET);    //Gamma curve selected
    WriteData(0x01);

    WriteComand(ILI9341_GMCTRP1);    //Set Gamma
    WriteData(0x0F);
    WriteData(0x31);
    WriteData(0x2B);
    WriteData(0x0C);
    WriteData(0x0E);
    WriteData(0x08);
    WriteData(0x4E);
    WriteData(0xF1);
    WriteData(0x37);
    WriteData(0x07);
    WriteData(0x10);
    WriteData(0x03);
    WriteData(0x0E);
    WriteData(0x09);
    WriteData(0x00);

    WriteComand(ILI9341_GMCTRN1);    //Set Gamma
    WriteData(0x00);
    WriteData(0x0E);
    WriteData(0x14);
    WriteData(0x03);
    WriteData(0x11);
    WriteData(0x07);
    WriteData(0x31);
    WriteData(0xC1);
    WriteData(0x48);
    WriteData(0x08);
    WriteData(0x0F);
    WriteData(0x0C);
    WriteData(0x31);
    WriteData(0x36);
    WriteData(0x0F);

    WriteComand(ILI9341_SLPOUT);    //Exit Sleep
    WriteComand(ILI9341_MADCTL);    // Memory Access Control
    switch(Option.DISPLAY_ORIENTATION) {
        case LANDSCAPE:     WriteData(ILI9341_Landscape); break;
        case PORTRAIT:      WriteData(ILI9341_Portrait); break;
        case RLANDSCAPE:    WriteData(ILI9341_Landscape180); break;
        case RPORTRAIT:     WriteData(ILI9341_Portrait180); break;
    }
    WriteComand(ILI9341_DISPON);    //Display on
    uSec(100000);
    ClearScreen(Option.DefaultBC);
}

void MIPS16 InitSSD1963(void) {

    if(Option.SSD_RESET){
        PinSetBit(SSD1963_RESET_PIN, LATCLR);                              // reset the SSD1963
        uSec(10000);
        PinSetBit(SSD1963_RESET_PIN, LATSET);                              // release from reset state to sleep state
        uSec(100000);
    }

    // IMPORTANT: At this stage the SSD1963 is running at a slow speed and cannot respond to high speed commands
    //            So we use slow speed versions of WriteComand/WriteData with a 3 uS delay between each control signal change

  // Set MN(multipliers) of PLL, VCO = crystal freq * (N+1)
  // PLL freq = VCO/M with 250MHz < VCO < 800MHz
  // The max PLL freq is around 120MHz. To obtain 120MHz as the PLL freq

    WriteComandSlow(CMD_SET_PLL_MN);                                 // Set PLL with OSC = 10MHz (hardware), command is 0xE3
    WriteDataSlow(0x23);                                              // Multiplier N = 35, VCO (>250MHz)= OSC*(N+1), VCO = 360MHz
    WriteDataSlow(0x02);                                              // Divider M = 2, PLL = 360/(M+1) = 120MHz
    WriteDataSlow(0x54);                                              // Validate M and N values

    WriteComandSlow(CMD_PLL_START);                                  // Start PLL command
    WriteDataSlow(0x01);                                              // enable PLL

    uSec(1000);                                                       // wait for it to stabilise
    WriteComandSlow(CMD_PLL_START);                                  // Start PLL command again
    WriteDataSlow(0x03);                                              // now, use PLL output as system clock

    WriteComandSlow(CMD_SOFT_RESET);                                 // Soft reset
    uSec(10000);

#define parallel_write_data WriteData
#define TFT_Write_Data WriteData

  // Configure for the TFT panel, varies from individual manufacturer
    WriteComandSlow(CMD_SET_PCLK);                                   // set pixel clock (LSHIFT signal) frequency
    WriteDataSlow(SSD1963PClock1);                                    // paramaters set by DISPLAY INIT
    WriteDataSlow(SSD1963PClock2);
    WriteDataSlow(SSD1963PClock3);
        //    uSec(10000);


    // Set panel mode, varies from individual manufacturer
    WriteComand(CMD_SET_PANEL_MODE);
    WriteData(SSD1963Mode1);                                          // parameters set by DISPLAY INIT
    WriteData(SSD1963Mode2);
    WriteData((DisplayHRes - 1) >> 8);                                // Set panel size
    WriteData(DisplayHRes - 1);
    WriteData((DisplayVRes - 1) >> 8);
    WriteData(DisplayVRes - 1);
    WriteData(0x00);                                                  // RGB sequence


    // Set horizontal period
    WriteComand(CMD_SET_HOR_PERIOD);
    #define HT (DisplayHRes + SSD1963HorizPulseWidth + SSD1963HorizBackPorch + SSD1963HorizFrontPorch)
    WriteData((HT - 1) >> 8);
    WriteData(HT - 1);
    #define HPS (SSD1963HorizPulseWidth + SSD1963HorizBackPorch)
    WriteData((HPS - 1) >> 8);
    WriteData(HPS - 1);
    WriteData(SSD1963HorizPulseWidth - 1);
    WriteData(0x00);
    WriteData(0x00);
    WriteData(0x00);

    // Set vertical period
    WriteComand(CMD_SET_VER_PERIOD);
    #define VT (SSD1963VertPulseWidth + SSD1963VertBackPorch + SSD1963VertFrontPorch + DisplayVRes)
    WriteData((VT - 1) >> 8);
    WriteData(VT - 1);
    #define VSP (SSD1963VertPulseWidth + SSD1963VertBackPorch)
    WriteData((VSP - 1) >> 8);
    WriteData(VSP - 1);
    WriteData(SSD1963VertPulseWidth - 1);
    WriteData(0x00);
    WriteData(0x00);

	// Set pixel data interface
	WriteComand(CMD_SET_DATA_INTERFACE);
    WriteData(SSD1963PixelInterface);                                                // 8-bit colour format
	WriteComand(CMD_SET_PIXEL_FORMAT);
    WriteData(SSD1963PixelFormat);                                                // 8-bit colour format

    // initialise the GPIOs
    WriteComand(CMD_SET_GPIO_CONF);                                  // Set all GPIOs to output, controlled by host
    WriteData(0x0f);                                                  // Set GPIO0 as output
    WriteData(0x01);                                                  // GPIO[3:0] used as normal GPIOs

 // LL Reset to LCD!!!
    GPIO_WR(LCD_SPENA, 1);
    GPIO_WR(LCD_SPCLK, 1);
    GPIO_WR(LCD_SPDAT,1);
    GPIO_WR(LCD_RESET,1);
    GPIO_WR(LCD_RESET,0);
    uSec(1000);
    GPIO_WR(LCD_RESET,1);



    // setup the pixel write order
    WriteComand(CMD_SET_ADDR_MODE);
    switch(Option.DISPLAY_ORIENTATION) {
        case LANDSCAPE:     WriteData(SSD1963_LANDSCAPE  | SSD1963rgb); break;
        case PORTRAIT:      WriteData(SSD1963_PORTRAIT  | SSD1963rgb); break;
        case RLANDSCAPE:    WriteData(SSD1963_RLANDSCAPE  | SSD1963rgb); break;
        case RPORTRAIT:     WriteData(SSD1963_RPORTRAIT  | SSD1963rgb); break;
    }

    // Set the scrolling area
    WriteComand(CMD_SET_SCROLL_AREA);
    WriteData(0);
    WriteData(0);
    WriteData(DisplayVRes >> 8);
    WriteData(DisplayVRes);
    WriteData(0);
    WriteData(0);
    ScrollStart = 0;

    ClearScreen(Option.DefaultBC);
    SetBacklightSSD1963(Option.DefaultBrightness);
    WriteComand(CMD_ON_DISPLAY);                                     // Turn on display; show the image on display

}
void  SetAreaIPS_4_16(int xstart, int ystart, int xend, int yend, int rw) {
    if(HRes == 0) error("Display not configured");
    WriteCmdDataIPS_4_16(0x2A00,1,xstart>>8);
    WriteCmdDataIPS_4_16(0x2A01,1,xstart & 0xFF);
    WriteCmdDataIPS_4_16(0x2A02,1,xend>>8);
    WriteCmdDataIPS_4_16(0x2A03,1,xend & 0xFF);
    WriteCmdDataIPS_4_16(0x2B00,1,ystart>>8);
    WriteCmdDataIPS_4_16(0x2B01,1,ystart & 0xFF);
    WriteCmdDataIPS_4_16(0x2B02,1,yend>>8);
    WriteCmdDataIPS_4_16(0x2B03,1,yend & 0xFF);

    if(rw){
    	Write16bitCommand(0x2C00);  //write to memory
    } else {
    	Write16bitCommand(0x2E00);  //read from memory
    }
}

void  SetAreaILI9341(int xstart, int ystart, int xend, int yend, int rw) {
    if(HRes == 0) error("Display not configured");
    WriteComand(ILI9341_COLADDRSET);
    WriteData(xstart >> 8);
    WriteData(xstart);
    WriteData(xend >> 8);
    WriteData(xend);
    WriteComand(ILI9341_PAGEADDRSET);
    WriteData(ystart >> 8);
    WriteData(ystart);
    WriteData(yend >> 8);
    WriteData(yend);
    if(rw){
    	WriteComand(ILI9341_MEMORYWRITE);
    } else {
    	WriteComand(ILI9341_RAMRD);
    }
}
/**********************************************************************************************
Draw a filled rectangle on the video output with physical frame buffer coordinates
     x1, y1 - the start physical frame buffer coordinate
     x2, y2 - the end physical frame buffer coordinate
     c - the colour to use for both the fill
 This is only called by DrawRectangleSSD1963() below
***********************************************************************************************/
void PhysicalDrawRect(int x1, int y1, int x2, int y2, int c) {
    int i;
    if(Option.DISPLAY_TYPE == ILI9341_8){
        SetAreaILI9341(x1, y1 , x2, y2, 1);
    } else if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16) {
        if(Option.DISPLAY_TYPE == ILI9486_16){
            Write16bitCommand(ILI9341_PIXELFORMAT);
            WriteData16bit(0x55);
        }
    	SetAreaILI9341(x1, y1 , x2, y2, 1);
    } else if(Option.DISPLAY_TYPE==IPS_4_16) {
    	if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x55);
    	SetAreaIPS_4_16(x1, y1 , x2, y2, 1);
    } else {
        SetAreaSSD1963(x1, y1 , x2, y2);                                // setup the area to be filled
        WriteComand(CMD_WR_MEMSTART);
    }
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        c=((c>>8) & 0xf800) | ((c>>5) & 0x07e0) | ((c>>3) & 0x001f);
        i=(x2 - x1 + 1) * (y2 - y1 + 1);
        while(i--){
            gpio_put(SSD1963_WR_GPPIN,0);
            gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
            //nop;gpio_put(SSD1963_WR_GPPIN,0);
            nop;gpio_put(SSD1963_WR_GPPIN,1);
        }
    } else {
        for(i = (x2 - x1 + 1) * (y2 - y1 + 1); i > 0; i--)
        WriteColor(c);
    }
    if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x66);
    if(Option.DISPLAY_TYPE == ILI9486_16){
    	Write16bitCommand(ILI9341_PIXELFORMAT);
    	WriteData16bit(0x66);
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Drawing primitives used by the functions in GUI.c and Draw.c
//
////////////////////////////////////////////////////////////////////////////////////////////////////////


/**********************************************************************************************
Draw a filled rectangle on the video output with logical (MMBasic) coordinates
     x1, y1 - the start coordinate
     x2, y2 - the end coordinate
     c - the colour to use for both the fill
***********************************************************************************************/
void DrawRectangleSSD1963(int x1, int y1, int x2, int y2, int c) {
    int t;

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

    t = y2 - y1;                                                    // get the distance between the top and bottom

    // set y1 to the physical location in the frame buffer (only really has an effect when scrolling is in action)
    if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
        y1 = (y1 + (VRes - ScrollStart)) % VRes;
    else
        y1 = (y1 + ScrollStart) % VRes;
    y2 = y1 + t;                                                    // and set y2 to the same
    if(y2 >= VRes) {                                                // if the box splits over the frame buffer boundary
        PhysicalDrawRect(x1, y1, x2, VRes - 1, c);                  // draw the top part
        PhysicalDrawRect(x1, 0, x2, y2 - VRes , c);                 // and the bottom part
    } else
        PhysicalDrawRect(x1, y1, x2, y2, c);                        // the whole box is within the frame buffer - much easier
}
void DrawRectangle320(int x1, int y1, int x2, int y2, int c) {
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        y1*=2;
        y2=y2*2+1;
        HRes=720;
        VRes=480;
        x1=x1*2+80;
        x2=x2*2+81;
    } else {
        HRes=400;
        VRes=272;
        x1+=80;
        x2+=80;
        y1+=16;
        y2+=16;
        if(y1<16 && y2<16)return;
    }
    if(x1<80 && x2<80)return;
    if(x1<80)x1=80;
    if(x2<80)x2=80;
    DrawRectangleSSD1963(x1,y1,x2,y2,c);
    HRes=320;
    VRes=240;
}



// written by Peter Mather (matherp on the Back Shed forum)
void DrawBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p) {
    int i,t,toggle=0;
	unsigned int bl=0;
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
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

    t = y2 - y1;                                                    // get the distance between the top and bottom
    if(Option.DISPLAY_TYPE!=ILI9341_8){
        if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
            y1 = (y1 + (VRes - ScrollStart)) % VRes;
        else
            y1 = (y1 + ScrollStart) % VRes;
        y2 = y1 + t; 
    }                                                   // and set y2 to the same
    if(y2 >= VRes) {
        SetAreaSSD1963(x1, y1, x2, VRes - 1);                       // if the box splits over the frame buffer boundary
        WriteComand(CMD_WR_MEMSTART);
        for(i = (x2 - x1 + 1) * ((VRes - 1) - y1 + 1); i > 0; i--){
            c.rgbbytes[0] = *p++;                                   // this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            if(Option.DISPLAY_TYPE>SSD_PANEL_8) WriteColor(((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f));
            else WriteColor(c.rgb);
        }
        SetAreaSSD1963(x1, 0, x2, y2 - VRes );
        WriteComand(CMD_WR_MEMSTART);
        for(i = (x2 - x1 + 1) * (y2 - VRes + 1); i > 0; i--) {
            c.rgbbytes[0] = *p++;                                   // this order swaps the bytes to match the .BMP file
            c.rgbbytes[1] = *p++;
            c.rgbbytes[2] = *p++;
            if(Option.DISPLAY_TYPE>SSD_PANEL_8) WriteColor(((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f));
            else WriteColor(c.rgb);
        }
    } else {
        // the whole box is within the frame buffer - much easier
        if(Option.DISPLAY_TYPE==ILI9341_8 || Option.DISPLAY_TYPE == ILI9341_16  || Option.DISPLAY_TYPE == ILI9486_16 ) {
            if(Option.DISPLAY_TYPE==ILI9486_16)LCDAttrib=2;
            SetAreaILI9341(x1, y1 , x2, y2, 1);
        } else if(Option.DISPLAY_TYPE==IPS_4_16) {
            SetAreaIPS_4_16(x1, y1 , x2, y2, 1);
        } else {
            SetAreaSSD1963(x1, y1 , x2, y2);                                // setup the area to be filled
            WriteComand(CMD_WR_MEMSTART);
        }
        for(int y=y1;y<=y2;y++){
            for(int x=x1;x<=x2;x++){
                if(x>=0 && x<HRes && y>=0 && y<VRes){
                    c.rgbbytes[0] = *p++;                                   // this order swaps the bytes to match the .BMP file
                    c.rgbbytes[1] = *p++;
                    c.rgbbytes[2] = *p++;
                    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
                        if(LCDAttrib==0){
                            WriteColor(((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f));
                        } else {
                            if(toggle==0){
                                gpio_put_masked64(0xFFFF<<SSD1963data,(((c.rgb>>8) & 0xf800) | ((c.rgb>>8) & 0x00fc))<<SSD1963data);
                                nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                                bl=(c.rgb & 0x00f8); //save blue
                                toggle=1;
                            } else {
                                gpio_put_masked64(0xFFFF<<SSD1963data,((bl<<8) | ((c.rgb>>16) & 0x00f8))<<SSD1963data);
                                nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                                gpio_put_masked64(0xFFFF<<SSD1963data,(((c.rgb) & 0xfc00) | ((c.rgb) & 0x00f8))<<SSD1963data);
                                nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                                toggle=0;
                            }
                        }
                    } else WriteColor(c.rgb);
                } else p+=3;
            }
        }
        if(LCDAttrib==2)LCDAttrib=0;
        if((LCDAttrib==1) && (toggle==1)){
   			// extra packet needed
            gpio_put_masked64(0xFFFF<<SSD1963data,((bl<<8) | ((c.rgb>>8) & 0x00f8))<<SSD1963data);
            nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
        }
    }
}
void DrawBuffer320(int x1, int y1, int x2, int y2, unsigned char* p){
    int t;
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
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        HRes=720;
        VRes=480;
    } else {
        HRes=400;
        VRes=256;
    }
    unsigned char *q = buff320;
    for(int y=y1;y<=y2;y++){
        int yo=y*2;
        unsigned char *pp=q;
        if(Option.DISPLAY_TYPE!=SSD1963_4_16){
            for(int x=x1;x<=x2;x++){
                pp[0]=pp[3]=*p++;
                pp[1]=pp[4]=*p++;
                pp[2]=pp[5]=*p++;
                pp+=6;
            }
            DrawBufferSSD1963(x1*2+80,yo,x2*2+81,yo,q);
            DrawBufferSSD1963(x1*2+80,yo+1,x2*2+81,yo+1,q);
        } else {
            for(int x=x1;x<=x2;x++){
                pp[0]=*p++;
                pp[1]=*p++;
                pp[2]=*p++;
                pp+=3;
            }
            DrawBufferSSD1963(x1+80,y+16,x2+80,y+16,q);
        }

    }
    HRes=320;
    VRes=240;
}
void DrawBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p) {
    int i,t;
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
    uint16_t *pp = (uint16_t *)p;
    t = y2 - y1;                                                    // get the distance between the top and bottom
    if(Option.DISPLAY_TYPE!=ILI9341_8){
        if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
            y1 = (y1 + (VRes - ScrollStart)) % VRes;
        else
            y1 = (y1 + ScrollStart) % VRes;
        y2 = y1 + t; 
    }                                                   // and set y2 to the same
    if(y2 >= VRes) {
        SetAreaSSD1963(x1, y1, x2, VRes - 1);                       // if the box splits over the frame buffer boundary
        WriteComand(CMD_WR_MEMSTART);
        for(i = (x2 - x1 + 1) * ((VRes - 1) - y1 + 1); i > 0; i--){
            gpio_put(SSD1963_WR_GPPIN,0);
            gpio_put_masked64(0xFFFF<<SSD1963data,(*pp++)<<SSD1963data);
            nop;gpio_put(SSD1963_WR_GPPIN,1);
        }
        SetAreaSSD1963(x1, 0, x2, y2 - VRes );
        WriteComand(CMD_WR_MEMSTART);
        for(i = (x2 - x1 + 1) * (y2 - VRes + 1); i > 0; i--) {
            gpio_put(SSD1963_WR_GPPIN,0);
            gpio_put_masked64(0xFFFF<<SSD1963data,(*pp++)<<SSD1963data);
            nop;gpio_put(SSD1963_WR_GPPIN,1);
        }
    } else {
        // the whole box is within the frame buffer - much easier
        if(Option.DISPLAY_TYPE==IPS_4_16) {
            SetAreaIPS_4_16(x1, y1 , x2, y2, 1);
        }  else {
            SetAreaSSD1963(x1, y1 , x2, y2);                                // setup the area to be filled
            WriteComand(CMD_WR_MEMSTART);
        }
        for(int y=y1;y<=y2;y++){
            for(int x=x1;x<=x2;x++){
                if(x>=0 && x<HRes && y>=0 && y<VRes){
                    gpio_put(SSD1963_WR_GPPIN,0);
                    gpio_put_masked64(0xFFFF<<SSD1963data,(*pp++)<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,1);
                } else pp++;
            }
        }
    }
}
void DrawBLITBuffer320(int x1, int y1, int x2, int y2, unsigned char* p){
    int t;
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
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        HRes=720;
        VRes=480;
    } else {
        HRes=400;
        VRes=256;
    }
    unsigned char *q = buff320;
    for(int x=x1;x<=x2;x++){
        unsigned char *pp=q;
        if(Option.DISPLAY_TYPE!=SSD1963_4_16){
            for(int y=y1;y<=y2;y++){
                pp[0]=pp[2]=*p++;
                pp[1]=pp[3]=*p++;
                pp+=4;
            }
            DrawBLITBufferSSD1963(x*2+80,y1*2,x*2+80,y2*2,q);
            DrawBLITBufferSSD1963(x*2+81,y1*2,x*2+81,y2*2,q);
        } else {
            for(int y=y1;y<=y2;y++){
                *pp++=*p++;
                *pp++=*p++;
            }
            DrawBLITBufferSSD1963(x+80,y1+16,x+80,y2+16,q);
        }
    }
    HRes=320;
    VRes=240;
}
// Read RGB colour over an 8 bit bus
inline __attribute((always_inline)) unsigned int ReadColor(void) {
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        uint32_t d=ReadData();
        return  ((d & 0xf800)<<8) | ((d & 0x7E0)<<5) | ((d & 0x1f)<<3);
    } else {
        return(ReadData() << 16) | (ReadData() << 8) | ReadData();
    }
}


// Read RGB colour over an 8 bit bus
// but do it slowly to avoid timing issues with the first pixel
unsigned int ReadColorSlow(void) {
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        uint32_t d=ReadDataSlow();
        return  ((d & 0xf800)<<8) | ((d & 0x7E0)<<5) | ((d & 0x1f)<<3);
    } else return(ReadDataSlow() << 16) | (ReadDataSlow() << 8) | ReadDataSlow();
}


// written by Peter Mather (matherp on the Back Shed forum)
void ReadBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p) {
    int i, t;
	int toggle=0,t1=0, nr=0 ;
    union colourmap {
      char rgbbytes[4];
      unsigned int rgb;
    } c;
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

    t = y2 - y1;                                                    // get the distance between the top and bottom
    if(!(Option.DISPLAY_TYPE==ILI9341_8 || Option.DISPLAY_TYPE == IPS_4_16 || Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16 )){
        if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
            y1 = (y1 + (VRes - ScrollStart)) % VRes;
        else
            y1 = (y1 + ScrollStart) % VRes;
        y2 = y1 + t;  
    }                                                  // and set y2 to the same

    if(y2 >= VRes) {
        SetAreaSSD1963(x1, y1, x2, VRes - 1);                       // if the box splits over the frame buffer boundary
        WriteComand(CMD_RD_MEMSTART);
        gpio_set_dir_in_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        i=(x2 - x1 + 1) * ((VRes - 1) - y1 + 1);
        uSec(10);
        for( ; i > 1; i--) {                                        // NB loop counter terminates 1 pixel earlier
            c.rgb = ReadColor();
            *p++ = c.rgbbytes[0];                                   // this order swaps the bytes to match the .BMP file
            *p++ = c.rgbbytes[1];
            *p++ = c.rgbbytes[2];
        }
        gpio_set_dir_out_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        uSec(10);
        SetAreaSSD1963(x1, 0, x2, y2 - VRes );
        WriteComand(CMD_RD_MEMSTART);
        gpio_set_dir_in_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        uSec(10);
         for(i = (x2 - x1 + 1) * (y2 - VRes + 1); i > 1; i--) {     // NB loop counter terminates 1 pixel earlier
            c.rgb = ReadColor();
            *p++ = c.rgbbytes[0];                                   // this order swaps the bytes to match the .BMP file
            *p++ = c.rgbbytes[1];
            *p++ = c.rgbbytes[2];
        }
        gpio_set_dir_out_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        uSec(10);
    } else {
        if(Option.DISPLAY_TYPE == ILI9341_8){
            Write16bitCommand(ILI9341_PIXELFORMAT) ; 
            WriteData16bit(0x66);
            SetAreaILI9341(x1, y1 , x2, y2, 0);
        } else if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16) {
            if(Option.DISPLAY_TYPE == ILI9486_16){
                Write16bitCommand(ILI9341_PIXELFORMAT);
                WriteData16bit(0x66);
            }
            SetAreaILI9341(x1, y1 , x2, y2, 0);
        } else if(Option.DISPLAY_TYPE==IPS_4_16) {
            if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x66);
            SetAreaIPS_4_16(x1, y1 , x2, y2, 0);
        } else {
            SetAreaSSD1963(x1, y1 , x2, y2);                                // setup the area to be filled
            WriteComand(CMD_RD_MEMSTART);
        }
        
        gpio_set_dir_in_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ?(0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        uSec(2);
        if(Option.DISPLAY_TYPE==ILI9341_8 || Option.DISPLAY_TYPE==ILI9341_16 || Option.DISPLAY_TYPE == IPS_4_16 )ReadDataSlow();
        for(i = (x2 - x1 + 1) * (y2 - y1 + 1); i > 0; i--){
            if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16  ) {
                if(toggle==0){
                    t=ReadData();
                    t<<=8 ;
                    t1=ReadData(); 
                    t|=(t1>>8);
                    t1 &=0xFF;
                    toggle=1;
                } else {
                    t=ReadData(); 
                t|= (t1<<16);
                toggle=0;
                }
                *p++=(t & 0xf8); *p++=(t & 0xfc00)>>8; *p++=(t & 0xf80000)>>16;

            } else if(Option.DISPLAY_TYPE==IPS_4_16) {
                if(toggle==0){     //RGBR 8bit each
                    t=ReadDataIPS(); 
                    t1=ReadDataIPS(); 

                    *p++=(t1 & 0xF800)>>8;  //BLUE
                    *p++=(t & 0xFC);      //GREEN
                    *p++=(t & 0xF800)>>8;   //RED

                    nr=(t1 & 0xF8);       //save next red

                    if(LCDAttrib==1){  //NT35510 does not need toggle=1
                        toggle=0;
                    }else{
                        toggle=1;
                	}
                } else {
                    t=ReadDataIPS(); //get the second  GB
                    *p++=(t & 0xF8);       //Blue
                    *p++=(t & 0xFC00)>>8;  //Green   FIX  HERE
                    *p++=nr ;              //add the red

                    toggle=0;
                }
            } else {
                c.rgb=ReadColor();
                *p++=c.rgbbytes[0];                                     // this order swaps the bytes to match the .BMP file
                *p++=c.rgbbytes[1];
                *p++=c.rgbbytes[2];
            }
        }
        gpio_set_dir_out_masked64((Option.DISPLAY_TYPE>SSD_PANEL_8 ? (0xFFFF<<SSD1963data) : (0xFF<<SSD1963data)));
        if(Option.DISPLAY_TYPE==ILI9341_16){
            Write16bitCommand(ILI9341_PIXELFORMAT) ; 
            WriteData16bit(0x55);
        }
        uSec(10);
    }
}
void ReadBuffer320(int x1, int y1, int x2, int y2, unsigned char* p) {
    int t;
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
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        HRes=720;
        VRes=480;
    } else {
        HRes=400;
        VRes=256;
    }
    unsigned char *q = buff320;
    for(int y=y1;y<=y2;y++){
        int yo=y*2;
        if(Option.DISPLAY_TYPE!=SSD1963_4_16)ReadBufferSSD1963(x1*2+80,yo,x2*2+81,yo,q);
        else ReadBufferSSD1963(x1+80,y+16,x2+80,y+16,q);
        unsigned char *pp=q;
        for(int x=x1;x<=x2;x++){
            *p++=*pp++;
            *p++=*pp++;
            *p++=*pp++;
            pp+=3;
        }
    }
    HRes=320;
    VRes=240;
}
void ReadBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char* p) {
    int i, t;
	int toggle=0,t1=0, nr=0 ;
    union colourmap {
      char rgbbytes[4];
      unsigned int rgb;
    } c;
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
    uint16_t *pp=(uint16_t *)p;
    t = y2 - y1;                                                    // get the distance between the top and bottom
    if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
        y1 = (y1 + (VRes - ScrollStart)) % VRes;
    else
        y1 = (y1 + ScrollStart) % VRes;
    y2 = y1 + t;  

    if(y2 >= VRes) {
        SetAreaSSD1963(x1, y1, x2, VRes - 1);                       // if the box splits over the frame buffer boundary
        WriteComand(CMD_RD_MEMSTART);
        gpio_set_dir_in_masked64(0xFFFF<<SSD1963data);
        i=(x2 - x1 + 1) * ((VRes - 1) - y1 + 1);
        uSec(2);
        for( ; i > 1; i--) {                                        // NB loop counter terminates 1 pixel earlier
            gpio_put(SSD1963_RD_GPPIN,0);nop;nop;nop;nop;nop;nop;gpio_put(SSD1963_RD_GPPIN,1);
            *pp++ =  ((gpio_get_all64() & (0xFFFF<<SSD1963data))>>SSD1963data);
        }
        gpio_set_dir_out_masked64(0xFFFF<<SSD1963data);
        uSec(2);
        SetAreaSSD1963(x1, 0, x2, y2 - VRes );
        WriteComand(CMD_RD_MEMSTART);
        gpio_set_dir_in_masked64(0xFFFF);
        uSec(2);
        for(i = (x2 - x1 + 1) * (y2 - VRes + 1); i > 1; i--) {     // NB loop counter terminates 1 pixel earlier
            gpio_put(SSD1963_RD_GPPIN,0);nop;nop;nop;nop;nop;nop;gpio_put(SSD1963_RD_GPPIN,1);
            *pp++ =  ((gpio_get_all64() & (0xFFFF<<SSD1963data))>>SSD1963data);
        }
        gpio_set_dir_out_masked64(0xFFFF<SSD1963data);
        uSec(2);
    } else {
        if(Option.DISPLAY_TYPE==IPS_4_16) {
            if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x66);
            SetAreaIPS_4_16(x1, y1 , x2, y2, 0);
            ReadDataSlow();
        } else {
            SetAreaSSD1963(x1, y1 , x2, y2);                                // setup the area to be filled
            WriteComand(CMD_RD_MEMSTART);
        }
        gpio_set_dir_in_masked64(0xFFFF<<SSD1963data);
        uSec(2);
        for(i = (x2 - x1 + 1) * (y2 - y1 + 1); i > 0; i--){
            if(Option.DISPLAY_TYPE==IPS_4_16) {
                if(toggle==0){     //RGBR 8bit each
                    t=ReadDataIPS(); 
                    t1=ReadDataIPS(); 

                    c.rgbbytes[0]=(t1 & 0xF800)>>8;  //BLUE
                    c.rgbbytes[1]=(t & 0xFC);      //GREEN
                    c.rgbbytes[2]=(t & 0xF800)>>8;   //RED
                    *pp++=((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f);
                    nr=(t1 & 0xF8);       //save next red

                    if(LCDAttrib==1){  //NT35510 does not need toggle=1
                        toggle=0;
                    }else{
                        toggle=1;

                	}
                } else {
                    t=ReadDataIPS(); //get the second  GB
                    c.rgbbytes[0]=(t & 0xF8);       //Blue
                    c.rgbbytes[1]=(t & 0xFC00)>>8;  //Green   FIX  HERE
                    c.rgbbytes[2]=nr ;              //add the red
                    *pp++=((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f);
                    toggle=0;
                }
            } else {
                gpio_put(SSD1963_RD_GPPIN,0);nop;nop;nop;nop;nop;nop;gpio_put(SSD1963_RD_GPPIN,1);
                *pp++ =  (gpio_get_all64() & (0xFFFF<<SSD1963data))>>SSD1963data;
            }
        }
        gpio_set_dir_out_masked64(0xFFFF<<SSD1963data);
    }
}
void ReadBLITBuffer320(int x1, int y1, int x2, int y2, unsigned char* p) {
    int t;
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
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        HRes=720;
        VRes=480;
    } else {
        HRes=400;
        VRes=256;
    }
    unsigned char *q = buff320;
    for(int x=x1;x<=x2;x++){
        if(Option.DISPLAY_TYPE!=SSD1963_4_16)ReadBLITBufferSSD1963(x*2+80,y1*2,x*2+80,y2*2,q);
        else ReadBLITBufferSSD1963(x1+80,y1+16,x2+80,y2+16,q);
        unsigned char *pp=q;
        for(int y=y1;y<=y2;y++){
            *p++=*pp++;
            *p++=*pp++;
            if(Option.DISPLAY_TYPE!=SSD1963_4_16)pp+=2;
        }
    }
    HRes=320;
    VRes=240;
}
/*  @endcond */

void MIPS16 fun_getscanline(void){
    if(Option.DISPLAY_TYPE < SSDPANEL && !(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488)) {
        iret=-1;
        targ = T_INT;
    }
    if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488){
        iret=GetLineILI9341();
        targ = T_INT;
    } else {
        WriteComand(CMD_GET_SCANLINE);
        gpio_set_dir_in_masked64(0xFF<<SSD1963data);
        iret = (ReadData() << 8) | ReadData();                          // get the scan line
        gpio_set_dir_out_masked64(0xFF<<SSD1963data);
        targ = T_INT;
    }
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */


/***********************************************************************************************
Print the bitmap of a char on the video output
Modifications by Peter Mather (matherp on the Back Shed forum) to support transparent text
  x1, y1 - the top left of the char
    width, height - size of the char's bitmap
    scale - how much to scale the bitmap
  fg, bg - foreground and background colour
    bitmap - pointer to the butmap
***********************************************************************************************/
void DrawBitmapSSD1963(int x1, int y1, int width, int height, int scale, int fg, int bg, unsigned char *bitmap ){
    int i, j, k, m, y, yt, n ;
    int vertCoord, horizCoord, XStart, XEnd;
    char *buff=NULL;
    union car {
        char rgbbytes[4];
        unsigned int rgb;
    } c;
    if(Option.DISPLAY_TYPE>SSD_PANEL_8){
        fg=((fg>>8) & 0xf800) | ((fg>>5) & 0x07e0) | ((fg>>3) & 0x001f);
        if(bg!=-1)bg=((bg>>8) & 0xf800) | ((bg>>5) & 0x07e0) | ((bg>>3) & 0x001f);
    }

    // adjust when part of the bitmap is outside the displayable coordinates
    vertCoord = y1; if(y1 < 0) y1 = 0;                                 // the y coord is above the top of the screen
    XStart = x1; if(XStart < 0) XStart = 0;                            // the x coord is to the left of the left marginn
    XEnd = x1 + (width * scale) - 1; if(XEnd >= HRes) XEnd = HRes - 1; // the width of the bitmap will extend beyond the right margin
    if(bg == -1) {
        buff = GetMemory(width * height * scale * scale * 3 );
        ReadBuffer(XStart, y1, XEnd, (y1 + (height * scale) - 1) , (unsigned char *)buff);
        n = 0;
    }

    // set y and yt to the physical location in the frame buffer (only is important when scrolling is in action)
    if(Option.DISPLAY_ORIENTATION == RLANDSCAPE)
        yt = y = (y1 + (VRes - ScrollStart)) % VRes;
    else
        yt = y = (y1 + ScrollStart) % VRes;

    if(Option.DISPLAY_TYPE == ILI9341_8){
        SetAreaILI9341(XStart, y, XEnd, (y + (height * scale) - 1)  % VRes, 1);
    } else if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16) {
        if(Option.DISPLAY_TYPE == ILI9486_16){
            Write16bitCommand(ILI9341_PIXELFORMAT);
            WriteData16bit(0x55);
        }
    	SetAreaILI9341(XStart, y, XEnd, (y + (height * scale) - 1)  % VRes, 1);
    } else if(Option.DISPLAY_TYPE==IPS_4_16) {
    	if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x55);
    	SetAreaIPS_4_16(XStart, y, XEnd, (y + (height * scale) - 1)  % VRes, 1);
    } else {
        SetAreaSSD1963(XStart, y, XEnd, (y + (height * scale) - 1)  % VRes);                                // setup the area to be filled
        WriteComand(CMD_WR_MEMSTART);
    }
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            if(vertCoord++ < 0) continue;                           // we are above the top of the screen
            if(vertCoord > VRes) {                                  // we have extended beyond the bottom of the screen
              if(buff != NULL) FreeMemory((unsigned char *)buff);
              return;
            }
            // if we have scrolling in action we could run over the end of the frame buffer
            // if so, terminate this area and start a new one at the top of the frame buffer
            if(y++ == VRes) {
                if(Option.DISPLAY_TYPE == ILI9341_8){
                    SetAreaILI9341(XStart, 0, XEnd, ((yt + (height * scale) - 1)  % VRes) - y, 1);
                } else if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16) {
                    if(Option.DISPLAY_TYPE == ILI9486_16){
                        Write16bitCommand(ILI9341_PIXELFORMAT);
                        WriteData16bit(0x55);
                    }
                    SetAreaILI9341(XStart, 0, XEnd, ((yt + (height * scale) - 1)  % VRes) - y, 1);
                } else if(Option.DISPLAY_TYPE==IPS_4_16) {
                    if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x55);
                    SetAreaIPS_4_16(XStart, 0, XEnd, ((yt + (height * scale) - 1)  % VRes) - y, 1);
                } else {
                    SetAreaSSD1963(XStart, 0, XEnd, ((yt + (height * scale) - 1)  % VRes) - y);                                // setup the area to be filled
                    WriteComand(CMD_WR_MEMSTART);
                }
            }
            horizCoord = x1;
                // optimise by dedicating the code to just writing to the 100 pin chip
                for(k = 0; k < width; k++) {                        // step through each bit in a scan line
                    for(m = 0; m < scale; m++) {                    // repeat pixels to scale in the x axis
                        if(horizCoord++ < 0) continue;              // we have not reached the left margin
                        if(horizCoord > HRes) continue;             // we are beyond the right margin
                        if((bitmap[((i * width) + k)/8] >> (7 - (((i * width) + k) % 8))) & 1)
//                        if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1)
                            WriteColor(fg);
                        else {
                            if(buff != NULL){
                                c.rgbbytes[0] = buff[n];
                                c.rgbbytes[1] = buff[n+1];
                                c.rgbbytes[2] = buff[n+2];
                                if(Option.DISPLAY_TYPE>SSD_PANEL_8)bg=((c.rgb>>8) & 0xf800) | ((c.rgb>>5) & 0x07e0) | ((c.rgb>>3) & 0x001f);
                                else bg=c.rgb;
                            } 
                            WriteColor(bg);
                        }
                        n+=3;
                    }
                }
        }
    }
    if(buff != NULL) FreeMemory((unsigned char *)buff);
}

void DrawBitmap320(int x1, int y1, int width, int height, int scale, int fg, int bg, unsigned char *bitmap ){
    if(Option.DISPLAY_TYPE!=SSD1963_4_16){
        y1*=2;
        scale *=2;
        HRes=720;
        VRes=480;
        x1=x1*2+80;
    } else {
        x1+=80;
        y1+=16;
        HRes=400;
        VRes=256;
    }
    ReadBuffer=ReadBufferSSD1963;
    if(x1<80){
        unsigned char *p=GetTempMemory((80-x1)*height*scale*2);
        ReadBLITBufferSSD1963(x1,y1,79,y1+(height*scale-1),p);
        DrawBitmapSSD1963(x1, y1, width, height, scale, fg, bg, bitmap );
        DrawBLITBufferSSD1963(x1,y1,79,y1+(height*scale-1),p);
    } else DrawBitmapSSD1963(x1, y1, width, height, scale, fg, bg, bitmap );
    ReadBuffer=ReadBuffer320;
    HRes=320;
    VRes=240;

}
/**********************************************************************************************
 Scroll the image by a number of scan lines
 Will only work in landscape or reverse landscape
 lines - the number of scan lines to scroll
        if positive the display will scroll up
        if negative it will scroll down
***********************************************************************************************/
void ScrollSSD1963(int lines) {
    int t;

    t = ScrollStart;

    if(lines >= 0) {
        DrawRectangleSSD1963(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the line to be scrolled off
        while(lines--) {
            if(Option.DISPLAY_ORIENTATION == LANDSCAPE) {
                if(++t >= VRes) t = 0;
            } else if(Option.DISPLAY_ORIENTATION == RLANDSCAPE) {
                if(--t < 0) t = VRes - 1;
            }
        }
    } else {
        while(lines++) {
            if(Option.DISPLAY_ORIENTATION == LANDSCAPE) {
                if(--t < 0) t = VRes - 1;
            } else if(Option.DISPLAY_ORIENTATION == RLANDSCAPE) {
                if(++t >= VRes) t = 0;
            }
        }
        DrawRectangleSSD1963(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the line introduced at the top
    }

  WriteComand(CMD_SET_SCROLL_START);
  WriteData(t >> 8);
  WriteData(t);

    ScrollStart = t;
}
/*  @endcond */

