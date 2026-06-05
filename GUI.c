/***********************************************************************************************************************
PicoMite MMBasic

GUI.c

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
 * @file GUI.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for GUI MMBasic commands and functions
 */
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "float.h"
#define BTN_SIDE_BRIGHT -25
#define BTN_SIDE_DULL -50
#define BTN_SIDE_WIDTH 4
#define BTN_CAPTION_SHIFT 2

#define CLICK_DURATION 3 // the duration of a "click" in mSec

#define CTRL_NORMAL 0b0000000    // the control should be displayed as normal
#define CTRL_DISABLED 0b0000001  // the control is disabled and displayed in dull colours
#define CTRL_DISABLED2 0b0000010 // as above but only used when a keyboard is active
#define CTRL_HIDDEN 0b0000100    // the control is hidden
#define CTRL_HIDDEN2 0b0001000   // only used when setting a control to hidden
#define CTRL_SPINUP 0b0010000    // the spinbox up arrow is touched
#define CTRL_SPINDOWN 0b0100000  // ditto down
#define CTRL_SELECTED 0b1000000  // for a textbox or numberbox indicated that the box is selected

#define BTN_DISABLED -55

// define what the function DrawKeyboard() will do
#define KEY_OPEN 1
#define KEY_DRAW_ALL 2
#define KEY_KEY_DWN 3
#define KEY_KEY_UP 4
#define KEY_KEY_CANCEL 5

#define CTRL_BUTTON 1
#define CTRL_SWITCH 2
#define CTRL_RADIOBTN 3
#define CTRL_CHECKBOX 4
#define CTRL_LED 5
#define CTRL_SPINNER 6
#define CTRL_FRAME 7
#define CTRL_NBRBOX 8
#define CTRL_TEXTBOX 9
#define CTRL_FMTBOX 10
#define CTRL_DISPLAYBOX 11
#define CTRL_CAPTION 12
#define CTRL_AREA 13
#define CTRL_GAUGE 14
#define CTRL_BARGAUGE 15

#define MAX_PAGES 32 // the number of pages that can be specifies (this must not exceed 32)
extern void cmd_guiMX170(void);

// used by the gauge control to store extra data in the allocated string space
#define GAUGE_UNITS_SIZE 32
struct s_GaugeS
{
    char units[GAUGE_UNITS_SIZE];
    MMFLOAT ta, tb, tc;
    int c1, c2, c3, c4;
    int laststrlen, cval, csaved, lastfc, lastbc;
};

int SetupPage;
unsigned int CurrentPages;

short gui_font_width, gui_font_height;

int display_backlight; // the brightness of the backlight (1 to 100)

int gui_click_pin = 0; // the sound pin

volatile int CursorTimer;    // used to time the flashing cursor
volatile int ClickTimer = 0; // used to time the click when touch occurs
volatile int TouchTimer;     // used to time the response to touch
int CheckGuiFlag = 0;        // used to tell the mSec timer to call CheckGui()

short CurrentRef; // if the pen is down this is the control (or zero if not on a control)
short LastRef;    // this is the last control touched
short LastX;      // this is the x coord when the pen was lifted
short LastY;      // ditto for y

MMFLOAT CtrlSavedVal; // a temporary place to save a control's value

int TouchX, TouchY;
volatile bool TouchDown = false;
volatile bool TouchUp = false;
volatile bool TouchState = false;
/* Tracks which source owns the current TouchState=true. Set by the
   mouse/click edge detector in PicoMite.c when it latches a down-edge,
   cleared by the touch panel's edge detector. ProcessTouch uses it to
   decide whether to overwrite TouchX/Y via GetTouch() (touch path) or
   trust the already-latched values (mouse/click path). */
volatile bool gui_click_from_mouse = false;

#ifdef PICOMITEVGA
/* VGA/HDMI builds drive GUI controls from the mouse rather than a
   resistive/capacitive touch panel. Touch.c is not compiled on these
   targets, so supply minimal stubs for the touch-screen interface that
   the shared GUI code expects to link against. GetTouch() returning
   TOUCH_ERROR is treated by ProcessTouch() as "no pen down" and
   prevents the legacy touch-hit-test path from firing.
   When a USB multi-touch screen is plugged in (USES_USB_HOST builds),
   the stub returns contact 0's scaled coordinates instead, making the
   USB touch indistinguishable from a resistive panel to the shared
   GUI code. */
int TOUCH_GETIRQTRIS = 0;
int GetTouch(int axis)
{
#ifdef USBKEYBOARD
    if (usb_touch_active)
    {
        if (axis == GET_X_AXIS)
            return usb_touch_x;
        if (axis == GET_Y_AXIS)
            return usb_touch_y;
    }
#endif
    (void)axis;
    return TOUCH_ERROR;
}
/* Cursor implementation moved to Draw.c so it can be used on all
   PICOMITEVGA builds (incl. RP2040 VGA which doesn't define
   GUICONTROLS). The GUI CURSOR BASIC command lives in cmd_guiMX170. */
#endif

int last_x2, last_y2; // defaults used when creating controls
MMFLOAT last_inc, last_min, last_max;
int last_fcolour, last_bcolour;
MMFLOAT last_ta, last_tb, last_tc;
int last_c1 = -1, last_c2, last_c3, last_c4;
char last_units[32];

// used for keypads
int GUIKeyDown = 0;       // true if a key is down
bool KeyAltShift = false; // true if in alt keypad layout
int InvokingCtrl = 0;     // the number of the control that invoked the keypad
// int InCallback = 0;                     // true if we are running MM.KEYPRESS
// int InPause = 0;                        // true if we are inside a PAUSE command (used to suppress calling MM.KEYPRESS)
char CancelValue[256]; // save the value of the control in case the user cancels

bool gui_int_down = false;                 // true if the touch has triggered an interrupt
char *GuiIntDownVector = NULL;             // address of the interrupt routine or NULL if no interrupt
bool gui_int_up = false;                   // true if the release of the touch has triggered an interrupt
char *GuiIntUpVector = NULL;               // address of the interrupt routine or NULL if no interrupt
volatile bool DelayedDrawKeyboard = false; // a flag to indicate that the pop-up keyboard should be drawn AFTER the pen down interrupt
volatile bool DelayedDrawFmtBox = false;   // a flag to indicate that the pop-up formatted keyboard should be drawn AFTER the pen down interrupt

// struct s_ctrl *Ctrl;                    // list of the controls

int ChangeBright(int c, int pct);
void SpecialWritePixel(int x, int y, unsigned int tc, int status);
void SpecialDrawLine(int x1, int y1, int x2, int y2, int w, int tc, int status);
void SpecialDrawBox(int x1, int y1, int x2, int y2, int w, int c, int fill, int status);
void SpecialDrawRBox(int x1, int y1, int x2, int y2, int radius, int c, int fill, int status);
void SpecialPrintString(int x, int y, int jh, int jv, int jo, int fc, int bc, char *str, int status);
void SpecialDrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect, int status);
void SpecialDrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill, int status);
void DrawBorder(int x1, int y1, int x2, int y2, int w, int tc, int bc, int status);
void DrawBasicButton(int x1, int y1, int x2, int y2, int w, int up, int c, int status);
void DrawButton(int r);
void DrawSwitch(int r);
void DrawCheckBox(int r);
void DrawRadioBtn(int r);
void DrawLED(int r);
void DrawSpinner(int r);
void DrawFrame(int r);
void DrawCaption(int r);
void DrawGauge(int r);
void DrawBarGauge(int r);
char *GetCaption(int k, int is_alpha, int alt);
void GetSingleKeyCoord(int k, int is_alpha, int *x1, int *y1, int *x2, int *y2);
void DrawDisplayBox(int r);
void PopUpRedrawAll(int r, int disabled);
void KeyPadErase(int is_alpha);
void DrawSingleKey(int is_alpha, int x1, int y1, int x2, int y2, char *s, int fc, int bc);
void DrawKeyboard(int mode);
void DrawControl(int r);
void UpdateControl(int r);
void SetCtrlState(int r, int state, int err);
void DoCallback(int InvokingCtrl, char *key);
void DrawFmtBox(int mode);

////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// The GUI command is the base of all the sophisticated GUI drawing features in the Micromite Plus
//
////////////////////////////////////////////////////////////////////////////////////////////////////////

/*********************************************************************************************
 Most controls have similar argument lists, so this function is a "universal" argument
 collector for all controls.
 It returns with the control's reference number in case the caller wants to make some
 more adjustments.
 ********************************************************************************************/
int GetCtrlParams(int type, unsigned char *p)
{
    int r, a;
    struct s_GaugeS *GaugeS; // in case we are dealing with a GAUGE control
    if (!Option.MaxCtrls)
        StandardError(13);
    getcsargs(&p, 40);
    if ((argc & 1) != 1)
        StandardError(2);
    if (*argv[0] == '#')
        argv[0]++;
    r = getint(argv[0], 1, Option.MaxCtrls - 1);
    if (Ctrl[r].type)
        error("GUI reference number #% is in use", r);
    if (argc < 5)
        StandardError(2);
    a = 0;

    // setup the defaults
    Ctrl[r].x2 = last_x2;
    Ctrl[r].y2 = last_y2;
    if (type == CTRL_CAPTION)
    {
        Ctrl[r].fc = gui_fcolour; // a caption defaults to the system colours set by the COLOUR command
        Ctrl[r].bc = gui_bcolour;
    }
    else
    {
        Ctrl[r].fc = last_fcolour; // the others take the last colours used in the last command
        Ctrl[r].bc = last_bcolour;
    }
    Ctrl[r].inc = last_inc;
    Ctrl[r].min = last_min;
    Ctrl[r].max = last_max;

    // save the current font, needed if we redraw the control
    Ctrl[r].font = gui_font;

    // get string space
    Ctrl[r].s = GetMemory(MAXSTRLEN);

    // and the caption if the control needs it
    switch (type)
    { // these need a caption
    case CTRL_CAPTION:
    case CTRL_BUTTON:
    case CTRL_LED:
    case CTRL_SWITCH:
    case CTRL_FRAME:
    case CTRL_RADIOBTN:
    case CTRL_CHECKBOX:
        strcpy((char *)Ctrl[r].s, (char *)getCstring(argv[a += 2]));
    }

    if (type == CTRL_FMTBOX)
    {
        // get string space
        a += 2;
        if (*argv[a] == '"')
        {
            Ctrl[r].fmt = GetMemory(MAXSTRLEN);
            strcpy((char *)Ctrl[r].fmt, (char *)getCstring(argv[a]));
        }
        else
        {
            // Format String:
            // xc where x is the maximum value digit and c is the ghost char
            // these are concatenated to make a string of digits (eg, seconds = 5s9s)
            // Special constructs: 1 to 32 = 3dDd       1 to 12 = 1mMm
            //                     1 to 24 = 2hHh       0 to 179 = 1dLd9d
            // A separator is in brackets.  Eg (:)   or   (' )
            // Special chars: A = AM/PM    N = N/S    E = E/W
            if (checkstring(argv[a], (unsigned char *)"DATE1"))
                Ctrl[r].fmt = (unsigned char *)"3dDd(/)1mMm(/)9y9y";
            if (checkstring(argv[a], (unsigned char *)"DATE2"))
                Ctrl[r].fmt = (unsigned char *)"1mMm(/)3dDd(/)9y9y";
            if (checkstring(argv[a], (unsigned char *)"DATE3"))
                Ctrl[r].fmt = (unsigned char *)"9y9y9y9y(/)1mMm(/)3dDd";
            if (checkstring(argv[a], (unsigned char *)"TIME1"))
                Ctrl[r].fmt = (unsigned char *)"2hHh(:)5m9m";
            if (checkstring(argv[a], (unsigned char *)"TIME2"))
                Ctrl[r].fmt = (unsigned char *)"2hHh(:)5m9m(:)5s9s";
            if (checkstring(argv[a], (unsigned char *)"TIME3"))
                Ctrl[r].fmt = (unsigned char *)"1hMh(:)5m9m( )A";
            if (checkstring(argv[a], (unsigned char *)"TIME4"))
                Ctrl[r].fmt = (unsigned char *)"1hMh(:)5m9m(:)5s9s( )A";
            if (checkstring(argv[a], (unsigned char *)"DATETIME1"))
                Ctrl[r].fmt = (unsigned char *)"3dDd(/)1mMm(/)9y9y( )1hMh(:)5m9m( )A";
            if (checkstring(argv[a], (unsigned char *)"DATETIME2"))
                Ctrl[r].fmt = (unsigned char *)"3dDd(/)1mMm(/)9y9y( )2hHh(:)5m9m";
            if (checkstring(argv[a], (unsigned char *)"DATETIME3"))
                Ctrl[r].fmt = (unsigned char *)"1mMm(/)3dDd(/)9y9y( )1hMh(:)5m9m( )A";
            if (checkstring(argv[a], (unsigned char *)"DATETIME4"))
                Ctrl[r].fmt = (unsigned char *)"1mMm(/)3dDd(/)9y9y( )2hHh(:)5m9m";
            if (checkstring(argv[a], (unsigned char *)"LAT1"))
                Ctrl[r].fmt = (unsigned char *)"8d9d(` )5m9m(' )5s9s(\" )N";
            if (checkstring(argv[a], (unsigned char *)"LAT2"))
                Ctrl[r].fmt = (unsigned char *)"8d9d(` )5m9m(' )5s9s(.)9s(\" )N";
            if (checkstring(argv[a], (unsigned char *)"LONG1"))
                Ctrl[r].fmt = (unsigned char *)"1dLd9d(` )5m9m(' )5s9s(\" )E";
            if (checkstring(argv[a], (unsigned char *)"LONG2"))
                Ctrl[r].fmt = (unsigned char *)"1dLd9d(` )5m9m(' )5s9s(.)9s(\" )E";
            if (checkstring(argv[a], (unsigned char *)"ANGLE1"))
                Ctrl[r].fmt = (unsigned char *)"9d9d9d(` )5m9m(')";
        }
    }

    // get x1 and y1 - all controls require these
    Ctrl[r].x1 = getint(argv[a += 2], 0, HRes);
    if (argc < a)
        StandardError(2);
    Ctrl[r].y1 = getint(argv[a += 2], 0, VRes);

    // the fourth argument in GUI CAPTION is the justification
    if (type == CTRL_CAPTION)
    {
        a += 2;
        if (!(argc < a || *argv[a] == 0))
        { // if justification is specified
            int jh = 0, jv = 0, jo = 0;
            if (!GetJustification((char *)argv[a], &jh, &jv, &jo))
                if (!GetJustification((char *)getCstring(argv[a]), &jh, &jv, &jo))
                    error("Justification");
            Ctrl[r].x2 = jh | jv << 2 | jo << 4; // stuff the justification parameters into the short int
        }
        else
            Ctrl[r].x2 = 0;
    }
    else
    {
        // get the width - all controls except CAPTION need this
        // for GAUGE, LED, etc this is the radius
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_x2 = Ctrl[r].x2 = getint(argv[a], BTN_SIDE_WIDTH, HRes);

        // now get or set the height
        switch (type)
        {
        case CTRL_NBRBOX:
            *Ctrl[r].s = '0'; // this needs to be set, then fall thru
        case CTRL_BUTTON:
        case CTRL_SWITCH:
        case CTRL_FRAME:
        case CTRL_TEXTBOX:
        case CTRL_FMTBOX:
        case CTRL_DISPLAYBOX:
        case CTRL_SPINNER:
        case CTRL_AREA:
        case CTRL_BARGAUGE: // these all need the height in addition to the width
            if (argc > a + 2)
                if (*argv[a += 2] != 0)
                    last_y2 = Ctrl[r].y2 = getint(argv[a], BTN_SIDE_WIDTH, VRes);
            if (type != CTRL_BARGAUGE)
            {
                Ctrl[r].x2 += Ctrl[r].x1;
                Ctrl[r].y2 += Ctrl[r].y1;
            }
            break;

        case CTRL_CHECKBOX: // the check box does not need the height and is a special case
            // its touch sensitive area is different from its drawing parameters
            // we have the width/height in x2 and it is saved in Ctrl[r].inc
            Ctrl[r].inc = Ctrl[r].x2;
            Ctrl[r].y2 = Ctrl[r].y1 + Ctrl[r].x2;                                                      // calculate the touch sensitive area
            Ctrl[r].x2 = Ctrl[r].x1 + Ctrl[r].x2 + (gui_font_width * (strlen((char *)Ctrl[r].s) + 1)); // calculate the touch sensitive area
            break;

        case CTRL_LED:
        case CTRL_RADIOBTN: // the LED and radio button also do not need the height and are a special case
            // their touch sensitive area is stored in x1, y1, x2 and y2
            // the radius is stored in Ctrl[r].max and the X and Y centre of the button is calculated when drawing the control
            Ctrl[r].max = Ctrl[r].x2;
            Ctrl[r].x2 = Ctrl[r].x1 + Ctrl[r].max + (gui_font_width * (strlen((char *)Ctrl[r].s) + 1)); // calculate the touch sensitive area
            Ctrl[r].x1 -= Ctrl[r].max;
            Ctrl[r].y2 = Ctrl[r].y1 + Ctrl[r].max;
            Ctrl[r].y1 -= Ctrl[r].max;
            break;
        }
    }

    // get the foreground colour - all controls except the area control need this
    if (type != CTRL_AREA && argc > a + 2)
        if (*argv[a += 2] != 0)
            last_fcolour = Ctrl[r].fc = getint(argv[a], BLACK, WHITE);

    switch (type)
    { // these all need the background colour
    case CTRL_SPINNER:
    case CTRL_BUTTON:
    case CTRL_SWITCH:
    case CTRL_TEXTBOX:
    case CTRL_FMTBOX:
    case CTRL_NBRBOX:
    case CTRL_DISPLAYBOX:
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_bcolour = Ctrl[r].bc = getint(argv[a], 0, WHITE);
        break;
    case CTRL_CAPTION:
    case CTRL_GAUGE:
    case CTRL_BARGAUGE:
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_bcolour = Ctrl[r].bc = getint(argv[a], -1, WHITE);
        break;
    }

    if (type == CTRL_SPINNER)
    {
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_inc = Ctrl[r].inc = getnumber(argv[a]);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_min = Ctrl[r].min = getnumber(argv[a]);
        if (argc > a + 2)
            last_max = Ctrl[r].max = getnumber(argv[a += 2]);
    }

    if (type == CTRL_GAUGE || type == CTRL_BARGAUGE)
    {
        // special processing for a gauge
        // Note that a GAUGE uses the allocated string memory (Ctrl[r].s) for also storing other data
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_min = Ctrl[r].min = getnumber(argv[a]);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                last_max = Ctrl[r].max = getnumber(argv[a]);
        if (type == CTRL_GAUGE)
        {
            if (argc > a + 2)
                if (*argv[a += 2] != 0)
                    last_inc = Ctrl[r].inc = getnumber(argv[a]);
        }

        strcpy((char *)Ctrl[r].s, last_units); // setup the default default
        if (type == CTRL_GAUGE && argc > a + 2)
        {
            if (*argv[a += 2] != 0)
            {
                strcpy((char *)Ctrl[r].s, (char *)getCstring(argv[a])); // get the units caption
                Ctrl[r].s[GAUGE_UNITS_SIZE - 1] = 0;                    // truncate to max size
                strcpy(last_units, (char *)Ctrl[r].s);                  // save as default
            }
            else
                *last_units = *Ctrl[r].s = 0;
        }
        GaugeS = (void *)(Ctrl[r].s);
        GaugeS->c1 = -1;
        GaugeS->c2 = GaugeS->c3 = GaugeS->c4 = gui_fcolour;
        GaugeS->ta = GaugeS->tb = GaugeS->tc = Ctrl[r].max;
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->c1 = getint(argv[a], 0, WHITE);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->ta = getnumber(argv[a]);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->c2 = getint(argv[a], 0, WHITE);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->tb = getnumber(argv[a]);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->c3 = getint(argv[a], 0, WHITE);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->tc = getnumber(argv[a]);
        if (argc > a + 2)
            if (*argv[a += 2] != 0)
                GaugeS->c4 = getint(argv[a], 0, WHITE);
        if (type == CTRL_GAUGE)
            Ctrl[r].y2 = -1; // on first use draw the full gauge
    }

    if (argc > a + 1)
        StandardError(2);
    Ctrl[r].type = type;
    if (type == CTRL_GAUGE || type == CTRL_BARGAUGE)
    {
        Ctrl[r].value = Ctrl[r].min;
    }
    else
    {
        Ctrl[r].value = 0;
    }
    Ctrl[r].page = SetupPage;
    Ctrl[r].fcc = gui_fcolour;
    SetCtrlState(r, CTRL_NORMAL, false);
    return r;
}

