/***********************************************************************************************************************
PicoMite MMBasic - Touch.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the
   original copyright message be displayed on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement:
   This product includes software developed by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
************************************************************************************************************************/

/**
 * @file Touch.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for the MMBasic Touch function
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hardware/structs/systick.h"
#include "hardware/i2c.h"

#ifdef PICOMITEWEB
#include "pico/cyw43_arch.h"
#endif

#ifndef PICOMITEWEB
#include "pico/multicore.h"
extern mutex_t frameBufferMutex;
#endif

// ========================================
// Function Prototypes
// ========================================
int GetTouchValue(int cmd);
void TDelay(void);

// ========================================
// Global Variables
// ========================================
int TouchIrqPortBit;
int TOUCH_IRQ_PIN;
int TOUCH_CS_PIN;
int TOUCH_Click_PIN;
int TOUCH_GETIRQTRIS = 0;
static int gt911_addr = GT911_ADDR;

// ========================================
// GT911 Device Mode Functions
// ========================================

int gt911_dev_mode_w(uint8_t value)
{
    uint8_t tmp = read8Register16(gt911_addr, GT911_DEV_MODE_REG);

    if (mmI2Cvalue == 0L)
    {
        tmp &= ~GT911_DEV_MODE_BIT_MASK;
        tmp |= value << GT911_DEV_MODE_BIT_POSITION;
        Write8Register16(gt911_addr, GT911_DEV_MODE_REG, tmp);
    }

    return mmI2Cvalue;
}

int32_t gt911_dev_mode_r(uint8_t *pValue)
{
    *pValue = read8Register16(gt911_addr, GT911_DEV_MODE_REG);

    if (mmI2Cvalue == 0L)
    {
        *pValue &= GT911_DEV_MODE_BIT_MASK;
        *pValue = *pValue >> GT911_DEV_MODE_BIT_POSITION;
    }

    return mmI2Cvalue;
}

// ========================================
// Touch Configuration
// ========================================

/**
 * Configure the touch parameters (chip select pin and the IRQ pin)
 * Called by the OPTION TOUCH command
 */
void MIPS16 ConfigTouch(unsigned char *p)
{
    int pin1, pin2 = 0, pin3 = 0;
    uint8_t TOUCH_CAP = 0;
    int threshold = 50;
    unsigned char *tp = NULL;

    // Check for FT6336 touch controller
    tp = checkstring(p, (unsigned char *)"FT6336");
    if (tp)
    {
        TOUCH_CAP = 1;
        p = tp;

        if (!Option.SYSTEM_I2C_SDA)
        {
            error("System I2C not set");
        }
        if (!TOUCH_CAP)
        {
            TOUCH_CAP = 2;
        }
    }

    getcsargs(&p, 7);

    if (!(Option.SYSTEM_CLK || TOUCH_CAP))
    {
        StandardError(45);
    }

    // Validate argument count
    if (!TOUCH_CAP)
    {
        if (!(argc == 3 || argc == 5))
        {
            StandardError(2);
        }
    }
    else if (argc < 3)
    {
        StandardError(2);
    }

    // Parse and validate pin1
    unsigned char code;
    if (!(code = codecheck(argv[0])))
    {
        argv[0] += 2;
    }
    pin1 = getinteger(argv[0]);
    if (!code)
    {
        pin1 = codemap(pin1);
    }
    if (IsInvalidPin(pin1))
    {
        StandardError(9);
    }

    // Parse and validate pin2
    if (!(code = codecheck(argv[2])))
    {
        argv[2] += 2;
    }
    pin2 = getinteger(argv[2]);
    if (!code)
    {
        pin2 = codemap(pin2);
    }
    if (IsInvalidPin(pin2))
    {
        StandardError(9);
    }

    // Parse and validate pin3 (optional)
    if (argc >= 5 && *argv[4])
    {
        if (!(code = codecheck(argv[4])))
        {
            argv[4] += 2;
        }
        pin3 = getinteger(argv[4]);
        if (!code)
        {
            pin3 = codemap(pin3);
        }
        if (IsInvalidPin(pin3))
        {
            StandardError(9);
        }
    }

    // Parse threshold for capacitive touch
    if (TOUCH_CAP && argc == 7)
    {
        threshold = getint(argv[6], 0, 255);
    }

    // Check if pins are already configured
    if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG)
    {
        StandardErrorParam2(27, pin1, pin1);
    }
    if (pin2 && ExtCurrentConfig[pin2] != EXT_NOT_CONFIG)
    {
        StandardErrorParam2(27, pin2, pin2);
    }
    if (pin3 && ExtCurrentConfig[pin3] != EXT_NOT_CONFIG)
    {
        StandardErrorParam2(27, pin3, pin3);
    }

    // Save configuration
    Option.TOUCH_CS = (TOUCH_CAP ? pin2 : pin1);
    Option.TOUCH_IRQ = (TOUCH_CAP ? pin1 : pin2);
    Option.TOUCH_Click = pin3;
    Option.TOUCH_XZERO = Option.TOUCH_YZERO = 0;
    Option.TOUCH_CAP = TOUCH_CAP;
    Option.THRESHOLD_CAP = threshold;
}

