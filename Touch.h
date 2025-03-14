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
    #define GET_X_AXIS2             0x10
    #define GET_Y_AXIS2             0x11
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

    extern int GetTouch(int x);
    extern int GetTouchAxis(int);
    extern int GetTouchAxisCap(int);
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
#define CAP_RESET Option.TOUCH_CS       

#define GT911_ADDR              0x14
#define GT911_ADDR2              0x5D
#define   GT911_DEV_MODE_BIT_MASK        0x70U
#define   GT911_DEV_MODE_BIT_POSITION    4U
#define   GT911_GEST_ID_BIT_MASK        0xFFU
#define   GT911_GEST_ID_BIT_POSITION    0U
#define   GT911_TD_STATUS_BIT_BUFFER_STAT 0x80U
#define   GT911_TD_STATUS_BIT_HAVEKEY     0x10U
#define   GT911_TD_STATUS_BITS_NBTOUCHPTS 0x0FU
//JCC adding END
#define   GT911_TD_STATUS_BIT_MASK        0x07U
#define   GT911_TD_STATUS_BIT_POSITION    0U
#define GT911_MAX_X_LENGTH                  HRes
#define GT911_MAX_Y_LENGTH                  VRes
#define   GT911_P1_XH_EF_BIT_MASK        0xC0U
#define   GT911_P1_XH_EF_BIT_POSITION    6U
#define   GT911_P1_XL_TP_BIT_MASK        0xFFU
#define   GT911_P1_XL_TP_BIT_POSITION    0U
#define   GT911_P1_TID_BIT_MASK           0xFFU
#define   GT911_P1_TID_BIT_POSITION       7U
/* Current mode register of the GT911 (R/W) */

/* Gesture ID register */
#define GT911_GEST_ID_REG          0x814BU

/* Gesture mode enabled */
#define GT911_GESTURE_EN           0x8U

/* Gesture Coordinates registers */
#define GT911_START_X_L            0x814DU
#define GT911_START_X_H            0x814EU
#define GT911_START_Y_L            0x814FU
#define GT911_START_Y_H            0x8150U
#define GT911_END_X_L              0x8151U
#define GT911_END_X_H              0x8152U
#define GT911_END_Y_L              0x8153U
#define GT911_END_Y_H              0x8154U
#define GT911_WEIGHT_L             0x8155U
#define GT911_WEIGHT_H             0x8156U
#define GT911_HEIGHT_L             0x8157U
#define GT911_HEIGHT_H             0x8158U


/* Touch Data Status register : gives number of active touch points (0..5) */
#define GT911_TD_STAT_REG          0x814EU

/* P1 X, Y coordinates, weight(Point size) and track-id registers */
#define GT911_P1_XL_REG            0x8150U
#define GT911_P1_XH_REG            0x8151U
#define GT911_P1_YL_REG            0x8152U
#define GT911_P1_YH_REG            0x8153U
#define GT911_P1_WEIGHTL_REG       0x8154U
#define GT911_P1_WEIGHTH_REG       0x8155U
#define GT911_P1_TID_REG           0x8157U

/* P2 X, Y coordinates, weight and tid registers */
#define GT911_P2_XL_REG            0x8158U
#define GT911_P2_XH_REG            0x8159U
#define GT911_P2_YL_REG            0x815AU
#define GT911_P2_YH_REG            0x815BU
#define GT911_P2_WEIGHTL_REG       0x815CU
#define GT911_P2_WEIGHTH_REG       0x815DU
#define GT911_P2_TID_REG           0x815FU

/* P3 X, Y coordinates, weight and tid registers */
#define GT911_P3_XL_REG            0x8160U
#define GT911_P3_XH_REG            0x8161U
#define GT911_P3_YL_REG            0x8162U
#define GT911_P3_YH_REG            0x8163U
#define GT911_P3_WEIGHTL_REG       0x8164U
#define GT911_P3_WEIGHTH_REG       0x8165U
#define GT911_P3_TID_REG           0x8167U

