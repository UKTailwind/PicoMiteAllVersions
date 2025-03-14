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
/** @file Touch.c
* @author Geoff Graham, Peter Mather
* @brief Source for the MMBasic Touch function
*/
/*
 * @cond
 * The following section will be excluded from the documentation.
 */


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
#include "hardware/i2c.h"

int GetTouchValue(int cmd);
void TDelay(void);

// these are defined so that the state of the touch PEN IRQ can be determined with the minimum of CPU cycles
int TouchIrqPortBit;
int TOUCH_IRQ_PIN;
int TOUCH_CS_PIN;
int TOUCH_Click_PIN;
int TOUCH_GETIRQTRIS=0;
static int gt911_addr=GT911_ADDR;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// configure the touch parameters (chip select pin and the IRQ pin)
// this is called by the OPTION TOUCH command
void MIPS16 ConfigTouch(unsigned char *p) {
    int pin1, pin2=0, pin3=0;
    uint8_t TOUCH_CAP=0;
    int threshold=50;
    unsigned char *tp=NULL;
    tp = checkstring(p, (unsigned char *)"FT6336");
    if(tp)TOUCH_CAP=1;
    if(tp){
        p=tp;
        if(!Option.SYSTEM_I2C_SDA)error("System I2C not set");
        if(!TOUCH_CAP)TOUCH_CAP=2;
    }
    getargs(&p, 7, (unsigned char *)",");
    if(!(Option.SYSTEM_CLK || TOUCH_CAP))error("System SPI not configured");
    if(!TOUCH_CAP){
        if(!(argc == 3 || argc == 5)) error("Argument count");
    } else if(argc<3)error("Argument count");
	unsigned char code;
	if(!(code=codecheck(argv[0])))argv[0]+=2;
	pin1 = getinteger(argv[0]);
	if(!code)pin1=codemap(pin1);
	if(IsInvalidPin(pin1)) error("Invalid pin");
    if(!(code=codecheck(argv[2])))argv[2]+=2;
    pin2 = getinteger(argv[2]);
    if(!code)pin2=codemap(pin2);
    if(IsInvalidPin(pin2)) error("Invalid pin");
    if(argc >= 5 && *argv[4]) {
        if(!(code=codecheck(argv[4])))argv[4]+=2;
        pin3 = getinteger(argv[4]);
        if(!code)pin3=codemap(pin3);
        if(IsInvalidPin(pin3)) error("Invalid pin");
    }
    if(TOUCH_CAP){
        if(argc==7) threshold=getint(argv[6],0,255);
    }
    if(ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin1,pin1);
	if(pin2)
        if(ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin2,pin2);
	if(pin3)
        if(ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)  error("Pin %/| is in use",pin3,pin3);
    Option.TOUCH_CS = (TOUCH_CAP ? pin2 :pin1);
    Option.TOUCH_IRQ = (TOUCH_CAP ? pin1 :pin2);
    Option.TOUCH_Click = pin3;
    Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;                    // record the touch feature as not calibrated
    Option.TOUCH_CAP=TOUCH_CAP;
    Option.THRESHOLD_CAP=threshold;
}