// ========================================
// Touch Initialization
// ========================================

/**
 * Setup touch based on the settings saved in flash
 */
void MIPS16 InitTouch(void)
{
    if (Option.TOUCH_CAP == 1)
    {
        if (!Option.TOUCH_IRQ || !Option.SYSTEM_I2C_SCL)
        {
            return;
        }

        // Reset capacitive touch controller
        PinSetBit(CAP_RESET, LATCLR);
        uSec(1000);
        PinSetBit(CAP_RESET, LATSET);
        uSec(500000);

        // Verify panel ID
        if (readRegister8(FT6X36_ADDR, FT6X36_REG_PANEL_ID) != FT6X36_VENDID)
        {
            MMPrintString("Touch panel ID not found\r\n");
        }

        // Verify chip ID
        uint8_t id = readRegister8(FT6X36_ADDR, FT6X36_REG_CHIPID);
        if (!(id == FT6206_CHIPID || id == FT6236_CHIPID || id == FT6336_CHIPID))
        {
            PIntH(id);
            MMPrintString(" Touch panel not found\r\n");
        }

        // Configure touch controller
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_DEVICE_MODE, 0x00);
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_INTERRUPT_MODE, 0x00);
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_CTRL, 0x00);
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_THRESHHOLD, Option.THRESHOLD_CAP);
        WriteRegister8(FT6X36_ADDR, FT6X36_REG_TOUCHRATE_ACTIVE, 0x01);

        TOUCH_GETIRQTRIS = 1;
    }
    else
    {
        if (!Option.TOUCH_CS)
        {
            return;
        }

        GetTouchValue(CMD_PENIRQ_ON);
        TOUCH_GETIRQTRIS = 1;
        GetTouchAxis(CMD_MEASURE_X);
    }
}

// ========================================
// Touch Calibration
// ========================================

/**
 * Draws the target, waits for touch to stabilize and returns raw x and y values
 * Used only during calibration
 */
void MIPS16 GetCalibration(int x, int y, int *xval, int *yval)
{
    int i, j;
#define TCAL_FONT 0x02

    // Draw calibration screen
    ClearScreen(BLACK);
    GUIPrintString(HRes / 2, VRes / 2 - GetFontHeight(TCAL_FONT) / 2,
                   TCAL_FONT, JUSTIFY_CENTER, JUSTIFY_MIDDLE, 0, WHITE, BLACK,
                   "Touch Target");
    GUIPrintString(HRes / 2, VRes / 2 + GetFontHeight(TCAL_FONT) / 2,
                   TCAL_FONT, JUSTIFY_CENTER, JUSTIFY_MIDDLE, 0, WHITE, BLACK,
                   "and Hold");

    // Draw target
    DrawLine(x - (TARGET_OFFSET * 3) / 4, y, x + (TARGET_OFFSET * 3) / 4, y, 1, WHITE);
    DrawLine(x, y - (TARGET_OFFSET * 3) / 4, x, y + (TARGET_OFFSET * 3) / 4, 1, WHITE);
    DrawCircle(x, y, TARGET_OFFSET / 2, 1, WHITE, -1, 1);

    if (!Option.TOUCH_CAP)
    {
        // Resistive touch calibration
        while (!TOUCH_DOWN)
        {
            CheckAbort();
        }

        // Discard initial readings
        for (i = 0; i < 50; i++)
        {
            GetTouchAxis(CMD_MEASURE_X);
            GetTouchAxis(CMD_MEASURE_Y);
        }

        // Average multiple readings for X
        for (i = j = 0; i < 50; i++)
        {
            j += GetTouchAxis(CMD_MEASURE_X);
        }
        *xval = j / 50;

        // Average multiple readings for Y
        for (i = j = 0; i < 50; i++)
        {
            j += GetTouchAxis(CMD_MEASURE_Y);
        }
        *yval = j / 50;

        ClearScreen(BLACK);
        while (TOUCH_DOWN)
        {
            CheckAbort();
        }
        uSec(25000);
    }
    else
    {
        // Capacitive touch calibration
        while (!TOUCH_DOWN)
        {
            CheckAbort();
        }
        uSec(100000);

        // Average multiple readings for X
        for (i = j = 0; i < 5; i++)
        {
            j += GetTouchAxisCap(GET_X_AXIS);
        }
        *xval = j / 5;

        // Average multiple readings for Y
        for (i = j = 0; i < 5; i++)
        {
            j += GetTouchAxisCap(GET_Y_AXIS);
        }
        *yval = j / 5;

        ClearScreen(BLACK);
        while (TOUCH_DOWN)
        {
            CheckAbort();
        }
        uSec(25000);
    }
}

