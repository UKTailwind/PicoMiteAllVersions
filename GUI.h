/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

GUI.h

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the distribution.
3. The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
   on the console at startup (additional copyright messages may be added).
4. All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
   by the <copyright holder>.
5. Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
   without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/

/* ============================================================================
 * Token table section
 * ============================================================================ */
#ifdef INCLUDE_TOKEN_TABLE
/* All other tokens (keywords, functions, operators) should be inserted in this table */
#endif

/* ============================================================================
 * Main header content
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#ifndef GUI_H_INCL
#define GUI_H_INCL

/* ============================================================================
 * Constants - Cursor timing
 * ============================================================================ */
#define CURSOR_OFF 350 // Cursor off time in ms
#define CURSOR_ON 650  // Cursor on time in ms

/* ============================================================================
 * Constants - GUI limits
 * ============================================================================ */
#define MAX_CAPTION_LINES 10 // Maximum number of lines in a caption

/* ============================================================================
 * External variables - Font configuration
 * ============================================================================ */
extern short gui_font;
extern short gui_font_width;
extern short gui_font_height;

/* ============================================================================
 * External variables - Color configuration
 * ============================================================================ */
extern int gui_fcolour, gui_bcolour;
extern int last_fcolour, last_bcolour;

/* ============================================================================
 * External variables - Hardware configuration
 * ============================================================================ */
extern int gui_click_pin;     // Sound pin for click feedback
extern int display_backlight; // Backlight brightness (1 to 100)

/* ============================================================================
 * External variables - Display position
 * ============================================================================ */
extern short CurrentX, CurrentY; // Current default position

/* ============================================================================
 * External variables - Touch interrupt handling
 * ============================================================================ */
extern bool gui_int_down;      // True if touch down triggered an interrupt
extern char *GuiIntDownVector; // Address of interrupt routine or NULL
extern bool gui_int_up;        // True if touch release triggered an interrupt
extern char *GuiIntUpVector;   // Address of interrupt routine or NULL

/* ============================================================================
 * External variables - Delayed drawing flags
 * ============================================================================ */
extern volatile bool DelayedDrawKeyboard; // Draw pop-up keyboard after pen down interrupt
extern volatile bool DelayedDrawFmtBox;   // Draw formatted keyboard after pen down interrupt

/* ============================================================================
 * External variables - Touch tracking
 * ============================================================================ */
extern short CurrentRef; // Control reference if pen is down (0 if not on control)
extern short LastRef;    // Last control touched
extern short LastX;      // X coordinate when pen was lifted
extern short LastY;      // Y coordinate when pen was lifted

/* ============================================================================
 * External variables - Control state
 * ============================================================================ */
extern MMFLOAT CtrlSavedVal; // Temporary storage for a control's value
extern struct s_ctrl *Ctrl;  // List of controls

/* ============================================================================
 * External variables - Timing
 * ============================================================================ */
extern int CheckGuiFlag;        // Flag to indicate CheckGuiTimeouts() needs calling
extern volatile int ClickTimer; // Timer for click when touch occurs
extern volatile int TouchTimer; // Timer for response to touch

/* ============================================================================
 * Function declarations - Display configuration
 * ============================================================================ */
void ConfigDisplaySSD(unsigned char *p);
void InitDisplaySSD(void);

/* ============================================================================
 * Function declarations - Drawing functions
 * ============================================================================ */
void DrawRectangleSSD1963(int x1, int y1, int x2, int y2, int c);
void DrawKeyboard(int);
void DrawFmtBox(int);

/* ============================================================================
 * Function declarations - GUI management
 * ============================================================================ */
void ResetGUI(void);
void HideAllControls(void);
void CheckGui(void);
void CheckGuiTimeouts(void);

/* ============================================================================
 * Function declarations - Touch processing
 * ============================================================================ */
void ProcessTouch(void);

#endif /* GUI_H_INCL */
#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

/*  @endcond */