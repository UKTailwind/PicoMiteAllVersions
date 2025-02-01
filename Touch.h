/**
 * @cond
 * The following section will be excluded from the documentation.
 */
/***********************************************************************************************************************
PicoMite MMBasic

Touch.h

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



/**********************************************************************************
 the C language function associated with commands, functions or operators should be
 declared here
**********************************************************************************/
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

void fun_touch(void);

#endif




/**********************************************************************************
 All command tokens tokens (eg, PRINT, FOR, etc) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_COMMAND_TABLE


#endif


/**********************************************************************************
 All other tokens (keywords, functions, operators) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_TOKEN_TABLE


#endif


#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)

    #define CAL_ERROR_MARGIN        16
    #define TARGET_OFFSET           30
    #define TOUCH_SAMPLES           8
    #define TOUCH_DISCARD           2

    #define GET_X_AXIS              0
    #define GET_Y_AXIS              1
    #define GET_X_AXIS2             10
    #define GET_Y_AXIS2             11
    #define PENIRQ_ON               3

    #define TOUCH_NOT_CALIBRATED    -999999
    #define TOUCH_ERROR             -1

    #define CMD_MEASURE_X           0b10010000
    #define CMD_MEASURE_Y           0b11010000
    #define CMD_PENIRQ_ON           0b10010000

    extern void ConfigTouch(unsigned char *p);
    extern void InitTouch(void);
    extern void GetCalibration(int x, int y, int *xval, int *yval);

//    extern volatile int TouchX, TouchY;
    extern volatile bool TouchState, TouchDown, TouchUp;
    extern int TOUCH_GETIRQTRIS;

    #define TOUCH_ERROR             -1

    // these are defined so that the state of the touch PEN IRQ can be determined with the minimum of CPU cycles
    extern volatile unsigned int TouchIrqPortAddr;
    extern int TouchIrqPortBit;
    #define TOUCH_DOWN  (!(PinRead(Option.TOUCH_IRQ)))

    extern int GetTouchValue(int cmd);
    extern int TOUCH_IRQ_PIN;
    extern int TOUCH_CS_PIN;
    extern int TOUCH_Click_PIN;

    extern int GetTouch(int x, uint8_t calibrate);
    extern int GetTouchAxis(int);
#define FT6X36_ADDR						0x38
#define FT6X36_REG_DEVICE_MODE			0x00
#define FT6X36_REG_GESTURE_ID			0x01
#define FT6X36_REG_NUM_TOUCHES			0x02
#define FT6X36_REG_P1_XH				0x03
#define FT6X36_REG_P1_XL				0x04
#define FT6X36_REG_P1_YH				0x05
#define FT6X36_REG_P1_YL				0x06
#define FT6X36_REG_P1_WEIGHT			0x07
#define FT6X36_REG_P1_MISC				0x08
#define FT6X36_REG_P2_XH				0x09
#define FT6X36_REG_P2_XL				0x0A
#define FT6X36_REG_P2_YH				0x0B
#define FT6X36_REG_P2_YL				0x0C
#define FT6X36_REG_P2_WEIGHT			0x0D
#define FT6X36_REG_P2_MISC				0x0E
#define FT6X36_REG_THRESHHOLD			0x80
#define FT6X36_REG_FILTER_COEF			0x85
#define FT6X36_REG_CTRL					0x86
#define FT6X36_REG_TIME_ENTER_MONITOR	0x87
#define FT6X36_REG_TOUCHRATE_ACTIVE		0x88
#define FT6X36_REG_TOUCHRATE_MONITOR	0x89 // value in ms
#define FT6X36_REG_RADIAN_VALUE			0x91
#define FT6X36_REG_OFFSET_LEFT_RIGHT	0x92
#define FT6X36_REG_OFFSET_UP_DOWN		0x93
#define FT6X36_REG_DISTANCE_LEFT_RIGHT	0x94
#define FT6X36_REG_DISTANCE_UP_DOWN		0x95
#define FT6X36_REG_DISTANCE_ZOOM		0x96
#define FT6X36_REG_LIB_VERSION_H		0xA1
#define FT6X36_REG_LIB_VERSION_L		0xA2
#define FT6X36_REG_CHIPID				0xA3
#define FT6X36_REG_INTERRUPT_MODE		0xA4
#define FT6X36_REG_POWER_MODE			0xA5
#define FT6X36_REG_FIRMWARE_VERSION		0xA6
#define FT6X36_REG_PANEL_ID				0xA8
#define FT6X36_REG_STATE				0xBC

#define FT6X36_PMODE_ACTIVE				0x00
#define FT6X36_PMODE_MONITOR			0x01
#define FT6X36_PMODE_STANDBY			0x02
#define FT6X36_PMODE_HIBERNATE			0x03

#define FT6X36_VENDID					0x11
#define FT6206_CHIPID					0x06
#define FT6236_CHIPID					0x36
#define FT6336_CHIPID					0x64

#define FT6X36_DEFAULT_THRESHOLD		22
#define FT6336_RESET Option.TOUCH_CS
#endif
/*  @endcond */