/* P4 X, Y coordinates, weight and tid registers */
#define GT911_P4_XL_REG            0x8168U
#define GT911_P4_XH_REG            0x8169U
#define GT911_P4_YL_REG            0x816AU
#define GT911_P4_YH_REG            0x816BU
#define GT911_P4_WEIGHTL_REG       0x816CU
#define GT911_P4_WEIGHTH_REG       0x816DU
#define GT911_P4_TID_REG           0x816FU

/* P5 X, Y coordinates, weight and tid registers */
#define GT911_P5_XL_REG            0x8170U
#define GT911_P5_XH_REG            0x8171U
#define GT911_P5_YL_REG            0x8172U
#define GT911_P5_YH_REG            0x8173U
#define GT911_P5_WEIGHTL_REG       0x8174U
#define GT911_P5_WEIGHTH_REG       0x8175U
#define GT911_P5_TID_REG           0x8177U

/* Threshold for touch detection */
#define GT911_TH_GROUP_REG         0x80U

/* Filter function coefficients */
#define GT911_TH_DIFF_REG          0x85U

/* Control register */
#define GT911_CTRL_REG             0x86U

/* The time period of switching from Active mode to Monitor mode when there is no touching */
#define GT911_TIMEENTERMONITOR_REG 0x87U

/* Report rate in Active mode */
#define GT911_PERIODACTIVE_REG     0x88U

/* Report rate in Monitor mode */
#define GT911_PERIODMONITOR_REG    0x89U

/* Maximum offset while Moving Left and Moving Right gesture */
#define GT911_OFFSET_LR_REG        0x92U

/* Maximum offset while Moving Up and Moving Down gesture */
#define GT911_OFFSET_UD_REG        0x93U

/* Minimum distance while moving gesture */
#define GT911_DIS_GESTURE_REG      0x8071U

/* High 8-bit of LIB Version info */
#define GT911_LIB_VER_H_REG        0xA1U

/* Low 8-bit of LIB Version info */
#define GT911_LIB_VER_L_REG        0xA2U

/* Chip Selecting */
#define GT911_CIPHER_REG           0xA3U

/* Module_Switch1 register for Interrupt */
#define GT911_MSW1_REG             0x804DU

/* Current power mode the GT911 system is in (R) */
#define GT911_PWR_MODE_REG         0xA5U

/* GT911 firmware version */
#define GT911_FIRMID_REG           0x8144U

/* GT911 Chip identification register */
#define GT911_CHIP_ID_REG          0x8140U

/* Release code version */
#define GT911_RELEASE_CODE_ID_REG  0xAFU

/* Current operating mode the GT911 system is in (R) */
#define GT911_COMMAND_REG          0x8040U

/* Coordinates report rate (= 5+N ms) */
#define GT911_REFRESH_RATE_REG     0x8056U
#define GT911_REFRESH_RATE_MSK     0x0FU

/* Version number configuration */
#define GT911_CONFIG_VERS_REG      0x8047U

/* Checksum configuration register */
#define GT911_CONFIG_CHKSUM_REG    0x80FFU

/* Configuration update flag register */
#define GT911_CONFIG_FRESH_REG     0x8100U

/* Command check register */
#define GT911_COMMAND_CHK_REG      0x8046U

/* Gesture configuration registers */
#define GT911_GESTURE_PRESS_TIME   0x8072U
#define GT911_GESTURE_TIME_ABORT   0x00U

#define GT911_GESTURE_SLOPE_ADJUST 0x8073U
#define GT911_GESTURE_ADJUST_VAL   0x00U

#define GT911_GESTURE_CTRL_REG     0x8074U
#define GT911_GESTURE_INVALID_TIM  0x0FU

#define GT911_GESTURE_SWITCH1_REG  0x8075U
#define GT911_GESTURE_SWITCH2_REG  0x8076U
#define GT911_GESTURE_SWITCH1_VAL  0x00U
#define GT911_GESTURE_SWITCH2_VAL  0x00U

#define GT911_GESTURE_REFRESH_REG  0x8077U

#define GT911_GESTURE_TH_REG       0x8078U
#define GT911_OK                      (0)
#define GT911_ERROR                   (-1)

/* Max detectable simultaneous touches */
#define GT911_MAX_NB_TOUCH             5U