/*  @endcond */

void cmd_gui(void)
{
    int r;
    unsigned char *p;

    CheckDisplay();

    if ((p = checkstring(cmdline, (unsigned char *)"PAGE")))
    {
        cmd_GUIpage(p);
        return;
    }
    if ((p = checkstring(cmdline, (unsigned char *)"BUTTON")))
    {
        r = GetCtrlParams(CTRL_BUTTON, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"SWITCH")))
    {
        r = GetCtrlParams(CTRL_SWITCH, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"CHECKBOX")))
    {
        r = GetCtrlParams(CTRL_CHECKBOX, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"RADIO")))
    {
        r = GetCtrlParams(CTRL_RADIOBTN, p);

        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"LED")))
    {
        r = GetCtrlParams(CTRL_LED, p);
        Ctrl[r].inc = 0;
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"FRAME")))
    {
        r = GetCtrlParams(CTRL_FRAME, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"NUMBERBOX")))
    {
        if ((checkstring(p, (unsigned char *)"CANCEL")))
        {
            if (!Option.MaxCtrls)
                StandardError(13);
            if (!InvokingCtrl)
                return;
            DrawKeyboard(KEY_KEY_CANCEL);
        }
        else
            r = GetCtrlParams(CTRL_NBRBOX, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"TEXTBOX")))
    {
        if ((checkstring(p, (unsigned char *)"CANCEL")))
        {
            if (!Option.MaxCtrls)
                StandardError(13);
            if (!InvokingCtrl)
                return;
            DrawKeyboard(KEY_KEY_CANCEL);
        }
        else
            r = GetCtrlParams(CTRL_TEXTBOX, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"FORMATBOX")))
    {
        if ((checkstring(p, (unsigned char *)"CANCEL")))
        {
            if (!Option.MaxCtrls)
                StandardError(13);
            if (!InvokingCtrl)
                return;
            DrawFmtBox(KEY_KEY_CANCEL);
        }
        else
            r = GetCtrlParams(CTRL_FMTBOX, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"SPINBOX")))
    {
        r = GetCtrlParams(CTRL_SPINNER, p);
        Ctrl[r].value = 0;
        if (Ctrl[r].value < Ctrl[r].min)
            Ctrl[r].value = Ctrl[r].min;
        if (Ctrl[r].value > Ctrl[r].max)
            Ctrl[r].value = Ctrl[r].max;
        FloatToStr((char *)Ctrl[r].s, Ctrl[r].value, 0, STR_AUTO_PRECISION, ' ');
        UpdateControl(r); // update the displayed string
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"DISPLAYBOX")))
    {
        r = GetCtrlParams(CTRL_DISPLAYBOX, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"CAPTION")))
    {
        r = GetCtrlParams(CTRL_CAPTION, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"GAUGE")))
    {
        r = GetCtrlParams(CTRL_GAUGE, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"BARGAUGE")))
    {
        r = GetCtrlParams(CTRL_BARGAUGE, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"AREA")))
    {
        r = GetCtrlParams(CTRL_AREA, p);
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"DELETE")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!(argc & 1))
            StandardError(2);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
                if (Ctrl[r].type != 0)
                {
                    SetCtrlState(r, CTRL_HIDDEN, true);
                    FreeMemorySafe((void **)&Ctrl[r].s);
                    if (Ctrl[r].fmt)
                        FreeMemorySafe((void **)&Ctrl[r].fmt);
                    memset(&Ctrl[r], 0, sizeof(struct s_ctrl));
                }
            return;
        }
        else
        {
            for (i = 0; i < argc; i += 2)
            {
                if (*argv[i] == '#')
                    argv[i]++;
                r = getint(argv[i], 1, Option.MaxCtrls - 1);
                SetCtrlState(r, CTRL_HIDDEN, true);
                FreeMemorySafe((void **)&Ctrl[r].s);
                if (Ctrl[r].fmt)
                    FreeMemorySafe((void **)&Ctrl[r].fmt);
                memset(&Ctrl[r], 0, sizeof(struct s_ctrl));
            }
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"DISABLE")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!(argc & 1))
            StandardError(2);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
                if (CurrentPages & (1 << Ctrl[r].page))
                    SetCtrlState(r, CTRL_DISABLED, false);
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            SetCtrlState(r, CTRL_DISABLED, true);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"HIDE")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (!(argc & 1))
            StandardError(2);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
                if (CurrentPages & (1 << Ctrl[r].page))
                    SetCtrlState(r, CTRL_HIDDEN, false);
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            SetCtrlState(r, CTRL_HIDDEN, true);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"ENABLE")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!(argc & 1))
            StandardError(2);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
            {
                if (CurrentPages & (1 << Ctrl[r].page))
                {
                    Ctrl[r].state &= ~(CTRL_DISABLED | CTRL_DISABLED2);
                    SetCtrlState(r, CTRL_NORMAL, false);
                }
            }
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            Ctrl[r].state &= ~(CTRL_DISABLED | CTRL_DISABLED2);
            SetCtrlState(r, CTRL_NORMAL, true);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"SHOW")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!(argc & 1))
            StandardError(2);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
            {
                if (CurrentPages & (1 << Ctrl[r].page))
                {
                    Ctrl[r].state &= ~CTRL_HIDDEN;
                    SetCtrlState(r, CTRL_NORMAL, false);
                }
            }
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            Ctrl[r].state &= ~CTRL_HIDDEN;
            SetCtrlState(r, CTRL_NORMAL, true);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"RESTORE")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (!(argc & 1))
            StandardError(2);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            for (r = 1; r < Option.MaxCtrls; r++)
            {
                if (CurrentPages & (1 << Ctrl[r].page))
                {
                    Ctrl[r].state &= ~(CTRL_DISABLED | CTRL_DISABLED2 | CTRL_HIDDEN);
                    SetCtrlState(r, CTRL_NORMAL, false);
                }
            }
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            Ctrl[r].state &= ~(CTRL_DISABLED | CTRL_DISABLED2 | CTRL_HIDDEN);
            SetCtrlState(r, CTRL_NORMAL, true);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"REDRAW")))
    {
        int i, r;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!(argc & 1))
            StandardError(2);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (checkstring(argv[0], (unsigned char *)"ALL"))
        {
            ClearScreen(gui_bcolour);
            for (r = 1; r < Option.MaxCtrls; r++)
                if (CurrentPages & (1 << Ctrl[r].page))
                {
                    if (Ctrl[r].type == CTRL_GAUGE)
                        Ctrl[r].y2 = -1; // this will force a full redraw of the gauge
                    UpdateControl(r);
                }
            return;
        }
        for (i = 0; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            if (Ctrl[r].type == CTRL_GAUGE)
                Ctrl[r].y2 = -1; // this will force a full redraw of the gauge
            UpdateControl(r);
        }
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"FCOLOUR")) || (p = checkstring(cmdline, (unsigned char *)"BCOLOUR")))
    {
        int i, r, c;
        getcsargs(&p, MAX_ARG_COUNT);
        if (!Option.MaxCtrls)
            StandardError(13);
        if (!(argc & 1) || argc < 3)
            StandardError(2);
        c = getint(argv[0], BLACK, WHITE);
        for (i = 2; i < argc; i += 2)
        {
            if (*argv[i] == '#')
                argv[i]++;
            r = getint(argv[i], 1, Option.MaxCtrls - 1);
            if (Ctrl[r].type == 0)
                error("Control #% does not exist", r);
            if (checkstring(cmdline, (unsigned char *)"FCOLOUR"))
                Ctrl[r].fc = c;
            else
                Ctrl[r].bc = c;
            UpdateControl(r);
        }
        return;
    }

    /*    if((p = checkstring(cmdline, "BEEP"))) {
            if(Option.TOUCH_Click == 0) error("Click option not set");
            ClickTimer = getint(p, 0, INT_MAX) + 1;
          return;
      }*/

    if ((p = checkstring(cmdline, (unsigned char *)"INTERRUPT")))
    {
        getcsargs(&p, 3);
        /* GUI INTERRUPT arms vectors fired by ProcessTouch when its
           TouchDown/TouchUp flags latch. Those flags can be driven by
           a touch panel (if configured + calibrated), by mouse motion,
           or by GUI CLICK DOWN/UP. Don't require touch hardware just
           to arm the vectors — if no input source is ever active the
           interrupt simply never fires, which is the right no-op. */
        if (*argv[0] == '0' && !isdigit(*(argv[0] + 1)))
            GuiIntDownVector = GuiIntUpVector = NULL;
        else
        {
            GuiIntDownVector = (char *)GetIntAddress(argv[0]); // get a pointer to the down interrupt routine
            if (argc == 3)
                GuiIntUpVector = (char *)GetIntAddress(argv[2]); // and for the up routine
            else
                GuiIntUpVector = NULL;
            InterruptUsed = true;
        }
        gui_int_down = gui_int_up = false;
        return;
    }

    if ((p = checkstring(cmdline, (unsigned char *)"SETUP")))
    {
        if (!Option.MaxCtrls)
            StandardError(13);
        SetupPage = getint(p, 1, MAX_PAGES) - 1;
        return;
    }

    // the final few commands are common to the MX170 and MX470 so execute them in Draw.c
    cmd_guiMX170();
}

void cmd_GUIpage(unsigned char *p)
{
    int i, r, OldPages;

    getcsargs(&p, MAX_ARG_COUNT);
    if (!(argc & 1))
        StandardError(2);
    if (!Option.MaxCtrls)
        StandardError(13);
    OldPages = CurrentPages;
    CurrentPages = 0;
    for (i = 0; i < argc; i += 2)
    { // get the new set of pages
        if (*argv[i] == '#')
            argv[i]++;
        CurrentPages |= (1 << (getint(argv[i], 1, MAX_PAGES) - 1));
    }

    // hide any that are showing but not on the new pages
    for (r = 0; r < Option.MaxCtrls; r++)
    { // step thru the controls
        if (Ctrl[r].type != 0)
        { // if the control is active
            if ((!(CurrentPages & (1 << Ctrl[r].page))) && (OldPages & (1 << Ctrl[r].page)) && (!(Ctrl[r].state & CTRL_HIDDEN)))
            {
                Ctrl[r].state |= CTRL_HIDDEN;
                if (r == CurrentRef)
                {
                    if (Ctrl[r].type == CTRL_BUTTON)
                        Ctrl[r].value = 0;
                    if (Ctrl[r].type == CTRL_SPINNER)
                        Ctrl[r].state &= ~(CTRL_SPINUP | CTRL_SPINDOWN);
                    LastRef = CurrentRef = 0;
                    gui_int_down = gui_int_up = false;
                }
                DrawControl(r);
                Ctrl[r].state &= ~CTRL_HIDDEN;
            }
        }
    }

    // show any that are on the new pages but not currently showing and are not hidden
    for (r = 0; r < Option.MaxCtrls; r++)
    { // step thru the controls
        if (Ctrl[r].type != 0)
        { // if the control is active
            if ((!(OldPages & (1 << Ctrl[r].page))) && (CurrentPages & (1 << Ctrl[r].page)) && (!(Ctrl[r].state & CTRL_HIDDEN)))
            {
                DrawControl(r);
            }
        }
    }
}