int  gt911_dev_mode_w(uint8_t value)
{
  uint8_t tmp;

  tmp = read8Register16(gt911_addr,GT911_DEV_MODE_REG);

  if (mmI2Cvalue == 0L)
  {
    tmp &= ~GT911_DEV_MODE_BIT_MASK;
    tmp |= value << GT911_DEV_MODE_BIT_POSITION;

    Write8Register16(gt911_addr,GT911_DEV_MODE_REG, tmp);
  }

  return mmI2Cvalue;
}
int32_t  gt911_dev_mode_r(uint8_t *pValue)
{

  *pValue=read8Register16(gt911_addr,GT911_DEV_MODE_REG);

  if (mmI2Cvalue == 0L)
  {
    *pValue &= GT911_DEV_MODE_BIT_MASK;
    *pValue = *pValue >> GT911_DEV_MODE_BIT_POSITION;
  }
  return mmI2Cvalue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// setup touch based on the settings saved in flash
void MIPS16 InitTouch(void) {
    if(Option.TOUCH_CAP==1){
        if(!Option.TOUCH_IRQ || !Option.SYSTEM_I2C_SCL) return; //shouldn't be needed
        PinSetBit(CAP_RESET, LATCLR);
        uSec(1000);
        PinSetBit(CAP_RESET, LATSET);
        uSec(500000);
        if(readRegister8(FT6X36_ADDR, FT6X36_REG_PANEL_ID) != FT6X36_VENDID)MMPrintString("Touch panel ID not found\r\n");
        uint8_t id = readRegister8(FT6X36_ADDR, FT6X36_REG_CHIPID);
	    if (!(id == FT6206_CHIPID || id == FT6236_CHIPID || id == FT6336_CHIPID)){PIntH(id);MMPrintString(" Touch panel not found\r\n");}
	    WriteRegister8(FT6X36_ADDR, FT6X36_REG_DEVICE_MODE, 0x00);
	    WriteRegister8(FT6X36_ADDR, FT6X36_REG_INTERRUPT_MODE, 0x00);
	    WriteRegister8(FT6X36_ADDR, FT6X36_REG_CTRL, 0x00);
	    WriteRegister8(FT6X36_ADDR, FT6X36_REG_THRESHHOLD, Option.THRESHOLD_CAP);
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_TOUCHRATE_ACTIVE, 0x01);
        TOUCH_GETIRQTRIS = 1;
/*    } else if(Option.TOUCH_CAP==2){
        if(!Option.TOUCH_IRQ || !Option.SYSTEM_I2C_SCL) return; //shouldn't be needed
        MMPrintString("Initialising GT911\r\n");
        uint8_t read_data;
        PinSetBit(CAP_RESET, LATCLR);
        uSec(1000);
        PinSetBit(CAP_RESET, LATSET);
        uSec(500000);
        gt911_addr=GT911_ADDR;
        int ret=i2c_read_blocking(I2C0locked? i2c0 : i2c1, gt911_addr, &read_data, 1, false);
        if(ret<0){
            gt911_addr=GT911_ADDR2;
            ret=i2c_read_blocking(I2C0locked? i2c0 : i2c1, gt911_addr, &read_data, 1, false); 
        }
        if(ret<0){
            MMPrintString("GT911 controller not found\r\n");
            return;
        }
        if(gt911_dev_mode_w(GT911_DEV_MODE_FACTORY) != GT911_OK){
            MMPrintString("e0\r\n");
        }
        else if (gt911_dev_mode_r(&read_data) != GT911_OK){
            MMPrintString("e1\r\n");
        } else {
            PInt(read_data);PRet();
            uSec(300000);
            if (read_data != GT911_DEV_MODE_FACTORY)
            {
            // Return error to caller 
            MMPrintString("e3\r\n");
            }
            else {
                read_data = 0x04U;
                Write8Register16(gt911_addr, GT911_TD_STAT_REG, read_data);
                if(mmI2Cvalue!=GT911_OK){
                    MMPrintString("e4\r\n");
                }
                else {
                    uint8_t end_calibration = 0U;
                    uSec(300000);
                    for (int nbr_attempt = 0; ((nbr_attempt < 100U) && (end_calibration == 0U)) ; nbr_attempt++)
                    {
                    if (gt911_dev_mode_r(&read_data) != GT911_OK)
                    {
                        MMPrintString("e5\r\n");
                        break;
                    }
                    if (read_data == GT911_DEV_MODE_WORKING)
                    {
                        // Auto Switch to GT911_DEV_MODE_WORKING : means calibration have ended 
                        end_calibration = 1U; // exit for loop 
                    }

                    uSec(300000);
                    }
                }
            }
        }*/
    } else {
        if(!Option.TOUCH_CS) return;
        GetTouchValue(CMD_PENIRQ_ON);                                   // send the controller the command to turn on PenIRQ
        TOUCH_GETIRQTRIS = 1;
        GetTouchAxis(CMD_MEASURE_X);
    }

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
    if(!Option.TOUCH_CAP){
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
    } else {
        while(!TOUCH_DOWN) CheckAbort();                                // wait for the touch
        uSec(100000);
//        for(i = j = 0; i < 10; i++) {                                   // throw away the first 50 reads as rubbish
//            GetTouch(GET_X_AXIS); GetTouch(GET_Y_AXIS);
//        }

        // make a lot of readings and average them
        for(i = j = 0; i < 5; i++) j +=  GetTouchAxisCap(GET_X_AXIS);
        *xval = j/5;
        for(i = j = 0; i < 5; i++) j += GetTouchAxisCap(GET_Y_AXIS);
        *yval = j/5;

        ClearScreen(BLACK);
        while(TOUCH_DOWN) CheckAbort();                                 // wait for the touch to be lifted
//       while(readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES)) CheckAbort();                                 // wait for the touch to be lifted
        uSec(25000);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// this is the main function to call to get a touch reading
// if y is true the y reading will be returned, otherwise the x reading
// this function does noise reduction and scales the reading to pixels
// a return of TOUCH_ERROR means that the pen is not down
int __not_in_flash_func(GetTouch)(int y) {
    int i=TOUCH_ERROR;
//    static int lastx, lasty;
    TOUCH_GETIRQTRIS=0;

    if(Option.TOUCH_CS == 0 && Option.TOUCH_IRQ ==0) error("Touch option not set");
    if(!Option.TOUCH_XZERO && !Option.TOUCH_YZERO) error("Touch not calibrated");
    if(PinRead(Option.TOUCH_IRQ)){ TOUCH_GETIRQTRIS=1 ; return TOUCH_ERROR;}
    if(Option.TOUCH_CAP==1){
        uint32_t in;
        if(y>=10){
            if(readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES)!=2){ TOUCH_GETIRQTRIS=1 ; return TOUCH_ERROR;}
            in=readRegister32(FT6X36_ADDR, FT6X36_REG_P2_XH);
            y-=10;
        } else in=readRegister32(FT6X36_ADDR, FT6X36_REG_P1_XH);
        if(Option.TOUCH_SWAPXY)y=!y;
        if(y){
            i=(in & 0xF0000)>>8;
            i |= (in>>24);
        } else {
            i=(in & 0xF)<<8;
            i |= ((in>>8) & 0xFF);
        }
        if(Option.TOUCH_SWAPXY)y=!y;
        if(y){
            i=(MMFLOAT)(i-Option.TOUCH_YZERO) * Option.TOUCH_YSCALE;
        } else {
            i=(MMFLOAT)(i-Option.TOUCH_XZERO) * Option.TOUCH_XSCALE;
        }
        if(i < 0 || i >= (y ? VRes : HRes))i=TOUCH_ERROR;
    } else {
        if(y) {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY? CMD_MEASURE_X:CMD_MEASURE_Y) - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE);
//            if(i < lasty - CAL_ERROR_MARGIN || i > lasty + CAL_ERROR_MARGIN) { lasty = i; i = TOUCH_ERROR; }
        } else {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY? CMD_MEASURE_Y:CMD_MEASURE_X) - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);