/* Touch GT911 IDs: "911" */
#define GT911_ID                        0x00313139U
#define GT911_ID1                       0x39U
#define GT911_ID2                       0x31U
#define GT911_ID3                       0x31U
/* Possible values of GT911_DEV_MODE_REG */
#define GT911_DEV_MODE_WORKING          0x00U
#define GT911_DEV_MODE_FACTORY          0x04U

/* Possible values of GT911_GEST_ID_REG */
#define GT911_GEST_ID_NO_GESTURE        0x00U
#define GT911_GEST_ID_SWIPE_RIGHT       0xAAU
#define GT911_GEST_ID_SWIPE_LEFT        0xBBU
#define GT911_GEST_ID_SWIPE_DOWN        0xABU
#define GT911_GEST_ID_SWIPE_UP          0xBAU
#define GT911_GEST_ID_DOUBLE_TAP        0xCCU

/* Values Pn_XH and Pn_YH related */
#define GT911_TOUCH_EVT_FLAG_PRESS_DOWN 0x00U
#define GT911_TOUCH_EVT_FLAG_LIFT_UP    0x01U
#define GT911_TOUCH_EVT_FLAG_CONTACT    0x02U
#define GT911_TOUCH_EVT_FLAG_NO_EVENT   0x03U

/* Possible values of GT911_MSW1_REG(Module_Switch1) */
#define GT911_M_SW1_INTERRUPT_RISING    0x00U
#define GT911_M_SW1_INTERRUPT_FALLING   0x01U
#define GT911_M_SW1_INTERRUPT_LOW       0x02U
#define GT911_M_SW1_INTERRUPT_HIGH      0x03U

/* Mask for reading the MSW1 register without INT trigger */
#define GT911_M_SW1_DATA_MASK           0xFCU
/* Current mode register of the GT911 (R/W) */
#define GT911_DEV_MODE_REG         0x8040U

/* Gesture ID register */
#define GT911_GEST_ID_REG          0x814BU

/* Gesture mode enabled */
#define GT911_GESTURE_EN           0x8U

/* Gesture Coordinates registers */
#define GT911_START_X_L            0x814DU
#define GT911_START_X_H            0x814EU
#define GT911_START_Y_L            0x814FU
#define GT911_START_Y_H            0x8150U
#define GT911_END_X_L              0x8151U
#define GT911_END_X_H              0x8152U
#define GT911_END_Y_L              0x8153U
#define GT911_END_Y_H              0x8154U
#define GT911_WEIGHT_L             0x8155U
#define GT911_WEIGHT_H             0x8156U
#define GT911_HEIGHT_L             0x8157U
#define GT911_HEIGHT_H             0x8158U


/* Touch Data Status register : gives number of active touch points (0..5) */
#define GT911_TD_STAT_REG          0x814EU

/* P1 X, Y coordinates, weight(Point size) and track-id registers */
#define GT911_P1_XL_REG            0x8150U
#define GT911_P1_XH_REG            0x8151U
#define GT911_P1_YL_REG            0x8152U
#define GT911_P1_YH_REG            0x8153U
#define GT911_P1_WEIGHTL_REG       0x8154U
#define GT911_P1_WEIGHTH_REG       0x8155U
#define GT911_P1_TID_REG           0x8157U

/* P2 X, Y coordinates, weight and tid registers */
#define GT911_P2_XL_REG            0x8158U
#define GT911_P2_XH_REG            0x8159U
#define GT911_P2_YL_REG            0x815AU
#define GT911_P2_YH_REG            0x815BU
#define GT911_P2_WEIGHTL_REG       0x815CU
#define GT911_P2_WEIGHTH_REG       0x815DU
#define GT911_P2_TID_REG           0x815FU

/* P3 X, Y coordinates, weight and tid registers */
#define GT911_P3_XL_REG            0x8160U
#define GT911_P3_XH_REG            0x8161U
#define GT911_P3_YL_REG            0x8162U
#define GT911_P3_YH_REG            0x8163U
#define GT911_P3_WEIGHTL_REG       0x8164U
#define GT911_P3_WEIGHTH_REG       0x8165U
#define GT911_P3_TID_REG           0x8167U