/*
 * @cond
 * The following section will be excluded from the documentation.
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// This is the detailed implementation of the GUI controls in the Micromite Plus
//
////////////////////////////////////////////////////////////////////////////////////////////////////////

int ChangeBright(int c, int pct)
{
    int r, g, b;
    if (pct == 0 || c <= 0)
        return c;
    r = (c >> 16) & 0xff;
    g = (c >> 8) & 0xff;
    b = c & 0xff;
    r += (r * pct) / 100;
    if (r > 255)
        r = 255;
    if (r < 0)
        r = 0;
    g += (g * pct) / 100;
    if (g > 255)
        g = 255;
    if (g < 0)
        g = 0;
    b += (b * pct) / 100;
    if (b > 255)
        b = 255;
    if (b < 0)
        b = 0;
    return RGB(r, g, b);
}

void SpecialWritePixel(int x, int y, unsigned int tc, int status)
{
    if (status & CTRL_HIDDEN)
    {
        tc = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        tc = ChangeBright(tc, BTN_DISABLED);
    }
    DrawPixel(x, y, tc);
}

void SpecialDrawLine(int x1, int y1, int x2, int y2, int w, int tc, int status)
{
    if (status & CTRL_HIDDEN)
    {
        tc = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        tc = ChangeBright(tc, BTN_DISABLED);
    }
    DrawLine(x1, y1, x2, y2, w, tc);
}

void SpecialDrawBox(int x1, int y1, int x2, int y2, int w, int c, int fill, int status)
{
    if (status & CTRL_HIDDEN)
    {
        c = gui_bcolour;
        fill = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        c = ChangeBright(c, BTN_DISABLED);
        if (fill != gui_bcolour)
            fill = ChangeBright(fill, BTN_DISABLED);
    }
    DrawBox(x1, y1, x2, y2, w, c, fill);
}

void SpecialDrawRBox(int x1, int y1, int x2, int y2, int radius, int c, int fill, int status)
{
    if (status & CTRL_HIDDEN)
    {
        c = gui_bcolour;
        fill = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        c = ChangeBright(c, BTN_DISABLED);
        if (fill != gui_bcolour)
            fill = ChangeBright(fill, BTN_DISABLED);
    }
    DrawRBox(x1, y1, x2, y2, radius, c, fill);
}

void SpecialPrintString(int x, int y, int jh, int jv, int jo, int fc, int bc, char *str, int status)
{
    int lines, i, y2;
    char t, *p, *idx[MAX_CAPTION_LINES + 1];

    // first check if the string contains one or more line split chars ('~')
    // while we are doing this save their addresses in idx[]
    idx[0] = p = str;
    lines = 1;
    while ((p = strchr((char *)p, '~')) != NULL && lines < MAX_CAPTION_LINES)
    {
        idx[lines] = p++;
        lines++;
    }

    if (lines > 1)
    {
        // this is two or more lines
        // recursively call this function to print each line
        idx[lines] = str + strlen(str);
        y2 = y - (gui_font_height / 2) * (lines - 1);
        for (i = 0; i < lines; i++)
        {
            t = *idx[i + 1];
            *idx[i + 1] = 0;
            SpecialPrintString(x, y2, jh, jv, jo, fc, bc, idx[i], status);
            *idx[i + 1] = t;
            if (t == '~')
                idx[i + 1]++;
            y2 += gui_font_height;
        }
    }
    else
    {
        // this is just a single line, so print it
        if (status & CTRL_HIDDEN)
        {
            fc = gui_bcolour;
            bc = gui_bcolour;
        }
        else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
        {
            fc = ChangeBright(fc, BTN_DISABLED);
            if (bc != gui_bcolour)
                bc = ChangeBright(bc, BTN_DISABLED);
        }
        GUIPrintString(x, y, gui_font, jh, jv, jo, fc, bc, str);
    }
}

void SpecialDrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect, int status)
{
    if (status & CTRL_HIDDEN)
    {
        c = gui_bcolour;
        fill = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        c = ChangeBright(c, BTN_DISABLED);
        if (fill != gui_bcolour)
            fill = ChangeBright(fill, BTN_DISABLED);
    }
    DrawCircle(x, y, radius, w, c, fill, aspect);
}

void SpecialDrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill, int status)
{
    if (status & CTRL_HIDDEN)
    {
        c = gui_bcolour;
        fill = gui_bcolour;
    }
    else if (status & (CTRL_DISABLED | CTRL_DISABLED2))
    {
        c = ChangeBright(c, BTN_DISABLED);
        if (fill != gui_bcolour)
            fill = ChangeBright(fill, BTN_DISABLED);
    }
    DrawTriangle(x0, y0, x1, y1, x2, y2, c, fill);
    DrawTriangle(x0, y0, x1, y1, x2, y2, c, -1);
}

void DrawBorder(int x1, int y1, int x2, int y2, int w, int tc, int bc, int status)
{
    int i;
    for (i = 0; i < w; i++)
    {
        SpecialDrawLine(x1 + i, y1 + i, x2 - i, y1 + i, 1, tc, status); // top border
        SpecialDrawLine(x1 + i, y1 + i, x1 + i, y2 - i, 1, tc, status); // left border
        SpecialDrawLine(x1 + i, y2 - i, x2 - i, y2 - i, 1, bc, status); // bottom
        SpecialDrawLine(x2 - i, y1 + i, x2 - i, y2 - i, 1, bc, status); // and right
    }
}

void DrawBasicButton(int x1, int y1, int x2, int y2, int w, int up, int c, int status)
{
    DrawBorder(x1, y1, x2, y2, w, ChangeBright(c, up ? BTN_SIDE_BRIGHT : BTN_SIDE_DULL), ChangeBright(c, BTN_SIDE_DULL), status);
    SpecialDrawBox(x1 + w, y1 + w, x2 - w, y2 - w, 0, 0, c, status); // fill in the face of the button
}

void DrawButton(int r)
{
    unsigned char *p, *pp, s[MAXSTRLEN];
    int bs = 0;

    DrawBasicButton(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x2, Ctrl[r].y2, BTN_SIDE_WIDTH, (Ctrl[r].value == 0), Ctrl[r].bc, Ctrl[r].state);

    // extract the up/down strings if necessary
    for (p = Ctrl[r].s, pp = s; *p != '|';)
    {
        if (*p == 0)
        {
            p = s - 1;
            bs = BTN_CAPTION_SHIFT;
            break;
        }
        *pp++ = *p++;
    }
    p++;
    *pp = 0;

    // draw the caption
    if (Ctrl[r].value == 0)
        SpecialPrintString(Ctrl[r].x1 + (Ctrl[r].x2 - Ctrl[r].x1) / 2, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fc, Ctrl[r].bc, (char *)s, Ctrl[r].state);
    else
        SpecialPrintString(Ctrl[r].x1 + (Ctrl[r].x2 - Ctrl[r].x1) / 2 + bs, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2 + bs, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fc, Ctrl[r].bc, (char *)p, Ctrl[r].state);
}

void DrawSwitch(int r)
{
    unsigned char *p, *pp, s[MAXSTRLEN];
    int half, on, twobtn, shift;

    // extract the up/down strings if necessary
    twobtn = true;
    for (p = Ctrl[r].s, pp = s; *p != '|';)
    {
        if (*p == 0)
        {
            p = s - 1;
            twobtn = false;
            break;
        }
        *pp++ = *p++;
    }
    p++;
    *pp = 0;

    on = (Ctrl[r].value == 0);
    if (on)
        shift = 0;
    else
        shift = BTN_CAPTION_SHIFT;
    if (twobtn)
    {
        half = Ctrl[r].x1 + (Ctrl[r].x2 - Ctrl[r].x1) / 2;
        if (!(on))
        { // use the min, max elements to store the active x area of the button
            Ctrl[r].min = half;
            Ctrl[r].max = Ctrl[r].x2;
        }
        else
        {
            Ctrl[r].min = Ctrl[r].x1;
            Ctrl[r].max = half;
        }
    }
    else
    {
        Ctrl[r].min = Ctrl[r].x1;
        Ctrl[r].max = half = Ctrl[r].x2;
    }

    DrawBasicButton(Ctrl[r].x1, Ctrl[r].y1, half, Ctrl[r].y2, BTN_SIDE_WIDTH, on, ChangeBright(Ctrl[r].bc, on ? 0 : -25), Ctrl[r].state);
    if (twobtn)
        DrawBasicButton(half, Ctrl[r].y1, Ctrl[r].x2, Ctrl[r].y2, BTN_SIDE_WIDTH, !on, ChangeBright(Ctrl[r].bc, on ? -25 : 0), Ctrl[r].state);

    // draw the captions
    if (on)
        shift = 0;
    else
        shift = BTN_CAPTION_SHIFT;
    SpecialPrintString(Ctrl[r].x1 + (half - Ctrl[r].x1) / 2 + shift, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2 + shift, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, ChangeBright(Ctrl[r].fc, (on || !twobtn) ? 0 : -25), ChangeBright(Ctrl[r].bc, on ? 0 : -25), (char *)s, Ctrl[r].state);
    if (!on)
        shift = 0;
    else
        shift = BTN_CAPTION_SHIFT;
    if (twobtn)
        SpecialPrintString(half + (Ctrl[r].x2 - half) / 2 + shift, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2 + shift, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, ChangeBright(Ctrl[r].fc, on ? -25 : 0), ChangeBright(Ctrl[r].bc, on ? -25 : 0), (char *)p, Ctrl[r].state);
}

// for the checkbox the width/height is saved in Ctrl[r].inc
void DrawCheckBox(int r)
{
    int i, w;

    SpecialDrawBox(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x1 + Ctrl[r].inc, Ctrl[r].y1 + Ctrl[r].inc, BTN_SIDE_WIDTH, ChangeBright(Ctrl[r].fc, -30), gui_bcolour, Ctrl[r].state);
    SpecialPrintString(Ctrl[r].x1 + Ctrl[r].inc + Ctrl[r].inc / 2, Ctrl[r].y1 + Ctrl[r].inc / 2, JUSTIFY_LEFT, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fcc, gui_bcolour, (char *)Ctrl[r].s, Ctrl[r].state);

    // draw the tick
    if (Ctrl[r].value != 0)
    {
        w = (Ctrl[r].inc / 28) + 1; // vary the tick size according to the box size
        for (i = -w; i <= w; i++)
        {
            SpecialDrawLine(Ctrl[r].x1 + i + BTN_SIDE_WIDTH * 2, Ctrl[r].y1 + BTN_SIDE_WIDTH * 2, Ctrl[r].x1 + Ctrl[r].inc + i - BTN_SIDE_WIDTH * 2, Ctrl[r].y2 - BTN_SIDE_WIDTH * 2, 1, Ctrl[r].fc, Ctrl[r].state);
            SpecialDrawLine(Ctrl[r].x1 + i + BTN_SIDE_WIDTH * 2, Ctrl[r].y2 - BTN_SIDE_WIDTH * 2, Ctrl[r].x1 + Ctrl[r].inc + i - BTN_SIDE_WIDTH * 2, Ctrl[r].y1 + BTN_SIDE_WIDTH * 2, 1, Ctrl[r].fc, Ctrl[r].state);
        }
    }
}

// the radio button is a special case, its touch sensitive area is different from its drawing parameters
// the touch sensitive area is stored in x1, y1 to x2, y2
// the buttons radius is stored in Ctrl[r].max and the X and Y centre of the button (xc and yc) are calculated using that
void DrawRadioBtn(int r)
{
    int i, frame;
    int xc = Ctrl[r].x1 + Ctrl[r].max;
    int yc = Ctrl[r].y1 + Ctrl[r].max;

    SpecialDrawCircle(xc, yc, Ctrl[r].max, BTN_SIDE_WIDTH, ChangeBright(Ctrl[r].fc, -30), gui_bcolour, 1.0, Ctrl[r].state);
    SpecialPrintString(xc + Ctrl[r].max + gui_font_width - (gui_font_width / 4), yc, JUSTIFY_LEFT, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fcc, gui_bcolour, (char *)Ctrl[r].s, Ctrl[r].state);

    if (Ctrl[r].value != 0)
    {
        // draw the button if this control has been selected
        SpecialDrawCircle(xc, yc, Ctrl[r].max - ((BTN_SIDE_WIDTH * 3) / 2), 0, 0, Ctrl[r].fc, 1.0, Ctrl[r].state);

        // make sure that all the other radio buttons are in the up state
        // first find the frame (if any) that the current button is in
        for (frame = 1; frame < Option.MaxCtrls; frame++)
            if (Ctrl[frame].type == CTRL_FRAME && Ctrl[r].page == Ctrl[frame].page && xc > Ctrl[frame].x1 && xc < Ctrl[frame].x2 && yc > Ctrl[frame].y1 && yc < Ctrl[frame].y2)
                break;
        // next look for any other radio buttons that are not the current one and is down and is in the same frame... and set it up
        for (i = 1; i < Option.MaxCtrls; i++)
        {
            if (Ctrl[i].type == CTRL_RADIOBTN && Ctrl[i].page == Ctrl[r].page && i != r && Ctrl[r].value != 0)
            {
                // if frame == MAX_CTRL this means that there is no frame and the whole screen can be considered the frame
                if (frame == Option.MaxCtrls || (Ctrl[i].x1 + Ctrl[i].max > Ctrl[frame].x1 && Ctrl[i].x1 + Ctrl[i].max < Ctrl[frame].x2 && Ctrl[i].y1 + Ctrl[i].max > Ctrl[frame].y1 && Ctrl[i].y1 + Ctrl[i].max < Ctrl[frame].y2))
                {
                    Ctrl[i].value = 0;
                    UpdateControl(i);
                }
            }
        }
    }
}

// the LED is a special case, its touch sensitive area is different from its drawing parameters
// the touch sensitive area is stored in x1, y1 to x2, y2
// the LED's radius is stored in Ctrl[r].max and the X and Y centre of the button (xc and yc) are calculated using that
void DrawLED(int r)
{
    int xc = Ctrl[r].x1 + Ctrl[r].max;
    int yc = Ctrl[r].y1 + Ctrl[r].max;

    SpecialDrawCircle(xc, yc, Ctrl[r].max, 0, 0, RGB(160, 160, 160), 1.0, Ctrl[r].state);
    SpecialDrawCircle(xc, yc, Ctrl[r].max - BTN_SIDE_WIDTH / 2, 0, 0, ChangeBright(Ctrl[r].fc, (Ctrl[r].value == 0) ? -65 : 0), 1.0, Ctrl[r].state);
    SpecialPrintString(xc + Ctrl[r].max + gui_font_width - (gui_font_width / 4), yc, JUSTIFY_LEFT, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fcc, gui_bcolour, (char *)Ctrl[r].s, Ctrl[r].state);
}

void DrawSpinner(int r)
{
    int x1, x2, y1, y2;
    int h, z;

    h = (Ctrl[r].y2 - Ctrl[r].y1);
    z = h / 6;
    x1 = Ctrl[r].x1;
    x2 = Ctrl[r].x2;
    y1 = Ctrl[r].y1;
    y2 = Ctrl[r].y2;
    SpecialDrawRBox(x1 + h, y1, x2 - h, y2, 10, Ctrl[r].fc, Ctrl[r].bc, Ctrl[r].state);
    FloatToStr((char *)Ctrl[r].s, Ctrl[r].value, 0, STR_AUTO_PRECISION, ' '); // convert the value to a string for display
    Ctrl[r].s[(x2 - x1 - h * 2) / gui_font_width] = 0;                        // truncate to the display width
    SpecialPrintString(x1 + (x2 - x1) / 2, y1 + h / 2, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fc, Ctrl[r].bc, (char *)Ctrl[r].s, Ctrl[r].state);
    if (Ctrl[r].state & CTRL_SPINUP)
        SpecialDrawTriangle(x2 - h / 2, y1 + z, x2 - z, y1 + h - z, x2 - h + z, y1 + h - z, Ctrl[r].bc, Ctrl[r].fc, Ctrl[r].state); // touched up pointing spin button
    else
        SpecialDrawTriangle(x2 - h / 2, y1 + z, x2 - z, y1 + h - z, x2 - h + z, y1 + h - z, Ctrl[r].fc, Ctrl[r].bc, Ctrl[r].state); // up pointing spin button
    if (Ctrl[r].state & CTRL_SPINDOWN)
        SpecialDrawTriangle(x1 + h / 2, y1 + h - z, x1 + z, y1 + z, x1 + h - z, y1 + z, Ctrl[r].bc, Ctrl[r].fc, Ctrl[r].state); // touched down pointing spin button
    else
        SpecialDrawTriangle(x1 + h / 2, y1 + h - z, x1 + z, y1 + z, x1 + h - z, y1 + z, Ctrl[r].fc, Ctrl[r].bc, Ctrl[r].state); // down pointing spin button
}

void DrawFrame(int r)
{
    SpecialDrawRBox(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x2, Ctrl[r].y2, 10, Ctrl[r].fc, -1, Ctrl[r].state); // the border
    SpecialPrintString(Ctrl[r].x1 + 15, Ctrl[r].y1, JUSTIFY_LEFT, JUSTIFY_MIDDLE, ORIENT_NORMAL, Ctrl[r].fc, gui_bcolour, (char *)Ctrl[r].s, Ctrl[r].state);
}

void DrawCaption(int r)
{
    SpecialPrintString(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x2 & 0b0000011, (Ctrl[r].x2 >> 2) & 0b0000011, (Ctrl[r].x2 >> 4) & 0b0000111, Ctrl[r].fc, Ctrl[r].bc, (char *)Ctrl[r].s, Ctrl[r].state);
}

void DrawGauge(int r)
{
    int x, y, c;
    MMFLOAT radius;
    int xo1, yo1, xo2, yo2, xi1, yi1, xi2, yi2;
    int i, v1, v2, v, ta, tb, tc;
    char buf[GAUGE_UNITS_SIZE * 2];
    struct s_GaugeS *GaugeS; // we store extra info in the string allocated to this control

    // these define the span of the gauge in degrees
    const int gstart = -225;
    const int gend = 45;

    // this defines the width of the gauge as a percentage of the radius
    const MMFLOAT width = 0.25;

    // this is the number of degrees resolution
    // smaller nbr gives a cleaner graph but is slower
    const int stepsize = 3;

    x = Ctrl[r].x1;
    y = Ctrl[r].y1;
    radius = Ctrl[r].x2;
    GaugeS = (void *)Ctrl[r].s;
    c = Ctrl[r].fc;

    // scale the value of the gauge and the thresholds to degrees within the span of the gauge
    v = (((Ctrl[r].value - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (gend - gstart)) + gstart;
    ta = (((GaugeS->ta - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (gend - gstart)) + gstart;
    tb = (((GaugeS->tb - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (gend - gstart)) + gstart;
    tc = (((GaugeS->tc - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (gend - gstart)) + gstart;

    // make sure that the value and the thresholds are within the range
    if (v < gstart)
        v = gstart;
    if (v > gend)
        v = gend;
    if (ta <= gstart)
        ta = gstart + 1;
    if (tb <= ta + 1)
        tb = ta + 2;
    if (tc <= tb + 1)
        tc = tb + 2;
    if (ta > gend)
        ta = gend;
    if (tb > gend)
        tb = gend;
    if (tc > gend)
        tc = gend;

    // set v1 and v2 so that we are drawing only the part that changed while making sure v1 < v2
    if (v > GaugeS->cval)
    {
        v1 = GaugeS->cval;
        v2 = v;
    }
    else
    {
        v2 = GaugeS->cval;
        v1 = v;
    }

    if (Ctrl[r].y2 != Ctrl[r].state || Ctrl[r].fc != GaugeS->lastfc || Ctrl[r].bc != GaugeS->lastbc)
    {
        // if the state of this control has changed we then need to redraw the complete gauge rather than just update the content of the gauge graph
        v1 = gstart;
        v2 = gend;
        GaugeS->laststrlen = GAUGE_UNITS_SIZE;
        Ctrl[r].y2 = Ctrl[r].state;
        GaugeS->cval = gend + 1;
        GaugeS->lastfc = Ctrl[r].fc;
        GaugeS->lastbc = Ctrl[r].bc;
    }

    if (v != GaugeS->cval)
    { // only draw the gauge part if something has changed
        // draw the gauge
        // this only draws the part changed since the previous call
        for (i = v1; i <= v2; i = i + stepsize)
        {
            xo1 = x + (cosf(Rad(i)) * radius);
            yo1 = y + (sinf(Rad(i)) * radius);
            xo2 = x + (cosf(Rad(i + stepsize)) * radius);
            yo2 = y + (sinf(Rad(i + stepsize)) * radius);
            xi1 = x + (cosf(Rad(i)) * (radius * (1 - width)));
            yi1 = y + (sinf(Rad(i)) * (radius * (1 - width)));
            xi2 = x + (cosf(Rad(i + stepsize)) * (radius * (1 - width)));
            yi2 = y + (sinf(Rad(i + stepsize)) * (radius * (1 - width)));

            c = Ctrl[r].fc;
            if (GaugeS->c1 >= 0)
                c = GaugeS->c1;
            if (i > ta)
                c = GaugeS->c2;
            if (i > tb)
                c = GaugeS->c3;
            if (i > tc)
                c = GaugeS->c4;

            if (v > i || v == gend)
            {
                // drawing a filled in part of the gauge
                SpecialDrawTriangle(xo1, yo1, xo2, yo2, xi1, yi1, c, c, Ctrl[r].state);
                SpecialDrawTriangle(xo2, yo2, xi1, yi1, xi2, yi2, c, c, Ctrl[r].state);
            }
            else
            {
                GaugeS->csaved = c;
                // erasing part of the gauge
                c = ChangeBright(c, -70);
                if (GaugeS->c1 < 0 || ta >= gend)
                {
                    c = Ctrl[r].bc;
                    GaugeS->csaved = Ctrl[r].fc;
                }
                SpecialDrawTriangle(xo1, yo1, xo2, yo2, xi1, yi1, c, c, Ctrl[r].state);
                SpecialDrawTriangle(xo2, yo2, xi1, yi1, xi2, yi2, c, c, Ctrl[r].state);
            }
            // draw the outside frame
            SpecialDrawLine(xo1, yo1, xo2, yo2, 1, Ctrl[r].fc, Ctrl[r].state);
            SpecialDrawLine(xi1, yi1, xi2, yi2, 1, Ctrl[r].fc, Ctrl[r].state);

            // draw the end stops
            if (i == gstart)
                SpecialDrawLine(xi1, yi1, xo1, yo1, 1, Ctrl[r].fc, Ctrl[r].state);
            if (i == gend)
                SpecialDrawLine(xi2, yi2, xo2, yo2, 1, Ctrl[r].fc, Ctrl[r].state);
        }
    }

    FloatToStr(buf, Ctrl[r].value, 0, Ctrl[r].inc, ' '); // convert the value to a string for display
    if (strlen(buf) < GaugeS->laststrlen)
    { // erase the last text if the length of the text has shrunk
        SpecialDrawCircle(x, y, (radius * 0.75) - 2, 0, Ctrl[r].bc, Ctrl[r].bc, 1.0, Ctrl[r].state);
    }
    GaugeS->laststrlen = strlen(buf);

    // get the colour of the text for the current value
    c = Ctrl[r].fc;
    if (GaugeS->c1 >= 0)
        c = GaugeS->c1;
    if (v > ta)
        c = GaugeS->c2;
    if (v > tb)
        c = GaugeS->c3;
    if (v > tc)
        c = GaugeS->c4;

    // draw the text
    if (*Ctrl[r].s)
    {
        strcat(buf, "~");
        strcat(buf, (char *)Ctrl[r].s);
    } // append the units string
    SpecialPrintString(x, y, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, c, Ctrl[r].bc, buf, Ctrl[r].state);

    GaugeS->cval = v;
}

void DrawBarGauge(int r)
{
    int vert, x, y, x2 = 0, y2 = 0, start, end, len;
    int v, ta, tb, tc;
    struct s_GaugeS *GaugeS; // we store extra info in the string allocated to this control

    if (Ctrl[r].x2 < Ctrl[r].y2)
    {
        // vertical bar
        // x, y refer to the bottom left of the bar (not the outline)
        // x2 is the width of the bar and len the overall length
        vert = true;
        x = Ctrl[r].x1 + 1;
        y = Ctrl[r].y1 + Ctrl[r].y2 - 1;
        x2 = Ctrl[r].x2 + x - 2;
        len = Ctrl[r].y2 - 2;
    }
    else
    {
        // horizontal bar
        // x, y refer to the top left coord of the bar
        // x2 is the width of the bar and len the overall length
        vert = false;
        x = Ctrl[r].x1 + 1;
        y = Ctrl[r].y1 + 1;
        y2 = Ctrl[r].y2 + y - 2;
        len = Ctrl[r].x2 - 2;
    }

    GaugeS = (void *)Ctrl[r].s;

    // scale the value of the gauge and the thresholds to pixels within the span of the gauge
    v = ((Ctrl[r].value - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (MMFLOAT)len;
    ta = ((GaugeS->ta - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (MMFLOAT)len;
    tb = ((GaugeS->tb - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (MMFLOAT)len;
    tc = ((GaugeS->tc - Ctrl[r].min) / (Ctrl[r].max - Ctrl[r].min)) * (MMFLOAT)len;

    // make sure that the value and the thresholds are within the range
    if (v < 0)
        v = 0;
    if (v > len)
        v = len;
    if (ta <= 0)
        ta = 1;
    if (tb <= ta + 1)
        tb = ta + 2;
    if (tc <= tb + 1)
        tc = tb + 2;
    if (ta > len)
        ta = len;
    if (tb > len)
        tb = len;
    if (tc > len)
        tc = len;

    SpecialDrawLine(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x1 + Ctrl[r].x2, Ctrl[r].y1, 1, Ctrl[r].fc, Ctrl[r].state);                           // draw the top of the outline
    SpecialDrawLine(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x1, Ctrl[r].y1 + Ctrl[r].y2, 1, Ctrl[r].fc, Ctrl[r].state);                           // draw the left side of the outline
    SpecialDrawLine(Ctrl[r].x1, Ctrl[r].y1 + Ctrl[r].y2, Ctrl[r].x1 + Ctrl[r].x2, Ctrl[r].y1 + Ctrl[r].y2, 1, Ctrl[r].fc, Ctrl[r].state); // draw the botton of the outline
    SpecialDrawLine(Ctrl[r].x1 + Ctrl[r].x2, Ctrl[r].y1, Ctrl[r].x1 + Ctrl[r].x2, Ctrl[r].y1 + Ctrl[r].y2, 1, Ctrl[r].fc, Ctrl[r].state); // draw the right side of the outline

    if (vert)
    {
        if (GaugeS->c1 < 0)
            GaugeS->c1 = Ctrl[r].fc;
        // draw the first bar
        if (v > ta)
            end = ta;
        else
            end = v;
        if (end > 0)
        {
            SpecialDrawBox(x, y, x2, y - end, 0, GaugeS->c1, GaugeS->c1, Ctrl[r].state);
            // draw the second bar
            start = end + 1;
            if (v > tb)
                end = tb;
            else
                end = v;
            if (end > start)
            {
                SpecialDrawBox(x, y - start, x2, y - end, 0, GaugeS->c2, GaugeS->c2, Ctrl[r].state);
                // draw the third bar
                start = end + 1;
                if (v > tc)
                    end = tc;
                else
                    end = v;
                if (end > start)
                {
                    SpecialDrawBox(x, y - start, x2, y - end, 0, GaugeS->c3, GaugeS->c3, Ctrl[r].state);
                    // finally draw the fourth bar
                    start = end + 1;
                    if (v > tc)
                        end = v;
                    if (end > start)
                    {
                        SpecialDrawBox(x, y - start, x2, y - end, 0, GaugeS->c4, GaugeS->c4, Ctrl[r].state);
                    }
                }
            }
        }
        // erase the remaining part of the bar
        if (end < len)
            SpecialDrawBox(x, y - end, x2, y - len, 0, Ctrl[r].bc, Ctrl[r].bc, Ctrl[r].state);
    }
    else
    {
        if (GaugeS->c1 < 0)
            GaugeS->c1 = Ctrl[r].fc;
        // draw the first bar
        if (v > ta)
            end = ta;
        else
            end = v;
        if (end > 0)
        {
            SpecialDrawBox(x, y, x + end, y2, 0, GaugeS->c1, GaugeS->c1, Ctrl[r].state);
            // draw the second bar
            start = end + 1;
            if (v > tb)
                end = tb;
            else
                end = v;
            if (end > start)
            {
                SpecialDrawBox(x + start, y, x + end, y2, 0, GaugeS->c2, GaugeS->c2, Ctrl[r].state);
                // draw the third bar
                start = end + 1;
                if (v > tc)
                    end = tc;
                else
                    end = v;
                if (end > start)
                {
                    SpecialDrawBox(x + start, y, x + end, y2, 0, GaugeS->c3, GaugeS->c3, Ctrl[r].state);
                    // finally draw the fourth bar
                    start = end + 1;
                    if (v > tc)
                        end = v;
                    if (end > start)
                    {
                        SpecialDrawBox(x + start, y, x + end, y2, 0, GaugeS->c4, GaugeS->c4, Ctrl[r].state);
                    }
                }
            }
        }
        // erase the remaining part of the bar
        if (end < len)
            SpecialDrawBox(x + end, y, x + len, y2, 0, Ctrl[r].bc, Ctrl[r].bc, Ctrl[r].state);
    }
}

const char *const KeypadCaption[12][2] = {{"Alt", "123"},
                                          {"0", "Can"},
                                          {"Ent", "Ent"},
                                          {"1", "."},
                                          {"2", ""},
                                          {"3", ""},
                                          {"4", "+"},
                                          {"5", "-"},
                                          {"6", "E"},
                                          {"7", "Del"},
                                          {"8", "CE"},
                                          {"9", ""}};

const char *const KeyboardCaption[33][4] = {
    // this is the bottom row of keys
    {"&12", "ABC", "&12", "ABC"},
    {"Z", "+", "z", "["},
    {"X", "-", "x", "]"},
    {"C", "=", "c", "{"},
    {"V", "?", "v", "}"},
    {"B", ":", "b", "|"},
    {"N", "/", "n", "\\"},
    {"M", ",", "m", ","},
    {".", ".", ".", "."},
    {"[ ]", "[ ]", "[ ]", "[ ]"},
    {"Ent", "Ent", "Ent", "Ent"},

    // this is the middle row
    {"Can", "Can", "Can", "Can"},
    {"A", "!", "a", "_"},
    {"S", "@", "s", "/"},
    {"D", "#", "d", "<"},
    {"F", "$", "f", ">"},
    {"G", "%", "g", ":"},
    {"H", "&", "h", ";"},
    {"J", "*", "j", "^"},
    {"K", "(", "k", "'"},
    {"L", ")", "l", "-"},
    {" ", " ", " ", " "},

    // and this is the top row
    {"Q", "1", "q", "1"},
    {"W", "2", "w", "2"},
    {"E", "3", "e", "3"},
    {"R", "4", "r", "4"},
    {"T", "5", "t", "5"},
    {"Y", "6", "y", "6"},
    {"U", "7", "u", "7"},
    {"I", "8", "i", "8"},
    {"O", "9", "o", "9"},
    {"P", "0", "p", "0"},
    {"Del", "Del", "Del", "Del"}};

char *GetCaption(int k, int is_alpha, int alt)
{
    if (is_alpha)
        return (char *)KeyboardCaption[k][alt];
    else
        return (char *)KeypadCaption[k][alt];
}

// returns the top left coord of a key (x1 & y1) and the bottom right (x2 & y2)
// these must be passed as pointers to ints
// k is the key number
// is_alpha is true if keypad is the text keyboard
void GetSingleKeyCoord(int k, int is_alpha, int *x1, int *y1, int *x2, int *y2)
{
    int horiznbr, width, height, keygap, BaseHoriz, BaseVert;

    if (is_alpha)
    {                  // if this is the keyboard
        horiznbr = 11; // number of horizontal buttons
        if (HRes == 800)
        {
            width = 69;
            height = 77;
            keygap = 4;
        }
        else if (HRes == 480)
        {
            width = 40;
            height = 41;
            keygap = 4;
        }
        else
        {
            keygap = 2;
            width = (HRes - keygap * 10) / 11;
            height = ((VRes / 2) - keygap * 2) / 3;
        }
        BaseVert = (Ctrl[InvokingCtrl].y2 > VRes / 2) ? (height * 3) + (keygap * 3) : VRes + keygap; // what half of the screen?
        BaseHoriz = HRes + keygap / 2;
    }
    else
    {                 // if this is the number pad
        horiznbr = 3; // number of horizontal buttons
        keygap = 5;
        height = VRes < HRes ? VRes : HRes;                                                 // find the smallest resolution
        height = width = ((height - keygap * 3) / 4 < 80) ? (height - keygap * 3) / 4 : 80; // scale the button size to the smallest resolution
        if (Ctrl[InvokingCtrl].y1 + height / 2 > VRes / 2)
        {
            BaseVert = (width * 4) + (keygap * 4); // should it be in the top part of the screen?
        }
        else
        {
            BaseVert = VRes + keygap; // or in the bottom
        }
        if (Ctrl[InvokingCtrl].x1 + width / 2 > HRes / 2)
        {
            BaseHoriz = (width * 3) + (keygap * 3);
        }
        else
        {
            BaseHoriz = HRes + keygap;
        }
    }

    *x1 = BaseHoriz - (horiznbr - (k % horiznbr)) * (width + keygap);
    *y1 = BaseVert - ((k / horiznbr) + 1) * (height + keygap);
    *x2 = *x1 + width;
    *y2 = *y1 + height;
}

void DrawDisplayBox(int r)
{
    int fc, bc;
    if (Ctrl[r].state & CTRL_SELECTED)
    {
        fc = Ctrl[r].bc;
        bc = Ctrl[r].fc;
    }
    else
    {
        fc = Ctrl[r].fc;
        bc = Ctrl[r].bc;
    }
    SpecialDrawRBox(Ctrl[r].x1, Ctrl[r].y1, Ctrl[r].x2, Ctrl[r].y2, 10, fc, bc, Ctrl[r].state);
    if (strlen((char *)Ctrl[r].s) > 2 && Ctrl[r].s[0] == '#' && Ctrl[r].s[1] == '#')
        SpecialPrintString(Ctrl[r].x1 + (Ctrl[r].x2 - Ctrl[r].x1) / 2, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, ChangeBright(fc, BTN_DISABLED), bc, (char *)Ctrl[r].s + 2, Ctrl[r].state);
    else
        SpecialPrintString(Ctrl[r].x1 + (Ctrl[r].x2 - Ctrl[r].x1) / 2, Ctrl[r].y1 + (Ctrl[r].y2 - Ctrl[r].y1) / 2, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, fc, bc, (char *)Ctrl[r].s, Ctrl[r].state);
}

// this will set all controls (except the control r) to disabled or non disabled
// note: r must always be a reference to the text box (never the keypad)
void PopUpRedrawAll(int r, int disabled)
{
    int i;
    for (i = 1; i < Option.MaxCtrls; i++)
    { // find all the other controls and set to disabled
        if (Ctrl[i].type && i != r)
        {
            if (disabled)
                Ctrl[i].state |= CTRL_DISABLED2; // set it to disable
            else
                Ctrl[i].state &= ~CTRL_DISABLED2; // set it to not disabled
            UpdateControl(i);
        }
    }
}

void KeyPadErase(int is_alpha)
{
    int x1, y1, x2, y2;
    int j1, j2;

    GetSingleKeyCoord(is_alpha ? 22 : 9, is_alpha, &x1, &y1, &j1, &j2); // get the top left coordinate of the key that is top left
    GetSingleKeyCoord(is_alpha ? 10 : 2, is_alpha, &j1, &j2, &x2, &y2); // get the bottom right coordinate of the key which is bottom right
    DrawBox(x1, y1, x2, y2, 0, 0, gui_bcolour);                         // erase the whole keyboard

    GUIKeyDown = -1;
}

void DrawSingleKey(int is_alpha, int x1, int y1, int x2, int y2, char *s, int fc, int bc)
{
    int fnt, i;
    fnt = gui_font;
    if (is_alpha)
    {
        if (HRes > 400)
            SetFont(0x31); // set a readable font
        else if (HRes > 400)
            SetFont(0x11);
        else
            SetFont(0x01);
    }
    else
    {
        if (VRes > 240)
            SetFont(0x31); // set a readable font
        else if (VRes > 160)
            SetFont(0x11);
        else
            SetFont(0x01);
    }
    SpecialDrawRBox(x1, y1, x2, y2, 10, fc, bc, 0); // the border
    SpecialPrintString(x1 + (x2 - x1) / 2, y1 + (y2 - y1) / 2, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, fc, bc, s, 0);
    if (is_alpha && *s == ' ')
    { // a space means that we should draw the shift symbol inside the button
        for (i = 0; i < ((x2 - x1) / 3); i++)
            SpecialDrawLine(x1 + (x2 - x1) / 2 - i / 2, y1 + i + (y2 - y1) / 3, x1 + (x2 - x1) / 2 + i / 2, y1 + i + (y2 - y1) / 3, 1, fc, 0);
    }
    SetFont(fnt);
}

// invoke the callback routine so that the programmer has the option of manipulating the result
// void DoCallback(int InvokingCtrl, char *key) {
//    char callstr[32];
//    char *nextstmtSaved = nextstmt;
//    if(!InPause && FindSubFun("MM.KEYPRESS", 0) >= 0) {
//        strcpy(callstr, "MM.KEYPRESS"); IntToStrPad(callstr + strlen(callstr), InvokingCtrl, ' ', 4, 10);
//        strcat(callstr, ", \""); strcat(callstr, key); strcat(callstr, "\"");
//        callstr[strlen(callstr)+1] = 0;                             // two NULL chars required to terminate the call
//        g_LocalIndex++;
//        InCallback = true;
//        ExecuteProgram(callstr);
//        InCallback = false;
//        g_LocalIndex--;
//        g_TempMemoryIsChanged = true;                                 // signal that temporary memory should be checked
//        nextstmt = nextstmtSaved;
//    }
//}

void DrawKeyboard(int mode)
{
    int x1, y1, x2, y2;
    int is_alpha, nbr_buttons;
    int i;
    unsigned char *p;

#ifdef GUICONTROLS
    /* Pop-up keyboard for TEXTBOX / NUMBERBOX draws directly to screen
       without going through DrawControl(). Hide the cursor first so its
       save buffer isn't stale relative to the about-to-change pixels.
       Next CursorRefresh repaints the cursor with a fresh save. */
    CursorHide();
