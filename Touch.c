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
// GT911 polled touch state
// ========================================
// The GT911 has no "pen down" output line that can be polled as a level
// (unlike the FT6X36, whose INT pin is held low for the duration of a
// touch in polling mode). Instead the host reads the status register
// (0x814E): bit 7 signals a fresh coordinate report is ready, the low
// nibble carries the number of active contacts. After reading the
// coordinates the host must clear the register or the controller stops
// updating. We cache the last reported state so that, between reports,
// TOUCH(X)/TOUCH(Y)/TOUCH(DOWN) all see a stable value rather than a
// momentary "no data ready".
static int gt911_x[GT911_MAX_NB_TOUCH];       // raw X for each tracked contact
static int gt911_y[GT911_MAX_NB_TOUCH];       // raw Y for each tracked contact
static int gt911_points = 0; // active contacts at the last fresh report
static bool gt911_down = false;   // debounced pen-down state
static uint64_t gt911_up_us = 0;  // when the current "no contact" streak began (0 = none)

// A held finger occasionally produces a single spurious "0 contacts"
// report (a capacitive controller dropping a light/still touch for one
// frame). Treating that as a release makes the pen flicker up/down, which
// e.g. skips calibration targets and double-fires GUI buttons. Require the
// no-contact condition to persist for this long before reporting release.
#define GT911_RELEASE_DEBOUNCE_US 60000ULL // 60 ms

/**
 * Refresh the cached GT911 touch state from the controller.
 * Only the status bit 7 ("report ready") path reads coordinates; between
 * reports the last values are retained, which keeps the pen-down level
 * steady while a finger is held. The pen-up edge is debounced (see above)
 * so a one-frame contact dropout does not register as a release.
 */
static void GT911Poll(void)
{
    uint8_t status = read8Register16(gt911_addr, GT911_TD_STAT_REG);

    if (status & GT911_TD_STATUS_BIT_BUFFER_STAT)
    {
        int n = status & GT911_TD_STATUS_BITS_NBTOUCHPTS;
        if (n > GT911_MAX_NB_TOUCH)
            n = 0; // garbage count — treat as no touch

        if (n > 0)
        {
            // Read coordinates for every contact we can expose. TOUCH(X/Y)
            // and TOUCH(X2/Y2) use contacts 0 and 1; TOUCH(XN/YN n) reaches
            // the rest, up to the GT911's GT911_MAX_NB_TOUCH (5) limit. The
            // per-contact coordinate blocks are 8 bytes apart and laid out
            // P1=0x8150, P2=0x8158, ... so the address steps by 8.
            int rd = (n > GT911_MAX_NB_TOUCH) ? GT911_MAX_NB_TOUCH : n;
            for (int pt = 0; pt < rd; pt++)
            {
                uint8_t buf[4];
                readNRegister16(gt911_addr, GT911_P1_XL_REG + pt * 8, buf, 4);
                gt911_x[pt] = buf[0] | (buf[1] << 8);
                gt911_y[pt] = buf[2] | (buf[3] << 8);
            }
            gt911_points = n;
            gt911_down = true;
            gt911_up_us = 0; // any contact cancels a pending release
        }
        else if (gt911_up_us == 0)
        {
            gt911_up_us = time_us_64(); // start of a possible release
        }

        // Clearing the status register is mandatory — the GT911 will not
        // produce a new report until the host acknowledges this one.
        Write8Register16(gt911_addr, GT911_TD_STAT_REG, 0);
    }

    // Confirm a release once the no-contact streak has lasted long enough.
    // Done outside the "report ready" gate so it still fires if the
    // controller goes quiet after a single lift-off report.
    if (gt911_up_us && (time_us_64() - gt911_up_us >= GT911_RELEASE_DEBOUNCE_US))
    {
        gt911_down = false;
        gt911_points = 0;
        gt911_up_us = 0;
    }
}

/**
 * Pen-down test for the GT911, used by the TOUCH_DOWN macro in place of
 * the IRQ-pin read used for resistive / FT6X36 panels.
 */
int GT911TouchDown(void)
{
    if (Option.TOUCH_CAP != 2)
        return 0;
    GT911Poll();
    return gt911_down;
}