/* P4 X, Y coordinates, weight and tid registers */
#define GT911_P4_XL_REG            0x8168U
#define GT911_P4_XH_REG            0x8169U
#define GT911_P4_YL_REG            0x816AU
#define GT911_P4_YH_REG            0x816BU
#define GT911_P4_WEIGHTL_REG       0x816CU
#define GT911_P4_WEIGHTH_REG       0x816DU
#define GT911_P4_TID_REG           0x816FU

/* P5 X, Y coordinates, weight and tid registers */
#define GT911_P5_XL_REG            0x8170U
#define GT911_P5_XH_REG            0x8171U
#define GT911_P5_YL_REG            0x8172U
#define GT911_P5_YH_REG            0x8173U
#define GT911_P5_WEIGHTL_REG       0x8174U
#define GT911_P5_WEIGHTH_REG       0x8175U
#define GT911_P5_TID_REG           0x8177U

/* Threshold for touch detection */
#define GT911_TH_GROUP_REG         0x80U

/* Filter function coefficients */
#define GT911_TH_DIFF_REG          0x85U

/* Control register */
#define GT911_CTRL_REG             0x86U

/* The time period of switching from Active mode to Monitor mode when there is no touching */
#define GT911_TIMEENTERMONITOR_REG 0x87U

/* Report rate in Active mode */
#define GT911_PERIODACTIVE_REG     0x88U

/* Report rate in Monitor mode */
#define GT911_PERIODMONITOR_REG    0x89U

/* Maximum offset while Moving Left and Moving Right gesture */
#define GT911_OFFSET_LR_REG        0x92U

/* Maximum offset while Moving Up and Moving Down gesture */
#define GT911_OFFSET_UD_REG        0x93U

/* Minimum distance while moving gesture */
#define GT911_DIS_GESTURE_REG      0x8071U

/* High 8-bit of LIB Version info */
#define GT911_LIB_VER_H_REG        0xA1U

/* Low 8-bit of LIB Version info */
#define GT911_LIB_VER_L_REG        0xA2U

/* Chip Selecting */
#define GT911_CIPHER_REG           0xA3U

/* Module_Switch1 register for Interrupt */
#define GT911_MSW1_REG             0x804DU

/* Current power mode the GT911 system is in (R) */
#define GT911_PWR_MODE_REG         0xA5U

/* GT911 firmware version */
#define GT911_FIRMID_REG           0x8144U

/* GT911 Chip identification register */
#define GT911_CHIP_ID_REG          0x8140U

/* Release code version */
#define GT911_RELEASE_CODE_ID_REG  0xAFU

/* Current operating mode the GT911 system is in (R) */
#define GT911_COMMAND_REG          0x8040U

/* Coordinates report rate (= 5+N ms) */
#define GT911_REFRESH_RATE_REG     0x8056U
#define GT911_REFRESH_RATE_MSK     0x0FU

/* Version number configuration */
#define GT911_CONFIG_VERS_REG      0x8047U

/* Checksum configuration register */
#define GT911_CONFIG_CHKSUM_REG    0x80FFU

/* Configuration update flag register */
#define GT911_CONFIG_FRESH_REG     0x8100U

/* Command check register */
#define GT911_COMMAND_CHK_REG      0x8046U

/* Gesture configuration registers */
#define GT911_GESTURE_PRESS_TIME   0x8072U
#define GT911_GESTURE_TIME_ABORT   0x00U

#define GT911_GESTURE_SLOPE_ADJUST 0x8073U
#define GT911_GESTURE_ADJUST_VAL   0x00U

#define GT911_GESTURE_CTRL_REG     0x8074U
#define GT911_GESTURE_INVALID_TIM  0x0FU

#define GT911_GESTURE_SWITCH1_REG  0x8075U
#define GT911_GESTURE_SWITCH2_REG  0x8076U
#define GT911_GESTURE_SWITCH1_VAL  0x00U
#define GT911_GESTURE_SWITCH2_VAL  0x00U

#define GT911_GESTURE_REFRESH_REG  0x8077U

#define GT911_GESTURE_TH_REG       0x8078U
#endif
/*  @endcond */