#endif

    if (Ctrl[InvokingCtrl].type == CTRL_TEXTBOX)
    {
        nbr_buttons = 33;
        is_alpha = true;
    }
    else
    {
        nbr_buttons = 12;
        is_alpha = false;
    }

    if (mode == KEY_OPEN)
    {
        //        DoCallback(InvokingCtrl, "Open");
        KeyPadErase(Ctrl[InvokingCtrl].type == CTRL_TEXTBOX); // erase the background
        mode = KEY_DRAW_ALL;
    }

    // special processing for KEY_KEY_UP when the numeric keypad is in alt mode
    // this switches back to number mode for everything except for backspace when there are still chars in the buffer
    if (mode == KEY_KEY_UP && !is_alpha && KeyAltShift && GUIKeyDown > 0 && (GUIKeyDown != 9 || *Ctrl[InvokingCtrl].s == 0))
    {
        KeyAltShift = false; // redraw all keys in number mode
        mode = KEY_DRAW_ALL; // drop thru to redraw all
    }

    if (mode == KEY_DRAW_ALL)
    {
        // just draw all the buttons
        for (i = 0; i < nbr_buttons; i++)
        {
            GetSingleKeyCoord(i, is_alpha, &x1, &y1, &x2, &y2);
            DrawSingleKey(is_alpha, x1, y1, x2, y2, GetCaption(i, is_alpha, KeyAltShift), Ctrl[InvokingCtrl].fc, Ctrl[InvokingCtrl].bc);
        }
        GUIKeyDown = -1; // no key to be down
        return;
    }

    // if a key has been released simply redraw it in normal mode and return immediately
    if (mode == KEY_KEY_UP)
    {
        if (GUIKeyDown >= 0)
        {
            GetSingleKeyCoord(GUIKeyDown, is_alpha, &x1, &y1, &x2, &y2);
            DrawSingleKey(is_alpha, x1, y1, x2, y2, GetCaption(GUIKeyDown, is_alpha, KeyAltShift), Ctrl[InvokingCtrl].fc, Ctrl[InvokingCtrl].bc);
        }
        GUIKeyDown = -1;
        return;
    }

    if (mode != KEY_KEY_CANCEL)
    {
        for (i = 0; i < nbr_buttons; i++)
        {                                                       // search through the keys
            GetSingleKeyCoord(i, is_alpha, &x1, &y1, &x2, &y2); // find the coordinates for this key
            if (TouchX > x1 && TouchX < x2 && TouchY > y1 && TouchY < y2)
            { // if the touch is on this key
                if (!is_alpha && KeyAltShift)
                { // if this is the number keypad
                    // this ignores the invalid keys on the number pad when in the alt mode
                    switch (i)
                    {
                    case 4:
                    case 5:
                    case 11:
                        return;
                    case 3:
                        if (*Ctrl[InvokingCtrl].s == 0)
                            break;
                        else if (strchr((char *)Ctrl[InvokingCtrl].s, '.') != NULL)
                            return;
                        break;
                    case 8:
                        if (*Ctrl[InvokingCtrl].s == 0)
                            break;
                        else if (strchr((char *)Ctrl[InvokingCtrl].s, 'E') != NULL)
                            return;
                        break;
                    case 9:
                    case 10:
                        if (*Ctrl[InvokingCtrl].s == 0)
                            return;
                    }
                }
                GUIKeyDown = i;               // record the key that has been touched
                ClickTimer += CLICK_DURATION; // make a click
                DrawSingleKey(is_alpha, x1, y1, x2, y2, GetCaption(i, is_alpha, KeyAltShift), Ctrl[InvokingCtrl].bc, Ctrl[InvokingCtrl].fc);
            }
        }

        if (GUIKeyDown == -1)
            return;

        if (GUIKeyDown == 0)
        {                                     // this is the alt key
            KeyAltShift = !(KeyAltShift & 1); // toggle alt/text bit, also resets shift to no shift
            DrawKeyboard(KEY_DRAW_ALL);       // redraw the keyboard/keypad
            return;
        }

        if (GUIKeyDown == 21)
        {                                                                        // if this is the shift key on a textbox
            KeyAltShift = ((!(KeyAltShift & 0b10) << 1) | (KeyAltShift & 0b01)); // toggle the shift key
            DrawKeyboard(KEY_DRAW_ALL);                                          // redraw the keyboard/keypad
            return;
        }
    }

    if (mode == KEY_KEY_CANCEL || (is_alpha && GUIKeyDown == 11) || (!is_alpha && KeyAltShift && GUIKeyDown == 1))
    {                                                      // the user has hit the cancel key
        strcpy((char *)Ctrl[InvokingCtrl].s, CancelValue); // restore the current value
        GUIKeyDown = (is_alpha ? 10 : 2);                  // fake the Enter key
    }

    if (GUIKeyDown == (is_alpha ? 10 : 2))
    { // this is the enter key
        //        DoCallback(InvokingCtrl, GetCaption(GUIKeyDown, is_alpha, KeyAltShift));
        if (!is_alpha)
        {
            int t = STR_FLOAT_PRECISION;
            // get the number of digits that have been entered after the decimal point and use that to determine the precision of the display
            if (strchr((char *)Ctrl[InvokingCtrl].s, 'e') == NULL && strchr((char *)Ctrl[InvokingCtrl].s, 'E') == NULL && strchr((char *)Ctrl[InvokingCtrl].s, '.') != NULL)
            {
                t = strlen(strchr((char *)Ctrl[InvokingCtrl].s, '.') + 1);
                if (t < 1 || t > 16)
                    t = STR_FLOAT_PRECISION;
            }
            FloatToStr((char *)Ctrl[InvokingCtrl].s, Ctrl[InvokingCtrl].value = atof((char *)Ctrl[InvokingCtrl].s), 1, t, ' '); // convert to a float and put back on the display
        }
        KeyPadErase(is_alpha);                      // hide the keypad
        Ctrl[InvokingCtrl].state &= ~CTRL_SELECTED; // deselect the text box
        PopUpRedrawAll(0, false);                   // redraw all the controls
        LastRef = InvokingCtrl;
        InvokingCtrl = 0;
        return;
    }

    p = Ctrl[InvokingCtrl].s + strlen((char *)Ctrl[InvokingCtrl].s);
    if (!is_alpha)
    {
        // this is the number pad
        if (KeyAltShift)
        {
            // the keypad is in the alt mode
            // process the special keys
            switch (GUIKeyDown)
            {
            case 3:
                *p++ = '.';
                break;
            case 6:
                *p++ = '+';
                break;
            case 7:
                *p++ = '-';
                break;
            case 8:
                *p++ = 'E';
                break;
            case 9:
                *(--p) = 0;
                break;
            case 10:
                *Ctrl[InvokingCtrl].s = 0;
                break;
            default:
                return;
            }
        }
        else
        {
            *p++ = *GetCaption(GUIKeyDown, is_alpha, KeyAltShift); // otherwise use the caption
        }
    }
    else
    {
        // this is the text keyboard
        if (GUIKeyDown == 32)
        { // is this the delete key
            if (strlen((char *)Ctrl[InvokingCtrl].s))
                *(--p) = 0; // delete the char
        }
        else if (GUIKeyDown == 9)
        {               // if it is space
            *p++ = ' '; // insert a space
        }
        else
        {
            *p++ = *GetCaption(GUIKeyDown, is_alpha, KeyAltShift); // otherwise use the caption
        }
    }
    *p = 0;

    //    DoCallback(InvokingCtrl, GetCaption(GUIKeyDown, is_alpha, KeyAltShift));
    DrawControl(InvokingCtrl);
}