// ========================================
// Touch Reading Functions
// ========================================

/**
 * Main function to get a touch reading
 * Returns scaled pixel coordinates or TOUCH_ERROR if pen is not down
 * @param y: if true, return Y reading; otherwise return X reading
 */
int __not_in_flash_func(GetTouch)(int y)
{
    int i = TOUCH_ERROR;
    TOUCH_GETIRQTRIS = 0;

    // Validate configuration
    if (Option.TOUCH_CS == 0 && Option.TOUCH_IRQ == 0)
    {
        error("Touch option not set");
    }
    if (!Option.TOUCH_XZERO && !Option.TOUCH_YZERO)
    {
        error("Touch not calibrated");
    }

    // Check if pen is down
    if (PinRead(Option.TOUCH_IRQ))
    {
        TOUCH_GETIRQTRIS = 1;
        return TOUCH_ERROR;
    }

    if (Option.TOUCH_CAP == 1)
    {
        // Capacitive touch reading
        uint32_t in;

        // Handle second touch point
        if (y >= 10)
        {
            if (readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES) != 2)
            {
                TOUCH_GETIRQTRIS = 1;
                return TOUCH_ERROR;
            }
            in = readRegister32(FT6X36_ADDR, FT6X36_REG_P2_XH);
            y -= 10;
        }
        else
        {
            in = readRegister32(FT6X36_ADDR, FT6X36_REG_P1_XH);
        }

        // Handle axis swap
        if (Option.TOUCH_SWAPXY)
        {
            y = !y;
        }

        // Extract coordinate
        if (y)
        {
            i = (in & 0xF0000) >> 8;
            i |= (in >> 24);
        }
        else
        {
            i = (in & 0xF) << 8;
            i |= ((in >> 8) & 0xFF);
        }

        // Restore axis orientation
        if (Option.TOUCH_SWAPXY)
        {
            y = !y;
        }

        // Scale to screen coordinates
        if (y)
        {
            i = (MMFLOAT)(i - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE;
        }
        else
        {
            i = (MMFLOAT)(i - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE;
        }

        // Validate range
        if (i < 0 || i >= (y ? VRes : HRes))
        {
            i = TOUCH_ERROR;
        }
    }
    else
    {
        // Resistive touch reading
        if (y)
        {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY ? CMD_MEASURE_X : CMD_MEASURE_Y) - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE);
        }
        else
        {
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY ? CMD_MEASURE_Y : CMD_MEASURE_X) - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);
        }

        // Validate range
        if (i < 0 || i >= (y ? VRes : HRes))
        {
            i = TOUCH_ERROR;
        }
    }

    TOUCH_GETIRQTRIS = 1;
    return i;
}

/**
 * Get a reading from a single axis (resistive touch)
 * Returns raw value from touch controller
 * Takes multiple readings and averages the middle values
 */
int __not_in_flash_func(GetTouchAxis)(int cmd)
{
    int i, j, t, b[TOUCH_SAMPLES];

    TOUCH_GETIRQTRIS = 0;
    PinSetBit(Option.TOUCH_IRQ, CNPDSET);

#ifdef PICOMITE
    if (SPIatRisk)
    {
        mutex_enter_blocking(&frameBufferMutex);
    }
#endif

    GetTouchValue(cmd);

    // Take TOUCH_SAMPLES readings and sort in descending order
    for (i = 0; i < TOUCH_SAMPLES; i++)
    {
        b[i] = GetTouchValue(cmd);

        // Handle audio playback
        if (CurrentlyPlaying == P_WAV || CurrentlyPlaying == P_FLAC ||
            CurrentlyPlaying == P_MIDI || CurrentlyPlaying == P_MP3 ||
            CurrentlyPlaying == P_ARRAY)
        {
#ifdef PICOMITE
            if (SPIatRisk)
            {
                mutex_enter_blocking(&frameBufferMutex);
            }
#endif
            checkWAVinput();
#ifdef PICOMITE
            if (SPIatRisk)
            {
                mutex_exit(&frameBufferMutex);
            }
#endif
        }

        if (CurrentlyPlaying == P_MOD || CurrentlyPlaying == P_STREAM)
        {
            checkWAVinput();
        }

        // Sort reading into position
        for (j = i; j > 0; j--)
        {
            if (b[j - 1] < b[j])
            {
                t = b[j - 1];
                b[j - 1] = b[j];
                b[j] = t;
            }
            else
            {
                break;
            }
        }
    }

    // Discard outliers and average middle values
    for (j = 0, i = TOUCH_DISCARD; i < TOUCH_SAMPLES - TOUCH_DISCARD; i++)
    {
        j += b[i];
    }

    i = j / (TOUCH_SAMPLES - (TOUCH_DISCARD * 2));

    GetTouchValue(CMD_PENIRQ_ON);
    PinSetBit(Option.TOUCH_IRQ, CNPUSET);
    TOUCH_GETIRQTRIS = 1;

#ifdef PICOMITE
    if (SPIatRisk)
    {
        mutex_exit(&frameBufferMutex);
    }
#endif

    return i;
}