//            if(i < lastx - CAL_ERROR_MARGIN || i > lastx + CAL_ERROR_MARGIN) { lastx = i; i = TOUCH_ERROR; }
        }
        if(i < 0 || i >= (y ? VRes : HRes))i=TOUCH_ERROR;
    }
    TOUCH_GETIRQTRIS=1;

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

int __not_in_flash_func(GetTouchAxisCap)(int y) {

    uint32_t i,in;
    TOUCH_GETIRQTRIS=0;
    in=readRegister32(FT6X36_ADDR, FT6X36_REG_P1_XH);
    if(y){
        i=(in & 0xF0000)>>8;
        i |= (in>>24);
    } else {
        i=(in & 0xF)<<8;
        i |= ((in>>8) & 0xFF);
    }
    TOUCH_GETIRQTRIS=1;
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

/*  @endcond */

// the MMBasic TOUCH() function
void fun_touch(void) {
    if(checkstring(ep, (unsigned char *)"X"))
        iret = GetTouch(GET_X_AXIS);
    else if(checkstring(ep, (unsigned char *)"Y"))
        iret = GetTouch(GET_Y_AXIS);
    else if(checkstring(ep, (unsigned char *)"DOWN"))
        iret = TOUCH_DOWN;
    else if(checkstring(ep, (unsigned char *)"UP"))
        iret = !TOUCH_DOWN;
#ifdef GUICONTROLS
    else if(checkstring(ep, (unsigned char *)"REF"))
        iret = CurrentRef;
    else if(checkstring(ep, (unsigned char *)"LASTREF"))
        iret = LastRef;
    else if(checkstring(ep, (unsigned char *)"LASTX"))
        iret = LastX;
    else if(checkstring(ep, (unsigned char *)"LASTY"))
        iret = LastY;
 #endif        
    else {
        if(Option.TOUCH_CAP){
            if(checkstring(ep, (unsigned char *)"X2"))
                iret = GetTouch(GET_X_AXIS2);
            else if(checkstring(ep, (unsigned char *)"Y2"))
                iret = GetTouch(GET_Y_AXIS2);
//            else if(checkstring(ep, (unsigned char *)"GESTURE"))
//                iret = readRegister8(FT6X36_ADDR, FT6X36_REG_GESTURE_ID);
            else error("Invalid argument");    
        } else error("Invalid argument");

    }        

    targ = T_INT;
}