char *FmtKeyCaption[12] = {"<<<", "0", "Can", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
char FmtKeyEnabled[12];

void DrawFmtKeypad(char x, char prev, int GUIKeyDown)
{
    int i, dimfc, x1, y1, x2, y2;
    //    dp("1"); uSec(10000);

    for (i = 0; i < 12; i++)
        FmtKeyEnabled[i] = false;
    FmtKeyEnabled[0] = FmtKeyEnabled[2] = true;
    FmtKeyCaption[7] = "5";
    FmtKeyCaption[10] = "8";

    if (!isdigit(x))
    {
        switch (x)
        {
        case '+':
            FmtKeyCaption[10] = "+";
            FmtKeyCaption[7] = "-";
            x = 0;
            break;
        case 'A':
            FmtKeyCaption[10] = "AM";
            FmtKeyCaption[7] = "PM";
            x = 0;
            break;
        case 'N':
            FmtKeyCaption[10] = "N";
            FmtKeyCaption[7] = "S";
            x = 0;
            break;
        case 'E':
            FmtKeyCaption[10] = "E";
            FmtKeyCaption[7] = "W";
            x = 0;
            break;
        case 'M':
            if (prev == '0')
            {
                x = '9';
            }
            else
            {
                x = '2';
                FmtKeyEnabled[1] = true;
            }
            break;
        case 'D':
            if (prev == '0')
            {
                x = '9';
            }
            else
            {
                FmtKeyEnabled[1] = true;
                if (prev == '3')
                    x = '1';
                else
                    x = '9';
            }
            break;
        case 'H':
            FmtKeyEnabled[1] = true;
            if (prev == '0' || prev == '1')
                x = '9';
            else
                x = '3';
            break;
        case 'L':
            FmtKeyEnabled[1] = true;
            if (prev == '0')
                x = '9';
            else
                x = '7';
            break;
        }
        if (x == 0)
            FmtKeyEnabled[10] = FmtKeyEnabled[7] = true;
        else
            for (i = 3; i < x - '0' + 3; i++)
                FmtKeyEnabled[i] = true;
    }
    else
    {
        FmtKeyEnabled[1] = true;
        for (i = 3; i < x - '0' + 3; i++)
            FmtKeyEnabled[i] = true;
    }

    //    dp("1"); uSec(10000);
    dimfc = ChangeBright(Ctrl[InvokingCtrl].fc, BTN_DISABLED);
    for (i = 0; i < 12; i++)
    {
        //    dp("2"); uSec(10000);
        GetSingleKeyCoord(i, false, &x1, &y1, &x2, &y2);
        //    dp("3"); uSec(10000);
        if (i == GUIKeyDown)
            DrawSingleKey(false, x1, y1, x2, y2, FmtKeyCaption[i], Ctrl[InvokingCtrl].bc, Ctrl[InvokingCtrl].fc);
        else
            DrawSingleKey(false, x1, y1, x2, y2, FmtKeyCaption[i], FmtKeyEnabled[i] ? Ctrl[InvokingCtrl].fc : dimfc, Ctrl[InvokingCtrl].bc);
    }
}

void DrawFmtControl(int r, char *pfmt)
{
    unsigned char *p;
    p = Ctrl[r].s + strlen((char *)Ctrl[r].s);
    if (*pfmt)
        *p++ = 0xff; // marker to change the following text brightness to dim
    while (*pfmt)
    {
        if (*pfmt == '(')
        {
            pfmt++;
            while (*pfmt != ')')
                *p++ = *pfmt++;
            pfmt++;
        }
        else
        {
            if (*(pfmt + 1) == 0)
                *p++ = *pfmt++;
            else
            {
                *p++ = *(pfmt + 1);
                pfmt += 2;
            }
        }
    }
    *p = 0;
    DrawControl(r);
}

void DrawFmtBox(int mode)
{
    int i, x1, y1, x2, y2;
    static unsigned char *pfmt, *p;

#ifdef GUICONTROLS
    /* Pop-up format keypad for FORMATBOX draws directly to screen
       without going through DrawControl(). See note in DrawKeyboard(). */
    CursorHide();
#endif

    if (mode == KEY_OPEN)
    {
        KeyPadErase(false); // erase the background
        pfmt = Ctrl[InvokingCtrl].fmt;
        p = Ctrl[InvokingCtrl].s;
        DrawFmtKeypad(*pfmt, 0, 99); // just draw all the buttons
        DrawFmtControl(InvokingCtrl, (char *)pfmt);
        GUIKeyDown = -1; // no key to be down
        return;
    }

    if (mode == KEY_KEY_UP)
    {
        if (GUIKeyDown < 0)
            return;
        DrawFmtKeypad(*pfmt, *(p - 1), 99); // just draw all the buttons to show that the key is up
        return;
    }

    if (mode == KEY_KEY_DWN)
    {
        // if we are here we have the simple case of a key down
        for (i = 0; i < 12; i++)
        {                                                    // search through the keys
            GetSingleKeyCoord(i, false, &x1, &y1, &x2, &y2); // find the coordinates for this key
            if (TouchX > x1 && TouchX < x2 && TouchY > y1 && TouchY < y2)
            { // if the touch is on this key
                // get the key and do something with it
                if (FmtKeyEnabled[i] == false || (i == 0 && pfmt == Ctrl[InvokingCtrl].fmt))
                    return;                              // do nothing if key is disabled
                DrawFmtKeypad((char)*pfmt, *(p - 1), i); // show that the key is down
                ClickTimer += CLICK_DURATION;            // make a click

                if (i == 0)
                {
                    // it is the backspace key
                    if (*(pfmt - 1) == ')')
                    {
                        pfmt -= 2;
                        while (*pfmt-- != '(')
                            p--;
                        pfmt--;
                        p--;
                    }
                    else
                    {
                        pfmt -= 2;
                        p--;
                    }
                    *p = 0;
                }
                else if (i == 2)
                {
                    mode = KEY_KEY_CANCEL;
                    break;
                }
                else
                {
                    // this is an ordinary key (eg, 0 to 9)
                    p += strlen(strcpy((char *)p, FmtKeyCaption[i])); // add they key caption to the display box string
                    pfmt++;
                    if (*pfmt)
                        pfmt++;
                    if (*pfmt)
                    {
                        if (*pfmt == '(')
                        {
                            pfmt++;
                            while (*pfmt && *pfmt != ')')
                                *p++ = *pfmt++;
                            *p = 0;
                            if (*pfmt)
                                pfmt++;
                        }
                    }
                    if (*pfmt == 0)
                        break; // *pfmt == 0 means that we have reached the end of the format string
                }
                GUIKeyDown = i;
                DrawFmtControl(InvokingCtrl, (char *)pfmt);
                return;
            }
        }
    }

    if (*pfmt == 0 || mode == KEY_KEY_CANCEL)
    {
        if (mode == KEY_KEY_CANCEL)
            strcpy((char *)Ctrl[InvokingCtrl].s, CancelValue); // if we are cancelling first restore the current value
        KeyPadErase(false);                                    // hide the keypad
        DrawControl(InvokingCtrl);
        Ctrl[InvokingCtrl].state &= ~CTRL_SELECTED; // deselect the text box
        PopUpRedrawAll(0, false);                   // redraw all the controls
        LastRef = InvokingCtrl;
        InvokingCtrl = 0;
        return;
    }
}

// draw a control on the screen
void DrawControl(int r)
{
    int fnt;
#ifdef GUICONTROLS
    /* Remove the mouse cursor from the screen before we redraw a
       control. Two problems if we don't:
       (1) The cursor's save buffer captured the OLD control image —
           a later CursorErase would restore stale pixels on top of
           the newly-drawn control.
       (2) Control drawing paints opaque pixels over the cursor sprite
           leaving partial cursor remnants until the next refresh.
       Erasing first clears cursor_painted; the next CursorRefresh
       tick (~10 ms) repaints the cursor on top of the new control
       image with a fresh save buffer. */
    CursorHide();
#endif
    fnt = gui_font;
    SetFont(Ctrl[r].font);
    switch (Ctrl[r].type)
    {
    case CTRL_BUTTON:
        DrawButton(r);
        break;
    case CTRL_SWITCH:
        DrawSwitch(r);
        break;
    case CTRL_CHECKBOX:
        DrawCheckBox(r);
        break;
    case CTRL_RADIOBTN:
        DrawRadioBtn(r);
        break;
    case CTRL_LED:
        DrawLED(r);
        break;
    case CTRL_SPINNER:
        DrawSpinner(r);
        break;
    case CTRL_FRAME:
        DrawFrame(r);
        break;
    case CTRL_TEXTBOX:
    case CTRL_FMTBOX:
    case CTRL_NBRBOX:
    case CTRL_DISPLAYBOX:
        DrawDisplayBox(r);
        break;
    case CTRL_CAPTION:
        DrawCaption(r);
        break;
    case CTRL_GAUGE:
        DrawGauge(r);
        break;
    case CTRL_BARGAUGE:
        DrawBarGauge(r);
        break;
    default:
        break;
    }
    SetFont(fnt);
}

// similar to DrawControl() but it will only redraw the control if it is in the current
// set of pages for display and if it is not hidden
void UpdateControl(int r)
{
    if (Ctrl[r].type && (CurrentPages & (1 << Ctrl[r].page)) && !(Ctrl[r].state & CTRL_HIDDEN))
        DrawControl(r);
}

/* Definition of OptionVResreserved lives in Draw.c so that builds which
   don't compile GUI.c (e.g. RP2040 VGA) still link — Draw.c, RGB121.c,
   Editor.c and FileIO.c all reference it from VResEdit-style math. */

#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
/* ============================================================================
 * On-Screen Keyboard (OSK)
 *
 * Lives inside the bottom strip reserved by OptionVResreserved. Two modes:
 *   - System mode (osk_state==OSK_STATE_SYSTEM): drawn automatically at the
 *     command prompt and in the editor.
 *   - Program mode (osk_state==OSK_STATE_PROGRAM): only when a running program
 *     calls KEYBOARD ON.
 * Keys are injected via USR_KEYBRD_ProcessData() so INKEY$ / INPUT / KEY
 * interrupts all see them through the same path as the USB HID host.
 * ============================================================================ */
extern void USR_KEYBRD_ProcessData(uint8_t data);

#define OSK_STATE_OFF 0
#define OSK_STATE_SYSTEM 1
#define OSK_STATE_PROGRAM 2

#define OSK_COLS 12
#define OSK_ROWS 4
#define OSK_KEYS (OSK_COLS * OSK_ROWS)

#define OSK_GAP_LARGE 3
#define OSK_GAP_SMALL 2

/* Special caption-only marker values stored in the value table. Anything
   in 0x01..0xFF is emitted verbatim via USR_KEYBRD_ProcessData. */
#define OSK_V_NONE 0 /* dead cell (e.g. extra Space cells) */
#define OSK_V_SHIFT 0x100
#define OSK_V_CTRL 0x101
#define OSK_V_SYM 0x102
#define OSK_V_FN 0x103

static int osk_state = OSK_STATE_OFF;
static bool osk_shift = false;
static bool osk_ctrl = false;
static bool osk_sym = false;
static bool osk_fn = false;        /* Fn page: F-keys + Home/End/PgUp/PgDn/Ins/Del */
static int osk_pressed = -1;       /* key index currently shown pressed, -1 = none */
static int osk_drawn_reserved = 0; /* what OptionVResreserved was when last drawn */
static bool osk_dirty = false;     /* pixels invalidated by an external operation */
/* ---- Multi-touch (Option.Multi) state -----------------------------------
   In multi-touch mode the modifier keys (Sh, Ctl, Sym, Fn) behave like
   physical keys: pressed-and-held by one finger while another finger taps
   the target key. We track the second contact's edges across ProcessTouch
   calls via the USB touch driver's usb_touch_active2 + x2/y2 globals. */
static bool osk_finger2_was_active = false;
static int osk_finger2_key = -1; /* the cell finger 2 last landed on */
/* True while finger 1 is holding a momentary modifier in multi-touch mode.
   Used so the TouchUp release of that finger correctly disengages the
   modifier instead of latch-toggling it (the toggle-mode behaviour). */
static bool osk_finger1_holds_modifier = false;
/* Set by KEYBOARD OFF at the command prompt; cleared by KEYBOARD ON. While
   set, the prompt's automatic OSK_OnPromptIdle path skips both the restore
   of OptionVResreserved and the redraw, so KEYBOARD OFF at the prompt
   actually persists instead of being undone on the very next iteration. */
static bool osk_user_disabled = false;

/* Option.Height = (VRes - VRes*OptionVResreserved/100) / gui_font_height —
   it's computed by SetFont using the current OptionVResreserved. When we
   change OptionVResreserved we must re-derive it, otherwise consumers like
   FM that read Option.Height directly see a stale row count and lay out as
   if the strip weren't reserved (or weren't unreserved). Calling
   SetFont(gui_font) is the cheapest way to recompute, since SetFont also
   re-derives Option.Width — we don't need that, but it's harmless. */
static void OSK_RefreshOptionHeight(void)
{
    if (gui_font)
        SetFont(gui_font);
}

/* Reset all modifier latches AND multi-touch tracking state. Used by
   state-transition helpers (OSK_OnRunStart / OSK_OnPromptIdle / etc.) so
   stale shift/fn/etc. don't carry across program runs and a held finger
   isn't remembered after the OSK has been redrawn. */
static void OSK_ResetModifiers(void)
{
    osk_shift = osk_ctrl = osk_sym = osk_fn = false;
    osk_finger1_holds_modifier = false;
    osk_finger2_was_active = false;
    osk_finger2_key = -1;
}

static void OSK_ZeroReserved(void)
{
    OptionVResreserved = 0;
    OSK_RefreshOptionHeight();
}
static void OSK_RestoreReserved(void)
{
    OptionVResreserved = Option.VRes_reserved;
    OSK_RefreshOptionHeight();
}

/* Scroll the screen up by the about-to-be-reserved strip's height so the
   prompt's command history (or whatever's at the bottom of the screen) is
   preserved instead of overwritten when the OSK appears. Must be called
   while OptionVResreserved is still 0 — ScrollLCD reads it to decide the
   scrollable region, so calling after the strip has been reserved would
   only scroll within the already-shrunk editable area and leave the strip
   contents untouched. Rounds the scroll amount up to a whole font cell so
   text rows stay aligned. */
static void OSK_ScrollForStrip(void)
{
    if (OptionVResreserved != 0)
        return; /* strip already reserved */
    if (Option.VRes_reserved == 0)
        return; /* no strip configured */
    if (gui_font_height <= 0)
        return;
    int reservedH = VRes * Option.VRes_reserved / 100;
    int scroll_amount = ((reservedH + gui_font_height - 1) / gui_font_height) * gui_font_height;
    if (scroll_amount <= 0)
        return;
    if (ScrollLCD != NULL)
        ScrollLCD(scroll_amount);
    if (CurrentY >= scroll_amount)
        CurrentY -= scroll_amount;
    else
        CurrentY = 0;
}

/* Key indices follow the GetSingleKeyCoord convention: k%COLS is the column,
   k/COLS is the row counted from the BOTTOM (so row 3 is visually on top).
   Bottom row layout (constant across all pages):
     col 0: Sym    col 1: Fn     col 2: Ctl   col 3: Esc   col 4: Tab
     col 5..7: space bar (one wide button, three cells)
     col 8..11: arrows left/down/up/right */
#define OSK_K(row, col) ((row) * OSK_COLS + (col))
#define OSK_KEY_SHIFT_INDEX OSK_K(1, 0)
#define OSK_KEY_SYM_INDEX OSK_K(0, 0)
#define OSK_KEY_FN_INDEX OSK_K(0, 1)
#define OSK_KEY_CTRL_INDEX OSK_K(0, 2)

/* Bottom row is identical in every page. Captioned "Space" so the wide
   button shows a clear label; the empty-caption alternative also works but
   reads less obviously as the space bar. */
#define OSK_BOTTOM_CAPS \
    "Sym", "Fn", "Ctl", "Esc", "Tab", "Space", OSK_DEAD_CAP, OSK_DEAD_CAP, "<-", "v", "^", "->"
#define OSK_BOTTOM_VALS \
    OSK_V_SYM, OSK_V_FN, OSK_V_CTRL, 0x1B, '\t', ' ', OSK_V_NONE, OSK_V_NONE, 0x82, 0x81, 0x80, 0x83
#define OSK_DEAD_CAP "" /* caption for cells absorbed by a wide button */

static const char *const osk_cap_alpha_lo[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_CAPS,
    /* row 1 */ "Sh",
    "z",
    "x",
    "c",
    "v",
    "b",
    "n",
    "m",
    ",",
    ".",
    "/",
    "Del",
    /* row 2 */ "a",
    "s",
    "d",
    "f",
    "g",
    "h",
    "j",
    "k",
    "l",
    ";",
    "'",
    "Ent",
    /* row 3 */ "q",
    "w",
    "e",
    "r",
    "t",
    "y",
    "u",
    "i",
    "o",
    "p",
    "\"",
    "Bsp",
};
static const char *const osk_cap_alpha_hi[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_CAPS,
    /* row 1 */ "Sh",
    "Z",
    "X",
    "C",
    "V",
    "B",
    "N",
    "M",
    "<",
    ">",
    "?",
    "Del",
    /* row 2 */ "A",
    "S",
    "D",
    "F",
    "G",
    "H",
    "J",
    "K",
    "L",
    ":",
    "'",
    "Ent",
    /* row 3 */ "Q",
    "W",
    "E",
    "R",
    "T",
    "Y",
    "U",
    "I",
    "O",
    "P",
    "\"",
    "Bsp",
};
static const char *const osk_cap_sym_lo[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_CAPS,
    /* row 1 */ "Sh",
    "!",
    "@",
    "#",
    "$",
    "%",
    "^",
    "&",
    "*",
    "(",
    ")",
    "Del",
    /* row 2 */ "`",
    "=",
    "[",
    "]",
    "\\",
    "\"",
    "'",
    ",",
    ".",
    "/",
    "+",
    "Ent",
    /* row 3 */ "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "0",
    "-",
    "Bsp",
};
static const char *const osk_cap_sym_hi[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_CAPS,
    /* row 1 */ "Sh",
    "!",
    "@",
    "#",
    "$",
    "%",
    "^",
    "&",
    "*",
    "(",
    ")",
    "Del",
    /* row 2 */ "~",
    "+",
    "{",
    "}",
    "|",
    ":",
    "\"",
    "<",
    ">",
    "?",
    "=",
    "Ent",
    /* row 3 */ "!",
    "@",
    "#",
    "$",
    "%",
    "^",
    "&",
    "*",
    "(",
    ")",
    "_",
    "Bsp",
};
static const char *const osk_cap_fn[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_CAPS,
    /* row 1 */ "Sh",
    "<",
    ">",
    "|",
    ":",
    ";",
    "'",
    ",",
    ".",
    "/",
    "+",
    "Del",
    /* row 2 */ "F12",
    "Hm",
    "End",
    "PU",
    "PD",
    "Ins",
    "Del",
    "\"",
    "`",
    "~",
    "\\",
    "Ent",
    /* row 3 */ "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "Bsp",
};

/* Corresponding value tables. OSK_V_NONE entries are dead cells; bytes
   0x01..0xFF are emitted verbatim. Single-byte arrow / nav / function
   key codes match Hardware_Includes.h. */
static const int osk_val_alpha_lo[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_VALS,
    /* row 1 */ OSK_V_SHIFT,
    'z',
    'x',
    'c',
    'v',
    'b',
    'n',
    'm',
    ',',
    '.',
    '/',
    0x7F,
    /* row 2 */ 'a',
    's',
    'd',
    'f',
    'g',
    'h',
    'j',
    'k',
    'l',
    ';',
    '\'',
    '\r',
    /* row 3 */ 'q',
    'w',
    'e',
    'r',
    't',
    'y',
    'u',
    'i',
    'o',
    'p',
    '"',
    '\b',
};
static const int osk_val_alpha_hi[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_VALS,
    /* row 1 */ OSK_V_SHIFT,
    'Z',
    'X',
    'C',
    'V',
    'B',
    'N',
    'M',
    '<',
    '>',
    '?',
    0x7F,
    /* row 2 */ 'A',
    'S',
    'D',
    'F',
    'G',
    'H',
    'J',
    'K',
    'L',
    ':',
    '\'',
    '\r',
    /* row 3 */ 'Q',
    'W',
    'E',
    'R',
    'T',
    'Y',
    'U',
    'I',
    'O',
    'P',
    '"',
    '\b',
};
static const int osk_val_sym_lo[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_VALS,
    /* row 1 */ OSK_V_SHIFT,
    '!',
    '@',
    '#',
    '$',
    '%',
    '^',
    '&',
    '*',
    '(',
    ')',
    0x7F,
    /* row 2 */ '`',
    '=',
    '[',
    ']',
    '\\',
    '"',
    '\'',
    ',',
    '.',
    '/',
    '+',
    '\r',
    /* row 3 */ '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    '0',
    '-',
    '\b',
};
static const int osk_val_sym_hi[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_VALS,
    /* row 1 */ OSK_V_SHIFT,
    '!',
    '@',
    '#',
    '$',
    '%',
    '^',
    '&',
    '*',
    '(',
    ')',
    0x7F,
    /* row 2 */ '~',
    '+',
    '{',
    '}',
    '|',
    ':',
    '"',
    '<',
    '>',
    '?',
    '=',
    '\r',
    /* row 3 */ '!',
    '@',
    '#',
    '$',
    '%',
    '^',
    '&',
    '*',
    '(',
    ')',
    '_',
    '\b',
};
static const int osk_val_fn[OSK_KEYS] = {
    /* row 0 */ OSK_BOTTOM_VALS,
    /* row 1 */ OSK_V_SHIFT,
    '<',
    '>',
    '|',
    ':',
    ';',
    '\'',
    ',',
    '.',
    '/',
    '+',
    0x7F,
    /* row 2 */ 0x9C,
    0x86,
    0x87,
    0x88,
    0x89,
    0x84,
    0x7F,
    '"',
    '`',
    '~',
    '\\',
    '\r',
    /* row 3 */ 0x91,
    0x92,
    0x93,
    0x94,
    0x95,
    0x96,
    0x97,
    0x98,
    0x99,
    0x9A,
    0x9B,
    '\b',
};

static const char *const *OSK_CaptionTable(void)
{
    if (osk_fn)
        return osk_cap_fn;
    if (osk_sym)
        return osk_shift ? osk_cap_sym_hi : osk_cap_sym_lo;
    return osk_shift ? osk_cap_alpha_hi : osk_cap_alpha_lo;
}
static const int *OSK_ValueTable(void)
{
    if (osk_fn)
        return osk_val_fn;
    if (osk_sym)
        return osk_shift ? osk_val_sym_hi : osk_val_sym_lo;
    return osk_shift ? osk_val_alpha_hi : osk_val_alpha_lo;
}

/* Reserved-area top edge — same formula used by Editor.c and Draw.c. */
static int OSK_ReservedTop(void)
{
    return VRes - (VRes * OptionVResreserved / 100);
}

/* Layout: compute the box for key k. Mirrors GetSingleKeyCoord's math but
   anchored to the reserved strip rather than to half the screen. */
static void OSK_GetCoord(int k, int *x1, int *y1, int *x2, int *y2)
{
    int reservedH = VRes - OSK_ReservedTop();
    int gap = (HRes >= 480) ? OSK_GAP_LARGE : OSK_GAP_SMALL;
    int width = (HRes - gap * (OSK_COLS + 1)) / OSK_COLS;
    int height = (reservedH - gap * (OSK_ROWS + 1)) / OSK_ROWS;
    int baseHoriz = HRes - gap;
    int baseVert = VRes - gap;
    *x1 = baseHoriz - (OSK_COLS - (k % OSK_COLS)) * (width + gap);
    *y1 = baseVert - ((k / OSK_COLS) + 1) * (height + gap);
    *x2 = *x1 + width;
    *y2 = *y1 + height;
}

/* Color choices: highlight latched modifiers so the user can see the state. */
static void OSK_KeyColors(int k, int *fc, int *bc, bool pressed)
{
    int normal_fc = WHITE;
    int normal_bc = BLACK;
    int latched_fc = BLACK;
    int latched_bc = GREEN;

    bool latched = false;
    if (k == OSK_KEY_SHIFT_INDEX && osk_shift)
        latched = true;
    if (k == OSK_KEY_CTRL_INDEX && osk_ctrl)
        latched = true;
    if (k == OSK_KEY_SYM_INDEX && osk_sym)
        latched = true;
    if (k == OSK_KEY_FN_INDEX && osk_fn)
        latched = true;

    if (pressed)
    {
        *fc = normal_bc;
        *bc = normal_fc;
        return;
    }
    if (latched)
    {
        *fc = latched_fc;
        *bc = latched_bc;
        return;
    }
    *fc = normal_fc;
    *bc = normal_bc;
}

static void OSK_DrawKeyAt(int k)
{
    int x1, y1, x2, y2, fc, bc;
    const char *const *caps = OSK_CaptionTable();
    const int *vals = OSK_ValueTable();
    /* NONE cells are dead — drawn as part of their left-anchor's wide box. */
    if (vals[k] == OSK_V_NONE)
        return;
    /* DrawSingleKey calls GUIPrintString which clobbers CurrentX/CurrentY
       (they become the caption's drawing coordinates). Save and restore so
       the prompt's text cursor doesn't get yanked into the keyboard. */
    short savedX = CurrentX, savedY = CurrentY;
    OSK_GetCoord(k, &x1, &y1, &x2, &y2);
    /* Extend the box rightward to absorb adjacent NONE cells in the same
       row — this is how the space bar covers three columns as one button. */
    int nk = k + 1;
    while (nk < OSK_KEYS && (nk % OSK_COLS) != 0 && vals[nk] == OSK_V_NONE)
    {
        int xx1, yy1, xx2, yy2;
        OSK_GetCoord(nk, &xx1, &yy1, &xx2, &yy2);
        x2 = xx2;
        nk++;
    }
    OSK_KeyColors(k, &fc, &bc, k == osk_pressed);
    /* SpecialPrintString treats '~' inside the caption as a line-split marker
       and mutates the string in place. Our captions live in flash, so pass a
       writable stack copy. Keep the buffer comfortably larger than any
       caption we use (max 3 chars today). */
    char captionbuf[8];
    const char *src = caps[k] ? caps[k] : "";
    int i = 0;
    while (i < (int)sizeof(captionbuf) - 1 && src[i])
    {
        captionbuf[i] = src[i];
        i++;
    }
    captionbuf[i] = 0;
    /* Draw inline rather than via DrawSingleKey: DrawSingleKey hard-codes the
       font via SetFont(HRes>400 ? 0x31 : 0x01) which overrides whatever font
       the user / prompt has chosen. We want the OSK to render in the current
       gui_font so it stays visually consistent with the rest of the UI. */
    SpecialDrawRBox(x1, y1, x2, y2, 10, fc, bc, 0);
    SpecialPrintString(x1 + (x2 - x1) / 2, y1 + (y2 - y1) / 2,
                       JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL,
                       fc, bc, captionbuf, 0);
    CurrentX = savedX;
    CurrentY = savedY;
}

void OSK_DrawAll(void)
{
    if (!OptionVResreserved)
        return;
    /* If we previously drew at a larger reserve, wipe the band above the
       new top that would otherwise be left with stale OSK pixels. */
    if (osk_drawn_reserved > OptionVResreserved)
    {
        int old_top = VRes - (VRes * osk_drawn_reserved / 100);
        int new_top = OSK_ReservedTop();
        DrawRectangle(0, old_top, HRes - 1, new_top - 1, BLACK);
    }
    /* Wipe the strip first in case it has stale pixels from the editor or
       a program draw. OSK_ReservedTop() is a percentage of VRes and is
       generally NOT a multiple of gui_font_height, so a thin orphan band
       (a few scanlines) sits between the last whole console text row
       (Option.Height*gui_font_height) and the strip top. The console only
       writes/scrolls whole gui_font_height rows, so that band is never
       cleared by text and shows stale junk just above the keyboard (most
       visible at low VRes / mode 5). Extend the wipe up to the text
       boundary so the orphan band is cleared too. This only ever reaches
       UP by < gui_font_height into the orphan band, never into a live
       text line. */
    int wipe_top = OSK_ReservedTop();
    if (gui_font_height > 0)
    {
        int text_bottom = (wipe_top / gui_font_height) * gui_font_height;
        if (text_bottom < wipe_top)
            wipe_top = text_bottom;
    }
    DrawRectangle(0, wipe_top, HRes - 1, VRes - 1, BLACK);
    for (int k = 0; k < OSK_KEYS; k++)
        OSK_DrawKeyAt(k);
    if (Option.Refresh)
        Display_Refresh();
    osk_drawn_reserved = OptionVResreserved;
}

void OSK_Erase(void)
{
    /* Use the geometry we last drew at, not the current option, in case
       OptionVResreserved has just changed underneath us. */
    int reserved = osk_drawn_reserved;
    if (reserved <= 0)
        return;
    int top = VRes - (VRes * reserved / 100);
    DrawRectangle(0, top, HRes - 1, VRes - 1, BLACK);
    if (Option.Refresh)
        Display_Refresh();
    osk_pressed = -1;
    osk_drawn_reserved = 0;
}

/* Tell the OSK its on-screen pixels are no longer valid (e.g. after a
   screen-mode change or NEW). The next OSK_OnPromptIdle will repaint. */
void OSK_Invalidate(void)
{
    osk_dirty = true;
}

static int OSK_HitTest(int x, int y)
{
    int x1, y1, x2, y2;
    const int *vals = OSK_ValueTable();
    if (y < OSK_ReservedTop())
        return -1;
    for (int k = 0; k < OSK_KEYS; k++)
    {
        OSK_GetCoord(k, &x1, &y1, &x2, &y2);
        if (x >= x1 && x <= x2 && y >= y1 && y <= y2)
        {
            /* If we hit a NONE cell (part of a multi-cell wide button),
               walk left within the same row to the anchor that owns it. */
            while (vals[k] == OSK_V_NONE && (k % OSK_COLS) != 0)
                k--;
            return (vals[k] == OSK_V_NONE) ? -1 : k;
        }
    }
    return -1;
}

/* Emit a keystroke into the same console RX path used by the USB HID host.
   For Ctrl-letter, fold to 0x01..0x1F; for Alt we'd prefix ESC but we don't
   expose Alt in v1. */
static void OSK_EmitValue(int v)
{
    if (v == OSK_V_NONE)
        return;
    if (osk_ctrl)
    {
        if (v >= 'a' && v <= 'z')
            v = v - 'a' + 1;
        else if (v >= 'A' && v <= 'Z')
            v = v - 'A' + 1;
        /* For non-letter keys, Ctrl is a no-op pass-through. */
    }
    USR_KEYBRD_ProcessData((uint8_t)(v & 0xFF));
}

/* Called from ProcessTouch on the touch DOWN edge when the tap lands in
   the reserved strip and the OSK is active. */
void OSK_HandleTouchDown(int x, int y)
{
    int k = OSK_HitTest(x, y);
    if (k < 0)
        return;
    const int *vals = OSK_ValueTable();
    int v = vals[k];
    if (v == OSK_V_NONE)
        return;
    /* Multi-touch (Option.Multi): a finger-1 press on a modifier engages
       it on the DOWN edge (so a finger-2 tap that follows is modified) and
       a release on UP disengages it. Tap-toggle behaviour is preserved
       when Option.Multi is off. */
    if (Option.Multi &&
        (v == OSK_V_SHIFT || v == OSK_V_CTRL || v == OSK_V_SYM || v == OSK_V_FN))
    {
        if (v == OSK_V_SHIFT)
            osk_shift = true;
        else if (v == OSK_V_CTRL)
            osk_ctrl = true;
        else if (v == OSK_V_SYM)
        {
            osk_sym = true;
            osk_fn = false;
        }
        else
        {
            osk_fn = true;
            osk_sym = false;
        }
        osk_pressed = k;
        osk_finger1_holds_modifier = true;
        /* Sym / Fn change the page so the whole keyboard must repaint;
           Shift / Ctrl only need their own cell redrawn (page-2 captions
           also shift, so for shift do a full redraw). */
        if (v == OSK_V_SYM || v == OSK_V_FN || v == OSK_V_SHIFT)
            OSK_DrawAll();
        else
            OSK_DrawKeyAt(k);
        ClickTimer += CLICK_DURATION;
        return;
    }
    /* Visual: invert the pressed key. */
    if (osk_pressed >= 0 && osk_pressed != k)
    {
        int prev = osk_pressed;
        osk_pressed = -1;
        OSK_DrawKeyAt(prev);
    }
    osk_pressed = k;
    OSK_DrawKeyAt(k);
    ClickTimer += CLICK_DURATION;
}

/* Polled each ProcessTouch cycle (only when Option.Multi). Detects edges of
   the second-contact (usb_touch_active2). On a finger-2 down-edge, if it
   lands on a non-modifier and finger 1 is holding a modifier, the tapped
   key is emitted with the modifier applied — without disengaging finger
   1's hold, so the user can chord multiple keys without re-pressing Shift
   etc. Returns nothing; harmless when no second contact is active. */
void OSK_PollFinger2(void)
{
    if (!Option.Multi)
        return;
    if (!OSK_IsActive())
        return;
    bool now_active = usb_touch_active2;
    if (now_active && !osk_finger2_was_active)
    {
        /* finger 2 just landed — only act if it's inside the strip */
        int reserved_top = VRes - (VRes * OptionVResreserved / 100);
        if (usb_touch_y2 >= reserved_top)
        {
            int k = OSK_HitTest(usb_touch_x2, usb_touch_y2);
            if (k >= 0)
            {
                const int *vals = OSK_ValueTable();
                int v = vals[k];
                if (v != OSK_V_NONE && v != OSK_V_SHIFT && v != OSK_V_CTRL && v != OSK_V_SYM && v != OSK_V_FN)
                {
                    OSK_EmitValue(v);
                    /* Brief visual feedback — show the key as pressed
                       until finger 2 lifts. */
                    osk_finger2_key = k;
                    int prev = osk_pressed;
                    osk_pressed = k;
                    OSK_DrawKeyAt(k);
                    osk_pressed = prev;
                    ClickTimer += CLICK_DURATION;
                }
            }
        }
    }
    else if (!now_active && osk_finger2_was_active)
    {
        /* finger 2 lifted — un-highlight if we'd drawn it pressed */
        if (osk_finger2_key >= 0)
        {
            int prev = osk_finger2_key;
            osk_finger2_key = -1;
            OSK_DrawKeyAt(prev);
        }
    }
    osk_finger2_was_active = now_active;
}

/* Called from ProcessTouch on the touch UP edge.
   Returns true iff the OSK consumed the event (i.e. there was a pressed key
   waiting to be released). */
bool OSK_HandleTouchUp(void)
{
    if (osk_pressed < 0)
        return false;
    int k = osk_pressed;
    osk_pressed = -1;

    const int *vals = OSK_ValueTable();
    int v = vals[k];

    /* Multi-touch (Option.Multi) — modifier was engaged on the DOWN edge
       (in OSK_HandleTouchDown), so on UP we DISENGAGE it rather than
       latch-toggling. Any finger-2 chords that happened in between have
       already emitted via OSK_PollFinger2. */
    if (Option.Multi && osk_finger1_holds_modifier &&
        (v == OSK_V_SHIFT || v == OSK_V_CTRL || v == OSK_V_SYM || v == OSK_V_FN))
    {
        osk_finger1_holds_modifier = false;
        if (v == OSK_V_SHIFT)
            osk_shift = false;
        else if (v == OSK_V_CTRL)
            osk_ctrl = false;
        else if (v == OSK_V_SYM)
            osk_sym = false;
        else
            osk_fn = false;
        OSK_DrawAll();
        return true;
    }

    /* Tap-toggle behaviour (Option.Multi off): modifier keys latch on tap
       so the user can press them with a single finger. Sym and Fn are
       mutually exclusive page selectors. */
    if (v == OSK_V_SHIFT)
    {
        osk_shift = !osk_shift;
        OSK_DrawAll();
        return true;
    }
    if (v == OSK_V_SYM)
    {
        osk_sym = !osk_sym;
        if (osk_sym)
            osk_fn = false;
        OSK_DrawAll();
        return true;
    }
    if (v == OSK_V_FN)
    {
        osk_fn = !osk_fn;
        if (osk_fn)
            osk_sym = false;
        OSK_DrawAll();
        return true;
    }
    if (v == OSK_V_CTRL)
    {
        osk_ctrl = !osk_ctrl;
        OSK_DrawKeyAt(OSK_KEY_CTRL_INDEX);
        return true;
    }

    /* Non-modifier: emit, then unlatch one-shot modifiers (Shift, Ctrl).
       Sym is a sticky page toggle — leave it alone. */
    OSK_EmitValue(v);

    bool need_full_redraw = false;
    if (osk_ctrl)
    {
        osk_ctrl = false;
        need_full_redraw = true;
    }
    if (osk_shift)
    {
        osk_shift = false;
        need_full_redraw = true;
    }

    if (need_full_redraw)
        OSK_DrawAll();
    else
        OSK_DrawKeyAt(k); /* repaint un-pressed */
    return true;
}

bool OSK_IsActive(void) { return osk_state != OSK_STATE_OFF; }

/* State-transition helpers — called from interpreter entry points. */
void OSK_OnPromptIdle(void)
{
    /* User explicitly issued KEYBOARD OFF at the prompt — leave OSK off
       and keep OptionVResreserved at 0. Only an explicit KEYBOARD ON
       clears this and re-enables the prompt's automatic OSK. */
    if (osk_user_disabled)
    {
        osk_state = OSK_STATE_OFF;
        osk_dirty = false;
        return;
    }
    /* Coming back to the prompt — scroll the screen up by the about-to-
       be-reserved strip's height (must happen while OptionVResreserved is
       still 0) so prompt output / program tail isn't overwritten. Then
       restore the user's reserved-strip value. */
    OSK_ScrollForStrip();
    OSK_RestoreReserved();
    if (!OptionVResreserved)
    {
        if (osk_drawn_reserved > 0)
            OSK_Erase();
        osk_state = OSK_STATE_OFF;
        osk_dirty = false;
        return;
    }
    /* Safety net: after the scroll the cursor should already be above the
       strip, but if the scroll fell short (e.g. CurrentY was at the very
       bottom) clamp so the cursor doesn't sit inside the OSK. */
    {
        int reserved_top = VRes - (VRes * OptionVResreserved / 100);
        if (gui_font_height > 0 && CurrentY + gui_font_height > reserved_top)
        {
            CurrentY = reserved_top - gui_font_height;
            if (CurrentY < 0)
                CurrentY = 0;
            osk_dirty = true;
        }
    }
    /* Already drawn at the current size and pixels still valid — skip. */
    if (osk_state == OSK_STATE_SYSTEM && !osk_dirty && osk_drawn_reserved == OptionVResreserved)
        return;
    osk_state = OSK_STATE_SYSTEM;
    OSK_ResetModifiers();
    osk_dirty = false;
    /* Clear the console area between the bottom of the cursor's line and the
       strip top. OSK_ScrollForStrip scrolled the screen up to make room but
       only cleared the strip band itself, so stale content scrolled up into
       the console area just above the keyboard remains. It is NOT caught by
       OSK_DrawAll's font-boundary wipe when ReservedTop happens to be an
       exact multiple of gui_font_height (e.g. mode 5: VRes 150 * 30% ->
       top 105 = 7*15, so the junk sits inside the last "valid" console line
       the cursor never reaches). Mirrors the editor's clear in Editor.c.
       Nothing legitimately lives below the prompt cursor. */
    {
        int strip_top = VRes - (VRes * OptionVResreserved / 100);
        int clear_from = CurrentY + gui_font_height;
        if (gui_font_height > 0 && clear_from < strip_top)
            DrawRectangle(0, clear_from, HRes - 1, strip_top - 1,
                          DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour);
    }
    OSK_DrawAll();
}
void OSK_OnRunStart(void)
{
    /* Erase pixels we drew (if any) and surrender the strip to the program. */
    if (osk_state != OSK_STATE_OFF)
        OSK_Erase();
    osk_state = OSK_STATE_OFF;
    OSK_ResetModifiers();
    OSK_ZeroReserved();
}
/* Drop OSK state without touching pixels — for callers that just
   cleared the screen themselves (e.g. CLS) and only need to stop the
   touch dispatcher from firing on now-invisible buttons. */
void OSK_DropState(void)
{
    osk_state = OSK_STATE_OFF;
    osk_pressed = -1;
    osk_drawn_reserved = 0;
    osk_dirty = false;
    OSK_ResetModifiers();
    OSK_ZeroReserved();
}
void OSK_SetProgramActive(bool on)
{
    /* osk_user_disabled is a *prompt-time* latch — it represents the user
       explicitly typing KEYBOARD OFF or KEYBOARD ON at the prompt. A
       running program's own KEYBOARD ON / OFF must not touch it, otherwise
       a program that toggles the OSK mid-run could overrule the user's
       prompt-time preference (and prevent the auto-restore on program
       exit that the prompt loop performs when user_disabled is false). */
    bool at_prompt = (CurrentLinePtr == NULL);
    if (on)
    {
        if (at_prompt)
            osk_user_disabled = false;
        /* Scroll the screen up before we reserve the strip so existing
           content (prompt history, program output) survives instead of
           being painted over by the OSK. */
        OSK_ScrollForStrip();
        OSK_RestoreReserved();
        if (!OptionVResreserved)
            error("Set OPTION SCREEN OFFSET first");
        if (osk_state == OSK_STATE_PROGRAM || osk_state == OSK_STATE_SYSTEM)
        {
            /* Already visible — nothing further to do. */
            return;
        }
        osk_state = OSK_STATE_PROGRAM;
        OSK_ResetModifiers();
        OSK_DrawAll();
    }
    else
    {
        /* Idempotent OFF: erase any visible OSK, drop to OFF, zero the
           reserved strip. Only latch user_disabled if this was a
           prompt-side KEYBOARD OFF. */
        if (osk_state != OSK_STATE_OFF)
            OSK_Erase();
        osk_state = OSK_STATE_OFF;
        osk_pressed = -1;
        OSK_ResetModifiers();
        OSK_ZeroReserved();
        if (at_prompt)
            osk_user_disabled = true;
    }
}
bool OSK_IsProgramActive(void) { return osk_state == OSK_STATE_PROGRAM; }
bool OSK_IsUserDisabled(void) { return osk_user_disabled; }

#endif /* USBKEYBOARD && GUICONTROLS && PICOMITEVGA */

// check if the pen has touched or been lifted and animate the GUI elements as required
// this is called after every command (from check_interrupt()), in the getchar() loop and repeatedly in a pause
// TouchDown and TouchUp are set in the Timer 4 interrupt
void ProcessTouch(void)
{
    static int repeat = 0;
    static int waiting = false;
    int r, spinup;
    //    if(!Option.MaxCtrls || !TOUCH_GETIRQTRIS)return;
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
    /* Multi-touch poll. Must run on every ProcessTouch entry — including
       cycles with no TouchDown/TouchUp edge — because a finger-2 contact
       on a USB touch panel doesn't toggle the primary TouchDown/Up state
       (those follow contact 0 only). Without this poll the chord-style
       Shift+letter sequence would never be detected. */
    OSK_PollFinger2();
#endif
    if (repeat)
    {
        /* Spinner auto-repeat while held. Source depends on who
           latched the down-edge: touch panel uses TOUCH_DOWN; mouse,
           synthetic click and the GUI CLICK PIN are all gated by the
           same OR. */
        bool held;
        if (gui_click_from_mouse)
            held = (nunstruct[2].L || gui_click_synthetic_down || click_pin_pressed()) ? true : false;
        else
            held = TOUCH_DOWN
#ifdef USBKEYBOARD
                   || usb_touch_active
#endif
                ;
        if (held)
            if (TouchTimer < repeat)
                return;
            else
                TouchDown = true;
        else
            repeat = 0;
    }
    else
    {
        if (waiting)
        {
            /* 50 ms debounce. Touch panels bounce; a mouse button or a
               synthetic GUI CLICK does not. Skipping the gate for the
               non-touch sources also avoids racing cmd_gui_click's own
               50 ms wait loop, which would otherwise expire just as
               the dispatch becomes eligible. */
            if (TouchTimer < 50 && !gui_click_from_mouse)
                return;
            else
                waiting = false;
        }
        else
        {
            waiting = true;
            TouchTimer = 0; // TouchTimer is incremented every mS in the Timer 4 interrupt
        }
    }

    if (!(TouchUp || TouchDown))
        return; // quick exit if there is nothing to do
    if (TouchDown)
    {
        // touch has just occurred
        if (gui_click_from_mouse)
        {
            /* Mouse / synthetic click: TouchX/TouchY were latched at
               the down-edge in PicoMite.c — don't overwrite them. */
        }
        else
        {
            /* Touch panel: read the coordinates now via GetTouch.
               If the pen has already lifted (TOUCH_ERROR), abort. */
            TouchX = GetTouch(GET_X_AXIS);
            TouchY = GetTouch(GET_Y_AXIS);
        }
        /* Record swipe-start at the down-edge for the mouse / USB-touch
           sources. A wired panel's gestures are driven separately by
           PanelGestureService() in routinechecks(), so they work even
           with no GUI controls allocated (ProcessTouch only runs when
           Ctrl != NULL) — don't double-drive the machine here. */
        if (gui_click_from_mouse
#ifdef USBKEYBOARD
            || usb_touch_present
#endif
        )
            touch_gesture_on_down(TouchX, TouchY);
        LastRef = CurrentRef = 0;
        TouchUp = TouchDown = false;
        if (!gui_click_from_mouse && TouchX == TOUCH_ERROR)
            return; // pen lifted before we read
        if (InvokingCtrl)
        { // the keyboard/keypad takes complete control when activated
            if (Ctrl[InvokingCtrl].type == CTRL_FMTBOX)
                DrawFmtBox(KEY_KEY_DWN);
            else
                DrawKeyboard(KEY_KEY_DWN);
            gui_int_down = false;
            return;
        }
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
        if (OSK_IsActive() && OptionVResreserved && TouchY >= (VRes - (VRes * OptionVResreserved / 100)))
        {
            OSK_HandleTouchDown(TouchX, TouchY);
            gui_int_down = false;
            return;
        }
#endif

        gui_int_down = true; // signal that a MMBasic interrupt is valid
        for (r = 1; r < Option.MaxCtrls; r++)
        {
            if (Ctrl[r].type && TouchX >= Ctrl[r].x1 && TouchY >= Ctrl[r].y1 && TouchX <= Ctrl[r].x2 && TouchY <= Ctrl[r].y2)
            {
                if (!(CurrentPages & (1 << Ctrl[r].page)))
                    continue; // ignore if the page is not displayed
                if (Ctrl[r].state & (CTRL_DISABLED | CTRL_DISABLED2 | CTRL_HIDDEN))
                    continue; // ignore if control is disabled
                switch (Ctrl[r].type)
                {
                case CTRL_SWITCH:
                    if (!(TouchX >= Ctrl[r].min && TouchX <= Ctrl[r].max))
                        return; // skip if it is not the touch sensitive area (depends on the switch state)
                    Ctrl[r].value = !Ctrl[r].value;
                    break;

                case CTRL_BUTTON:
                case CTRL_RADIOBTN:;
                case CTRL_AREA:
                    Ctrl[r].value = 1;
                    break;

                case CTRL_FRAME:
                    continue; // a frame does not respond to touch but it might contain controls that do

                case CTRL_CAPTION:
                    continue; // a caption does not respond to touch but it might be overlayed by an AREA control

                case CTRL_DISPLAYBOX:
                case CTRL_LED:
                    CurrentRef = r;
                    return; // these controls do not respond to touch

                case CTRL_SPINNER:
                    if (TouchX < (Ctrl[r].x1 + (Ctrl[r].y2 - Ctrl[r].y1)))      // if it is in the left side
                        spinup = false;                                         // it is spin down
                    else if (TouchX > (Ctrl[r].x2 - (Ctrl[r].y2 - Ctrl[r].y1))) // if it is in the right side
                        spinup = true;                                          // it is spin up
                    else
                        return;
                    if (spinup)
                    {
                        if (Ctrl[r].state & CTRL_SPINDOWN)
                            return;
                        if (Ctrl[r].value + Ctrl[r].inc > Ctrl[r].max)
                        {
                            repeat = 0;
                            break;
                        }
                        Ctrl[r].state |= CTRL_SPINUP;
                        Ctrl[r].value += Ctrl[r].inc;
                    }
                    else
                    {
                        if (Ctrl[r].state & CTRL_SPINUP)
                            return;
                        if (Ctrl[r].value - Ctrl[r].inc < Ctrl[r].min)
                        {
                            repeat = 0;
                            break;
                        }
                        Ctrl[r].state |= CTRL_SPINDOWN;
                        Ctrl[r].value -= Ctrl[r].inc;
                    }
                    if (Ctrl[r].value < Ctrl[r].inc && Ctrl[r].value > -Ctrl[r].inc)
                        Ctrl[r].value = 0;
                    if (repeat == 0)
                        repeat = 500;
                    else
                        repeat = 100;
                    TouchTimer = 0;
                    break;

                case CTRL_NBRBOX:
                    FloatToStr((char *)Ctrl[r].s, Ctrl[r].value, 0, STR_AUTO_PRECISION, ' '); // set the string value to be saved
                                                                                              // fall thru

                case CTRL_TEXTBOX:
                    strcpy(CancelValue, (char *)Ctrl[r].s); // save the current value in case the user cancels
                    *Ctrl[r].s = 0;                         // set it to an empty string
                    Ctrl[r].state |= CTRL_SELECTED;         // select the number text/box
                    PopUpRedrawAll(r, true);
                    CurrentRef = InvokingCtrl = r; // tell the keypad what text/number box it is servicing
                    GUIKeyDown = -1;
                    KeyAltShift = 0;
                    DrawControl(r);
                    ClickTimer += CLICK_DURATION;
                    if (GuiIntDownVector == NULL)
                        DrawKeyboard(KEY_OPEN); // initial draw of the keypad
                    else
                        DelayedDrawKeyboard = true;
                    return;

                case CTRL_FMTBOX:
                    strcpy(CancelValue, (char *)Ctrl[r].s); // save the current value in case the user cancels
                    *Ctrl[r].s = 0;                         // set it to an empty string
                    Ctrl[r].state |= CTRL_SELECTED;         // select the number text/box
                    PopUpRedrawAll(r, true);
                    CurrentRef = InvokingCtrl = r; // save the format box being serviced
                    GUIKeyDown = -1;
                    ClickTimer += CLICK_DURATION;
                    if (GuiIntDownVector == NULL)
                        DrawFmtBox(KEY_OPEN); // initial draw of the keypad
                    else
                        DelayedDrawFmtBox = true; // leave it until after the keyboard interrupt
                    return;

                default:
                    Ctrl[r].value = !Ctrl[r].value;
                    break;
                }
                CurrentRef = r;
                DrawControl(r);
                ClickTimer += CLICK_DURATION; // sound a "click"
                return;
            }
        }
    }

    if (TouchUp)
    {
        // the touch has just been released
        TouchUp = TouchDown = false;
        LastX = TouchX;
        LastY = TouchY;
        /* Classify any swipe for the mouse / USB-touch sources. End
           position priority:
             USB touch:   usb_touch_x/y still hold the last reported
                          position before release (set false-before-x/y
                          in process_touch_report)
             mouse-driven: nunstruct[2].ax/ay (cursor location at lift)
           A wired panel's gestures are driven by PanelGestureService()
           in routinechecks(), not here. */
        {
            int16_t end_x = TouchX, end_y = TouchY;
#ifdef USBKEYBOARD
            if (usb_touch_present)
            {
                end_x = usb_touch_x;
                end_y = usb_touch_y;
            }
            else
#endif
                if (gui_click_from_mouse)
            {
                end_x = nunstruct[2].ax;
                end_y = nunstruct[2].ay;
            }
            if (gui_click_from_mouse
#ifdef USBKEYBOARD
                || usb_touch_present
#endif
            )
                touch_gesture_on_up(end_x, end_y);
        }
        if (InvokingCtrl)
        { // the keyboard/keypad takes complete control when activated
            if (Ctrl[InvokingCtrl].type == CTRL_FMTBOX)
                DrawFmtBox(KEY_KEY_UP);
            else
                DrawKeyboard(KEY_KEY_UP);
            gui_int_down = false;
            return;
        }
#if defined(USBKEYBOARD) && defined(GUICONTROLS) && defined(PICOMITEVGA)
        if (OSK_IsActive() && OSK_HandleTouchUp())
        {
            gui_int_down = false;
            return;
        }
#endif

        gui_int_up = true;
        if (CurrentRef)
        {
            if (Ctrl[CurrentRef].type)
            {
                if ((CurrentPages & (1 << Ctrl[CurrentRef].page)) && !(Ctrl[CurrentRef].state & (CTRL_DISABLED | CTRL_DISABLED2 | CTRL_HIDDEN)))
                { // ignore if control is disabled or the page is not displayed
                    if (CurrentRef)
                        LastRef = CurrentRef;
                    switch (Ctrl[CurrentRef].type)
                    {
                    case CTRL_AREA:
                    case CTRL_BUTTON:
                        Ctrl[CurrentRef].value = 0;
                        DrawControl(CurrentRef);
                        break;

                    case CTRL_SPINNER:
                        Ctrl[CurrentRef].state &= ~(CTRL_SPINUP | CTRL_SPINDOWN);
                        DrawControl(CurrentRef);
                        break;

                    default:
                        break;
                    }
                }
            }
        }
        CurrentRef = 0;
    }
}

void SetCtrlState(int r, int state, int err)
{
    if (Ctrl[r].type == 0)
    {
        if (err)
            error("Control #% does not exist", r);
    }
    else
    {
        if (r == CurrentRef)
        {
            if (Ctrl[r].type == CTRL_BUTTON)
            {
                Ctrl[r].value = 0;
                UpdateControl(r);
            }
            if (Ctrl[r].type == CTRL_SPINNER)
            {
                Ctrl[r].state &= ~(CTRL_SPINUP | CTRL_SPINDOWN);
                UpdateControl(r);
            }
            LastRef = CurrentRef = 0;
            gui_int_down = gui_int_up = false;
        }
        Ctrl[r].state |= state;
        if (state & CTRL_HIDDEN)
            DrawControl(r);
        else
            UpdateControl(r);
    }
}

// this function is used by the CLS command
void HideAllControls(void)
{
    int r;
    for (r = 1; r < Option.MaxCtrls; r++)
        if ((CurrentPages & (1 << Ctrl[r].page)) && !(Ctrl[r].state & CTRL_HIDDEN))
            SetCtrlState(r, CTRL_HIDDEN, false);
}

// This will check if an interrupt is pending and will service if yes.
// Essentially this runs a short program which does nothing but it gives the
// interrupt checking routines in ExecuteProgram() a chance to do their job.
// This routine should be called repeatedly during long delays.
void ServiceInterrupts(void)
{
    char *ttp, *s, tcmdtoken;
    char p[4] = {0};

    CheckAbort();
    g_LocalIndex++;         // preserve the current temporary string memory allocations
    ttp = (char *)nextstmt; // save the globals used by commands
    tcmdtoken = cmdtoken;
    s = (char *)cmdline;

    p[0] = (cmdEND_IF & 0x7f) + C_BASETOKEN;
    p[1] = (cmdEND_IF >> 7) + C_BASETOKEN; // tokens can be 14-bit
    ExecuteProgram((unsigned char *)p);    // execute the program's code

    cmdline = (unsigned char *)s; // restore the globals
    cmdtoken = tcmdtoken;
    nextstmt = (unsigned char *)ttp;
    g_LocalIndex--;
    g_TempMemoryIsChanged = true; // signal that temporary memory should be checked
}

/*  @endcond */

void fun_msgbox(void)
{
    int i, j, k;
    int x, y, x1, y1, x2, y2, msgnbr, btnnbr, btnwidth;
    char *p, *msg;
    char *btn;
    int btnx1[4], btny1, btnx2[4], btny2;
    long long int timeout;
    if (!Option.MaxCtrls)
        StandardError(13);
    /* MsgBox blocks the BASIC main loop. If the click that triggered
       us came from a synthetic GUI CLICK or a GUI CLICK PIN, the user
       has no way to steer the cursor over the popup's buttons
       (joystick / keyboard / pin handlers all live in the suspended
       main loop), so MsgBox would deadlock. Refuse loudly instead —
       this is how the user learns the popup needs touch or a mouse. */
    if (gui_click_emulated)
        error("MsgBox needs a real touch or mouse — cursor cannot be steered while MsgBox is open");
    getcsargs(&ep, 9);
    if (argc < 3)
        StandardError(2);
    msg = GetMemory(MAX_CAPTION_LINES * MAXSTRLEN);
    btn = GetMemory(4 * MAXSTRLEN);

    p = (char *)getCstring(argv[0]);
    i = msgnbr = 0;
    while (*p)
    {
        if (*p == '~')
        {
            if (msgnbr + 1 >= MAX_CAPTION_LINES)
                break;
            msg[msgnbr++ * MAXSTRLEN + i] = 0;
            i = 0;
            p++;
        }
        else
        {
            msg[msgnbr * MAXSTRLEN + i++] = *p++;
        }
    }
    msg[msgnbr++ * MAXSTRLEN + i] = 0;

    btnnbr = ((argc - 2) / 2) + 1;
    for (i = 0; i < btnnbr; i++)
        strcpy(&btn[i * MAXSTRLEN], (char *)getCstring(argv[(i * 2) + 2]));

    for (j = i = 0; i < msgnbr; i++)
        if (strlen(&msg[i * MAXSTRLEN]) > j)
            j = strlen(&msg[i * MAXSTRLEN]);
    j *= gui_font_width;

    for (k = i = 0; i < btnnbr; i++)
        if (strlen(&btn[i * MAXSTRLEN]) > k)
            k = strlen(&btn[i * MAXSTRLEN]);
    btnwidth = (k + 2) * gui_font_width;
    k = (btnwidth * btnnbr) + ((btnnbr - 1) * 2 * gui_font_width);
    if (k > j)
        j = k;
    j += 4 * gui_font_width;

    i = (((5 + msgnbr) * gui_font_height) / 2) + gui_font_height / 4;
    x1 = HRes / 2 - j / 2;
    y1 = VRes / 2 - i;
    x2 = HRes / 2 + j / 2;
    y2 = VRes / 2 + i;

    // wait for any pen touch to be lifted
    timeout = mSecTimer + 10000;
    do
    {
        ServiceInterrupts();
    } while (TouchState && mSecTimer < timeout);

#ifdef GUICONTROLS
    /* About to paint the modal box. Hide the cursor first so its
       save buffer doesn't trap pixels that are about to change.
       No-op on builds without an active cursor. */
    CursorHide();
#endif
    PopUpRedrawAll(0, true);
    SpecialDrawRBox(x1, y1, x2, y2, 25, gui_fcolour, gui_bcolour, CTRL_NORMAL);

    for (i = 0; i < msgnbr; i++)
        SpecialPrintString(HRes / 2, y1 + gui_font_height + (gui_font_height * i), JUSTIFY_CENTER, JUSTIFY_TOP, ORIENT_NORMAL, gui_fcolour, gui_bcolour, &msg[i * MAXSTRLEN], CTRL_NORMAL);

    btny1 = y2 - (gui_font_height * 3);
    btny2 = y2 - gui_font_height;
    for (i = 0; i < btnnbr; i++)
    {
        btnx1[i] = HRes / 2 - (k / 2) + (btnwidth * i) + (i * 2 * gui_font_width);
        btnx2[i] = btnx1[i] + btnwidth;
        DrawBasicButton(btnx1[i], btny1, btnx2[i], btny2, BTN_SIDE_WIDTH, 1, gui_fcolour, CTRL_NORMAL);
        SpecialPrintString(btnx1[i] + btnwidth / 2, btny1 + gui_font_height, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, gui_bcolour, gui_fcolour, &btn[i * MAXSTRLEN], CTRL_NORMAL);
    }

    while (1)
    {
        ServiceInterrupts();
        /* Click-source resolution:
           - Touch panel reports a touch via GetTouch (returns
             TOUCH_ERROR if not touched).
           - Mouse / synthetic click: only count it while the left
             button is held (otherwise hovering would dismiss).
           Try touch first; fall back to mouse. On builds with no
           touch panel, GetTouch is the stub that returns TOUCH_ERROR. */
        x = GetTouch(GET_X_AXIS);
        y = GetTouch(GET_Y_AXIS);
        if (x == TOUCH_ERROR)
        {
            if (nunstruct[2].L || gui_click_synthetic_down)
            {
                x = nunstruct[2].ax;
                y = nunstruct[2].ay;
            }
            else if (click_pin_pressed())
            {
                /* Click-pin source: coords come from the soft cursor,
                   wherever the user has steered it. */
                x = cursor_x;
                y = cursor_y;
            }
            else
            {
                x = -1;
                y = -1;
            }
        }
        for (i = 0; i < btnnbr; i++)
            if (x >= btnx1[i] && x <= btnx2[i] && y >= btny1 && y <= btny2)
                break;
        if (i < btnnbr)
        {
#ifdef GUICONTROLS
            /* About to paint the "pressed" button state. */
            CursorHide();
#endif
            DrawBasicButton(btnx1[i], btny1, btnx2[i], btny2, BTN_SIDE_WIDTH, 0, gui_fcolour, CTRL_NORMAL);
            SpecialPrintString(btnx1[i] + btnwidth / 2 + BTN_CAPTION_SHIFT, btny1 + gui_font_height + BTN_CAPTION_SHIFT, JUSTIFY_CENTER, JUSTIFY_MIDDLE, ORIENT_NORMAL, gui_bcolour, gui_fcolour, &btn[i * MAXSTRLEN], CTRL_NORMAL);
            ClickTimer += CLICK_DURATION; // sound a "click"
            /* Wait for the click to be released. Touch panel, mouse,
               synthetic GUI CLICK and the GUI CLICK PIN all need to
               clear before we proceed. */
            while (GetTouch(GET_X_AXIS) != TOUCH_ERROR || nunstruct[2].L || gui_click_synthetic_down || click_pin_pressed())
                ServiceInterrupts();
#ifdef GUICONTROLS
            /* Now erasing the modal box. The cursor may have wandered
               while the user held the button (the wait loop above runs
               ServiceInterrupts → CursorRefresh). Hide it before the
               cleanup paint so the next refresh re-saves a clean
               background. */
            CursorHide();
#endif
            SpecialDrawRBox(x1, y1, x2, y2, 25, gui_bcolour, gui_bcolour, CTRL_NORMAL);
            PopUpRedrawAll(0, false);
            iret = i + 1;
            targ = T_INT;
            FreeMemory((unsigned char *)msg);
            FreeMemory((unsigned char *)btn);
            return;
        }
    }
}

void fun_ctrlval(void)
{
    int r;
    CheckDisplay();
    if (!Option.MaxCtrls)
        StandardError(13);
    if (*ep == '#')
        ep++;
    r = getint(ep, 1, Option.MaxCtrls);
    if (Ctrl[r].type == 0)
        error("Control #% does not exist", r);
    if (Ctrl[r].type == CTRL_NBRBOX)
    {
        if (r == InvokingCtrl)
        {                                      // is the keypad for the number box being displayed?
            fret = (MMFLOAT)atof(CancelValue); // if NOT in the call back just return the value saved in case of a cancel
        }
        else
        {
            char a[16];
            FloatToStr(a, Ctrl[r].value, 1, STR_FLOAT_PRECISION, ' '); // convert to a float and put back on the display
            fret = atof(a);
        }
        targ = T_NBR;
    }
    else if (Ctrl[r].type == CTRL_TEXTBOX || Ctrl[r].type == CTRL_FMTBOX)
    {
        sret = GetTempStrMemory();             // this will last for the life of the command
        if (r == InvokingCtrl)                 // is the keypad for the number box being displayed?
            strcpy((char *)sret, CancelValue); // just return the value saved in case of a cancel
        else
            strcpy((char *)sret, (char *)Ctrl[r].s);
        if (Ctrl[r].s[0] == '#' && Ctrl[r].s[1] == '#')
            *sret = 0; // return a zero length string if it is just "ghost text"
        CtoM(sret);    // convert to a MMBasic string
        targ = T_STR;
    }
    else if (Ctrl[r].type == CTRL_DISPLAYBOX || Ctrl[r].type == CTRL_CAPTION)
    {
        sret = GetTempStrMemory();               // this will last for the life of the command
        strcpy((char *)sret, (char *)Ctrl[r].s); // copy the string
        CtoM(sret);                              // convert to a MMBasic string
        targ = T_STR;
    }
    else
    {
        char a[16];
        FloatToStr(a, Ctrl[r].value, 1, STR_FLOAT_PRECISION, ' '); // convert to a float and put back on the display
        fret = atof(a);
        targ = T_NBR;
    }
}

void cmd_ctrlval(void)
{
    int r;
    MMFLOAT v;
    CheckDisplay();
    if (!Option.MaxCtrls)
        StandardError(13);
    if (*cmdline == '#')
        cmdline++;
    r = getint(cmdline, 1, Option.MaxCtrls);
    if (Ctrl[r].type == 0)
        error("Control #% does not exist", r);
    while (*cmdline && tokenfunction(*cmdline) != op_equal)
        cmdline++; // search for the = symbol
    if (!*cmdline)
        SyntaxError();
    ;
    ++cmdline; // step over the = symbol
    skipspace(cmdline);
    if (!*cmdline || *cmdline == '\'')
        SyntaxError();
    ;
    switch (Ctrl[r].type)
    {
    case CTRL_BUTTON:
    case CTRL_SWITCH:
    case CTRL_CHECKBOX:
    case CTRL_RADIOBTN:
        v = (getnumber(cmdline) != 0);
        if (Ctrl[r].value == v)
            return; // don't update if no change
        Ctrl[r].value = v;
        break;

    case CTRL_LED:
        v = getnumber(cmdline);
        if (v > 1)
        {
            Ctrl[r].inc = v;
            Ctrl[r].value = 1;
            CheckGuiFlag = true;
        }
        else
        {
            int i;
            CheckGuiFlag = false;
            Ctrl[r].inc = 0;
            // scan through all controls to see if any others are in a flash
            // if there is, set the CheckGuiFlag which indicates that flash timing should continue
            for (i = 1; i < Option.MaxCtrls; i++)
            {
                if (Ctrl[i].type == CTRL_LED && Ctrl[i].inc != 0)
                {
                    CheckGuiFlag = true;
                    break;
                }
            }
            if (Ctrl[r].value == v)
                return; // don't update if no change
            Ctrl[r].value = v;
        }
        break;

    case CTRL_SPINNER:
    case CTRL_GAUGE:
    case CTRL_BARGAUGE:
        v = getnumber(cmdline);
        if (v < Ctrl[r].min)
            v = Ctrl[r].min;
        if (v > Ctrl[r].max)
            v = Ctrl[r].max;
        if (Ctrl[r].value == v)
            return; // don't update if no change
        Ctrl[r].value = v;
        if (Ctrl[r].type == CTRL_SPINNER)
            FloatToStr((char *)Ctrl[r].s, v, 0, STR_AUTO_PRECISION, ' '); // update the displayed string
        break;

    case CTRL_NBRBOX:
        if (strlen((char *)cmdline) > 3 && cmdline[0] == '"' && cmdline[1] == '#' && cmdline[2] == '#')
        {
            // the user wants ghost text
            strcpy((char *)Ctrl[r].s, (char *)getCstring(cmdline));
            Ctrl[r].value = 0;
        }
        else
        {
            // a normal number
            v = getnumber(cmdline);
            if (r != InvokingCtrl && Ctrl[r].value == v)
                return; // don't update if no change
            Ctrl[r].value = v;
            FloatToStr((char *)Ctrl[r].s, v, 0, STR_AUTO_PRECISION, ' '); // update the displayed string
        }
        break;

    case CTRL_FRAME:
    case CTRL_CAPTION:
        memset(Ctrl[r].s, ' ', strlen((char *)Ctrl[r].s)); // set the caption to spaces
        UpdateControl(r);                                  // erase the existing text
                                                           // fall through

    case CTRL_DISPLAYBOX:
    case CTRL_TEXTBOX:
    case CTRL_FMTBOX:
        strcpy((char *)Ctrl[r].s, (char *)getCstring(cmdline));
        break;
    }

    if (!(Ctrl[r].state & CTRL_DISABLED2))
        UpdateControl(r); // don't update if the gauge is disabled by a keypad (updating may mess they keypad)
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */

void fun_mmhpos(void)
{
    CheckDisplay();
    iret = CurrentX;
    targ = T_INT;
}

void fun_mmvpos(void)
{
    CheckDisplay();
    iret = CurrentY;
    targ = T_INT;
}

/*void cmd_backlight(void) {
    if(HRes != 0 && Option.DISPLAY_TYPE > SSD_PANEL) error("SSD1963 display only");
    SetBacklightSSD1963(getint(cmdline, 0, 100));
}*/

void ResetGUI(void)
{
    int i;
    InvokingCtrl = CurrentRef = LastRef = 0;
    LastX = LastY = TOUCH_ERROR;
    gui_int_down = gui_int_up = false;
    GuiIntDownVector = GuiIntUpVector = NULL;
    GUIKeyDown = 0;
    KeyAltShift = 0;
    last_x2 = 100;
    last_y2 = 100;
    last_fcolour = gui_fcolour;
    last_bcolour = gui_bcolour;
#ifdef PICOMITEVGA
    /* SCREENMODE2/3 use a 4-bit RGB121 palette with no grey, so
       ChangeBright() on a black button face produces black borders too
       (ChangeBright early-exits on c <= 0). The default Option.DefaultBC
       is BLACK, which gives an invisible button face. When defaults
       haven't been overridden, pick palette colours that quantise to
       three distinct shades for face / bright-border / dim-border —
       CERULEAN face gives CERULEAN / COBALT / MYRTLE after RGB121
       quantisation. Users can still override via GUI FCOLOUR /
       GUI BCOLOUR. */
    if (last_bcolour == BLACK)
        last_bcolour = CERULEAN;
    if (last_fcolour == last_bcolour)
        last_fcolour = WHITE;
#endif
    last_inc = 1;
    last_min = -FLT_MAX;
    last_max = FLT_MAX;

    SetupPage = 0;
    CurrentPages = 1;
    for (i = 1; i < Option.MaxCtrls; i++)
    {
        if (Ctrl[i].s)
            FreeMemorySafe((void **)&Ctrl[i].s);
        if (Ctrl[i].fmt)
            FreeMemorySafe((void **)&Ctrl[i].fmt);
        memset(&Ctrl[i], 0, sizeof(struct s_ctrl));
    }
}

// This is called by Timer.c every mSec
// it scans through all controls looking for a LED with a timeout set (in Ctrl[r].inc) and decrements it
void CheckGuiTimeouts(void)
{
    int r;
    for (r = 1; r < Option.MaxCtrls; r++)
    {
        if (Ctrl[r].type == CTRL_LED && Ctrl[r].inc > 1)
        {
            Ctrl[r].inc--; // decrement the timeout
        }
    }
}

// This implements a LED flash
// it is called by check_interrupt()
// it scans through all controls looking for a LED with a timeout set (in Ctrl[r].inc)
void CheckGui(void)
{
    int r;
    CheckGuiFlag = false;
    for (r = 1; r < Option.MaxCtrls; r++)
    {
        if (Ctrl[r].type == CTRL_LED)
        {
            if (Ctrl[r].inc > 1)
                CheckGuiFlag = true; // keep calling this function while LEDs have timeouts
            if (Ctrl[r].inc == 1)
            { // if this one has timed out
                Ctrl[r].inc = 0;
                Ctrl[r].value = 0; // turn off the LED
                UpdateControl(r);
            }
        }
    }
}
/*  @endcond */
