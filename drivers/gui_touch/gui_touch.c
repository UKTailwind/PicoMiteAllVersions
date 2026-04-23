/*
 * drivers/gui_touch/gui_touch.c — GUI touch-panel sub-commands for
 * cmd_guiMX170.
 *
 * Extracted from Draw.c's `#if !defined(PICOMITEVGA) && !defined(
 * MMBASIC_HOST)` blocks. Handles:
 *   - GUI BEEP           sound Option.TOUCH_Click clicker (GUICONTROLS)
 *   - GUI RESET LCDPANEL re-init SPI/I2C displays + touch PENIRQ
 *   - GUI CALIBRATE      4-corner touch calibration wizard
 *   - GUI TEST TOUCH     visual touch tracker
 *
 * Linked only on targets that have an SPI-LCD + touch-panel
 * (PICOMITE / WEB variants). VGA/HDMI/host link
 * drivers/gui_touch/gui_touch_stub.c.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif

int hal_port_gui_touch_cmd(unsigned char *cmdline) {
    unsigned char *p;
#ifdef GUICONTROLS
    if((p = checkstring(cmdline, (unsigned char *)"BEEP"))) {
        if(Option.TOUCH_Click == 0) error("Click option not set");
        ClickTimer = getint(p, 0, INT_MAX) + 1;
        return 1;
    }
#endif
    if((p = checkstring(cmdline, (unsigned char *)"RESET"))) {
        if((checkstring(p, (unsigned char *)"LCDPANEL"))) {
            hal_display_merge_abort();
            InitDisplaySPI(true);
            InitDisplayI2C(true);
            if((Option.TOUCH_CS || Option.TOUCH_IRQ) && !Option.TOUCH_CAP) {
                GetTouchValue(CMD_PENIRQ_ON);
                GetTouchAxis(CMD_MEASURE_X);
            }
            return 1;
        }
    }

    if((p = checkstring(cmdline, (unsigned char *)"CALIBRATE"))) {
        int tlx, tly, trx, try, blx, bly, brx, bry, midy;
        char *s;
        if(Option.TOUCH_CS == 0 && Option.TOUCH_IRQ ==0) error("Touch not configured");

        if(*p && *p != '\'') {
            getargs(&p, 9, (unsigned char *)",");
            if(argc != 9) error("Argument count");
            Option.TOUCH_SWAPXY = getinteger(argv[0]);
            Option.TOUCH_XZERO = getinteger(argv[2]);
            Option.TOUCH_YZERO = getinteger(argv[4]);
            Option.TOUCH_XSCALE = getinteger(argv[6]) / 10000.0;
            Option.TOUCH_YSCALE = getinteger(argv[8]) / 10000.0;
            if(!CurrentLinePtr) SaveOptions();
            return 1;
        } else {
            if(CurrentLinePtr) error("Invalid in a program");
            Option.TOUCH_SWAPXY = 0;
            Option.TOUCH_XZERO = 0;
            Option.TOUCH_YZERO = 0;
            Option.TOUCH_XSCALE = 1.0f;
            Option.TOUCH_YSCALE = 1.0f;
        }
        calibrate=1;
        GetCalibration(TARGET_OFFSET, TARGET_OFFSET, &tlx, &tly);
        GetCalibration(HRes - TARGET_OFFSET, TARGET_OFFSET, &trx, &try);
        if(abs(trx - tlx) < CAL_ERROR_MARGIN && abs(tly - try) < CAL_ERROR_MARGIN) {
            calibrate=0;
            error("Touch hardware failure %,%,%,%",tlx,trx,tly,try);
        }

        GetCalibration(TARGET_OFFSET, VRes - TARGET_OFFSET, &blx, &bly);
        GetCalibration(HRes - TARGET_OFFSET, VRes - TARGET_OFFSET, &brx, &bry);
        calibrate=0;
        midy = max(max(tly, try), max(bly, bry)) / 2;
        Option.TOUCH_SWAPXY = ((tly < midy && try > midy) || (tly > midy && try < midy));

        if(Option.TOUCH_SWAPXY) {
            swap(tlx, tly);
            swap(trx, try);
            swap(blx, bly);
            swap(brx, bry);
        }

        Option.TOUCH_XSCALE = (MMFLOAT)(HRes - TARGET_OFFSET * 2) / (MMFLOAT)(trx - tlx);
        Option.TOUCH_YSCALE = (MMFLOAT)(VRes - TARGET_OFFSET * 2) / (MMFLOAT)(bly - tly);
        Option.TOUCH_XZERO = ((MMFLOAT)tlx - ((MMFLOAT)TARGET_OFFSET / Option.TOUCH_XSCALE));
        Option.TOUCH_YZERO = ((MMFLOAT)tly - ((MMFLOAT)TARGET_OFFSET / Option.TOUCH_YSCALE));
        SaveOptions();
        brx = (HRes - TARGET_OFFSET) - ((brx - Option.TOUCH_XZERO) * Option.TOUCH_XSCALE);
        bry = (VRes - TARGET_OFFSET) - ((bry - Option.TOUCH_YZERO)*Option.TOUCH_YSCALE);
        if(abs(brx) > CAL_ERROR_MARGIN || abs(bry) > CAL_ERROR_MARGIN) {
            s = "Warning: Inaccurate calibration\r\n";
        } else
            s = "Done. No errors\r\n";
        CurrentX = CurrentY = 0;
        MMPrintString(s);
        strcpy((char *)inpbuf, "Deviation X = "); IntToStr((char *)inpbuf + strlen((char *)inpbuf), brx, 10);
        strcat((char *)inpbuf, ", Y = "); IntToStr((char *)inpbuf + strlen((char *)inpbuf), bry, 10); strcat((char *)inpbuf, " (pixels)\r\n");
        MMPrintString((char *)inpbuf);
        if(!Option.DISPLAY_CONSOLE) {
            GUIPrintString(0, 0, 0x11, JUSTIFY_LEFT, JUSTIFY_TOP, ORIENT_NORMAL, WHITE, BLACK, s);
            GUIPrintString(0, 36, 0x11, JUSTIFY_LEFT, JUSTIFY_TOP, ORIENT_NORMAL, WHITE, BLACK, (char *)inpbuf);
        }
        return 1;
    }
    return 0;
}

int hal_port_gui_touch_test(unsigned char *p) {
    if((checkstring(p, (unsigned char *)"TOUCH"))) {
        int x, y;
        ClearScreen(gui_bcolour);
        while(getConsole() < '\r') {
            x = GetTouch(GET_X_AXIS);
            y = GetTouch(GET_Y_AXIS);
            if(x != TOUCH_ERROR && y != TOUCH_ERROR) DrawBox(x - 1, y - 1, x + 1, y + 1, 0, WHITE, WHITE);
        }
        ClearScreen(gui_bcolour);
        return 1;
    }
    return 0;
}