/**
 * Get a reading from capacitive touch controller
 */
int __not_in_flash_func(GetTouchAxisCap)(int y)
{
    uint32_t i, in;

    TOUCH_GETIRQTRIS = 0;
    in = readRegister32(FT6X36_ADDR, FT6X36_REG_P1_XH);

    if (y)
    {
        i = (in & 0xF0000) >> 8;
        i |= (in >> 24);
    }
    else
    {
        i = (in & 0xF) << 8;
        i |= ((in >> 8) & 0xFF);
    }

    TOUCH_GETIRQTRIS = 1;
    return i;
}

/**
 * Get a single reading from the touch controller via SPI
 * Assumes PenIRQ line is low and SPI baudrate is correct
 * Takes approximately 260µS at 120MHz
 */
int __not_in_flash_func(GetTouchValue)(int cmd)
{
    int val;
    unsigned int lb, hb;

    // Set SPI speed
    if (!SSDTYPE)
    {
        SPISpeedSet(TOUCH);
    }
    else
    {
        SPISpeedSet(SLOWTOUCH);
    }

    // Configure CS pin
    gpio_init(TOUCH_CS_PIN);
    gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);

    if (Option.CombinedCS)
    {
        gpio_put(TOUCH_CS_PIN, GPIO_PIN_SET);
    }
    else
    {
        gpio_put(TOUCH_CS_PIN, GPIO_PIN_RESET);
    }

    TDelay();

    // Read touch value via SPI
    val = xchg_byte(cmd);
    hb = xchg_byte(0);
    val = (hb & 0b1111111) << 5; // Top 7 bits
    lb = xchg_byte(0);
    val |= (lb >> 3) & 0b11111; // Bottom 5 bits

    // Release CS
    if (Option.CombinedCS)
    {
        gpio_set_dir(TOUCH_CS_PIN, GPIO_IN);
    }
    else
    {
        ClearCS(Option.TOUCH_CS);
    }

#ifdef PICOMITEWEB
    ProcessWeb(1);
#endif

    return val;
}

/**
 * Provides a small (~200ns) delay for the touch screen controller
 */
void __not_in_flash_func(TDelay)(void)
{
    int ticks_per_millisecond = ticks_per_second / 1000;
    int T = 16777215 + setuptime - ((4 * ticks_per_millisecond) / 20000);
    shortpause(T);
}

// ========================================
// MMBasic TOUCH() Function
// ========================================

/**
 * MMBasic TOUCH() function implementation
 */
void fun_touch(void)
{
    if (checkstring(ep, (unsigned char *)"X"))
    {
        iret = GetTouch(GET_X_AXIS);
    }
    else if (checkstring(ep, (unsigned char *)"Y"))
    {
        iret = GetTouch(GET_Y_AXIS);
    }
    else if (checkstring(ep, (unsigned char *)"DOWN"))
    {
        iret = TOUCH_DOWN;
    }
    else if (checkstring(ep, (unsigned char *)"UP"))
    {
        iret = !TOUCH_DOWN;
    }
#ifdef GUICONTROLS
    else if (checkstring(ep, (unsigned char *)"REF"))
    {
        iret = CurrentRef;
    }
    else if (checkstring(ep, (unsigned char *)"LASTREF"))
    {
        iret = LastRef;
    }
    else if (checkstring(ep, (unsigned char *)"LASTX"))
    {
        iret = LastX;
    }
    else if (checkstring(ep, (unsigned char *)"LASTY"))
    {
        iret = LastY;
    }
#endif
    else
    {
        if (Option.TOUCH_CAP)
        {
            if (checkstring(ep, (unsigned char *)"X2"))
            {
                iret = GetTouch(GET_X_AXIS2);
            }
            else if (checkstring(ep, (unsigned char *)"Y2"))
            {
                iret = GetTouch(GET_Y_AXIS2);
            }
            else
            {
                SyntaxError();
            }
        }
        else
        {
            SyntaxError();
        }
    }

    targ = T_INT;
}