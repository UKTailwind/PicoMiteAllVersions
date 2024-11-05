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


/* ********************************************************************************
 All other tokens (keywords, functions, operators) should be inserted in this table
**********************************************************************************/
#ifdef INCLUDE_TOKEN_TABLE


#endif

#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
  #ifndef GUI_H_INCL
    #define GUI_H_INCL
    extern void ConfigDisplaySSD(unsigned char *p);
    extern void InitDisplaySSD(void);
    extern void DrawRectangleSSD1963(int x1, int y1, int x2, int y2, int c);

    extern void ProcessTouch(void);

    extern void ResetGUI(void);
    extern void DrawKeyboard(int);
    extern void DrawFmtBox(int);

    // define the blink rate for the cursor
    #define CURSOR_OFF        350              // cursor off time in mS
    #define CURSOR_ON     650                  // cursor on time in mS

    #define MAX_CAPTION_LINES   10             // maximum number of lines in a caption

    extern void HideAllControls(void);

    extern short gui_font, gui_font_width, gui_font_height;

    extern int gui_fcolour, gui_bcolour;
    extern int last_fcolour, last_bcolour;

    extern int gui_click_pin;                  // the sound pin
    extern int display_backlight;              // the brightness of the backlight (1 to 100)

    extern short CurrentX, CurrentY;             // and the current default position

    extern bool gui_int_down;                   // true if the touch down has triggered an interrupt
    extern char *GuiIntDownVector;             // address of the interrupt routine or NULL if no interrupt
    extern bool gui_int_up;                     // true if the release of the touch has triggered an interrupt
    extern char *GuiIntUpVector;               // address of the interrupt routine or NULL if no interrupt
    extern volatile bool DelayedDrawKeyboard;            // a flag to indicate that the pop-up keyboard should be drawn AFTER the pen down interrupt
    extern volatile bool DelayedDrawFmtBox;              // a flag to indicate that the pop-up formatted keyboard should be drawn AFTER the pen down interrupt

    extern short CurrentRef;                     // if the pen is down this is the control (or zero if not on a control)
    extern short LastRef;                        // this is the last control touched
    extern short LastX;                          // this is the x coord when the pen was lifted
    extern short LastY;                          // ditto for y

    extern MMFLOAT CtrlSavedVal;               // a temporary place to save a control's value

    extern int CheckGuiFlag;                   // used by Timer.c to tell if it has to call CheckGuiTimeouts()
    extern void CheckGui(void);
    extern void CheckGuiTimeouts(void);
    extern volatile int ClickTimer;            // used to time the click when touch occurs
    extern volatile int TouchTimer;                // used to time the response to touch

    extern struct s_ctrl *Ctrl;             // list of the controls

  #endif
#endif
/*  @endcond */