/**
 * Run the GT911 power-on reset sequence and probe for the controller.
 * The GT911 latches its I2C slave address from the INT pin level as RESET
 * is released: INT high selects 0x14 (GT911_ADDR), INT low selects 0x5D
 * (GT911_ADDR2). Returns true if the "911" product id is read back at
 * @addr after the reset.
 *   Option.TOUCH_IRQ -> INT pin, Option.TOUCH_CS -> RESET pin.
 */
static int GT911ResetProbe(int int_level, int addr)
{
    gpio_init(TOUCH_CS_PIN);
    gpio_set_dir(TOUCH_CS_PIN, GPIO_OUT);
    gpio_init(TOUCH_IRQ_PIN);
    gpio_set_dir(TOUCH_IRQ_PIN, GPIO_OUT);

    gpio_put(TOUCH_CS_PIN, 0);            // assert RESET (active low)
    gpio_put(TOUCH_IRQ_PIN, 0);
    uSec(200);
    gpio_put(TOUCH_IRQ_PIN, int_level);   // select slave address
    uSec(200);
    gpio_put(TOUCH_CS_PIN, 1);            // release RESET, address is latched
    uSec(6000);
    gpio_put(TOUCH_IRQ_PIN, 0);
    uSec(50000);                          // controller boot time

    // Return the INT pin to a high-impedance input (matches how
    // InitReservedIO leaves it for the resistive / FT6X36 path).
    gpio_set_dir(TOUCH_IRQ_PIN, GPIO_IN);
    gpio_pull_up(TOUCH_IRQ_PIN);

    // Verify the product id ("911" in ASCII at 0x8140).
    uint8_t pid[4];
    readNRegister16(addr, GT911_CHIP_ID_REG, pid, 4);
    uint32_t id = pid[0] | (pid[1] << 8) | (pid[2] << 16) | (pid[3] << 24);
    return (id & 0x00FFFFFFU) == (GT911_ID & 0x00FFFFFFU);
}

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

    // Check for a capacitive touch controller (I2C based). Both the
    // FT6X36 family and the GT911 are wired the same way at the OPTION
    // TOUCH level: INT pin, RESET pin[, Click pin][, threshold].
    tp = checkstring(p, (unsigned char *)"FT6336");
    if (tp)
    {
        TOUCH_CAP = 1; // FT6206 / FT6236 / FT6336
        p = tp;
    }
    else
    {
        tp = checkstring(p, (unsigned char *)"GT911");
        if (tp)
        {
            TOUCH_CAP = 2; // Goodix GT911
            p = tp;
        }
    }

    if (TOUCH_CAP && !Option.SYSTEM_I2C_SDA)
    {
        error("System I2C not set");
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
    pin1 = getpinarg(argv[0]);

    // Parse and validate pin2
    pin2 = getpinarg(argv[2]);

    // Parse and validate pin3 (optional)
    if (argc >= 5 && *argv[4])
    {
        pin3 = getpinarg(argv[4]);
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
    else if (Option.TOUCH_CAP == 2)
    {
        // GT911 (Goodix) capacitive controller.
        // Option.TOUCH_IRQ -> INT pin, Option.TOUCH_CS -> RESET pin.
        if (!Option.TOUCH_IRQ || !Option.TOUCH_CS || !Option.SYSTEM_I2C_SCL)
        {
            return;
        }

        // The slave address is fixed by the INT level during reset, so each
        // candidate address needs its own reset. Try 0x14 (INT high) first,
        // then fall back to 0x5D (INT low) for panels strapped that way.
        if (GT911ResetProbe(1, GT911_ADDR))
        {
            gt911_addr = GT911_ADDR;
        }
        else if (GT911ResetProbe(0, GT911_ADDR2))
        {
            gt911_addr = GT911_ADDR2;
        }
        else
        {
            gt911_addr = GT911_ADDR;
            MMPrintString("GT911 touch panel not found\r\n");
        }

        // Discard any pending report so the first poll starts clean.
        Write8Register16(gt911_addr, GT911_TD_STAT_REG, 0);
        gt911_points = 0;

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

    // No touch panel configured / not calibrated — report "no touch"
    // rather than erroring. BASIC programs that mix mouse/CLICK and
    // touch input can poll TOUCH(X)/TOUCH(Y) on any build without
    // first checking whether the panel is wired up.
    if (Option.TOUCH_CS == 0 && Option.TOUCH_IRQ == 0)
        return TOUCH_ERROR;
    if (!Option.TOUCH_XZERO && !Option.TOUCH_YZERO)
        return TOUCH_ERROR;

    if (Option.TOUCH_CAP == 2)
    {
        // GT911 — pen-down state and coordinates come from the polled
        // status register, not the INT pin (which only pulses).
        int point = 0;
        if (y >= 0x10)
        {
            point = 1; // second contact: TOUCH(X2) / TOUCH(Y2)
            y -= 0x10; // strip the contact-2 flag (bit 4), keep X/Y selector
        }

        GT911Poll();
        if (!gt911_down || point >= gt911_points)
        {
            TOUCH_GETIRQTRIS = 1;
            return TOUCH_ERROR;
        }

        // Pick the raw axis, honouring the calibration's swap setting.
        int raw = (Option.TOUCH_SWAPXY ? !y : y) ? gt911_y[point] : gt911_x[point];

        if (y)
            i = (MMFLOAT)(raw - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE;
        else
            i = (MMFLOAT)(raw - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE;

        if (i < 0 || i >= (y ? VRes : HRes))
            i = TOUCH_ERROR;

        TOUCH_GETIRQTRIS = 1;
        return i;
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

        // Handle second touch point. TOUCH(X2)/TOUCH(Y2) arrive as
        // GET_X_AXIS2 (0x10) / GET_Y_AXIS2 (0x11): bit 4 flags "contact 2",
        // the low bit still selects X(0)/Y(1). Strip the flag with 0x10 —
        // NOT decimal 10, which would leave 6/7 and read X2 as the Y axis.
        if (y >= 0x10)
        {
            // TD_STATUS low nibble = active contact count (upper bits
            // reserved); need both contacts present for X2/Y2.
            if ((readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES) & 0x0F) != 2)
            {
                TOUCH_GETIRQTRIS = 1;
                return TOUCH_ERROR;
            }
            in = readRegister32(FT6X36_ADDR, FT6X36_REG_P2_XH);
            y -= 0x10;
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
 * Generalised multi-touch reader behind TOUCH(XN n) / TOUCH(YN n).
 * @param point  zero-based contact index (BASIC's 1..10 maps to 0..9)
 * @param axis   GET_X_AXIS or GET_Y_AXIS
 * Returns the screen-scaled coordinate of that contact, or TOUCH_ERROR if
 * the contact is not currently touching (or the panel can't track it).
 *
 * Contact 0 returns exactly what TOUCH(X)/TOUCH(Y) return and contact 1
 * matches TOUCH(X2)/TOUCH(Y2); the existing accessors are left untouched.
 * Hardware limits: resistive panels are single-touch (point 0 only), the
 * FT6X36 family tracks 2 contacts, the GT911 up to GT911_MAX_NB_TOUCH (5).
 * A caller walking the list should stop at the first TOUCH_ERROR — there is
 * no point reading contact n+1 once contact n is inactive.
 */
int __not_in_flash_func(GetTouchN)(int point, int axis)
{
    int i = TOUCH_ERROR;
    int y = (axis == GET_Y_AXIS);
    TOUCH_GETIRQTRIS = 0;

    // Mirror GetTouch's "no panel / not calibrated -> no touch" guards.
    if (Option.TOUCH_CS == 0 && Option.TOUCH_IRQ == 0)
        return TOUCH_ERROR;
    if (!Option.TOUCH_XZERO && !Option.TOUCH_YZERO)
        return TOUCH_ERROR;
    if (point < 0)
        return TOUCH_ERROR;

    if (Option.TOUCH_CAP == 2)
    {
        // GT911 — all live contacts are cached by the status-register poll.
        GT911Poll();
        if (!gt911_down || point >= gt911_points || point >= GT911_MAX_NB_TOUCH)
        {
            TOUCH_GETIRQTRIS = 1;
            return TOUCH_ERROR;
        }

        int raw = (Option.TOUCH_SWAPXY ? !y : y) ? gt911_y[point] : gt911_x[point];

        if (y)
            i = (MMFLOAT)(raw - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE;
        else
            i = (MMFLOAT)(raw - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE;

        if (i < 0 || i >= (y ? VRes : HRes))
            i = TOUCH_ERROR;

        TOUCH_GETIRQTRIS = 1;
        return i;
    }

    // Resistive / FT6X36: pen-down is the INT pin level.
    if (PinRead(Option.TOUCH_IRQ))
    {
        TOUCH_GETIRQTRIS = 1;
        return TOUCH_ERROR;
    }

    if (Option.TOUCH_CAP == 1)
    {
        // FT6X36 family tracks at most two contacts.
        if (point >= 2)
        {
            TOUCH_GETIRQTRIS = 1;
            return TOUCH_ERROR;
        }
        // TD_STATUS low nibble = active contact count; need this contact present.
        if ((readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES) & 0x0F) <= point)
        {
            TOUCH_GETIRQTRIS = 1;
            return TOUCH_ERROR;
        }

        uint32_t in = readRegister32(FT6X36_ADDR, point ? FT6X36_REG_P2_XH : FT6X36_REG_P1_XH);

        if (Option.TOUCH_SWAPXY)
            y = !y;
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
        if (Option.TOUCH_SWAPXY)
            y = !y;

        if (y)
            i = (MMFLOAT)(i - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE;
        else
            i = (MMFLOAT)(i - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE;

        if (i < 0 || i >= (y ? VRes : HRes))
            i = TOUCH_ERROR;
    }
    else
    {
        // Resistive panels report a single contact — nothing beyond point 0.
        if (point != 0)
        {
            TOUCH_GETIRQTRIS = 1;
            return TOUCH_ERROR;
        }
        if (y)
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY ? CMD_MEASURE_X : CMD_MEASURE_Y) - Option.TOUCH_YZERO) * Option.TOUCH_YSCALE);
        else
            i = ((MMFLOAT)(GetTouchAxis(Option.TOUCH_SWAPXY ? CMD_MEASURE_Y : CMD_MEASURE_X) - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);

        if (i < 0 || i >= (y ? VRes : HRes))
            i = TOUCH_ERROR;
    }

    TOUCH_GETIRQTRIS = 1;
    return i;
}

/**
 * Number of contacts currently touching the panel, behind TOUCH(XN 0) /
 * TOUCH(YN 0). Lets a program read the count once and then read exactly
 * that many contacts, instead of probing TOUCH(XN n) upward until -1.
 * Returns 0 when nothing is touching. Caps at the panel's tracking limit
 * (resistive 1, FT6X36 2, GT911 GT911_MAX_NB_TOUCH).
 */
int __not_in_flash_func(GetTouchCount)(void)
{
    int n = 0;
    TOUCH_GETIRQTRIS = 0;

    if (Option.TOUCH_CS == 0 && Option.TOUCH_IRQ == 0)
        return 0;
    if (!Option.TOUCH_XZERO && !Option.TOUCH_YZERO)
        return 0;

    if (Option.TOUCH_CAP == 2)
    {
        GT911Poll();
        if (gt911_down)
        {
            n = gt911_points;
            if (n > GT911_MAX_NB_TOUCH)
                n = GT911_MAX_NB_TOUCH;
        }
        TOUCH_GETIRQTRIS = 1;
        return n;
    }

    // Resistive / FT6X36: pen-down is the INT pin level.
    if (PinRead(Option.TOUCH_IRQ))
    {
        TOUCH_GETIRQTRIS = 1;
        return 0;
    }

    if (Option.TOUCH_CAP == 1)
    {
        n = readRegister8(FT6X36_ADDR, FT6X36_REG_NUM_TOUCHES) & 0x0F;
        if (n > 2)
            n = 2; // FT6X36 family tracks at most two contacts
    }
    else
    {
        n = 1; // resistive panels are single-touch
    }

    TOUCH_GETIRQTRIS = 1;
    return n;
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
#if defined(PICOMITE) || defined(PICOMITEMIN)
            if (SPIatRisk)
            {
                mutex_enter_blocking(&frameBufferMutex);
            }
#endif
            checkWAVinput();
#if defined(PICOMITE) || defined(PICOMITEMIN)
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
#if LOWRAM
int GetTouchAxisCap(int y)
#else
int __not_in_flash_func(GetTouchAxisCap)(int y)
#endif
{
    uint32_t i, in;

    TOUCH_GETIRQTRIS = 0;

    if (Option.TOUCH_CAP == 2)
    {
        // GT911 raw contact-0 coordinate (used during calibration).
        GT911Poll();
        i = y ? gt911_y[0] : gt911_x[0];
        TOUCH_GETIRQTRIS = 1;
        return i;
    }

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

/* On GUICONTROLS builds, fun_touch() lives in Draw.c — it has to
   compile on PICOMITEVGA+GUICONTROLS (where there's no touch panel but
   mouse / GUI CLICK drive the same semantics), and Touch.c isn't part
   of the VGA/HDMI build. The Draw.c version is source-aware via the
   gui_click_from_mouse ownership flag.

   On builds WITHOUT GUICONTROLS, none of that state exists — this
   file's simpler touch-panel-only implementation is the right one. */
#ifndef GUICONTROLS

void fun_touch(void)
{
    unsigned char *tp;
    if (checkstring(ep, (unsigned char *)"X"))
        iret = GetTouch(GET_X_AXIS);
    else if (checkstring(ep, (unsigned char *)"Y"))
        iret = GetTouch(GET_Y_AXIS);
    else if (checkstring(ep, (unsigned char *)"DOWN"))
        iret = TOUCH_DOWN;
    else if (checkstring(ep, (unsigned char *)"UP"))
        iret = !TOUCH_DOWN;
    else if (Option.TOUCH_CAP && checkstring(ep, (unsigned char *)"X2"))
        iret = GetTouch(GET_X_AXIS2);
    else if (Option.TOUCH_CAP && checkstring(ep, (unsigned char *)"Y2"))
        iret = GetTouch(GET_Y_AXIS2);
    /* TOUCH(XN n) / TOUCH(YN n): nth contact (n = 1..MAX_TOUCH_CONTACTS).
       n=1 matches TOUCH(X/Y), n=2 matches TOUCH(X2/Y2); higher contacts are
       available on a GT911 (up to 5). Any inactive / unsupported contact
       returns -1, so a program can walk n upward until it sees -1.
       n=0 returns the number of contacts currently touching, so a program
       can size its loop instead of probing for the -1. */
    else if ((tp = checkstring(ep, (unsigned char *)"XN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        iret = n ? GetTouchN(n - 1, GET_X_AXIS) : GetTouchCount();
    }
    else if ((tp = checkstring(ep, (unsigned char *)"YN")))
    {
        int n = getint(tp, 0, MAX_TOUCH_CONTACTS);
        iret = n ? GetTouchN(n - 1, GET_Y_AXIS) : GetTouchCount();
    }
#ifdef TOUCH_GESTURES
    /* Gesture accessors — same keywords and clear-on-read semantics as the
       GUICONTROLS fun_touch() in Draw.c. Two-finger results stay 0 on a
       resistive panel. */
    else if (checkstring(ep, (unsigned char *)"SWL"))
    {
        iret = (touch_swipe_dir == 1);
        if (iret)
            touch_swipe_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWR"))
    {
        iret = (touch_swipe_dir == 2);
        if (iret)
            touch_swipe_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWU"))
    {
        iret = (touch_swipe_dir == 3);
        if (iret)
            touch_swipe_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWD"))
    {
        iret = (touch_swipe_dir == 4);
        if (iret)
            touch_swipe_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"SWIPE"))
    {
        iret = touch_swipe_dir;
        touch_swipe_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"TAP"))
    {
        iret = touch_tap;
        touch_tap = 0;
    }
    else if (checkstring(ep, (unsigned char *)"HOLD"))
    {
        iret = touch_longpress;
        touch_longpress = 0;
    }
    else if (checkstring(ep, (unsigned char *)"DTAP"))
    {
        iret = touch_doubletap;
        touch_doubletap = 0;
    }
    else if (checkstring(ep, (unsigned char *)"EXPAND"))
    {
        iret = (touch_pinch_dir == 1);
        if (iret)
            touch_pinch_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"CONTRACT"))
    {
        iret = (touch_pinch_dir == 2);
        if (iret)
            touch_pinch_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"PINCH"))
    {
        iret = touch_pinch_dir;
        touch_pinch_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"CW"))
    {
        iret = (touch_rotate_dir == 1);
        if (iret)
            touch_rotate_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"CCW"))
    {
        iret = (touch_rotate_dir == 2);
        if (iret)
            touch_rotate_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"ROTATE"))
    {
        iret = touch_rotate_dir;
        touch_rotate_dir = 0;
    }
    else if (checkstring(ep, (unsigned char *)"TTAP"))
    {
        iret = touch_twotap;
        touch_twotap = 0;
    }
#endif /* TOUCH_GESTURES */
    else
        SyntaxError();
    targ = T_INT;
}
#endif