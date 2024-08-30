/***********************************************************************************************************************
PicoMite MMBasic

Touch.c

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


#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/structs/systick.h"
#ifdef PICOMITEWEB
#include "pico/cyw43_arch.h"
#endif
#ifndef PICOMITEWEB
#include "pico/multicore.h"
extern mutex_t	frameBufferMutex;
#endif

int GetTouchValue(int cmd);
void TDelay(void);

// these are defined so that the state of the touch PEN IRQ can be determined with the minimum of CPU cycles
volatile unsigned int TouchIrqPortAddr = 0;
int TouchIrqPortBit;
int TOUCH_IRQ_PIN;
int TOUCH_CS_PIN;
int TOUCH_Click_PIN;
int TOUCH_GETIRQTRIS=0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// configure the touch parameters (chip select pin and the IRQ pin)
// this is called by the OPTION TOUCH command
void MIPS16 ConfigTouch(unsigned char *p) {
    int pin1, pin2, pin3=0;
    getargs(&p, 5, (unsigned char *)",");
    if(!Option.SYSTEM_CLK)error("System SPI not configured");
    if(!(argc == 3 || argc == 5)) error("Argument count");
	unsigned char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin1 = getinteger(argv[0]);
	if(!code)pin1=codemap(pin1);
	if(IsInvalidPin(pin1)) error("Invalid pin");
	if(!(code=codecheck(argv[2])))argv[2]+=2;
	pin2 = getinteger(argv[2]);
	if(!code)pin2=codemap(pin2);
	if(IsInvalidPin(pin2)) error("Invalid pin");

    if(argc == 5) {
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        pin3 = getinteger(argv[4]);
        if(!code)pin3=codemap(pin3);
        if(IsInvalidPin(pin3)) error("Invalid pin");
    }
    if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
    if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
	if(pin3)
        if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
    Option.TOUCH_CS = pin1;
    Option.TOUCH_IRQ = pin2;
    Option.TOUCH_Click = pin3;
    Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;                    // record the touch feature as not calibrated
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// setup touch based on the settings saved in flash
void MIPS16 InitTouch(void) {
    if(!Option.TOUCH_CS) return;
    GetTouchValue(CMD_PENIRQ_ON);                                   // send the controller the command to turn on PenIRQ
    TOUCH_GETIRQTRIS = 1;
    GetTouchAxis(CMD_MEASURE_X);
    TouchIrqPortAddr = 1;

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// this function is only used in calibration
// it draws the target, waits for the touch to stabilise and returns the x and y in raw touch controller numbers (ie, not scaled)
void MIPS16 GetCalibration(int x, int y, int *xval, int *yval) {
    int i, j;
    #define TCAL_FONT    0x02

    ClearScreen(BLACK);
    GUIPrintString(HRes/2, VRes/2 - GetFontHeight(TCAL_FONT)/2, TCAL_FONT, JUSTIFY_CENTER, JUSTIFY_MIDDLE, 0, WHITE, BLACK, "Touch Target");
    GUIPrintString(HRes/2, VRes/2 + GetFontHeight(TCAL_FONT)/2, TCAL_FONT, JUSTIFY_CENTER, JUSTIFY_MIDDLE, 0, WHITE, BLACK, "and Hold");
    DrawLine(x - (TARGET_OFFSET * 3)/4, y, x + (TARGET_OFFSET * 3)/4, y, 1, WHITE);
    DrawLine(x, y - (TARGET_OFFSET * 3)/4, x, y + (TARGET_OFFSET * 3)/4, 1, WHITE);
    DrawCircle(x, y, TARGET_OFFSET/2, 1, WHITE, -1, 1);
    while(!TOUCH_DOWN) CheckAbort();                                // wait for the touch
    for(i = j = 0; i < 50; i++) {                                   // throw away the first 50 reads as rubbish
        GetTouchAxis(CMD_MEASURE_X); GetTouchAxis(CMD_MEASURE_Y);
    }

    // make a lot of readings and average them
    for(i = j = 0; i < 50; i++) j += GetTouchAxis(CMD_MEASURE_X);
    *xval = j/50;
    for(i = j = 0; i < 50; i++) j += GetTouchAxis(CMD_MEASURE_Y);
    *yval = j/50;

    ClearScreen(BLACK);
    while(TOUCH_DOWN) CheckAbort();                                 // wait for the touch to be lifted
    uSec(25000);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// this is the main function to call to get a touch reading
// if y is true the y reading will be returned, otherwise the x reading
// this function does noise reduction and scales the reading to pixels
// a return of TOUCH_ERROR means that the pen is not down
int __not_in_flash_func(GetTouch)(int y) {
    int i;
//    static int lastx, lasty;

    if(Option.TOUCH_CS == 0) error("Touch option not set");
    if(!Option.TOUCH_XZERO && !Option.TOUCH_YZERO) error("Touch not calibrated");
        if(PinRead(Option.TOUCH_IRQ)) return TOUCH_ERROR;
        if(y) {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY? CMD_MEASURE_X:CMD_MEASURE_Y) - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE);
//            if(i < lasty - CAL_ERROR_MARGIN || i > lasty + CAL_ERROR_MARGIN) { lasty = i; i = TOUCH_ERROR; }
        } else {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY? CMD_MEASURE_Y:CMD_MEASURE_X) - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);
//            if(i < lastx - CAL_ERROR_MARGIN || i > lastx + CAL_ERROR_MARGIN) { lastx = i; i = TOUCH_ERROR; }
        }
	if(i < 0 || i >= (y ? VRes : HRes))i=TOUCH_ERROR;
    return i;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// this will get a reading from a single axis
// the returned value is not scaled, it is the raw number produced by the touch controller
// it takes multiple readings, discards the outliers and returns the average of the medium values
int __not_in_flash_func(GetTouchAxis)(int cmd) {
    int i, j, t, b[TOUCH_SAMPLES];
    TOUCH_GETIRQTRIS=0;
    PinSetBit(Option.TOUCH_IRQ, CNPDSET);                           // Set the PenIRQ to an output
#ifdef PICOMITE
    if(SPIatRisk)mutex_enter_blocking(&frameBufferMutex);			// lock the frame buffer
#endif
    GetTouchValue(cmd);
    // we take TOUCH_SAMPLES readings and sort them into descending order in buffer b[].
    for(i = 0; i < TOUCH_SAMPLES; i++) {
        b[i] = GetTouchValue(cmd);                                  // get the value
        if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC || CurrentlyPlaying == P_MIDI || CurrentlyPlaying == P_MP3){
#ifdef PICOMITE
            if(SPIatRisk)mutex_enter_blocking(&frameBufferMutex);			// lock the frame buffer
#endif
            checkWAVinput();
#ifdef PICOMITE
            if(SPIatRisk)mutex_exit(&frameBufferMutex);
#endif
        }
        if(CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_STREAM ) checkWAVinput();
        for(j = i; j > 0; j--) {                                    // and sort into position
            if(b[j - 1] < b[j]) {
                t = b[j - 1];
                b[j - 1] = b[j];
                b[j] = t;
            }
            else
                break;
        }
    }

    // we then discard the top TOUCH_DISCARD samples and the bottom TOUCH_DISCARD samples and add up the remainder
    for(j = 0, i = TOUCH_DISCARD; i < TOUCH_SAMPLES - TOUCH_DISCARD; i++) j += b[i];

    // and return the average
    i = j / (TOUCH_SAMPLES - (TOUCH_DISCARD * 2));
    GetTouchValue(CMD_PENIRQ_ON);                                   // send the command to turn PenIRQ on
    PinSetBit(Option.TOUCH_IRQ, CNPUSET);                           // Set the PenIRQ to an input
    TOUCH_GETIRQTRIS=1;
#ifdef PICOMITE
    if(SPIatRisk)mutex_exit(&frameBufferMutex);
#endif
    return i;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// this will get a single reading from the touch controller
//
// it assumes that PenIRQ line has been pulled low and that the SPI baudrate is correct
// this takes 260uS at 120MHz
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int __not_in_flash_func(GetTouchValue)(int cmd) {
    int val;
    unsigned int lb, hb;
	if(!SSDTYPE)SPISpeedSet(TOUCH);
    else SPISpeedSet(SLOWTOUCH);
    if(Option.CombinedCS){
        gpio_put(TOUCH_CS_PIN,GPIO_PIN_SET);
        gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);
    } else gpio_put(TOUCH_CS_PIN,GPIO_PIN_RESET);  // set CS low
    TDelay();
    val=xchg_byte(cmd);    //    SpiChnPutC(TOUCH_SPI_CHANNEL, cmd);
    hb=xchg_byte(0);                             // send the read command (also selects the axis)
	val = (hb & 0b1111111) << 5;         // the top 7 bits
    lb=xchg_byte(0);                             // send the read command (also selects the axis)
    val |= (lb >> 3) & 0b11111;          // the bottom 5 bits
    if(Option.CombinedCS)gpio_set_dir(TOUCH_CS_PIN, GPIO_IN);
    else ClearCS(Option.TOUCH_CS);
    #ifdef PICOMITEWEB
            ProcessWeb(1);
    #endif
   return val;
}


void __not_in_flash_func(TDelay)(void)     // provides a small (~200ns) delay for the touch screen controller.
{
    int ticks_per_millisecond=ticks_per_second/1000;
   	int T=16777215 + setuptime-((4*ticks_per_millisecond)/20000) ;
    shortpause(T);
}


// the MMBasic TOUCH() function
void fun_touch(void) {
    if(checkstring(ep, (unsigned char *)"X"))
        iret = GetTouch(GET_X_AXIS);
    else if(checkstring(ep, (unsigned char *)"Y"))
        iret = GetTouch(GET_Y_AXIS);
#ifndef PICOMITEWEB
    else if(checkstring(ep, (unsigned char *)"REF"))
        iret = CurrentRef;
    else if(checkstring(ep, (unsigned char *)"LASTREF"))
        iret = LastRef;
    else if(checkstring(ep, (unsigned char *)"LASTX"))
        iret = LastX;
    else if(checkstring(ep, (unsigned char *)"LASTY"))
        iret = LastY;
    else if(checkstring(ep, (unsigned char *)"DOWN"))
        iret = TOUCH_DOWN;
    else if(checkstring(ep, (unsigned char *)"UP"))
        iret = !TOUCH_DOWN;
#endif        
    else
        error("Invalid argument");

    targ = T_INT;
}

