/***********************************************************************************************************************
PicoMite MMBasic

Editor.c

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
 * @file Audio.c
 * @author Geoff Graham, Peter Mather
 * @brief Source for Editor MMBasic commands
 *  Includes bug fix for non-standard colours from Ernst Bokkelkamp
 */
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#ifndef USBKEYBOARD
#include "class/cdc/cdc_device.h"
#endif

#define DISPLAY_CLS 1
#define REVERSE_VIDEO 3
#define CLEAR_TO_EOL 4
#define CLEAR_TO_EOS 5
#define SCROLL_DOWN 6
#define DRAW_LINE 7
#define SCROLLCHARS 3

#define GUI_C_NORMAL WHITE
#define GUI_C_BCOLOUR BLACK
#define GUI_C_COMMENT YELLOW
#define GUI_C_KEYWORD CYAN
#define GUI_C_QUOTE MAGENTA
#define GUI_C_NUMBER GREEN
#define GUI_C_LINE MAGENTA
#define GUI_C_STATUS WHITE

//======================================================================================
//      Attribute               VT100 Code      VT 100 Colour       LCD Screen Colour
//======================================================================================
#define VT100_C_NORMAL "\033[37m"  // White            Foreground Colour
#define VT100_C_COMMENT "\033[33m" // Yellow               Yellow
#define VT100_C_KEYWORD "\033[36m" // Cyan                 Cyan
#define VT100_C_QUOTE "\033[35m"   // Green                Green
#define VT100_C_NUMBER "\033[32m"  // Red                  Red
#define VT100_C_LINE "\033[35m"    // White                Grey
#define VT100_C_STATUS "\033[37m"  // Black                Brown
#define VT100_C_ERROR "\033[31m"   // Red                  Red

// these two are the only global variables, the default place for the cursor when the editor opens
unsigned char *StartEditPoint = NULL;
int StartEditChar = 0;
static bool markmode = false;
extern void routinechecks(void);
int8_t optioncolourcodesave;
#if !defined(LITE)
#ifdef PICOMITEVGA
int editactive = 0;
static int r_on = 0;
void DisplayPutClever(char c)
{
    if ((DISPLAY_TYPE == SCREENMODE1 && markmode && Option.ColourCode && ytileheight == gui_font_height && gui_font_width % 8 == 0))
    {
        if (c >= FontTable[gui_font >> 4][2] && c < FontTable[gui_font >> 4][2] + FontTable[gui_font >> 4][3])
        {
            if (CurrentX + gui_font_width > HRes)
            {
                DisplayPutClever('\r');
                DisplayPutClever('\n');
            }
        }

        // handle the standard control chars
        switch (c)
        {
        case '\b':
            CurrentX -= gui_font_width;
            if (CurrentX < 0)
                CurrentX = 0;
            return;
        case '\r':
            CurrentX = 0;
            return;
        case '\n':
            CurrentY += gui_font_height;
            if (CurrentY + gui_font_height >= VRes)
            {
                ScrollLCD(CurrentY + gui_font_height - VRes);
                CurrentY -= (CurrentY + gui_font_height - VRes);
            }
            return;
        case '\t':
            do
            {
                DisplayPutClever(' ');
            } while ((CurrentX / gui_font_width) % Option.Tab);
            return;
        }
#ifdef HDMI
        if (r_on)
        {
            if (FullColour)
            {
                if (r_on)
                    for (int i = 0; i < gui_font_width / 8; i++)
                        tilebcols[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = RGB555(BLUE);
            }
            else
            {
                if (r_on)
                    for (int i = 0; i < gui_font_width / 8; i++)
                        tilebcols_w[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = RGB332(BLUE);
            }
        }
#else
        if (r_on)
            for (int i = 0; i < gui_font_width / 8; i++)
                tilebcols[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = 0x1111;
#endif
#ifdef HDMI
        else
        {
            if (FullColour)
            {
                for (int i = 0; i < gui_font_width / 8; i++)
                    tilebcols[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = RGB555(Option.DefaultBC);
            }
            else
            {
                for (int i = 0; i < gui_font_width / 8; i++)
                    tilebcols_w[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = RGB332(Option.DefaultBC);
            }
        }
#else
        else
            for (int i = 0; i < gui_font_width / 8; i++)
                tilebcols[CurrentY / gui_font_height * X_TILE + CurrentX / 8 + i] = RGB121pack(Option.DefaultBC);
#endif
        CurrentX += gui_font_width;
    }
    else
        DisplayPutC(c);
}
#endif

static int mark_lcd_reverse = 0;
static uint32_t mark_saved_fcolour = 0;
static uint32_t mark_saved_bcolour = 0;

void DisplayPutS(char *s)
{
    while (*s)
        DisplayPutC(*s++);
}

static inline void printnothing(char *dummy)
{
}
static inline char nothingchar(char dummy, int flush)
{
    return 0;
}
int OriginalFC, OriginalBC; // the original fore/background colours used by MMBasic
static void (*PrintString)(char *buff) = SSPrintString;
static char (*SSputchar)(char buff, int flush) = SerialConsolePutC;
//    #define PrintString       SSPrintString
#ifdef PICOMITEVGA
#define MX470PutC(c) DisplayPutClever(c)
#else
#define MX470PutC(c) DisplayPutC(c);
#endif
// Only SSD1963 displays support H/W scrolling so for other displays it is much quicker to redraw the screen than scroll it
// However, we don't want to redraw the serial console so we dummy out the serial writes whiole re-drawing the physical screen
#ifdef PICOMITEVGA
#define MX470Scroll(n) ScrollLCD(n);
#else
#if PICOMITERP2350
#define MX470Scroll(n)                                                                                       \
    if (Option.DISPLAY_CONSOLE)                                                                              \
    {                                                                                                        \
        if (!((SPIREAD && ScrollLCD != ScrollLCDSPISCR && ScrollLCD != ScrollLCDMEM332) || Option.NoScroll)) \
            ScrollLCD(n);                                                                                    \
        else                                                                                                 \
        {                                                                                                    \
            PrintString = printnothing;                                                                      \
            SSputchar = nothingchar;                                                                         \
            printScreen();                                                                                   \
            PrintString = SSPrintString;                                                                     \
            SSputchar = SerialConsolePutC;                                                                   \
        }                                                                                                    \
    }
#else
#define MX470Scroll(n)                                                       \
    if (Option.DISPLAY_CONSOLE)                                              \
    {                                                                        \
        if (!((SPIREAD && ScrollLCD != ScrollLCDSPISCR) || Option.NoScroll)) \
            ScrollLCD(n);                                                    \
        else                                                                 \
        {                                                                    \
            PrintString = printnothing;                                      \
            SSputchar = nothingchar;                                         \
            printScreen();                                                   \
            PrintString = SSPrintString;                                     \
            SSputchar = SerialConsolePutC;                                   \
        }                                                                    \
    }
#endif
#endif

//    #define dx(...) {char s[140];sprintf(s,  __VA_ARGS__); SerUSBPutS(s); SerUSBPutS("\r\n");}

static inline void MX470PutS(char *s, int fc, int bc)
{
    if (!Option.DISPLAY_CONSOLE)
        return;
    int tfc, tbc;
    tfc = gui_fcolour;
    tbc = gui_bcolour;
    gui_fcolour = fc;
    gui_bcolour = bc;
    DisplayPutS(s);
    gui_fcolour = tfc;
    gui_bcolour = tbc;
}

static inline void MX470Cursor(int x, int y)
{
    if (!Option.DISPLAY_CONSOLE)
        return;
    CurrentX = x;
    CurrentY = y;
}

void MX470Display(int fn)
{
    if (!Option.DISPLAY_CONSOLE)
        return;
    switch (fn)
    {
    case DISPLAY_CLS:
        ClearScreen(gui_bcolour);
        break;
#ifdef PICOMITEVGA
    case CLEAR_TO_EOS:
        MX470Display(CLEAR_TO_EOL);
        DrawRectangle(0, CurrentY + gui_font_height, HRes - 1, VRes - 1, DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour);
#ifdef HDMI
        if (FullColour)
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
            {
                for (int y = (CurrentY + gui_font_height) / ytileheight; y < Y_TILE; y++)
                    for (int x = 0; x < X_TILE; x++)
                    {
                        tilefcols[y * X_TILE + x] = RGB555(gui_fcolour);
                        tilebcols[y * X_TILE + x] = RGB555(gui_bcolour);
                    }
            }
        }
        else
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
            {
                for (int y = (CurrentY + gui_font_height) / ytileheight; y < Y_TILE; y++)
                    for (int x = 0; x < X_TILE; x++)
                    {
                        tilefcols_w[CurrentY / ytileheight * X_TILE + x] = RGB332(gui_fcolour);
                        tilebcols_w[CurrentY / ytileheight * X_TILE + x] = RGB332(gui_bcolour);
                    }
            }
        }
#else
        if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
        {
            for (int y = (CurrentY + gui_font_height) / ytileheight; y < Y_TILE; y++)
                for (int x = 0; x < X_TILE; x++)
                {
                    tilefcols[CurrentY / ytileheight * X_TILE + x] = RGB121pack(gui_fcolour);
                    tilebcols[CurrentY / ytileheight * X_TILE + x] = RGB121pack(gui_bcolour);
                }
        }

#endif
        break;
    case REVERSE_VIDEO:
        if ((DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height))
        {
            r_on ^= 1;
        }
        else
        {
            if (!mark_lcd_reverse)
            {
                mark_saved_fcolour = gui_fcolour;
                mark_saved_bcolour = gui_bcolour;
                if (Option.ColourCode)
                {
                    gui_bcolour = BLUE;
                }
                else
                {
                    gui_fcolour = BLACK;
                    gui_bcolour = WHITE;
                }
                mark_lcd_reverse = 1;
            }
            else
            {
                gui_fcolour = mark_saved_fcolour;
                gui_bcolour = mark_saved_bcolour;
                mark_lcd_reverse = 0;
            }
        }
        break;
    case CLEAR_TO_EOL:
        DrawBox(CurrentX, CurrentY, HRes - 1, CurrentY + gui_font_height - 1, 0, 0, DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour);
#ifdef HDMI
        if (FullColour)
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
            {
                for (int x = CurrentX / 8; x < X_TILE; x++)
                {
                    tilefcols[CurrentY / ytileheight * X_TILE + x] = RGB555(gui_fcolour);
                    tilebcols[CurrentY / ytileheight * X_TILE + x] = RGB555(gui_bcolour);
                }
            }
        }
        else
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
            {
                for (int x = CurrentX / 8; x < X_TILE; x++)
                {
                    tilefcols_w[CurrentY / ytileheight * X_TILE + x] = RGB332(gui_fcolour);
                    tilebcols_w[CurrentY / ytileheight * X_TILE + x] = RGB332(gui_bcolour);
                }
            }
        }
#else
        if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
        {
            for (int x = CurrentX / 8; x < X_TILE; x++)
            {
                tilefcols[CurrentY / ytileheight * X_TILE + x] = RGB121pack(gui_fcolour);
                tilebcols[CurrentY / ytileheight * X_TILE + x] = RGB121pack(gui_bcolour);
            }
        }

#endif

        break;
#else
    case REVERSE_VIDEO:
        if (!mark_lcd_reverse)
        {
            mark_saved_fcolour = gui_fcolour;
            mark_saved_bcolour = gui_bcolour;
            if (Option.ColourCode)
            {
                gui_bcolour = BLUE;
            }
            else
            {
                gui_fcolour = BLACK;
                gui_bcolour = WHITE;
            }
            mark_lcd_reverse = 1;
        }
        else
        {
            gui_fcolour = mark_saved_fcolour;
            gui_bcolour = mark_saved_bcolour;
            mark_lcd_reverse = 0;
        }
        break;
    case CLEAR_TO_EOL:
        DrawBox(CurrentX, CurrentY, HRes - 1, CurrentY + gui_font_height - 1, 0, 0, gui_bcolour);
        break;
    case CLEAR_TO_EOS:
        DrawBox(CurrentX, CurrentY, HRes - 1, CurrentY + gui_font_height - 1, 0, 0, gui_bcolour);
        DrawRectangle(0, CurrentY + gui_font_height, HRes - 1, VRes - 1, gui_bcolour);
        break;
#endif
    case SCROLL_DOWN:
        break;
    case DRAW_LINE:
        DrawBox(0, gui_font_height * (Option.Height - 2), HRes - 1, VRes - 1, 0, 0, (DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour));
        DrawLine(0, (VRes / gui_font_height) * gui_font_height - gui_font_height - 6, HRes - 1, (VRes / gui_font_height) * gui_font_height - gui_font_height - 6, 1, Option.ColourCode ? GUI_C_LINE : gui_fcolour);
#ifdef PICOMITEVGA
#ifdef HDMI
        if (FullColour)
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
                for (int i = 0; i < HRes / 8; i++)
                    tilefcols[(Option.Height - 2) * X_TILE + i] = RGB555(MAGENTA);
        }
        else
        {
            if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
                for (int i = 0; i < HRes / 8; i++)
                    tilefcols_w[(Option.Height - 2) * X_TILE + i] = RGB332(MAGENTA);
        }
#else
        if (DISPLAY_TYPE == SCREENMODE1 && Option.ColourCode && ytileheight == gui_font_height)
            for (int i = 0; i < HRes / 8; i++)
                tilefcols[(Option.Height - 2) * X_TILE + i] = 0x9999;
#endif
#endif
        CurrentX = 0;
        CurrentY = (VRes / gui_font_height) * gui_font_height - gui_font_height;
        break;
    }
}

/********************************************************************************************************************************************
 THE EDIT COMMAND
********************************************************************************************************************************************/

unsigned char *EdBuff = NULL; // the buffer used for editing the text
int nbrlines;                 // size of the text held in the buffer (in lines)
int VWidth, VHeight;          // editing screen width and height in characters
int edx, edy;                 // column and row at the top left hand corner of the editing screen (in characters)
int curx, cury;               // column and row of the current cursor (in characters) relative to the top left hand corner of the editing screen
unsigned char *txtp;          // position of the current cursor in the text being edited
int drawstatusline;           // true if the status line needs to be redrawn on the next keystroke
static bool edit_is_bas;      // true when editing a .bas file or in-memory program (enforces 255-char line limit)
int insert;                   // true if the editor is in insert text mode
int tempx;                    // used to track the prefered x position when up/down arrowing
int TextChanged;              // true if the program has been modified and therefor a save might be required

#define EDIT 1 // used to select the status line string
#define MARK 2

void FullScreenEditor(int x, int y, char *fname, int edit_buff_size, bool cmdfile);
char *findLine(int ln, int *inmulti);
void SetColour(unsigned char *p, int DoVT100);
void restoreColourFromLineStart(unsigned char *target);
void printLine(int ln);
void printScreen(void);
void SCursor(int x, int y);
int editInsertChar(unsigned char c, char *multi, int edit_buff_size);
void PrintFunctKeys(int);
void PrintStatus(void);
void SaveToProgMemory(void);
void editDisplayMsg(unsigned char *msg);
void GetInputString(unsigned char *prompt);
void Scroll(void);
void ScrollDown(void);
void MarkMode(unsigned char *cb, unsigned char *buf);
void PositionCursor(unsigned char *curp);
extern void setterminal();
static int multilinecomment = false;
bool modmode = false;
int oldfont;
int oldmode;
#define MAXCLIP 1024
// edit command:
//  EDIT              Will run the full screen editor on the current program memory, if run after an error will place the cursor on the error line
void edit(unsigned char *cmdline, bool cmdfile)
{
    unsigned char *fromp, *p = NULL;
    int y, x, edit_buff_size;
    optioncolourcodesave = Option.ColourCode;
    char name[STRINGSIZE], *filename = NULL;
    getcsargs(&cmdline, 1);
    if (argc)
    {
        strcpy(name, (char *)getFstring(argv[0]));
        filename = name;
    }
    if (CurrentLinePtr && cmdfile)
        StandardError(10);
    if (argc == 0 && !cmdfile)
        SyntaxError();
    ;
    if (!cmdfile)
    {
        SaveContext();
        ClearVars(0, FALSE);
    }
#ifdef PICOMITEVGA
    modmode = false;
    editactive = 1;
    oldmode = DISPLAY_TYPE;
    oldfont = PromptFont;
    {
        int chars_per_line = HRes / gui_font_width;
        if (chars_per_line < 64)
        {
            // Rule 5: too few columns - switch to mode 1 and standard font
            DISPLAY_TYPE = SCREENMODE1;
            modmode = true;
            ResetDisplay();
#ifdef HDMI
            if (HRes >= 1024)
            {
                SetFont(2 << 4 | 1); // font3 16x24 for HDMI high-res
                PromptFont = 2 << 4 | 1;
            }
            else
#endif
            {
                SetFont(1); // font1 8x12 for standard resolutions
                PromptFont = 1;
            }
        }
        else if (Option.ColourCode && DISPLAY_TYPE == SCREENMODE1 && gui_font_width % 8 != 0)
        {
            // Rule 4: mode 1 with colour coding but font not tile-aligned - switch font
#ifdef HDMI
            if (HRes >= 1024)
            {
                SetFont(2 << 4 | 1); // font3 16x24 for HDMI high-res
                PromptFont = 2 << 4 | 1;
            }
            else
#endif
            {
                SetFont(1); // font1 8x12 for standard resolutions
                PromptFont = 1;
            }
        }
        // Rule 1: chars>=64, ColourCode=0 - no switch (handled below for tiles)
        // Rule 2: chars>=64, ColourCode=1, not mode 1 - no switch
        // Rule 3: chars>=64, ColourCode=1, mode 1, fw%8==0 - no switch (happy path)
    }
    memset((void *)WriteBuf, 0, ScreenSize);
    // Set tiles black and white if in mode 1 without colour coding
    if (DISPLAY_TYPE == SCREENMODE1 && !Option.ColourCode)
    {
#ifdef HDMI
        if (FullColour)
        {
            for (int t = 0; t < X_TILE * Y_TILE; t++)
            {
                tilefcols[t] = RGB555(WHITE);
                tilebcols[t] = RGB555(BLACK);
            }
        }
        else
        {
            for (int t = 0; t < X_TILE * Y_TILE; t++)
            {
                tilefcols_w[t] = RGB332(WHITE);
                tilebcols_w[t] = RGB332(BLACK);
            }
        }
#else
        for (int t = 0; t < X_TILE * Y_TILE; t++)
        {
            tilefcols[t] = RGB121pack(WHITE);
            tilebcols[t] = RGB121pack(BLACK);
        }
#endif
    }
#ifdef rp2350
#ifdef HDMI
    mapreset();
#endif
#endif
#endif
#ifndef USBKEYBOARD
    if (mouse0 == false && Option.MOUSE_CLOCK)
        initMouse0(0); // see if there is a mouse to initialise
#endif
#ifdef PICOMITEVGA
    ytileheight = gui_font_height;
#endif
    if (Option.ColourCode)
    {
        OriginalFC = gui_fcolour; // *EB*
        OriginalBC = gui_bcolour; // *EB*
        gui_fcolour = WHITE;
        gui_bcolour = BLACK;
        ClearScreen(gui_bcolour);
    }
    if (Option.DISPLAY_CONSOLE == true && HRes / gui_font_width < 32)
        error("Font is too large");
    if (cmdfile)
    {
        ClearVars(0, true);
        int tf = gui_fcolour;
        int tb = gui_bcolour; // *EB*
        int tpf = PromptFont; // save font chosen by decision block
        ClearRuntime(true);   // *EB* (calls ResetDisplay which overwrites font)
        gui_bcolour = tb;
        gui_fcolour = tf; // *EB*
        SetFont(tpf);     // restore font after ClearRuntime/ResetDisplay
        PromptFont = tpf;
    }

#ifdef PICOMITEWEB
    cleanserver();
#endif
    multilinecomment = false;
    EdBuff = GetTempMemory(EDIT_BUFFER_SIZE);
    edit_buff_size = EDIT_BUFFER_SIZE;
    char buff[STRINGSIZE * 2] = {0};
    *EdBuff = 0;

    VHeight = Option.Height - 2;
    VWidth = Option.Width;
    edx = edy = curx = cury = y = x = tempx = 0;
    txtp = EdBuff;
    *tknbuf = 0;
    if (filename == NULL)
    {
        fromp = ProgMemory;
        p = EdBuff;
        nbrlines = 0;
        while (*fromp != 0xff)
        {
            if (*fromp == T_NEWLINE)
            {
                if (StartEditPoint >= ProgMemory)
                {
                    if (StartEditPoint == fromp)
                    {
                        y = nbrlines; // we will start editing at this line
                        tempx = x = StartEditChar;
                        txtp = p + StartEditChar;
                    }
                }
                else
                {
                    if (StartEditPoint == (unsigned char *)nbrlines)
                    {
                        y = nbrlines;
                        tempx = x = StartEditChar;
                        txtp = p + StartEditChar;
                    }
                }
                nbrlines++;
                if (Option.continuation)
                {
                    fromp = llist((unsigned char *)buff, fromp); // otherwise expand the line
                    if (!(nbrlines == 1 && buff[0] == '\'' && buff[1] == '#'))
                    {
                        nbrlines += format_string(&buff[0], Option.Width);
                        strcat((char *)p, buff);
                        p += strlen((char *)buff);
                        *p++ = '\n';
                        *p = 0;
                    }
                    else
                        nbrlines = 0;
                }
                else
                {
                    fromp = llist(p, fromp);
                    if (!(nbrlines == 1 && p[0] == '\'' && p[1] == '#'))
                    {
                        p += strlen((char *)p);
                        *p++ = '\n';
                        *p = 0;
                    }
                    else
                        nbrlines = 0;
                }
            }
            // finally, is it the end of the program?
            if (fromp[0] == 0 || fromp[0] == 0xff)
                break;
        }
    }
    else
    {
        //        char *fname = (char *)filename;
        char c;
        int fsize;
        //        strcpy(name,fname);
        if (!ExistsFile(filename))
        {
            if (strchr(filename, '.') == NULL)
                strcat(filename, ".bas");
        }
        if (!fstrstr(filename, ".bas"))
            Option.ColourCode = 0;
        if (ExistsFile(filename))
        {
            int fnbr1;
            fnbr1 = FindFreeFileNbr();
            fsize = FileSize(filename);
            BasicFileOpen(filename, fnbr1, FA_READ);
            if (fsize > edit_buff_size - 10)
                StandardError(29);
            p = EdBuff;
            char *q = (char *)EdBuff;
            do
            { // while waiting for the end of file
                c = FileGetChar(fnbr1);
                if (c == '\n')
                {
                    nbrlines++;
                    if (Option.continuation)
                    {
                        strcpy(buff, q);
                        nbrlines += format_string(&buff[0], Option.Width);
                        strcpy(q, buff);
                        p = (unsigned char *)q + strlen(q);
                        q = (char *)p;
                    }
                }
                if (c == '\r')
                    continue;
                *p++ = c;
            } while (!FileEOF(fnbr1));
            p++;
            FileClose(fnbr1);
        }
        txtp = EdBuff;
        tempx = x = 0;
#ifdef MMBASIC_FM
        if (fm_pending_edit_seek_valid)
        {
            int target_line = fm_pending_edit_seek_line;
            int target_col = fm_pending_edit_seek_char;
            if (target_line < 0)
                target_line = 0;
            if (target_col < 0)
                target_col = 0;
            y = 0;
            while (*txtp && y < target_line)
            {
                if (*txtp == '\n')
                    y++;
                txtp++;
            }
            x = 0;
            while (*txtp && *txtp != '\n' && x < target_col)
            {
                txtp++;
                x++;
            }
            tempx = x;
            fm_pending_edit_seek_valid = 0;
        }
#endif
    }
    if (nbrlines == 0)
        nbrlines++;
    if (p > EdBuff)
        --p;
    *p = 0; // erase the last line terminator
    // Only setterminal if editor requires is bigger than 80*24
    if (Option.Width > SCREENWIDTH || Option.Height > SCREENHEIGHT)
    {
        // Ask the attached serial terminal to grow to the editor's size.
        // setterminal() no longer changes autowrap; the explicit DECAWM
        // (CSI ?7l) below disables wrap for the duration of the editor and
        // is paired with a CSI ?7h on exit further down in this function.
        setterminal((Option.Height > SCREENHEIGHT) ? Option.Height : SCREENHEIGHT, (Option.Width > SCREENWIDTH) ? Option.Width : SCREENWIDTH); // or height is > 24
    }
    PrintString("\033[?1000h");        // Tera Term turn on mouse click report in VT200 mode
    PrintString("\033[?7l");           // disable autowrap while in editor
    PrintString("\0337\033[2J\033[H"); // vt100 clear screen and home cursor
    MX470Display(DISPLAY_CLS);         // clear screen on the MX470 display only
    SCursor(0, 0);
    PrintFunctKeys(EDIT);

    if (nbrlines > VHeight)
    {
        edy = y - VHeight / 2; // edy is the line displayed at the top
        if (edy < 0)
            edy = 0; // compensate if we are near the start
        y = y - edy; // y is the line on the screen
    }
    if (cmdfile)
        m_alloc(M_VAR); // clean up clipboard usage
    FullScreenEditor(x, y, filename, edit_buff_size, cmdfile);
    if (cmdfile)
        memset(tknbuf, 0, STRINGSIZE); // zero this so that nextstmt is pointing to the end of program
    MMCharPos = 0;
}

/*  @endcond */
void cmd_edit(void)
{
    edit(cmdline, true);
}
void cmd_editfile(void)
{
    edit(cmdline, FALSE);
}

#ifdef MMBASIC_FM
#define FM_MIN_WIDTH 50
#define FM_CONTEXT_FILE "/.fm"
#define FM_CONTEXT_MAGIC "FMCTX1"
#define FM_VOLUME_STEP 5
#define FM_SORT_NAME 0
#define FM_SORT_TIME 1
#define FM_SORT_TYPE 2
#define FM_MIN_CACHE_NAME_CHARS 32

typedef struct
{
    char *name;
    int is_dir;
    uint32_t size;
    uint16_t fdate;
    uint16_t ftime;
} fm_entry_t;

typedef struct
{
    int filesystem;
    char path[FF_MAX_LFN];
    char filter[FF_MAX_LFN];
    int selected;
    int top;
    int count;
    int sortorder;
    fm_entry_t *entries;
    char *name_pool;
    int name_stride;
    int *type_next;
    unsigned char *marks;
    int type_head[256];
    int entry_count;
    int marked_count;
    int cache_valid;
} fm_panel_t;

typedef struct
{
    char name[FF_MAX_LFN + 1];
    int is_dir;
} fm_mark_item_t;

extern char filepath[HAS_USB_MSC ? 3 : 2][FF_MAX_LFN];
extern void A2A(unsigned char *fromfile, unsigned char *tofile);
extern void A2B(unsigned char *fromfile, unsigned char *tofile, int dstfs);
extern void B2A(unsigned char *fromfile, unsigned char *tofile, int srcfs);
extern void B2B(unsigned char *fromfile, unsigned char *tofile, int srcfs, int dstfs);
extern void setVolumes(int valueL, int valueR);
extern void chdir(char *p);
extern int fm_program_launched_from_fm;

static const char *FM_ANSI_NORMAL = "\033[37;40m";
static const char *FM_ANSI_TITLE = "\033[36;40m";
static const char *FM_ANSI_ACTIVE = "\033[30;46m";
static const char *FM_ANSI_INACTIVE = "\033[30;47m";
static const char *FM_ANSI_STATUS = "\033[33;40m";
static unsigned char fm_break_key_for_list = BREAK_KEY;
static int fm_skip_serial_output = 0;
static int fm_edit_wants_run = 0;
#ifdef rp2350
static int fm_local_cache_valid = 0;
static int fm_local_cache_w = 0;
static int fm_local_cache_h = 0;
static char *fm_local_chars = NULL;
static unsigned char *fm_local_fc = NULL;
static unsigned char *fm_local_bc = NULL;
#endif

static int fm_copy_file_path(int srcfs, const char *srcfile, int dstfs, const char *dstfile,
                             char *status, int statuslen, int *file_count, int *dir_count);
static int fm_path_contains_icase(const char *parent, const char *child);
static int fm_copy_dir_recursive(int srcfs, const char *srcdir, int dstfs, const char *dstdir,
                                 char *status, int statuslen, int *file_count, int *dir_count);
static int fm_get_entry_at(fm_panel_t *panel, int index, fm_entry_t *entry, char *errmsg, int errmsglen);
static const char *fm_top_help_line(int width);
static void fm_format_panel_entry(const fm_panel_t *panel, int idx, char *out, int outlen);

#ifdef rp2350
static void fm_local_cache_release(void)
{
    FreeMemorySafe((void **)&fm_local_chars);
    FreeMemorySafe((void **)&fm_local_fc);
    FreeMemorySafe((void **)&fm_local_bc);
    fm_local_cache_w = 0;
    fm_local_cache_h = 0;
    fm_local_cache_valid = 0;
}

static void fm_local_cache_invalidate(void)
{
    fm_local_cache_valid = 0;
}

static void fm_local_cache_prepare(void)
{
    int cells;

    if (Option.Width <= 0 || Option.Height <= 0)
    {
        fm_local_cache_valid = 0;
        return;
    }

    if (fm_local_cache_w != Option.Width || fm_local_cache_h != Option.Height ||
        fm_local_chars == NULL || fm_local_fc == NULL || fm_local_bc == NULL)
    {
        fm_local_cache_release();
        cells = Option.Width * Option.Height;
        fm_local_chars = (char *)GetMemory(cells);
        fm_local_fc = (unsigned char *)GetMemory(cells);
        fm_local_bc = (unsigned char *)GetMemory(cells);
        fm_local_cache_w = Option.Width;
        fm_local_cache_h = Option.Height;
        fm_local_cache_valid = 0;
    }

    if (!fm_local_cache_valid)
    {
        memset(fm_local_chars, 0, fm_local_cache_w * fm_local_cache_h);
        memset(fm_local_fc, 0xFF, fm_local_cache_w * fm_local_cache_h);
        memset(fm_local_bc, 0xFF, fm_local_cache_w * fm_local_cache_h);
        fm_local_cache_valid = 1;
    }
}
#else
static inline void fm_local_cache_invalidate(void)
{
}

static inline void fm_local_cache_prepare(void)
{
}

static inline void fm_local_cache_release(void)
{
}
#endif

static inline char fm_drive_letter(int filesystem)
{
    return (char)('A' + filesystem);
}

static int fm_path_is_root(const char *path)
{
    return path == NULL || path[0] == 0 || strcmp(path, "/") == 0;
}

static void fm_set_panel_path_from_cwd(fm_panel_t *panel)
{
    const char *src = &filepath[panel->filesystem][2];
    if (src[0] == 0)
    {
        strcpy(panel->path, "/");
        return;
    }
    if (src[0] != '/')
    {
        panel->path[0] = '/';
        panel->path[1] = 0;
        strncat(panel->path, src, FF_MAX_LFN - 2);
    }
    else
    {
        strncpy(panel->path, src, FF_MAX_LFN - 1);
        panel->path[FF_MAX_LFN - 1] = 0;
    }
}

static void fm_parent_path(char *path)
{
    int i;
    if (fm_path_is_root(path))
        return;
    i = strlen(path) - 1;
    while (i > 0 && path[i] != '/')
        i--;
    if (i <= 0)
        strcpy(path, "/");
    else
        path[i] = 0;
}

static int fm_make_child_path(const char *base, const char *name, char *out, int outlen)
{
    if (fm_path_is_root(base))
        return snprintf(out, outlen, "/%s", name) < outlen;
    return snprintf(out, outlen, "%s/%s", base, name) < outlen;
}

static int fm_make_full_filename(int filesystem, const char *path, const char *name, char *out, int outlen)
{
    if (fm_path_is_root(path))
        return snprintf(out, outlen, "%c:/%s", fm_drive_letter(filesystem), name) < outlen;
    return snprintf(out, outlen, "%c:%s/%s", fm_drive_letter(filesystem), path, name) < outlen;
}

static int fm_stricmp(const char *a, const char *b)
{
    for (;; a++, b++)
    {
        int d = tolower(*a) - tolower(*b);
        if (d != 0 || !*a)
            return d;
    }
}

static void fm_flush_keyboard_buffer(void)
{
    // Reset repeat state and drain queued keycodes accumulated during redraw.
    clearrepeat();
    while (MMInkey() != -1)
        ;
}

static int fm_name_is_bas(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot != NULL && fm_stricmp(dot, ".bas") == 0;
}

static int fm_panel_name_limit(const fm_panel_t *panel)
{
    int limit = FM_MIN_CACHE_NAME_CHARS;

    if (panel && panel->name_stride > 1)
    {
        int observed = panel->name_stride - 1;
        if (observed > limit)
            limit = observed;
    }
    if (limit > FF_MAX_LFN)
        limit = FF_MAX_LFN;
    return limit;
}

enum
{
    FM_AUDIO_NONE = 0,
    FM_AUDIO_WAV,
    FM_AUDIO_FLAC,
    FM_AUDIO_MP3,
    FM_AUDIO_MIDI,
    FM_AUDIO_MOD
};

static int fm_name_audio_type(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot == NULL)
        return FM_AUDIO_NONE;
    if (fm_stricmp(dot, ".wav") == 0)
        return FM_AUDIO_WAV;
    if (fm_stricmp(dot, ".flac") == 0)
        return FM_AUDIO_FLAC;
    if (fm_stricmp(dot, ".mp3") == 0)
        return FM_AUDIO_MP3;
    if (fm_stricmp(dot, ".mid") == 0 || fm_stricmp(dot, ".midi") == 0)
        return FM_AUDIO_MIDI;
    if (fm_stricmp(dot, ".mod") == 0)
        return FM_AUDIO_MOD;
    return FM_AUDIO_NONE;
}

enum
{
    FM_IMAGE_NONE = 0,
    FM_IMAGE_BMP,
    FM_IMAGE_JPG,
    FM_IMAGE_PNG
};

static int fm_name_image_type(const char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot == NULL)
        return FM_IMAGE_NONE;
    if (fm_stricmp(dot, ".bmp") == 0)
        return FM_IMAGE_BMP;
    if (fm_stricmp(dot, ".jpg") == 0 || fm_stricmp(dot, ".jpeg") == 0)
        return FM_IMAGE_JPG;
    if (fm_stricmp(dot, ".png") == 0)
        return FM_IMAGE_PNG;
    return FM_IMAGE_NONE;
}

static void fm_wait_for_escape(void)
{
    int c = -1;

    while (1)
    {
        routinechecks();
        c = MMInkey();
        if (c != -1)
            break;
    }
}

static int fm_drive_available(int filesystem)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;
    int ok;

    if (filesystem == 0)
        return 1;

    if (filesystem == 1)
    {
        // Mirror InitSDCard() config guard, but never throw from FM.
        if ((IsInvalidPin(Option.SD_CS) && !Option.CombinedCS) ||
            (IsInvalidPin(Option.SYSTEM_MOSI) && IsInvalidPin(Option.SD_MOSI_PIN)) ||
            (IsInvalidPin(Option.SYSTEM_MISO) && IsInvalidPin(Option.SD_MISO_PIN)) ||
            (IsInvalidPin(Option.SYSTEM_CLK) && IsInvalidPin(Option.SD_CLK_PIN)))
            return 0;
    }

#if !HAS_USB_MSC
    if (filesystem == 2)
        return 0;
#endif

    FatFSFileSystem = filesystem;
    OptionFileErrorAbort = 0;
    ok = InitSDCard();
    OptionFileErrorAbort = oldabort;
    FatFSFileSystem = oldfs;
    return ok ? 1 : 0;
}

static int fm_filesystem_supported(int filesystem)
{
    if (filesystem == 0 || filesystem == 1)
        return 1;
#if HAS_USB_MSC
    if (filesystem == 2)
        return 1;
#endif
    return 0;
}

static int fm_path_exists_dir(int filesystem, const char *path)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;
    int ok = 0;

    if (!fm_filesystem_supported(filesystem))
        return 0;
    if (filesystem != 0 && !fm_drive_available(filesystem))
        return 0;
    if (path == NULL || path[0] == 0 || strcmp(path, "/") == 0)
        return 1;
    if (path[0] != '/')
        return 0;

    FatFSFileSystem = filesystem;
    if (filesystem == 0)
    {
        struct lfs_info info;
        memset(&info, 0, sizeof(info));
        if (lfs_stat(&lfs, path, &info) == 0 && info.type == LFS_TYPE_DIR)
            ok = 1;
    }
    else
    {
        FILINFO info;
        FRESULT fr;
        char q[FF_MAX_LFN];
        size_t qlen;
        memset(&info, 0, sizeof(info));
        strncpy(q, path, sizeof(q) - 1);
        q[sizeof(q) - 1] = 0;
        qlen = strlen(q);
        if (qlen > 0 && q[qlen - 1] == '/' && qlen + 1 < sizeof(q))
            strcat(q, ".");
        OptionFileErrorAbort = 0;
        fr = f_stat(q, &info);
        OptionFileErrorAbort = oldabort;
        if (fr == FR_OK && (info.fattrib & AM_DIR))
            ok = 1;
    }

    OptionFileErrorAbort = oldabort;
    FatFSFileSystem = oldfs;
    return ok;
}

static int fm_parse_panel_context_line(const char *line, fm_panel_t *panel)
{
    char tmp[FF_MAX_LFN * 2 + 64];
    char *f0, *f1, *f2, *f3, *f4, *f5;
    int fs, sel, top, sortorder = FM_SORT_NAME;

    if (line == NULL || panel == NULL)
        return 0;
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    f0 = tmp;
    f1 = strchr(f0, '|');
    if (!f1)
        return 0;
    *f1++ = 0;
    f2 = strchr(f1, '|');
    if (!f2)
        return 0;
    *f2++ = 0;
    f3 = strchr(f2, '|');
    if (!f3)
        return 0;
    *f3++ = 0;
    f4 = strchr(f3, '|');
    if (!f4)
        return 0;
    *f4++ = 0;
    f5 = strchr(f4, '|');
    if (f5)
    {
        *f5++ = 0;
        sortorder = atoi(f5);
    }

    fs = atoi(f0);
    sel = atoi(f3);
    top = atoi(f4);

    if (!fm_filesystem_supported(fs))
        return 0;
    if (!fm_drive_available(fs))
        return 0;
    if (!fm_path_exists_dir(fs, f1))
        return 0;

    panel->filesystem = fs;
    strncpy(panel->path, f1, FF_MAX_LFN - 1);
    panel->path[FF_MAX_LFN - 1] = 0;
    if (f2[0])
    {
        strncpy(panel->filter, f2, FF_MAX_LFN - 1);
        panel->filter[FF_MAX_LFN - 1] = 0;
    }
    else
        strcpy(panel->filter, "*");
    panel->selected = sel < 0 ? 0 : sel;
    panel->top = top < 0 ? 0 : top;
    panel->count = 0;
    panel->sortorder = (sortorder >= FM_SORT_NAME && sortorder <= FM_SORT_TYPE) ? sortorder : FM_SORT_NAME;
    panel->entries = NULL;
    panel->name_pool = NULL;
    panel->name_stride = 0;
    panel->type_next = NULL;
    panel->marks = NULL;
    panel->entry_count = 0;
    panel->marked_count = 0;
    panel->cache_valid = 0;
    memset(panel->type_head, 0xFF, sizeof(panel->type_head));
    return 1;
}

static int fm_load_context(fm_panel_t *panels, int *active)
{
    lfs_file_t file;
    char buf[FF_MAX_LFN * 2 + 256];
    char *line;
    int n;
    int restored_any = 0;
    int degraded = 0;

    if (!panels || !active)
        return 0;

    memset(&file, 0, sizeof(file));
    if (lfs_file_open(&lfs, &file, FM_CONTEXT_FILE, LFS_O_RDONLY) < 0)
        return 0;

    n = lfs_file_read(&lfs, &file, buf, sizeof(buf) - 1);
    lfs_file_close(&lfs, &file);
    if (n <= 0)
        return 0;

    buf[n] = 0;
    line = strtok(buf, "\r\n");
    if (!line || strcmp(line, FM_CONTEXT_MAGIC) != 0)
        return 0;

    while ((line = strtok(NULL, "\r\n")) != NULL)
    {
        if (strncmp(line, "P0=", 3) == 0)
        {
            if (fm_parse_panel_context_line(line + 3, &panels[0]))
                restored_any = 1;
            else
                degraded = 1;
        }
        else if (strncmp(line, "P1=", 3) == 0)
        {
            if (fm_parse_panel_context_line(line + 3, &panels[1]))
                restored_any = 1;
            else
                degraded = 1;
        }
        else if (strncmp(line, "ACTIVE=", 7) == 0)
        {
            int a = atoi(line + 7);
            if (a == 0 || a == 1)
                *active = a;
            else
                degraded = 1;
        }
    }

    return restored_any ? (degraded ? 2 : 1) : 0;
}

static void fm_save_context(const fm_panel_t *panels, int active)
{
    lfs_file_t file;
    static char prev_data[FF_MAX_LFN * 2 + 256] = {0};
    char data[FF_MAX_LFN * 2 + 256] = {0};
    int n;

    if (!panels)
        return;

    n = snprintf(data, sizeof(data),
                 "%s\n"
                 "P0=%d|%s|%s|%d|%d|%d\n"
                 "P1=%d|%s|%s|%d|%d|%d\n"
                 "ACTIVE=%d\n",
                 FM_CONTEXT_MAGIC,
                 panels[0].filesystem,
                 panels[0].path,
                 panels[0].filter,
                 panels[0].selected < 0 ? 0 : panels[0].selected,
                 panels[0].top < 0 ? 0 : panels[0].top,
                 (panels[0].sortorder >= FM_SORT_NAME && panels[0].sortorder <= FM_SORT_TYPE) ? panels[0].sortorder : FM_SORT_NAME,
                 panels[1].filesystem,
                 panels[1].path,
                 panels[1].filter,
                 panels[1].selected < 0 ? 0 : panels[1].selected,
                 panels[1].top < 0 ? 0 : panels[1].top,
                 (panels[1].sortorder >= FM_SORT_NAME && panels[1].sortorder <= FM_SORT_TYPE) ? panels[1].sortorder : FM_SORT_NAME,
                 (active == 1) ? 1 : 0);

    if (n <= 0 || n >= (int)sizeof(data))
        return;
    if (memcmp(data, prev_data, n) == 0)
        return;
    memcpy(prev_data, data, n);

    memset(&file, 0, sizeof(file));
    if (lfs_file_open(&lfs, &file, FM_CONTEXT_FILE, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0)
        return;

    if (lfs_file_write(&lfs, &file, data, n) != n)
    {
        lfs_file_close(&lfs, &file);
        return;
    }
    lfs_file_close(&lfs, &file);
}

static void fm_draw_field(int x, int y, int width, const char *text, const char *ansi, int fc, int bc)
{
    char line[STRINGSIZE * 2];
    int n;
    /* Audio launched from FM uses a producer/consumer double buffer fed by
     * checkWAVinput().  A full FM redraw can spend tens to hundreds of ms in
     * filesystem reads, serial blasts and framebuffer paints; if no buffer
     * refill happens in that window the IRQ replays the same buffer and the
     * playback stutters at startup.  Pump the audio at every field draw -
     * this is the lowest-level call site shared by every FM repaint path.   */
    if (CurrentlyPlaying != P_NOTHING)
        checkWAVinput();
    if (width <= 0)
        return;
    if (width >= (int)sizeof(line))
        width = sizeof(line) - 1;
    memset(line, ' ', width);
    line[width] = 0;
    if (text != NULL)
    {
        n = strlen(text);
        if (n > width)
            n = width;
        memcpy(line, text, n);
    }
    if (!fm_skip_serial_output)
    {
        SCursor(x, y);
        PrintString((char *)ansi);
        PrintString(line);
        PrintString("\033[0m");
    }
    if (Option.DISPLAY_CONSOLE)
    {
#ifdef rp2350
        int draw_needed = 1;
        int cached_width = width;
        int maxw;

        fm_local_cache_prepare();
        if (fm_local_cache_valid && x >= 0 && x < fm_local_cache_w && y >= 0 && y < fm_local_cache_h)
        {
            int i;
            maxw = fm_local_cache_w - x;
            if (x + cached_width > fm_local_cache_w)
                cached_width = maxw;
            for (i = 0; i < cached_width; i++)
            {
                int idx = y * fm_local_cache_w + (x + i);
                if (fm_local_chars[idx] != line[i] ||
                    fm_local_fc[idx] != (unsigned char)fc ||
                    fm_local_bc[idx] != (unsigned char)bc)
                {
                    draw_needed = 1;
                    break;
                }
                draw_needed = 0;
            }
            if (cached_width <= 0)
                draw_needed = 1;
        }

        if (draw_needed)
        {
            MX470Cursor(x * gui_font_width, y * gui_font_height);
            MX470PutS(line, fc, bc);
        }

        if (fm_local_cache_valid && x >= 0 && x < fm_local_cache_w && y >= 0 && y < fm_local_cache_h)
        {
            int i;
            maxw = fm_local_cache_w - x;
            if (x + cached_width > fm_local_cache_w)
                cached_width = maxw;
            for (i = 0; i < cached_width; i++)
            {
                int idx = y * fm_local_cache_w + (x + i);
                fm_local_chars[idx] = line[i];
                fm_local_fc[idx] = (unsigned char)fc;
                fm_local_bc[idx] = (unsigned char)bc;
            }
        }
#else
        MX470Cursor(x * gui_font_width, y * gui_font_height);
        MX470PutS(line, fc, bc);
#endif
    }
}

static void fm_fill_padded_line(char *out, int width, const char *text)
{
    int n = 0;

    if (width <= 0)
        return;
    memset(out, ' ', width);
    out[width] = 0;

    if (text != NULL)
    {
        n = strlen(text);
        if (n > width)
            n = width;
        if (n > 0)
            memcpy(out, text, n);
    }
}

static void fm_serial_repaint_ui(fm_panel_t *panels, int active, const char *status,
                                 int left_ok, const char *left_err,
                                 int right_ok, const char *right_err)
{
    char line[STRINGSIZE * 2];
    char left_line[STRINGSIZE * 2];
    char right_line[STRINGSIZE * 2];
    char left_text[STRINGSIZE * 2];
    char right_text[STRINGSIZE * 2];
    int list_rows = Option.Height - 4;
    int left_width = (Option.Width - 1) / 2;
    int right_width = Option.Width - (left_width + 1);
    int row;

    if (Option.Width <= 0 || Option.Height <= 0)
        return;

    PrintString("\033[H");

    fm_fill_padded_line(line, Option.Width, fm_top_help_line(Option.Width));
    PrintString((char *)FM_ANSI_TITLE);
    PrintString(line);
    PrintString("\033[0m");

    if (Option.Height > 1)
        PrintString("\r\n");

    snprintf(left_text, sizeof(left_text), "%c:%s [%s]",
             fm_drive_letter(panels[0].filesystem), panels[0].path, panels[0].filter);
    snprintf(right_text, sizeof(right_text), "%c:%s [%s]",
             fm_drive_letter(panels[1].filesystem), panels[1].path, panels[1].filter);
    fm_fill_padded_line(left_line, left_width, left_text);
    fm_fill_padded_line(right_line, right_width, right_text);

    PrintString((char *)(active == 0 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE));
    PrintString(left_line);
    PrintString((char *)FM_ANSI_TITLE);
    PrintString("|");
    PrintString((char *)(active == 1 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE));
    PrintString(right_line);
    PrintString("\033[0m");

    for (row = 0; row < list_rows; row++)
    {
        int li = panels[0].top + row;
        int ri = panels[1].top + row;
        const char *left_ansi = FM_ANSI_NORMAL;
        const char *right_ansi = FM_ANSI_NORMAL;

        /* Pump the audio double-buffer: serial PrintString of the full
         * two-panel UI can take 100+ ms on slow consoles, long enough for
         * the IRQ to drain the only buffer the file callback primed and
         * begin replaying it (the audible "warble" at start of playback). */
        if (CurrentlyPlaying != P_NOTHING)
            checkWAVinput();

        if (Option.Height > 2 || row > 0)
            PrintString("\r\n");

        left_text[0] = 0;
        if (!left_ok)
        {
            if (row == 0 && left_err)
                snprintf(left_text, sizeof(left_text), "%s", left_err);
            left_ansi = FM_ANSI_STATUS;
        }
        else if (li >= 0 && li < panels[0].count)
        {
            fm_format_panel_entry(&panels[0], li, left_text, sizeof(left_text));
            if (li == panels[0].selected)
                left_ansi = active == 0 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE;
        }
        fm_fill_padded_line(left_line, left_width, left_text);

        right_text[0] = 0;
        if (!right_ok)
        {
            if (row == 0 && right_err)
                snprintf(right_text, sizeof(right_text), "%s", right_err);
            right_ansi = FM_ANSI_STATUS;
        }
        else if (ri >= 0 && ri < panels[1].count)
        {
            fm_format_panel_entry(&panels[1], ri, right_text, sizeof(right_text));
            if (ri == panels[1].selected)
                right_ansi = active == 1 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE;
        }
        fm_fill_padded_line(right_line, right_width, right_text);

        PrintString((char *)left_ansi);
        PrintString(left_line);
        PrintString((char *)FM_ANSI_TITLE);
        PrintString("|");
        PrintString((char *)right_ansi);
        PrintString(right_line);
        PrintString("\033[0m");
    }

    if (Option.Height > 4)
        PrintString("\r\n\r\n");
    fm_fill_padded_line(line, Option.Width, status ? status : "");
    PrintString((char *)FM_ANSI_STATUS);
    PrintString(line);
    PrintString("\033[0m");
}

static void fm_free_panel_cache(fm_panel_t *panel)
{
    if (!panel)
        return;
    FreeMemorySafe((void **)&panel->entries);
    FreeMemorySafe((void **)&panel->name_pool);
    FreeMemorySafe((void **)&panel->type_next);
    FreeMemorySafe((void **)&panel->marks);
    panel->name_stride = 0;
    panel->entry_count = 0;
    panel->count = 0;
    panel->marked_count = 0;
    panel->cache_valid = 0;
    memset(panel->type_head, 0xFF, sizeof(panel->type_head));
}

// If the inactive panel is showing the same directory as the active panel,
// invalidate its cache so it refreshes on the next redraw.
static void fm_sync_other_panel_cache(fm_panel_t *panels, int active)
{
    fm_panel_t *cur = &panels[active];
    fm_panel_t *other = &panels[active ^ 1];
    if (other->filesystem == cur->filesystem &&
        fm_stricmp(other->path, cur->path) == 0)
        other->cache_valid = 0;
}

static const char *fm_find_extension(const char *name)
{
    const char *dot = NULL;

    while (*name)
    {
        if (*name == '.')
            dot = name;
        name++;
    }
    return dot;
}

static int fm_entry_cmp_name(const void *a, const void *b)
{
    const fm_entry_t *ea = (const fm_entry_t *)a;
    const fm_entry_t *eb = (const fm_entry_t *)b;

    if (ea->is_dir != eb->is_dir)
        return eb->is_dir - ea->is_dir;
    return fm_stricmp(ea->name, eb->name);
}

static int fm_entry_cmp_time(const void *a, const void *b)
{
    const fm_entry_t *ea = (const fm_entry_t *)a;
    const fm_entry_t *eb = (const fm_entry_t *)b;
    uint32_t da = ((uint32_t)ea->fdate << 16) | ea->ftime;
    uint32_t db = ((uint32_t)eb->fdate << 16) | eb->ftime;

    if (da < db)
        return 1;
    if (da > db)
        return -1;
    return fm_entry_cmp_name(a, b);
}

static int fm_entry_cmp_type(const void *a, const void *b)
{
    const fm_entry_t *ea = (const fm_entry_t *)a;
    const fm_entry_t *eb = (const fm_entry_t *)b;
    const char *xa, *xb;
    int r;

    if (ea->is_dir != eb->is_dir)
        return eb->is_dir - ea->is_dir;

    xa = fm_find_extension(ea->name);
    xb = fm_find_extension(eb->name);

    if (!xa && !xb)
        return fm_stricmp(ea->name, eb->name);
    if (!xa)
        return -1;
    if (!xb)
        return 1;

    r = fm_stricmp(xa, xb);
    if (r != 0)
        return r;
    return fm_stricmp(ea->name, eb->name);
}

static int fm_build_panel_cache(fm_panel_t *panel, char *errmsg, int errmsglen)
{
    int oldfs = FatFSFileSystem;
    int count = 1;
    int idx = 1;
    int max_name_len = FM_MIN_CACHE_NAME_CHARS;
    int i;

    if (!panel)
        return 0;
    if (errmsglen > 0)
        errmsg[0] = 0;

    if (panel->cache_valid)
        return 1;

    fm_free_panel_cache(panel);

    FatFSFileSystem = panel->filesystem;
    if (!fm_drive_available(panel->filesystem))
    {
        snprintf(errmsg, errmsglen, "Drive %c: not ready", fm_drive_letter(panel->filesystem));
        FatFSFileSystem = oldfs;
        return 0;
    }

    if (panel->filesystem == 0)
    {
        lfs_dir_t dir;
        struct lfs_info info;
        int rc;

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        rc = lfs_dir_open(&lfs, &dir, panel->path);
        if (rc < 0)
        {
            snprintf(errmsg, errmsglen, "Cannot open %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
        while ((rc = lfs_dir_read(&lfs, &dir, &info)) > 0)
        {
            int namelen;
            if (CurrentlyPlaying != P_NOTHING)
                checkWAVinput();
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
                continue;
            if (!pattern_matching(panel->filter, info.name, 0, 0))
                continue;
            count++;
            namelen = strlen(info.name);
            if (namelen > max_name_len)
                max_name_len = namelen;
        }
        lfs_dir_close(&lfs, &dir);
        if (rc < 0)
        {
            snprintf(errmsg, errmsglen, "Read error on %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
    }
    else
    {
        DIR dir;
        FILINFO info;
        FRESULT fr;

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        fr = f_findfirst(&dir, &info, panel->path, "*");
        if (fr != FR_OK)
        {
            snprintf(errmsg, errmsglen, "Cannot open %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
        while (fr == FR_OK && info.fname[0])
        {
            int namelen;
            if (CurrentlyPlaying != P_NOTHING)
                checkWAVinput();
            if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if (!(info.fattrib & (AM_SYS | AM_HID)) && pattern_matching(panel->filter, info.fname, 0, 0))
            {
                count++;
                namelen = strlen(info.fname);
                if (namelen > max_name_len)
                    max_name_len = namelen;
            }
            fr = f_findnext(&dir, &info);
        }
        f_closedir(&dir);
        if (fr != FR_OK)
        {
            snprintf(errmsg, errmsglen, "Read error on %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
    }

    if (max_name_len < FM_MIN_CACHE_NAME_CHARS)
        max_name_len = FM_MIN_CACHE_NAME_CHARS;
    if (max_name_len > FF_MAX_LFN)
        max_name_len = FF_MAX_LFN;
    panel->name_stride = max_name_len + 1;

    panel->entries = GetMemory(sizeof(fm_entry_t) * count);
    panel->name_pool = GetMemory(panel->name_stride * count);
    panel->type_next = GetMemory(sizeof(int) * count);
    panel->marks = GetMemory(count);
    if (!panel->entries || !panel->name_pool || !panel->type_next || !panel->marks)
    {
        fm_free_panel_cache(panel);
        snprintf(errmsg, errmsglen, "Not enough memory for FM list");
        FatFSFileSystem = oldfs;
        return 0;
    }

    for (i = 0; i < count; i++)
        panel->entries[i].name = panel->name_pool + (i * panel->name_stride);

    strcpy(panel->entries[0].name, fm_path_is_root(panel->path) ? "." : "..");
    panel->entries[0].is_dir = 1;
    panel->entries[0].size = 0;
    panel->entries[0].fdate = 0xFFFF;
    panel->entries[0].ftime = 0xFFFF;

    if (panel->filesystem == 0)
    {
        lfs_dir_t dir;
        struct lfs_info info;
        int rc;

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        rc = lfs_dir_open(&lfs, &dir, panel->path);
        if (rc < 0)
        {
            fm_free_panel_cache(panel);
            snprintf(errmsg, errmsglen, "Cannot open %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
        while ((rc = lfs_dir_read(&lfs, &dir, &info)) > 0)
        {
            fm_entry_t *e;
            if (CurrentlyPlaying != P_NOTHING)
                checkWAVinput();
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
                continue;
            if (!pattern_matching(panel->filter, info.name, 0, 0))
                continue;
            if (idx >= count)
                break;

            e = &panel->entries[idx++];
            strncpy(e->name, info.name, panel->name_stride - 1);
            e->name[panel->name_stride - 1] = 0;
            e->is_dir = (info.type == LFS_TYPE_DIR);
            e->size = info.size;
            if (e->is_dir)
            {
                e->fdate = 0xFFFF;
                e->ftime = 0xFFFF;
            }
            else if (panel->sortorder == FM_SORT_TIME)
            {
                int dt = 0;
                char fullfilename[FF_MAX_LFN];
                if (fm_make_child_path(panel->path, info.name, fullfilename, sizeof(fullfilename)) &&
                    lfs_getattr(&lfs, fullfilename, 'A', &dt, 4) == 4)
                {
                    WORD *wp = (WORD *)&dt;
                    e->fdate = wp[1];
                    e->ftime = wp[0];
                }
                else
                {
                    e->fdate = 0;
                    e->ftime = 0;
                }
            }
            else
            {
                e->fdate = 0;
                e->ftime = 0;
            }
        }
        lfs_dir_close(&lfs, &dir);
        if (rc < 0)
        {
            fm_free_panel_cache(panel);
            snprintf(errmsg, errmsglen, "Read error on %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
    }
    else
    {
        DIR dir;
        FILINFO info;
        FRESULT fr;

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        fr = f_findfirst(&dir, &info, panel->path, "*");
        if (fr != FR_OK)
        {
            fm_free_panel_cache(panel);
            snprintf(errmsg, errmsglen, "Cannot open %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
        while (fr == FR_OK && info.fname[0])
        {
            fm_entry_t *e;
            if (CurrentlyPlaying != P_NOTHING)
                checkWAVinput();
            if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if ((info.fattrib & (AM_SYS | AM_HID)) || !pattern_matching(panel->filter, info.fname, 0, 0))
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if (idx >= count)
                break;

            e = &panel->entries[idx++];
            strncpy(e->name, info.fname, panel->name_stride - 1);
            e->name[panel->name_stride - 1] = 0;
            e->is_dir = (info.fattrib & AM_DIR) != 0;
            e->size = info.fsize;
            if (e->is_dir)
            {
                e->fdate = 0xFFFF;
                e->ftime = 0xFFFF;
            }
            else
            {
                e->fdate = info.fdate;
                e->ftime = info.ftime;
            }
            fr = f_findnext(&dir, &info);
        }
        f_closedir(&dir);
        if (fr != FR_OK)
        {
            fm_free_panel_cache(panel);
            snprintf(errmsg, errmsglen, "Read error on %c:%s", fm_drive_letter(panel->filesystem), panel->path);
            FatFSFileSystem = oldfs;
            return 0;
        }
    }

    panel->entry_count = idx;
    panel->count = idx;

    if (panel->entry_count > 2)
    {
        if (panel->sortorder == FM_SORT_TIME)
            qsort(&panel->entries[1], panel->entry_count - 1, sizeof(fm_entry_t), fm_entry_cmp_time);
        else if (panel->sortorder == FM_SORT_TYPE)
            qsort(&panel->entries[1], panel->entry_count - 1, sizeof(fm_entry_t), fm_entry_cmp_type);
        else
            qsort(&panel->entries[1], panel->entry_count - 1, sizeof(fm_entry_t), fm_entry_cmp_name);
    }

    memset(panel->type_head, 0xFF, sizeof(panel->type_head));
    for (i = panel->entry_count - 1; i >= 1; i--)
    {
        int key = tolower((unsigned char)panel->entries[i].name[0]);
        panel->type_next[i] = panel->type_head[key];
        panel->type_head[key] = i;
    }
    panel->type_next[0] = -1;
    memset(panel->marks, 0, panel->entry_count > 0 ? panel->entry_count : 1);
    panel->marked_count = 0;
    panel->cache_valid = 1;
    FatFSFileSystem = oldfs;
    return 1;
}

static void fm_format_entry(const fm_entry_t *entry, char *out, int outlen)
{
    if (!out || outlen <= 0)
        return;
    out[0] = 0;

    if (entry->is_dir)
    {
        if (strcmp(entry->name, "..") == 0)
            snprintf(out, outlen, "[..]");
        else if (strcmp(entry->name, ".") == 0)
            snprintf(out, outlen, "[.]");
        else
        {
            strncpy(out, "<DIR> ", outlen - 1);
            out[outlen - 1] = 0;
            strncat(out, entry->name, outlen - strlen(out) - 1);
        }
    }
    else
    {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%10u ", (unsigned int)entry->size);
        strncpy(out, tmp, outlen - 1);
        out[outlen - 1] = 0;
        strncat(out, entry->name, outlen - strlen(out) - 1);
    }
}

static int fm_entry_is_markable(const fm_entry_t *entry)
{
    return entry && strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0;
}

static int fm_mark_toggle(fm_panel_t *panel, int index, char *errmsg, int errmsglen)
{
    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
        return 0;
    if (index < 0 || index >= panel->entry_count)
        return 0;
    if (!fm_entry_is_markable(&panel->entries[index]))
        return 0;

    if (panel->marks[index])
    {
        panel->marks[index] = 0;
        if (panel->marked_count > 0)
            panel->marked_count--;
    }
    else
    {
        panel->marks[index] = 1;
        panel->marked_count++;
    }
    return 1;
}

static int fm_mark_all(fm_panel_t *panel, char *errmsg, int errmsglen)
{
    int i;
    int changed = 0;

    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
        return 0;

    panel->marked_count = 0;
    for (i = 1; i < panel->entry_count; i++)
    {
        if (fm_entry_is_markable(&panel->entries[i]))
        {
            if (!panel->marks[i])
                changed = 1;
            panel->marks[i] = 1;
            panel->marked_count++;
        }
        else
        {
            panel->marks[i] = 0;
        }
    }
    if (panel->entry_count > 0)
        panel->marks[0] = 0;
    return changed;
}

static int fm_mark_clear_all(fm_panel_t *panel, char *errmsg, int errmsglen)
{
    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
        return 0;
    if (panel->entry_count > 0)
        memset(panel->marks, 0, panel->entry_count);
    panel->marked_count = 0;
    return 1;
}

static void fm_format_panel_entry(const fm_panel_t *panel, int idx, char *out, int outlen)
{
    char item[STRINGSIZE * 2];

    item[0] = 0;
    if (idx < 0 || idx >= panel->entry_count)
    {
        out[0] = 0;
        return;
    }

    fm_format_entry(&panel->entries[idx], item, sizeof(item));
    if (outlen <= 0)
        return;
    out[0] = panel->marks && panel->marks[idx] ? '*' : ' ';
    if (outlen == 1)
        return;
    out[1] = ' ';
    out[2] = 0;
    if (outlen > 3)
        strncat(out, item, outlen - 3);
}

static void fm_clamp_panel(fm_panel_t *panel, int list_rows)
{
    if (panel->count < 0)
        panel->count = 0;
    if (panel->selected >= panel->count)
        panel->selected = panel->count > 0 ? panel->count - 1 : 0;
    if (panel->selected < 0)
        panel->selected = 0;
    if (panel->top > panel->selected)
        panel->top = panel->selected;
    if (panel->top < 0)
        panel->top = 0;
    if (panel->count > list_rows && panel->top > panel->count - list_rows)
        panel->top = panel->count - list_rows;
}

static int fm_get_entry_at(fm_panel_t *panel, int index, fm_entry_t *entry, char *errmsg, int errmsglen)
{
    if (errmsglen > 0)
        errmsg[0] = 0;
    if (index < 0)
        return 0;

    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
        return 0;
    if (index >= panel->entry_count)
    {
        snprintf(errmsg, errmsglen, "Entry not found");
        return 0;
    }

    *entry = panel->entries[index];
    return 1;
}

static void fm_set_status_to_selected(fm_panel_t *panel, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];

    if (!status || statuslen <= 0)
        return;

    if (panel && panel->count > 0 && panel->selected >= 0 && panel->selected < panel->count &&
        fm_get_entry_at(panel, panel->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", entry.name);
    }
    else if (panel)
    {
        snprintf(status, statuslen, "%c:%s", fm_drive_letter(panel->filesystem), panel->path);
    }
    else
    {
        snprintf(status, statuslen, "FM");
    }
}

static int fm_draw_panel_entries(fm_panel_t *panel, int active, int x, int width, int list_top, int list_rows, char *errmsg, int errmsglen)
{
    int idx, drawn = 0, row;
    char item[STRINGSIZE * 2];
    if (errmsglen > 0)
        errmsg[0] = 0;

    for (row = 0; row < list_rows; row++)
    {
        fm_draw_field(x, list_top + row, width, "", FM_ANSI_NORMAL, WHITE, BLACK);
        /* Painting a full panel can take many milliseconds; service the
         * audio double-buffer so a MOD/WAV/FLAC/MP3 launched from FM does
         * not underrun and re-play its first buffer until the redraw
         * completes.                                                       */
        if (CurrentlyPlaying != P_NOTHING)
            checkWAVinput();
    }

    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
    {
        panel->count = 0;
        fm_draw_field(x, list_top, width, errmsg, FM_ANSI_STATUS, YELLOW, BLACK);
        return 0;
    }

    for (row = 0; row < list_rows; row++)
    {
        idx = panel->top + row;
        item[0] = 0;
        if (idx >= 0 && idx < panel->count)
        {
            fm_format_panel_entry(panel, idx, item, sizeof(item));
            drawn++;
        }
        fm_draw_field(x, list_top + row, width, item,
                      idx == panel->selected ? (active ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE) : FM_ANSI_NORMAL,
                      (idx == panel->selected ? BLACK : WHITE),
                      (idx == panel->selected ? (active ? CYAN : WHITE) : BLACK));
        if (CurrentlyPlaying != P_NOTHING)
            checkWAVinput();
    }

    if (drawn == 0 && panel->count == 0)
        fm_draw_field(x, list_top, width, "<empty>", FM_ANSI_NORMAL, WHITE, BLACK);
    return 1;
}

static const char *fm_top_help_line(int width)
{
    if (width >= 78)
        return "F1:Help  A/B/C:Drive  S:Sort  L/F9:List  SPACE:Mark  /:Type-select";
    if (width >= 64)
        return "F1:Help  A/B/C:Drive  S:Sort  L/F9:List  SPACE:Mark";
    if (width >= 50)
        return "F1:Help  A/B/C:Drv  S:Sort  L/F9:List";
    return "F1:Help  S:Sort  L/F9:List";
}

static int fm_help_draw_wrapped_line(int y, int width, const char *text)
{
    const char *p;

    if (width <= 0 || y >= Option.Height - 1)
        return y;
    if (text == NULL || text[0] == 0)
    {
        fm_draw_field(0, y, width, "", FM_ANSI_NORMAL, WHITE, BLACK);
        return y + 1;
    }

    p = text;
    while (*p && y < Option.Height - 1)
    {
        int remaining;
        int take;
        int i;
        int last_space;
        int outlen;
        char line[STRINGSIZE * 2];

        while (*p == ' ')
            p++;
        if (!*p)
            break;

        remaining = strlen(p);
        if (remaining <= width)
        {
            fm_draw_field(0, y, width, p, FM_ANSI_NORMAL, WHITE, BLACK);
            y++;
            break;
        }

        take = width;
        last_space = -1;
        for (i = 0; i < take; i++)
        {
            if (p[i] == ' ')
                last_space = i;
        }
        if (last_space > 0)
            take = last_space;

        outlen = take;
        while (outlen > 0 && p[outlen - 1] == ' ')
            outlen--;
        if (outlen <= 0)
            outlen = take;

        if (outlen >= (int)sizeof(line))
            outlen = sizeof(line) - 1;
        memcpy(line, p, outlen);
        line[outlen] = 0;
        fm_draw_field(0, y, width, line, FM_ANSI_NORMAL, WHITE, BLACK);
        y++;

        p += take;
        while (*p == ' ')
            p++;
    }

    return y;
}

static void fm_show_help_screen(void)
{
    static const char *help_lines[] = {
        "Arrows/Home/End/PgUp/PgDn or ^E/^X/^U/^K/^P/^L: move selection",
        "TAB/LEFT/RIGHT or ^S/^D: switch active panel",
        "ENTER: open dir, run .BAS, play audio, view .BMP/.JPG/.PNG",
        "L or F9: list selected file using paged LIST",
        "F2/^W: edit selected file (ESC/F1/F2 exits editor back to FM)",
        "A/B/C: change active drive   F3/^R set filter   F4/^T clear filter",
        "SPACE:mark/unmark  *:mark all  \\:clear marks  F5/^Y copy  DEL/^] delete  X rec del",
        "F6/^O stop  F7/F8 +/- vol  F10/^B mkdir  G:go  N:new  ^N:rename  D:dup  M:move",
        "/:type-select (name sort, ENTER open, ESC cancel)",
        "CTRL-C ignored in FM (except LIST, where it exits listing)"};
    int i, c;
    int y;
    int clipped;

    for (i = 0; i < Option.Height; i++)
        fm_draw_field(0, i, Option.Width, "", FM_ANSI_NORMAL, WHITE, BLACK);

    fm_draw_field(0, 0, Option.Width, "FM HELP  (press any key)", FM_ANSI_TITLE, CYAN, BLACK);
    y = 2;
    clipped = 0;
    for (i = 0; i < (int)(sizeof(help_lines) / sizeof(help_lines[0])); i++)
    {
        if (y >= Option.Height - 1)
        {
            clipped = 1;
            break;
        }
        y = fm_help_draw_wrapped_line(y, Option.Width, help_lines[i]);
    }

    if (clipped)
        fm_draw_field(0, Option.Height - 1, Option.Width, "Press any key to return to FM (help truncated)", FM_ANSI_STATUS, YELLOW, BLACK);
    else
        fm_draw_field(0, Option.Height - 1, Option.Width, "Press any key to return to FM", FM_ANSI_STATUS, YELLOW, BLACK);

    c = -1;
    while (c == -1)
    {
        routinechecks();
        c = MMInkey();
    }
}

static int fm_is_valid_leaf_name(const char *name)
{
    const char *p;
    if (name == NULL || name[0] == 0)
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    for (p = name; *p; p++)
    {
        if (*p == '/' || *p == '\\' || *p == ':')
            return 0;
    }
    return 1;
}

static int fm_prompt_text(const char *label, const char *initial, char *out, int outlen)
{
    int c;
    int len;
    char temp[FF_MAX_LFN + 1];
    char line[STRINGSIZE * 2];

    if (outlen <= 0)
        return 0;

    temp[0] = 0;
    if (initial && initial[0])
    {
        strncpy(temp, initial, FF_MAX_LFN);
        temp[FF_MAX_LFN] = 0;
    }
    len = strlen(temp);

    while (1)
    {
        line[0] = 0;
        if (label)
        {
            strncpy(line, label, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
        }
        strncat(line, temp, sizeof(line) - strlen(line) - 1);
        fm_draw_field(0, Option.Height - 1, Option.Width, line, FM_ANSI_STATUS, YELLOW, BLACK);
        c = MMgetchar();
        if (c == '\r' || c == '\n')
        {
            strncpy(out, temp, outlen - 1);
            out[outlen - 1] = 0;
            return 1;
        }
        if (c == ESC)
            return 0;
        if (c == BKSP)
        {
            if (len > 0)
                temp[--len] = 0;
            continue;
        }
        if (isprint(c) && len < FF_MAX_LFN)
        {
            temp[len++] = c;
            temp[len] = 0;
        }
    }
}

static int fm_confirm_action(const char *prompt)
{
    int c = -1;
    char line[STRINGSIZE * 2];
    const char *suffix = " [Y/N]";
    int room;

    strncpy(line, prompt ? prompt : "Confirm", sizeof(line) - 1);
    line[sizeof(line) - 1] = 0;

    room = (int)sizeof(line) - (int)strlen(line) - 1;
    if (room > 0)
        strncat(line, suffix, room);

    fm_draw_field(0, Option.Height - 1, Option.Width, line, FM_ANSI_STATUS, YELLOW, BLACK);

    while (1)
    {
        routinechecks();
        c = MMInkey();
        if (c == -1)
            continue;
        if (c >= 'a' && c <= 'z')
            c = mytoupper(c);
        if (c == 'Y')
            return 1;
        if (c == 'N' || c == ESC)
            return 0;
    }
}

static int fm_rename_path(int filesystem, const char *oldpath, const char *newpath, char *status, int statuslen)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;

    if (filesystem == 0)
    {
        int rc = lfs_rename(&lfs, oldpath, newpath);
        if (rc == 0)
            return 1;
        if (rc == LFS_ERR_EXIST)
            snprintf(status, statuslen, "Target already exists");
        else if (rc == LFS_ERR_NOENT)
            snprintf(status, statuslen, "Source not found");
        else
            snprintf(status, statuslen, "Rename failed (%d)", rc);
        return 0;
    }

    FatFSFileSystem = filesystem;
    OptionFileErrorAbort = 0;
    if (!InitSDCard())
    {
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(filesystem));
        return 0;
    }
    {
        FRESULT fr = f_rename(oldpath, newpath);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        if (fr == FR_OK)
            return 1;
        if (fr == FR_EXIST)
            snprintf(status, statuslen, "Target already exists");
        else if (fr == FR_NO_FILE || fr == FR_NO_PATH)
            snprintf(status, statuslen, "Source not found");
        else if (fr == FR_DENIED)
            snprintf(status, statuslen, "Rename denied");
        else
            snprintf(status, statuslen, "Rename failed (%d)", (int)fr);
        return 0;
    }
}

static int fm_delete_path(int filesystem, const char *path, int is_dir, char *status, int statuslen)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;

    if (filesystem == 0)
    {
        int rc = lfs_remove(&lfs, path);
        if (rc == 0)
            return 1;
        if (rc == LFS_ERR_NOTEMPTY)
            snprintf(status, statuslen, "Directory not empty");
        else if (rc == LFS_ERR_NOENT)
            snprintf(status, statuslen, "Not found");
        else
            snprintf(status, statuslen, "Delete failed (%d)", rc);
        return 0;
    }

    FatFSFileSystem = filesystem;
    OptionFileErrorAbort = 0;
    if (!InitSDCard())
    {
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(filesystem));
        return 0;
    }
    {
        FRESULT fr = f_unlink(path);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        if (fr == FR_OK)
            return 1;
        if (fr == FR_NO_FILE || fr == FR_NO_PATH)
            snprintf(status, statuslen, "Not found");
        else if (fr == FR_DENIED && is_dir)
            snprintf(status, statuslen, "Directory not empty");
        else if (fr == FR_DENIED)
            snprintf(status, statuslen, "Delete denied");
        else
            snprintf(status, statuslen, "Delete failed (%d)", (int)fr);
        return 0;
    }
}

static int fm_mkdir_path(int filesystem, const char *path, char *status, int statuslen)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;

    if (filesystem == 0)
    {
        int rc = lfs_mkdir(&lfs, path);
        if (rc == 0)
            return 1;
        if (rc == LFS_ERR_EXIST)
            snprintf(status, statuslen, "Directory already exists");
        else
            snprintf(status, statuslen, "Create directory failed (%d)", rc);
        return 0;
    }

    FatFSFileSystem = filesystem;
    OptionFileErrorAbort = 0;
    if (!InitSDCard())
    {
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(filesystem));
        return 0;
    }
    {
        FRESULT fr = f_mkdir(path);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        if (fr == FR_OK)
            return 1;
        if (fr == FR_EXIST)
            snprintf(status, statuslen, "Directory already exists");
        else
            snprintf(status, statuslen, "Create directory failed (%d)", (int)fr);
        return 0;
    }
}

static int fm_rename_selected(fm_panel_t *panel, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char oldpath[FF_MAX_LFN];
    char newpath[FF_MAX_LFN];
    char newname[FF_MAX_LFN + 1];
    char prompt[STRINGSIZE * 2];

    if (panel->count <= 0 || panel->selected < 0 || panel->selected >= panel->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(panel, panel->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
    {
        snprintf(status, statuslen, "Select a file or directory");
        return 0;
    }

    snprintf(prompt, sizeof(prompt), "Rename %s to: ", entry.name);
    if (!fm_prompt_text(prompt, entry.name, newname, sizeof(newname)))
    {
        snprintf(status, statuslen, "Rename cancelled");
        return 0;
    }
    if (!fm_is_valid_leaf_name(newname))
    {
        snprintf(status, statuslen, "Invalid name");
        return 0;
    }
    if (fm_stricmp(entry.name, newname) == 0)
    {
        snprintf(status, statuslen, "Name unchanged");
        return 0;
    }

    if (!fm_make_child_path(panel->path, entry.name, oldpath, sizeof(oldpath)) ||
        !fm_make_child_path(panel->path, newname, newpath, sizeof(newpath)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    if (!fm_rename_path(panel->filesystem, oldpath, newpath, status, statuslen))
        return 0;

    snprintf(status, statuslen, "Renamed %s -> %s", entry.name, newname);
    return 1;
}

static int fm_delete_selected(fm_panel_t *panel, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char fullpath[FF_MAX_LFN];
    char prompt[STRINGSIZE * 2];

    if (panel->count <= 0 || panel->selected < 0 || panel->selected >= panel->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(panel, panel->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
    {
        snprintf(status, statuslen, "Cannot delete this entry");
        return 0;
    }
    if (!fm_make_child_path(panel->path, entry.name, fullpath, sizeof(fullpath)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    snprintf(prompt, sizeof(prompt), "Delete %s?", entry.name);
    if (!fm_confirm_action(prompt))
    {
        snprintf(status, statuslen, "Delete cancelled");
        return 0;
    }

    if (!fm_delete_path(panel->filesystem, fullpath, entry.is_dir, status, statuslen))
        return 0;

    if (panel->selected > 0)
        panel->selected--;

    snprintf(status, statuslen, "Deleted %s", entry.name);
    return 1;
}

static int fm_delete_recursive_path(int filesystem, const char *path, char *status, int statuslen, int *file_count, int *dir_count)
{
    if (path == NULL || path[0] == 0 || strcmp(path, "/") == 0)
    {
        snprintf(status, statuslen, "Refusing to delete root directory");
        return 0;
    }

    if (filesystem == 0)
    {
        lfs_dir_t dir;
        struct lfs_info info;
        int rc;

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        rc = lfs_dir_open(&lfs, &dir, path);
        if (rc < 0)
        {
            snprintf(status, statuslen, "Cannot open %c:%s", fm_drive_letter(filesystem), path);
            return 0;
        }

        while ((rc = lfs_dir_read(&lfs, &dir, &info)) > 0)
        {
            char child[FF_MAX_LFN];
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
                continue;
            if (!fm_make_child_path(path, info.name, child, sizeof(child)))
            {
                lfs_dir_close(&lfs, &dir);
                snprintf(status, statuslen, "Path too long");
                return 0;
            }
            if (info.type == LFS_TYPE_DIR)
            {
                if (!fm_delete_recursive_path(filesystem, child, status, statuslen, file_count, dir_count))
                {
                    lfs_dir_close(&lfs, &dir);
                    return 0;
                }
            }
            else
            {
                if (!fm_delete_path(filesystem, child, 0, status, statuslen))
                {
                    lfs_dir_close(&lfs, &dir);
                    return 0;
                }
                if (file_count)
                    (*file_count)++;
            }
        }

        lfs_dir_close(&lfs, &dir);
        if (rc < 0)
        {
            snprintf(status, statuslen, "Read error on %c:%s", fm_drive_letter(filesystem), path);
            return 0;
        }

        if (!fm_delete_path(filesystem, path, 1, status, statuslen))
            return 0;
        if (dir_count)
            (*dir_count)++;
        return 1;
    }
    else
    {
        int oldfs = FatFSFileSystem;
        int oldabort = OptionFileErrorAbort;
        DIR dir;
        FILINFO info;
        FRESULT fr;

        FatFSFileSystem = filesystem;
        OptionFileErrorAbort = 0;
        if (!InitSDCard())
        {
            OptionFileErrorAbort = oldabort;
            FatFSFileSystem = oldfs;
            snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(filesystem));
            return 0;
        }

        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        fr = f_findfirst(&dir, &info, path, "*");
        if (fr != FR_OK)
        {
            OptionFileErrorAbort = oldabort;
            FatFSFileSystem = oldfs;
            snprintf(status, statuslen, "Cannot open %c:%s", fm_drive_letter(filesystem), path);
            return 0;
        }

        while (fr == FR_OK && info.fname[0])
        {
            char child[FF_MAX_LFN];
            if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if (!fm_make_child_path(path, info.fname, child, sizeof(child)))
            {
                f_closedir(&dir);
                OptionFileErrorAbort = oldabort;
                FatFSFileSystem = oldfs;
                snprintf(status, statuslen, "Path too long");
                return 0;
            }

            if (info.fattrib & AM_DIR)
            {
                if (!fm_delete_recursive_path(filesystem, child, status, statuslen, file_count, dir_count))
                {
                    f_closedir(&dir);
                    OptionFileErrorAbort = oldabort;
                    FatFSFileSystem = oldfs;
                    return 0;
                }
            }
            else
            {
                if (!fm_delete_path(filesystem, child, 0, status, statuslen))
                {
                    f_closedir(&dir);
                    OptionFileErrorAbort = oldabort;
                    FatFSFileSystem = oldfs;
                    return 0;
                }
                if (file_count)
                    (*file_count)++;
            }

            fr = f_findnext(&dir, &info);
        }

        f_closedir(&dir);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        if (fr != FR_OK)
        {
            snprintf(status, statuslen, "Read error on %c:%s", fm_drive_letter(filesystem), path);
            return 0;
        }

        if (!fm_delete_path(filesystem, path, 1, status, statuslen))
            return 0;
        if (dir_count)
            (*dir_count)++;
        return 1;
    }
}

static int fm_confirm_recursive_delete(const char *name)
{
    char prompt[STRINGSIZE * 2];

    snprintf(prompt, sizeof(prompt), "Recursive delete %s and contents?", name);
    if (!fm_confirm_action(prompt))
        return 0;

    snprintf(prompt, sizeof(prompt), "Final confirmation: permanently delete %s?", name);
    if (!fm_confirm_action(prompt))
        return 0;

    return 1;
}

static int fm_delete_selected_recursive(fm_panel_t *panel, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char fullpath[FF_MAX_LFN];
    int files = 0;
    int dirs = 0;

    if (panel->count <= 0 || panel->selected < 0 || panel->selected >= panel->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(panel, panel->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
    {
        snprintf(status, statuslen, "Cannot delete this entry");
        return 0;
    }
    if (!fm_make_child_path(panel->path, entry.name, fullpath, sizeof(fullpath)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    if (!fm_confirm_recursive_delete(entry.name))
    {
        snprintf(status, statuslen, "Recursive delete cancelled");
        return 0;
    }

    if (entry.is_dir)
    {
        if (!fm_delete_recursive_path(panel->filesystem, fullpath, status, statuslen, &files, &dirs))
            return 0;
        if (panel->selected > 0)
            panel->selected--;
        snprintf(status, statuslen, "Deleted %s (%d files, %d dirs)", entry.name, files, dirs);
        return 1;
    }

    if (!fm_delete_path(panel->filesystem, fullpath, 0, status, statuslen))
        return 0;
    if (panel->selected > 0)
        panel->selected--;
    snprintf(status, statuslen, "Deleted %s", entry.name);
    return 1;
}

static int fm_delete_marked(fm_panel_t *panel, int recursive, char *status, int statuslen)
{
    fm_mark_item_t *items = NULL;
    char prompt[STRINGSIZE * 2];
    int i, n = 0;
    int files = 0, dirs = 0;

    if (!fm_build_panel_cache(panel, status, statuslen))
        return 0;
    if (panel->marked_count <= 0)
        return recursive ? fm_delete_selected_recursive(panel, status, statuslen)
                         : fm_delete_selected(panel, status, statuslen);

    items = GetMemory(sizeof(fm_mark_item_t) * panel->marked_count);
    for (i = 1; i < panel->entry_count; i++)
    {
        if (panel->marks[i] && fm_entry_is_markable(&panel->entries[i]))
        {
            strncpy(items[n].name, panel->entries[i].name, FF_MAX_LFN);
            items[n].name[FF_MAX_LFN] = 0;
            items[n].is_dir = panel->entries[i].is_dir;
            n++;
        }
    }
    if (n <= 0)
    {
        FreeMemorySafe((void **)&items);
        snprintf(status, statuslen, "No marked entries");
        return 0;
    }

    if (!recursive)
    {
        snprintf(prompt, sizeof(prompt), "Delete %d marked item%s?", n, n == 1 ? "" : "s");
        if (!fm_confirm_action(prompt))
        {
            FreeMemorySafe((void **)&items);
            snprintf(status, statuslen, "Delete cancelled");
            return 0;
        }
    }
    else
    {
        snprintf(prompt, sizeof(prompt), "Recursive delete %d marked item%s and contents?", n, n == 1 ? "" : "s");
        if (!fm_confirm_action(prompt))
        {
            FreeMemorySafe((void **)&items);
            snprintf(status, statuslen, "Recursive delete cancelled");
            return 0;
        }
        snprintf(prompt, sizeof(prompt), "Final confirmation: permanently delete %d marked item%s?", n, n == 1 ? "" : "s");
        if (!fm_confirm_action(prompt))
        {
            FreeMemorySafe((void **)&items);
            snprintf(status, statuslen, "Recursive delete cancelled");
            return 0;
        }
    }

    for (i = 0; i < n; i++)
    {
        char fullpath[FF_MAX_LFN];

        if (!fm_make_child_path(panel->path, items[i].name, fullpath, sizeof(fullpath)))
        {
            FreeMemorySafe((void **)&items);
            snprintf(status, statuslen, "Path too long");
            panel->cache_valid = 0;
            panel->count = 0;
            return 0;
        }

        if (items[i].is_dir && recursive)
        {
            if (!fm_delete_recursive_path(panel->filesystem, fullpath, status, statuslen, &files, &dirs))
            {
                FreeMemorySafe((void **)&items);
                panel->cache_valid = 0;
                panel->count = 0;
                return 0;
            }
        }
        else
        {
            if (!fm_delete_path(panel->filesystem, fullpath, items[i].is_dir, status, statuslen))
            {
                FreeMemorySafe((void **)&items);
                panel->cache_valid = 0;
                panel->count = 0;
                return 0;
            }
            if (items[i].is_dir)
                dirs++;
            else
                files++;
        }
    }

    FreeMemorySafe((void **)&items);
    panel->cache_valid = 0;
    panel->count = 0;
    panel->selected = 0;
    panel->top = 0;
    if (recursive)
        snprintf(status, statuslen, "Deleted %d marked (%d files, %d dirs)", n, files, dirs);
    else
        snprintf(status, statuslen, "Deleted %d marked item%s", n, n == 1 ? "" : "s");
    return 1;
}

static int fm_make_directory(fm_panel_t *panel, char *status, int statuslen)
{
    char name[FF_MAX_LFN + 1];
    char path[FF_MAX_LFN];
    char prompt[STRINGSIZE * 2];

    snprintf(prompt, sizeof(prompt), "New directory name: ");
    if (!fm_prompt_text(prompt, "", name, sizeof(name)))
    {
        snprintf(status, statuslen, "Create directory cancelled");
        return 0;
    }
    if (!fm_is_valid_leaf_name(name))
    {
        snprintf(status, statuslen, "Invalid directory name");
        return 0;
    }
    if (!fm_make_child_path(panel->path, name, path, sizeof(path)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }
    if (!fm_mkdir_path(panel->filesystem, path, status, statuslen))
        return 0;

    snprintf(status, statuslen, "Created directory %s", name);
    return 1;
}

static int fm_normalize_path(const char *in, char *out, int outlen)
{
    const char *p;
    char seg[FF_MAX_LFN];

    if (!in || !out || outlen < 2)
        return 0;
    if (in[0] != '/')
        return 0;

    out[0] = '/';
    out[1] = 0;
    p = in;

    while (*p)
    {
        int n;
        while (*p == '/')
            p++;
        if (*p == 0)
            break;

        n = 0;
        while (p[n] && p[n] != '/')
        {
            if (n >= FF_MAX_LFN - 1)
                return 0;
            seg[n] = p[n];
            n++;
        }
        seg[n] = 0;
        p += n;

        if (strcmp(seg, ".") == 0)
            continue;
        if (strcmp(seg, "..") == 0)
        {
            if (out[1] != 0)
            {
                char *slash = strrchr(out, '/');
                if (slash == out)
                    out[1] = 0;
                else if (slash)
                    *slash = 0;
            }
            continue;
        }

        if (out[1] != 0)
        {
            size_t len = strlen(out);
            if ((int)len + 1 >= outlen)
                return 0;
            out[len] = '/';
            out[len + 1] = 0;
        }

        if ((int)(strlen(out) + strlen(seg)) >= outlen)
            return 0;
        strcat(out, seg);
    }

    return 1;
}

static int fm_resolve_path(const char *base, const char *input, char *out, int outlen)
{
    char combined[FF_MAX_LFN];

    if (!base || !input || !out)
        return 0;
    if (strchr(input, ':'))
        return 0;

    if (input[0] == '/')
        return fm_normalize_path(input, out, outlen);

    if (!fm_make_child_path(base, input, combined, sizeof(combined)))
        return 0;
    return fm_normalize_path(combined, out, outlen);
}

static int fm_go_to_path(fm_panel_t *panel, char *status, int statuslen)
{
    char input[FF_MAX_LFN + 1];
    char target[FF_MAX_LFN];

    if (!fm_prompt_text("Go to path: ", panel->path, input, sizeof(input)))
    {
        snprintf(status, statuslen, "Go to path cancelled");
        return 0;
    }
    if (input[0] == 0)
    {
        snprintf(status, statuslen, "Path unchanged");
        return 0;
    }
    if (!fm_resolve_path(panel->path, input, target, sizeof(target)))
    {
        snprintf(status, statuslen, "Invalid path");
        return 0;
    }
    if (!fm_path_exists_dir(panel->filesystem, target))
    {
        snprintf(status, statuslen, "Path not found");
        return 0;
    }

    strncpy(panel->path, target, FF_MAX_LFN - 1);
    panel->path[FF_MAX_LFN - 1] = 0;
    panel->selected = 0;
    panel->top = 0;
    panel->count = 0;
    snprintf(status, statuslen, "%c:%s", fm_drive_letter(panel->filesystem), panel->path);
    return 1;
}

static int fm_create_empty_file(int filesystem, const char *dir, const char *name, char *status, int statuslen)
{
    char path[FF_MAX_LFN];

    if (!fm_make_child_path(dir, name, path, sizeof(path)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    if (filesystem == 0)
    {
        lfs_file_t file;
        int rc;
        memset(&file, 0, sizeof(file));
        rc = lfs_file_open(&lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (rc < 0)
        {
            snprintf(status, statuslen, "Create file failed (%d)", rc);
            return 0;
        }
        lfs_file_close(&lfs, &file);
        return 1;
    }
    else
    {
        int oldfs = FatFSFileSystem;
        int oldabort = OptionFileErrorAbort;
        FIL f;
        FRESULT fr;

        FatFSFileSystem = filesystem;
        OptionFileErrorAbort = 0;
        if (!InitSDCard())
        {
            OptionFileErrorAbort = oldabort;
            FatFSFileSystem = oldfs;
            snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(filesystem));
            return 0;
        }

        fr = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr == FR_OK)
            f_close(&f);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;

        if (fr != FR_OK)
        {
            snprintf(status, statuslen, "Create file failed (%d)", (int)fr);
            return 0;
        }
        return 1;
    }
}

static void fm_suggest_duplicate_name(const char *name, char *out, int outlen)
{
    const char *dot = strrchr(name, '.');

    if (dot && dot != name)
    {
        int baselen = (int)(dot - name);
        if (snprintf(out, outlen, "%.*s_copy%s", baselen, name, dot) < outlen)
            return;
    }

    if (snprintf(out, outlen, "%s_copy", name) >= outlen)
    {
        strncpy(out, name, outlen - 1);
        out[outlen - 1] = 0;
    }
}

static int fm_duplicate_selected(fm_panel_t *panel, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char srcpath[FF_MAX_LFN], dstpath[FF_MAX_LFN];
    char srcfile[FF_MAX_LFN], dstfile[FF_MAX_LFN];
    char newname[FF_MAX_LFN + 1];
    int files = 0, dirs = 0;

    if (panel->count <= 0 || panel->selected < 0 || panel->selected >= panel->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(panel, panel->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
    {
        snprintf(status, statuslen, "Select a file or directory");
        return 0;
    }

    fm_suggest_duplicate_name(entry.name, newname, sizeof(newname));
    if (!fm_prompt_text("Duplicate as: ", newname, newname, sizeof(newname)))
    {
        snprintf(status, statuslen, "Duplicate cancelled");
        return 0;
    }
    if (!fm_is_valid_leaf_name(newname))
    {
        snprintf(status, statuslen, "Invalid name");
        return 0;
    }
    if (fm_stricmp(newname, entry.name) == 0)
    {
        snprintf(status, statuslen, "Name unchanged");
        return 0;
    }

    if (!fm_make_child_path(panel->path, entry.name, srcpath, sizeof(srcpath)) ||
        !fm_make_child_path(panel->path, newname, dstpath, sizeof(dstpath)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    if (entry.is_dir)
    {
        if (fm_path_contains_icase(srcpath, dstpath))
        {
            snprintf(status, statuslen, "Destination inside source not allowed");
            return 0;
        }
        if (!fm_copy_dir_recursive(panel->filesystem, srcpath, panel->filesystem, dstpath,
                                   status, statuslen, &files, &dirs))
            return 0;
        snprintf(status, statuslen, "Duplicated directory %s", newname);
        return 1;
    }

    if (!fm_make_full_filename(panel->filesystem, panel->path, entry.name, srcfile, sizeof(srcfile)) ||
        !fm_make_full_filename(panel->filesystem, panel->path, newname, dstfile, sizeof(dstfile)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }
    if (!fm_copy_file_path(panel->filesystem, srcfile, panel->filesystem, dstfile,
                           status, statuslen, &files, &dirs))
        return 0;

    snprintf(status, statuslen, "Duplicated %s", newname);
    return 1;
}

static int fm_move_selected(fm_panel_t *src, fm_panel_t *dst, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char srcpath[FF_MAX_LFN], dstpath[FF_MAX_LFN];
    char srcfile[FF_MAX_LFN], dstfile[FF_MAX_LFN];
    int files = 0, dirs = 0;

    if (src->count <= 0 || src->selected < 0 || src->selected >= src->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(src, src->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
    {
        snprintf(status, statuslen, "Select a file or directory");
        return 0;
    }

    if (!fm_make_child_path(src->path, entry.name, srcpath, sizeof(srcpath)) ||
        !fm_make_child_path(dst->path, entry.name, dstpath, sizeof(dstpath)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }

    if (src->filesystem == dst->filesystem)
    {
        if (fm_stricmp(srcpath, dstpath) == 0)
        {
            snprintf(status, statuslen, "Source and destination are the same");
            return 0;
        }
        if (entry.is_dir && fm_path_contains_icase(srcpath, dstpath))
        {
            snprintf(status, statuslen, "Destination inside source not allowed");
            return 0;
        }
        if (!fm_rename_path(src->filesystem, srcpath, dstpath, status, statuslen))
            return 0;
    }
    else
    {
        if (entry.is_dir)
        {
            snprintf(status, statuslen, "Cross-drive move for directories not supported");
            return 0;
        }

        if (!fm_make_full_filename(src->filesystem, src->path, entry.name, srcfile, sizeof(srcfile)) ||
            !fm_make_full_filename(dst->filesystem, dst->path, entry.name, dstfile, sizeof(dstfile)))
        {
            snprintf(status, statuslen, "Path too long");
            return 0;
        }
        if (!fm_copy_file_path(src->filesystem, srcfile, dst->filesystem, dstfile,
                               status, statuslen, &files, &dirs))
            return 0;
        if (!fm_delete_path(src->filesystem, srcpath, 0, status, statuslen))
            return 0;
    }

    if (src->selected > 0)
        src->selected--;

    snprintf(status, statuslen, "Moved %s", entry.name);
    return 1;
}

static void fm_draw_ui(fm_panel_t *panels, int active, const char *status)
{
    char line[STRINGSIZE * 2];
    char errmsg[STRINGSIZE];
    char left_err[STRINGSIZE];
    char right_err[STRINGSIZE];
    int left_ok;
    int right_ok;
    int list_top = 2;
    int list_rows = Option.Height - 4;
    int left_width = (Option.Width - 1) / 2;
    int right_x = left_width + 1;
    int right_width = Option.Width - right_x;
    int row;

    left_err[0] = 0;
    right_err[0] = 0;
    left_ok = fm_build_panel_cache(&panels[0], left_err, sizeof(left_err));
    right_ok = fm_build_panel_cache(&panels[1], right_err, sizeof(right_err));
    if (!left_ok)
        panels[0].count = 0;
    if (!right_ok)
        panels[1].count = 0;

    fm_clamp_panel(&panels[0], list_rows);
    fm_clamp_panel(&panels[1], list_rows);

    if (CurrentlyPlaying != P_NOTHING)
        checkWAVinput();

    fm_serial_repaint_ui(panels, active, status, left_ok, left_err, right_ok, right_err);

    fm_skip_serial_output = 1;

    snprintf(line, sizeof(line), "%s", fm_top_help_line(Option.Width));
    fm_draw_field(0, 0, Option.Width, line, FM_ANSI_TITLE, CYAN, BLACK);

    snprintf(line, sizeof(line), "%c:%s [%s]", fm_drive_letter(panels[0].filesystem), panels[0].path, panels[0].filter);
    fm_draw_field(0, 1, left_width, line, active == 0 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE,
                  active == 0 ? BLACK : BLACK, active == 0 ? CYAN : WHITE);
    snprintf(line, sizeof(line), "%c:%s [%s]", fm_drive_letter(panels[1].filesystem), panels[1].path, panels[1].filter);
    fm_draw_field(right_x, 1, right_width, line, active == 1 ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE,
                  active == 1 ? BLACK : BLACK, active == 1 ? CYAN : WHITE);
    for (row = 1; row < Option.Height - 1; row++)
        fm_draw_field(left_width, row, 1, "|", FM_ANSI_TITLE, CYAN, BLACK);

    fm_draw_panel_entries(&panels[0], active == 0, 0, left_width, list_top, list_rows, errmsg, sizeof(errmsg));
    fm_draw_panel_entries(&panels[1], active == 1, right_x, right_width, list_top, list_rows, errmsg, sizeof(errmsg));

    fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);

    fm_skip_serial_output = 0;
}

static int fm_prompt_filter(fm_panel_t *panel)
{
    int c;
    int len;
    char temp[FF_MAX_LFN];
    char line[STRINGSIZE * 2];

    if (strcmp(panel->filter, "*") == 0)
        temp[0] = 0;
    else
        strncpy(temp, panel->filter, FF_MAX_LFN - 1);
    temp[FF_MAX_LFN - 1] = 0;
    len = strlen(temp);

    while (1)
    {
        snprintf(line, sizeof(line), "Filter %c: %s", fm_drive_letter(panel->filesystem), temp);
        fm_draw_field(0, Option.Height - 1, Option.Width, line, FM_ANSI_STATUS, YELLOW, BLACK);
        c = MMgetchar();
        if (c == '\r' || c == '\n')
        {
            if (len == 0)
                strcpy(panel->filter, "*");
            else
                strcpy(panel->filter, temp);
            return 1;
        }
        if (c == ESC)
            return 0;
        if (c == BKSP)
        {
            if (len > 0)
                temp[--len] = 0;
            continue;
        }
        if (isprint(c) && len < FF_MAX_LFN - 1)
        {
            temp[len++] = c;
            temp[len] = 0;
        }
    }
}

static void fm_draw_panel_row(fm_panel_t *panel, int active, int x, int width, int list_top, int list_rows, int row, char *errmsg, int errmsglen)
{
    int idx;
    fm_entry_t e;
    char item[STRINGSIZE * 2] = {0};
    idx = panel->top + row;
    if (row < 0 || row >= list_rows)
        return;
    if (idx >= 0 && idx < panel->count)
    {
        if (fm_get_entry_at(panel, idx, &e, errmsg, errmsglen))
            fm_format_panel_entry(panel, idx, item, sizeof(item));
    }
    fm_draw_field(x, list_top + row, width, item,
                  idx == panel->selected ? (active ? FM_ANSI_ACTIVE : FM_ANSI_INACTIVE) : FM_ANSI_NORMAL,
                  (idx == panel->selected ? BLACK : WHITE),
                  (idx == panel->selected ? (active ? CYAN : WHITE) : BLACK));
}

static int fm_name_starts_with_icase(const char *name, const char *prefix)
{
    while (*prefix)
    {
        if (*name == 0)
            return 0;
        if (tolower((unsigned char)*name) != tolower((unsigned char)*prefix))
            return 0;
        name++;
        prefix++;
    }
    return 1;
}

static int fm_find_prefix_match(fm_panel_t *panel, const char *prefix, int *match_index, char *errmsg, int errmsglen)
{
    int i;
    int key;

    if (!panel || !prefix || !prefix[0])
        return 0;

    if (!fm_build_panel_cache(panel, errmsg, errmsglen))
        return 0;

    key = tolower((unsigned char)prefix[0]);

    for (i = panel->type_head[key]; i != -1; i = panel->type_next[i])
    {
        if (fm_name_starts_with_icase(panel->entries[i].name, prefix))
        {
            if (match_index)
                *match_index = i;
            return 1;
        }
    }

    return 0;
}

static int fm_mkdir_one(int filesystem, const char *path)
{
    int oldfs = FatFSFileSystem;
    int oldabort = OptionFileErrorAbort;
    int ok = 0;

    if (path == NULL || path[0] == 0 || (path[0] == '/' && path[1] == 0))
        return 1;

    if (filesystem == 0)
    {
        int rc = lfs_mkdir(&lfs, path);
        ok = (rc == 0 || rc == LFS_ERR_EXIST);
    }
    else
    {
        FatFSFileSystem = filesystem;
        OptionFileErrorAbort = 0;
        if (InitSDCard())
        {
            FRESULT fr = f_mkdir(path);
            ok = (fr == FR_OK || fr == FR_EXIST);
        }
    }

    OptionFileErrorAbort = oldabort;
    FatFSFileSystem = oldfs;
    return ok;
}

static int fm_ensure_dir(int filesystem, const char *path)
{
    char partial[FF_MAX_LFN];
    int i;

    if (path == NULL || path[0] == 0 || (path[0] == '/' && path[1] == 0))
        return 1;
    if (strlen(path) >= sizeof(partial))
        return 0;

    strcpy(partial, path);
    for (i = 1; partial[i]; i++)
    {
        if (partial[i] == '/')
        {
            partial[i] = 0;
            if (partial[0] && !fm_mkdir_one(filesystem, partial))
                return 0;
            partial[i] = '/';
        }
    }
    return fm_mkdir_one(filesystem, partial);
}

static void fm_copy_progress(const char *item, int files, int dirs)
{
    char line[STRINGSIZE * 2];
    snprintf(line, sizeof(line), "Copying... %d file%s, %d dir%s : %s",
             files, files == 1 ? "" : "s",
             dirs, dirs == 1 ? "" : "s",
             item ? item : "");
    fm_draw_field(0, Option.Height - 1, Option.Width, line, FM_ANSI_STATUS, YELLOW, BLACK);
    routinechecks();
}

static int fm_copy_file_path(int srcfs, const char *srcfile, int dstfs, const char *dstfile, char *status, int statuslen, int *file_count, int *dir_count)
{
    int oldabort = OptionFileErrorAbort;
    int olderrno = MMerrno;

    OptionFileErrorAbort = 0;
    MMerrno = 0;

    if (srcfs == 0 && dstfs == 0)
        A2A((unsigned char *)srcfile, (unsigned char *)dstfile);
    else if (srcfs == 0)
        A2B((unsigned char *)srcfile, (unsigned char *)dstfile, dstfs);
    else if (dstfs == 0)
        B2A((unsigned char *)srcfile, (unsigned char *)dstfile, srcfs);
    else
        B2B((unsigned char *)srcfile, (unsigned char *)dstfile, srcfs, dstfs);

    OptionFileErrorAbort = oldabort;
    if (MMerrno)
    {
        snprintf(status, statuslen, "%s", MMErrMsg[0] ? MMErrMsg : "Copy failed");
        MMerrno = olderrno;
        return 0;
    }
    MMerrno = olderrno;
    if (file_count)
    {
        (*file_count)++;
        fm_copy_progress(dstfile, *file_count, dir_count ? *dir_count : 0);
    }
    return 1;
}

static int fm_path_contains_icase(const char *parent, const char *child)
{
    int i = 0;
    while (parent[i] && child[i])
    {
        if (tolower(parent[i]) != tolower(child[i]))
            return 0;
        i++;
    }
    if (parent[i] == 0)
        return child[i] == 0 || child[i] == '/';
    return 0;
}

static int fm_copy_dir_recursive(int srcfs, const char *srcdir, int dstfs, const char *dstdir, char *status, int statuslen, int *file_count, int *dir_count)
{
    if (!fm_ensure_dir(dstfs, dstdir))
    {
        snprintf(status, statuslen, "Cannot create %c:%s", fm_drive_letter(dstfs), dstdir);
        return 0;
    }
    if (dir_count)
    {
        (*dir_count)++;
        fm_copy_progress(dstdir, file_count ? *file_count : 0, *dir_count);
    }

    if (srcfs == 0)
    {
        lfs_dir_t dir;
        struct lfs_info info;
        int rc;
        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        rc = lfs_dir_open(&lfs, &dir, srcdir);
        if (rc < 0)
        {
            snprintf(status, statuslen, "Cannot open %c:%s", fm_drive_letter(srcfs), srcdir);
            return 0;
        }
        while ((rc = lfs_dir_read(&lfs, &dir, &info)) > 0)
        {
            char csrc[FF_MAX_LFN], cdst[FF_MAX_LFN], srcfile[FF_MAX_LFN], dstfile[FF_MAX_LFN];
            if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
                continue;
            if (!fm_make_child_path(srcdir, info.name, csrc, sizeof(csrc)) ||
                !fm_make_child_path(dstdir, info.name, cdst, sizeof(cdst)))
            {
                lfs_dir_close(&lfs, &dir);
                snprintf(status, statuslen, "Path too long");
                return 0;
            }
            if (info.type == LFS_TYPE_DIR)
            {
                if (!fm_copy_dir_recursive(srcfs, csrc, dstfs, cdst, status, statuslen, file_count, dir_count))
                {
                    lfs_dir_close(&lfs, &dir);
                    return 0;
                }
            }
            else
            {
                if (!fm_make_full_filename(srcfs, srcdir, info.name, srcfile, sizeof(srcfile)) ||
                    !fm_make_full_filename(dstfs, dstdir, info.name, dstfile, sizeof(dstfile)))
                {
                    lfs_dir_close(&lfs, &dir);
                    snprintf(status, statuslen, "Path too long");
                    return 0;
                }
                if (!fm_copy_file_path(srcfs, srcfile, dstfs, dstfile, status, statuslen, file_count, dir_count))
                {
                    lfs_dir_close(&lfs, &dir);
                    return 0;
                }
            }
        }
        lfs_dir_close(&lfs, &dir);
        if (rc < 0)
        {
            snprintf(status, statuslen, "Read error on %c:%s", fm_drive_letter(srcfs), srcdir);
            return 0;
        }
    }
    else
    {
        int oldfs = FatFSFileSystem;
        int oldabort = OptionFileErrorAbort;
        DIR dir;
        FILINFO info;
        FRESULT fr;
        FatFSFileSystem = srcfs;
        OptionFileErrorAbort = 0;
        if (!InitSDCard())
        {
            OptionFileErrorAbort = oldabort;
            FatFSFileSystem = oldfs;
            snprintf(status, statuslen, "Drive %c: not ready", fm_drive_letter(srcfs));
            return 0;
        }
        memset(&dir, 0, sizeof(dir));
        memset(&info, 0, sizeof(info));
        fr = f_findfirst(&dir, &info, srcdir, "*");
        while (fr == FR_OK && info.fname[0])
        {
            char csrc[FF_MAX_LFN], cdst[FF_MAX_LFN], srcfile[FF_MAX_LFN], dstfile[FF_MAX_LFN];
            if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0)
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if (info.fattrib & (AM_SYS | AM_HID))
            {
                fr = f_findnext(&dir, &info);
                continue;
            }
            if (!fm_make_child_path(srcdir, info.fname, csrc, sizeof(csrc)) ||
                !fm_make_child_path(dstdir, info.fname, cdst, sizeof(cdst)))
            {
                f_closedir(&dir);
                OptionFileErrorAbort = oldabort;
                FatFSFileSystem = oldfs;
                snprintf(status, statuslen, "Path too long");
                return 0;
            }
            if (info.fattrib & AM_DIR)
            {
                if (!fm_copy_dir_recursive(srcfs, csrc, dstfs, cdst, status, statuslen, file_count, dir_count))
                {
                    f_closedir(&dir);
                    OptionFileErrorAbort = oldabort;
                    FatFSFileSystem = oldfs;
                    return 0;
                }
            }
            else
            {
                if (!fm_make_full_filename(srcfs, srcdir, info.fname, srcfile, sizeof(srcfile)) ||
                    !fm_make_full_filename(dstfs, dstdir, info.fname, dstfile, sizeof(dstfile)))
                {
                    f_closedir(&dir);
                    OptionFileErrorAbort = oldabort;
                    FatFSFileSystem = oldfs;
                    snprintf(status, statuslen, "Path too long");
                    return 0;
                }
                if (!fm_copy_file_path(srcfs, srcfile, dstfs, dstfile, status, statuslen, file_count, dir_count))
                {
                    f_closedir(&dir);
                    OptionFileErrorAbort = oldabort;
                    FatFSFileSystem = oldfs;
                    return 0;
                }
            }
            fr = f_findnext(&dir, &info);
        }
        f_closedir(&dir);
        OptionFileErrorAbort = oldabort;
        FatFSFileSystem = oldfs;
        if (fr != FR_OK)
        {
            snprintf(status, statuslen, "Read error on %c:%s", fm_drive_letter(srcfs), srcdir);
            return 0;
        }
    }

    return 1;
}

static int fm_copy_selected(fm_panel_t *src, fm_panel_t *dst, char *status, int statuslen)
{
    fm_entry_t entry;
    char errmsg[STRINGSIZE];
    char srcdir[FF_MAX_LFN];
    char dstdir[FF_MAX_LFN];
    char srcfile[FF_MAX_LFN];
    char dstfile[FF_MAX_LFN];
    int file_count = 0;
    int dir_count = 0;
    if (src->count <= 0 || src->selected < 0 || src->selected >= src->count)
    {
        snprintf(status, statuslen, "No file selected");
        return 0;
    }
    if (!fm_get_entry_at(src, src->selected, &entry, errmsg, sizeof(errmsg)))
    {
        snprintf(status, statuslen, "%s", errmsg);
        return 0;
    }
    if (entry.is_dir)
    {
        if (!fm_make_child_path(src->path, entry.name, srcdir, sizeof(srcdir)) ||
            !fm_make_child_path(dst->path, entry.name, dstdir, sizeof(dstdir)))
        {
            snprintf(status, statuslen, "Path too long");
            return 0;
        }
        if (src->filesystem == dst->filesystem && fm_path_contains_icase(srcdir, dstdir))
        {
            snprintf(status, statuslen, "Destination inside source not allowed");
            return 0;
        }
        if (!fm_copy_dir_recursive(src->filesystem, srcdir, dst->filesystem, dstdir, status, statuslen, &file_count, &dir_count))
            return 0;
        snprintf(status, statuslen, "Copied directory %s (%d files, %d dirs)", entry.name, file_count, dir_count);
        return 1;
    }
    if (!fm_make_full_filename(src->filesystem, src->path, entry.name, srcfile, sizeof(srcfile)) ||
        !fm_make_full_filename(dst->filesystem, dst->path, entry.name, dstfile, sizeof(dstfile)))
    {
        snprintf(status, statuslen, "Path too long");
        return 0;
    }
    if (fm_stricmp(srcfile, dstfile) == 0)
    {
        snprintf(status, statuslen, "Source and destination are the same");
        return 0;
    }

    if (!fm_copy_file_path(src->filesystem, srcfile, dst->filesystem, dstfile, status, statuslen, &file_count, &dir_count))
        return 0;

    snprintf(status, statuslen, "Copied %s", entry.name);
    return 1;
}

static int fm_copy_marked(fm_panel_t *src, fm_panel_t *dst, char *status, int statuslen)
{
    int i;
    int copied = 0;
    int old_selected = src->selected;

    if (!fm_build_panel_cache(src, status, statuslen))
        return 0;
    if (src->marked_count <= 0)
        return fm_copy_selected(src, dst, status, statuslen);

    for (i = 1; i < src->entry_count; i++)
    {
        if (!src->marks[i] || !fm_entry_is_markable(&src->entries[i]))
            continue;
        src->selected = i;
        if (!fm_copy_selected(src, dst, status, statuslen))
        {
            src->selected = old_selected;
            return 0;
        }
        copied++;
    }

    src->selected = old_selected;
    snprintf(status, statuslen, "Copied %d marked item%s", copied, copied == 1 ? "" : "s");
    return copied > 0;
}

static int fm_run_bas_program(const char *filename, const char *fullpath)
{
    unsigned char runarg[MAXSTRLEN + 1];
    unsigned char *saved_cmdline = cmdline;
    int loaded = 0;

    if (filename && filename[0])
    {
        if (snprintf((char *)runarg, sizeof(runarg), "\"%s\",", filename) < (int)sizeof(runarg))
            loaded = FileLoadProgram(runarg, false);
    }

    // Fallback for edge cases where current drive/path has not yet been fully applied.
    if (!loaded && fullpath && fullpath[0])
    {
        if (snprintf((char *)runarg, sizeof(runarg), "\"%s\",", fullpath) < (int)sizeof(runarg))
        {
            cmdline = (unsigned char *)"FM";
            loaded = FileLoadProgram(runarg, false);
            cmdline = saved_cmdline;
        }
    }

    cmdline = saved_cmdline;
    if (!loaded)
        return 0;

    ClearRuntime(true);
    if (PrepareProgram(true))
    {
        PrintPreprogramError();
        return 0;
    }

    if (Option.DISPLAY_CONSOLE && (SPIREAD || Option.NoScroll))
    {
        ClearScreen(gui_bcolour);
        CurrentX = 0;
        CurrentY = 0;
    }

    cmdlinebuff[0] = 0;
    IgnorePIN = false;
    if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
        ExecuteProgram(LibMemory);
    if (*ProgMemory != T_NEWLINE)
        return 1;
#ifdef PICOMITEWEB
    cleanserver();
#endif
#ifndef USBKEYBOARD
    if (mouse0 == false && Option.MOUSE_CLOCK)
        initMouse0(0); // see if there is a mouse to initialise
#endif
    nextstmt = ProgMemory;
    return 1;
}

static void fm_set_default_drive_dir(const fm_panel_t *panel)
{
    char cwd[FF_MAX_LFN + 3];

    FatFSFileSystem = panel->filesystem;
    FatFSFileSystemSave = panel->filesystem;

    cwd[0] = fm_drive_letter(panel->filesystem);
    cwd[1] = ':';
    if (fm_path_is_root(panel->path))
    {
        cwd[2] = '/';
        cwd[3] = 0;
    }
    else
    {
        strncpy(&cwd[2], panel->path, FF_MAX_LFN);
        cwd[FF_MAX_LFN + 2] = 0;
    }

    chdir(cwd);
}

static int fm_audio_uses_vs1053(void)
{
#if defined(PICOMITEMIN)
    return 0;
#else
    return Option.AUDIO_MISO_PIN != 0;
#endif
}

static void fm_adjust_volume(int delta, char *status, int status_len)
{
    int v = vol_target_left + delta;

    if (v < 0)
        v = 0;
    if (v > 100)
        v = 100;

    vol_target_left = v;
    vol_target_right = v;

    // VS1053 playback uses the chip mixer, so apply changes immediately.
    if (fm_audio_uses_vs1053() && CurrentlyPlaying != P_NOTHING)
    {
        vol_left = vol_target_left;
        vol_right = vol_target_right;
        setVolumes(vol_left, vol_right);
    }

    snprintf(status, status_len, "Volume %d%%", v);
}

static int fm_play_audio_file(const fm_panel_t *panel, const fm_entry_t *entry, char *status, int status_len)
{
    char full[FF_MAX_LFN];
    int audio_type = fm_name_audio_type(entry->name);

    if (audio_type == FM_AUDIO_NONE)
        return 0;

    if (!(Option.AUDIO_L || Option.AUDIO_CLK_PIN || Option.audio_i2s_bclk))
    {
        snprintf(status, status_len, "Audio not enabled");
        return 1;
    }

    if (audio_type == FM_AUDIO_MIDI && !fm_audio_uses_vs1053())
    {
        snprintf(status, status_len, "MIDI needs VS1053 audio");
        return 1;
    }

    if (audio_type == FM_AUDIO_MOD)
    {
#ifdef rp2350
        if (!(modbuff || PSRAMsize))
        {
            snprintf(status, status_len, "Mod playback not enabled");
            return 1;
        }
#else
        if (!modbuff)
        {
            snprintf(status, status_len, "Mod playback not enabled");
            return 1;
        }
#endif
    }

#ifndef rp2350
    if (audio_type == FM_AUDIO_MP3 && !fm_audio_uses_vs1053())
    {
        snprintf(status, status_len, "MP3 needs VS1053 audio");
        return 1;
    }
#else
    if (audio_type == FM_AUDIO_MP3 && Option.CPU_Speed < 200000 && !fm_audio_uses_vs1053())
    {
        snprintf(status, status_len, "MP3 needs CPUSPEED >=200000");
        return 1;
    }
#endif

    if (!fm_make_full_filename(panel->filesystem, panel->path, entry->name, full, sizeof(full)))
    {
        snprintf(status, status_len, "Path too long");
        return 1;
    }

    fm_set_default_drive_dir(panel);
    if (CurrentlyPlaying != P_NOTHING)
        CloseAudio(1);

    switch (audio_type)
    {
    case FM_AUDIO_WAV:
        wavcallback(full);
        break;
    case FM_AUDIO_FLAC:
        flaccallback(full);
        break;
    case FM_AUDIO_MP3:
        mp3callback(full, 0);
        break;
    case FM_AUDIO_MIDI:
        midicallback(full);
        break;
    case FM_AUDIO_MOD:
        modcallback(full);
        break;
    default:
        return 0;
    }

    if (CurrentlyPlaying == P_NOTHING)
        snprintf(status, status_len, "Unable to play %s", entry->name);
    else
        snprintf(status, status_len, "Playing %s", entry->name);
    return 1;
}

static int fm_show_image_file(const fm_panel_t *panel, const fm_entry_t *entry, char *status, int status_len,
                              int oldmode_local, int oldfont_local)
{
    char full[FF_MAX_LFN];
    char loadline[FF_MAX_LFN + 16];
    int image_type = fm_name_image_type(entry->name);
    unsigned char *saved_cmdline;
    int fm_font_local = PromptFont;
#ifdef PICOMITEVGA
    int fm_mode_local = DISPLAY_TYPE;
    bool switched_out_of_fm_mode = false;
#endif

    if (image_type == FM_IMAGE_NONE)
        return 0;

#ifndef rp2350
    if (image_type == FM_IMAGE_PNG)
    {
        snprintf(status, status_len, "PNG preview requires RP2350 build");
        return 1;
    }
#endif

    if (!fm_make_full_filename(panel->filesystem, panel->path, entry->name, full, sizeof(full)))
    {
        snprintf(status, status_len, "Path too long");
        return 1;
    }

    fm_set_default_drive_dir(panel);

    if (image_type == FM_IMAGE_BMP)
    {
        if (snprintf(loadline, sizeof(loadline), "BMP \"%s\"", full) >= (int)sizeof(loadline))
        {
            snprintf(status, status_len, "Path too long");
            return 1;
        }
    }
    else if (image_type == FM_IMAGE_JPG)
    {
        if (snprintf(loadline, sizeof(loadline), "JPG \"%s\"", full) >= (int)sizeof(loadline))
        {
            snprintf(status, status_len, "Path too long");
            return 1;
        }
    }
    else
    {
        if (snprintf(loadline, sizeof(loadline), "PNG \"%s\"", full) >= (int)sizeof(loadline))
        {
            snprintf(status, status_len, "Path too long");
            return 1;
        }
    }

#ifdef PICOMITEVGA
    if (DISPLAY_TYPE == SCREENMODE1)
    {
#ifdef rp2350
        DISPLAY_TYPE = SCREENMODE3;
#else
        DISPLAY_TYPE = SCREENMODE2;
#endif
        ResetDisplay();
        switched_out_of_fm_mode = true;
    }
    else if (DISPLAY_TYPE != oldmode_local || PromptFont != oldfont_local)
    {
        DISPLAY_TYPE = oldmode_local;
        ResetDisplay();
        SetFont(oldfont_local);
        PromptFont = oldfont_local;
        switched_out_of_fm_mode = true;
    }
    if (switched_out_of_fm_mode)
    {
        ClearScreen(gui_bcolour);
        CurrentX = 0;
        CurrentY = 0;
    }
#endif

    {
        jmp_buf saved_mark;
        int load_error = 0;
        int old_suppress_error_output = fm_suppress_error_output;
        saved_cmdline = cmdline;
        fm_suppress_error_output = 1;
        memcpy(saved_mark, mark, sizeof(jmp_buf));
        if (setjmp(mark) != 0)
        {
            load_error = 1;
            cmdline = saved_cmdline;
        }
        else
        {
            MMErrMsg[0] = 0;
            cmdline = (unsigned char *)loadline;
            cmd_load();
            cmdline = saved_cmdline;
        }
        memcpy(mark, saved_mark, sizeof(jmp_buf));
        fm_suppress_error_output = old_suppress_error_output;

        // FM calls cmd_load() directly, so release command-temporary allocations
        // here (MMBasic normally does this at command-loop boundaries).
        ClearTempMemory();

        if (load_error)
        {
            snprintf(status, status_len, "Error: %s", MMErrMsg[0] ? MMErrMsg : "Image decode failed");
        }
        else
        {
            fm_wait_for_escape();
            fm_flush_keyboard_buffer();
            snprintf(status, status_len, "%s", entry->name);
        }
    }

#ifdef PICOMITEVGA
    if (switched_out_of_fm_mode)
    {
        DISPLAY_TYPE = fm_mode_local;
        ResetDisplay();
    }
#endif
    SetFont(fm_font_local);
    PromptFont = fm_font_local;

    ClearScreen(gui_bcolour);
    CurrentX = 0;
    CurrentY = 0;
    fm_local_cache_invalidate();

    return 1;
}

static int fm_list_file(const fm_panel_t *panel, const fm_entry_t *entry, char *status, int status_len)
{
    char full[FF_MAX_LFN];
    jmp_buf saved_mark;
    unsigned char *saved_cmdline;
    unsigned char old_break_key = BreakKey;
    int old_suppress_error_output;

    if (entry->is_dir)
    {
        snprintf(status, status_len, "Select a file to list");
        return 1;
    }

    if (!fm_make_full_filename(panel->filesystem, panel->path, entry->name, full, sizeof(full)))
    {
        snprintf(status, status_len, "Path too long");
        return 1;
    }

    fm_set_default_drive_dir(panel);
    saved_cmdline = cmdline;
    old_suppress_error_output = fm_suppress_error_output;

    MMErrMsg[0] = 0;
    MMAbort = false;
    fm_suppress_error_output = 1;
    fm_flush_keyboard_buffer();

    if (Option.DISPLAY_CONSOLE)
    {
        ClearScreen(gui_bcolour);
        CurrentX = 0;
        CurrentY = 0;
        fm_local_cache_invalidate();
    }

    BreakKey = fm_break_key_for_list;

    memcpy(saved_mark, mark, sizeof(jmp_buf));
    if (setjmp(mark) != 0)
    {
        cmdline = saved_cmdline;
    }
    else
    {
        ListFilePaged(full);
        cmdline = saved_cmdline;
    }
    memcpy(mark, saved_mark, sizeof(jmp_buf));
    BreakKey = old_break_key;

    fm_suppress_error_output = old_suppress_error_output;
    MMAbort = false;

    clearrepeat();
    fm_flush_keyboard_buffer();

    // Always return to the selected filename status after LIST exits (including CTRL-C).
    snprintf(status, status_len, "%s", entry->name);

    return 1;
}

// Saved pre-FM state — persisted across the FM→program→FM relaunch cycle so
// that the second cmd_fm invocation restores the same values as the first.
static int fm_saved_fc = 0;
static int fm_saved_bc = 0;
static int fm_saved_font = 0;
static int fm_saved_valid = 0;

void cmd_fm(void)
{
    fm_panel_t panels[2];
    char status[STRINGSIZE * 2];
    char errmsg[STRINGSIZE];
    char run_bas_file[FF_MAX_LFN];
    char run_bas_full[FF_MAX_LFN];
    char run_edit_file[FF_MAX_LFN + 3];
    int active = 0;
    int context_state = 0;
    int done = 0;
    int run_from_fm = 0;
    int edit_from_fm = 0;
    int oldmode_local = DISPLAY_TYPE;
    // On the first entry save the true pre-FM state; on relaunch after a
    // BASIC program use the values saved by the first entry so that colors
    // and font are always restored to what they were before FM was launched.
    if (!fm_saved_valid)
    {
        fm_saved_fc = gui_fcolour;
        fm_saved_bc = gui_bcolour;
        fm_saved_font = PromptFont;
        fm_saved_valid = 1;
    }
    int oldfont_local = fm_saved_font;
    int oldfc_local = fm_saved_fc;
    int oldbc_local = fm_saved_bc;
    int need_full_redraw = 1;
    int need_panel_redraw = 0;
    int type_select_active = 0;
    int type_select_dirty = 0;
    uint64_t type_select_last_input_us = 0;
    int pi;
    unsigned char break_key_save = BreakKey;
    char type_select_prefix[FF_MAX_LFN + 1];
    bool font_changed = false;
#ifdef PICOMITEVGA
    bool mode_changed = false;
#endif

fm_relaunch:

    font_changed = false;
#ifdef PICOMITEVGA
    mode_changed = false;
#endif

    /* Release any per-program memory left behind by a BASIC program that
     * was launched from FM (RUN-from-FM, F2, or auto-relaunch after END).
     * The trace-cache slab and OPTION PROFILING counters live on the
     * BASIC heap and persist across the program-end -> FM-relaunch path
     * (do_end + longjmp do not wipe the heap).  FM is about to allocate
     * its own panel/buffer memory; without this cleanup a cached or
     * profiled program can starve the file manager of user heap.
     * These calls are safe no-ops when nothing is allocated.            */
#ifdef CACHE
    TraceCacheFree();
#endif
    ProfilingFree();
    g_option_profiling = 0;

    /* If the just-finished BASIC program left an off-screen framebuffer
     * active (FRAMEBUFFER CREATE / WRITE F / WRITE L), do_end does NOT
     * release it - only ClearRuntime does, and the FM-relaunch path skips
     * ClearRuntime.  Leaving it active means FM's draw calls (Box, Text,
     * etc. via DrawRectangle / DrawPixel function pointers) still write
     * into the off-screen buffer instead of the physical panel, so the
     * serial console redraws correctly but the screen shows the stale
     * pre-program contents.  Free the buffers and restore the panel
     * draw-function pointers before FM repaints.                         */
    if (FrameBuf != NULL || LayerBuf != NULL)
        closeframebuffer('A');

    if (CurrentLinePtr)
        StandardError(10);
    if (*cmdline)
        SyntaxError();

#if defined(PICOMITEVGA)
    {
        int chars_per_line = HRes / gui_font_width;
        if (chars_per_line < 64)
        {
            /* Clear the framebuffer before switching modes.  Otherwise the
             * pixels left behind by the just-finished BASIC program get
             * re-interpreted in the new (higher-res / different-bpp) mode
             * and appear as garbled blocks until FM finishes its first
             * full redraw a few milliseconds later.                       */
            ClearScreen(BLACK);
            DISPLAY_TYPE = SCREENMODE1;
            mode_changed = true;
            ResetDisplay();
            /* New mode's framebuffer contents are undefined; clear it with
             * the default background colour ResetDisplay() just restored. */
            ClearScreen(gui_bcolour);
#ifdef HDMI
            if (HRes >= 1024)
            {
                SetFont(2 << 4 | 1);
                PromptFont = 2 << 4 | 1;
            }
            else
#endif
            {
                SetFont(1);
                PromptFont = 1;
            }
            font_changed = true;
        }
        else if (DISPLAY_TYPE == SCREENMODE1 && gui_font_width % 8 != 0)
        {
#ifdef HDMI
            if (HRes >= 1024)
            {
                SetFont(2 << 4 | 1);
                PromptFont = 2 << 4 | 1;
            }
            else
#endif
            {
                SetFont(1);
                PromptFont = 1;
            }
            font_changed = true;
        }
    }
#elif defined(PICOMITE) || defined(PICOMITEMIN)
    if (!Option.DISPLAY_CONSOLE && Option.Width < FM_MIN_WIDTH)
        error("FM needs at least % columns", FM_MIN_WIDTH);
    if (Option.DISPLAY_CONSOLE && Option.Width < FM_MIN_WIDTH)
    {
        SetFont(((7 - 1) << 4) | 1);
        PromptFont = ((7 - 1) << 4) | 1;
        font_changed = true;
        if (Option.Width < FM_MIN_WIDTH)
            error("FM needs at least % columns", FM_MIN_WIDTH);
    }
#endif

    if (Option.Height < 8)
        error("FM needs at least 8 rows");

    if (Option.Width > SCREENWIDTH || Option.Height > SCREENHEIGHT)
        // FM entry: ask the attached serial terminal to grow to fit the
        // file manager. Like the editor, FM manages its own DECAWM state if
        // it needs autowrap disabled; setterminal() itself no longer touches
        // wrap mode.
        setterminal((Option.Height > SCREENHEIGHT) ? Option.Height : SCREENHEIGHT,
                    (Option.Width > SCREENWIDTH) ? Option.Width : SCREENWIDTH);

    // Trap and ignore Ctrl-C while FM is active; ESC is the exit key.
    fm_break_key_for_list = break_key_save;
    BreakKey = 0;

    run_bas_file[0] = 0;
    run_bas_full[0] = 0;
    run_edit_file[0] = 0;
    type_select_prefix[0] = 0;

    memset(panels, 0, sizeof(panels));
    for (pi = 0; pi < 2; pi++)
    {
        panels[pi].sortorder = FM_SORT_NAME;
        memset(panels[pi].type_head, 0xFF, sizeof(panels[pi].type_head));
    }

    panels[0].filesystem = 0;
    if (fm_drive_available(1))
        panels[1].filesystem = 1;
#if HAS_USB_MSC
    else if (fm_drive_available(2))
        panels[1].filesystem = 2;
#endif
    else
        panels[1].filesystem = 0;
    panels[0].selected = panels[1].selected = 0;
    panels[0].top = panels[1].top = 0;
    panels[0].count = panels[1].count = 0;
    strcpy(panels[0].filter, "*");
    strcpy(panels[1].filter, "*");
    fm_set_panel_path_from_cwd(&panels[0]);
    fm_set_panel_path_from_cwd(&panels[1]);

    active = 0;
    context_state = fm_load_context(panels, &active);

    if (context_state == 2)
        strcpy(status, "FM context restored (some entries unavailable)");
    else if (context_state == 1)
        strcpy(status, "FM context restored");
    else
        strcpy(status, "FM ready");
    if (fm_relaunch_status_valid)
    {
        strncpy(status, fm_relaunch_status, sizeof(status) - 1);
        status[sizeof(status) - 1] = 0;
        fm_relaunch_status_valid = 0;
    }
    PrintString("\0337\033[2J\033[H\033[?25l");
    MX470Display(DISPLAY_CLS);
    fm_local_cache_invalidate();
    clearrepeat();

    while (!done)
    {
        int c;
        fm_panel_t *p;
        fm_entry_t entry;
        int old_selected;
        int old_top;
        int list_rows = Option.Height - 4;
        int list_top = 2;
        int left_width = (Option.Width - 1) / 2;
        int right_x = left_width + 1;
        int right_width = Option.Width - right_x;

        p = &panels[active];

        if (need_full_redraw)
        {
            fm_draw_ui(panels, active, status);
            // Keep queued printable keys during type-select so multi-letter prefixes are not lost.
            if (!type_select_active)
                fm_flush_keyboard_buffer();
            need_full_redraw = 0;
        }
        else if (need_panel_redraw)
        {
            char left_err[STRINGSIZE];
            char right_err[STRINGSIZE];
            int left_ok;
            int right_ok;

            left_err[0] = 0;
            right_err[0] = 0;
            left_ok = fm_build_panel_cache(&panels[0], left_err, sizeof(left_err));
            right_ok = fm_build_panel_cache(&panels[1], right_err, sizeof(right_err));
            if (!left_ok)
                panels[0].count = 0;
            if (!right_ok)
                panels[1].count = 0;
            fm_clamp_panel(&panels[0], list_rows);
            fm_clamp_panel(&panels[1], list_rows);

            // Serial always uses a single full repaint to avoid many cursor repositions.
            fm_serial_repaint_ui(panels, active, status, left_ok, left_err, right_ok, right_err);

            // Local display uses targeted redraw to avoid unnecessary flashing.
            fm_skip_serial_output = 1;
            fm_draw_panel_entries(p, active == 1 ? 0 : 1,
                                  active == 0 ? 0 : right_x,
                                  active == 0 ? left_width : right_width,
                                  list_top, list_rows,
                                  errmsg, sizeof(errmsg));
            fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);
            fm_skip_serial_output = 0;
            if (!type_select_active)
                fm_flush_keyboard_buffer();
            need_panel_redraw = 0;
        }
        c = -1;
        while (c == -1)
        {
            routinechecks();
            if (type_select_active && type_select_dirty)
            {
                if (time_us_64() - type_select_last_input_us >= 300000)
                {
                    type_select_dirty = 0;
                    if (type_select_prefix[0])
                    {
                        int match_index = -1;
                        if (fm_find_prefix_match(p, type_select_prefix, &match_index, errmsg, sizeof(errmsg)))
                        {
                            p->selected = match_index;
                            if (p->selected < p->top)
                                p->top = p->selected;
                            else if (p->selected >= p->top + list_rows)
                                p->top = p->selected - list_rows + 1;
                            snprintf(status, sizeof(status), "Type-select: %s", type_select_prefix);
                        }
                        else
                        {
                            snprintf(status, sizeof(status), "No match: %s", type_select_prefix);
                        }
                    }
                    else
                    {
                        snprintf(status, sizeof(status), "Type-select: type filename prefix");
                    }
                    need_full_redraw = 1;
                    if (need_full_redraw)
                    {
                        fm_draw_ui(panels, active, status);
                        need_full_redraw = 0;
                    }
                    continue;
                }
            }
            c = MMInkey();
        }

        if (type_select_active)
        {
            int consumed = 0;
            int len = strlen(type_select_prefix);

            if (c == ESC)
            {
                type_select_active = 0;
                type_select_dirty = 0;
                type_select_prefix[0] = 0;
                snprintf(status, sizeof(status), "Type-select cancelled");
                need_full_redraw = 1;
                continue;
            }
            if (c == '\r' || c == '\n')
            {
                type_select_active = 0;
                type_select_dirty = 0;
                type_select_prefix[0] = 0;
                snprintf(status, sizeof(status), "Type-select: open selected item");
            }
            if (c == BKSP)
            {
                if (len > 0)
                    type_select_prefix[--len] = 0;
                consumed = 1;
            }
            else if (isprint(c) && c != '/' && len < FF_MAX_LFN)
            {
                type_select_prefix[len++] = (char)((c >= 'a' && c <= 'z') ? mytoupper(c) : c);
                type_select_prefix[len] = 0;
                consumed = 1;
            }

            if (consumed)
            {
                type_select_dirty = 1;
                type_select_last_input_us = time_us_64();
                continue;
            }

            type_select_active = 0;
            type_select_dirty = 0;
            type_select_prefix[0] = 0;
        }

        if (c >= 'a' && c <= 'z')
            c = mytoupper(c);

        old_selected = p->selected;
        old_top = p->top;

        switch (c)
        {
        case ESC:
            done = 1;
            break;
        case '\t':
            active ^= 1;
            fm_set_status_to_selected(&panels[active], status, sizeof(status));
            need_full_redraw = 1;
            break;
        case CTRLKEY('S'):
        case LEFT:
            active = 0;
            fm_set_status_to_selected(&panels[active], status, sizeof(status));
            need_full_redraw = 1;
            break;
        case CTRLKEY('D'):
        case RIGHT:
            active = 1;
            fm_set_status_to_selected(&panels[active], status, sizeof(status));
            need_full_redraw = 1;
            break;
        case CTRLKEY('E'):
        case UP:
            if (p->selected > 0)
                p->selected--;
            if (p->selected < p->top)
                p->top = p->selected;
            if (p->selected != old_selected)
            {
                fm_set_status_to_selected(p, status, sizeof(status));
                if (p->top == old_top)
                {
                    fm_draw_panel_row(p, 1,
                                      active == 0 ? 0 : right_x,
                                      active == 0 ? left_width : right_width,
                                      list_top, list_rows,
                                      old_selected - p->top,
                                      errmsg, sizeof(errmsg));
                    fm_draw_panel_row(p, 1,
                                      active == 0 ? 0 : right_x,
                                      active == 0 ? left_width : right_width,
                                      list_top, list_rows,
                                      p->selected - p->top,
                                      errmsg, sizeof(errmsg));
                    fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);
                    fm_flush_keyboard_buffer();
                }
                else
                    need_panel_redraw = 1;
            }
            break;
        case CTRLKEY('X'):
        case DOWN:
            if (p->selected < p->count - 1)
                p->selected++;
            if (p->selected >= p->top + list_rows)
                p->top = p->selected - list_rows + 1;
            if (p->selected != old_selected)
            {
                fm_set_status_to_selected(p, status, sizeof(status));
                if (p->top == old_top)
                {
                    fm_draw_panel_row(p, 1,
                                      active == 0 ? 0 : right_x,
                                      active == 0 ? left_width : right_width,
                                      list_top, list_rows,
                                      old_selected - p->top,
                                      errmsg, sizeof(errmsg));
                    fm_draw_panel_row(p, 1,
                                      active == 0 ? 0 : right_x,
                                      active == 0 ? left_width : right_width,
                                      list_top, list_rows,
                                      p->selected - p->top,
                                      errmsg, sizeof(errmsg));
                    fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);
                    fm_flush_keyboard_buffer();
                }
                else
                    need_panel_redraw = 1;
            }
            break;
        case CTRLKEY('P'):
        case PUP:
            if (p->selected > 0)
            {
                p->selected -= list_rows;
                if (p->selected < 0)
                    p->selected = 0;
            }
            if (p->top > 0)
            {
                p->top -= list_rows;
                if (p->top < 0)
                    p->top = 0;
            }
            if (p->selected < p->top)
                p->top = p->selected;
            need_panel_redraw = 1;
            break;
        case CTRLKEY('L'):
        case PDOWN:
            if (p->selected < p->count - 1)
            {
                p->selected += list_rows;
                if (p->selected >= p->count)
                    p->selected = p->count - 1;
            }
            if (p->selected >= p->top + list_rows)
                p->top = p->selected - list_rows + 1;
            if (p->count > list_rows && p->top > p->count - list_rows)
                p->top = p->count - list_rows;
            if (p->top < 0)
                p->top = 0;
            need_panel_redraw = 1;
            break;
        case CTRLKEY('U'):
        case HOME:
            strcpy(p->path, "/");
            p->selected = 0;
            p->top = 0;
            p->count = 0;
            p->cache_valid = 0;
            snprintf(status, sizeof(status), "%c:%s", fm_drive_letter(p->filesystem), p->path);
            need_full_redraw = 1;
            break;
        case CTRLKEY('K'):
        case END:
            p->selected = p->count - 1;
            if (p->selected < 0)
                p->selected = 0;
            if (p->selected >= list_rows)
                p->top = p->selected - list_rows + 1;
            else
                p->top = 0;
            need_panel_redraw = 1;
            break;
        case BKSP:
            if (!fm_path_is_root(p->path))
            {
                fm_parent_path(p->path);
                p->selected = p->top = 0;
                p->cache_valid = 0;
                snprintf(status, sizeof(status), "%c:%s", fm_drive_letter(p->filesystem), p->path);
                need_full_redraw = 1;
            }
            break;
        case ' ':
            if (fm_mark_toggle(p, p->selected, errmsg, sizeof(errmsg)))
            {
                if (p->selected < p->count - 1)
                    p->selected++;
                if (p->selected >= p->top + list_rows)
                    p->top = p->selected - list_rows + 1;
                snprintf(status, sizeof(status), "%d marked", p->marked_count);
            }
            else
            {
                snprintf(status, sizeof(status), "Select a file or directory to mark");
            }
            need_full_redraw = 1;
            break;
        case '*':
            if (!fm_build_panel_cache(p, errmsg, sizeof(errmsg)))
            {
                snprintf(status, sizeof(status), "%s", errmsg);
            }
            else
            {
                fm_mark_all(p, errmsg, sizeof(errmsg));
                snprintf(status, sizeof(status), "%d marked", p->marked_count);
            }
            need_full_redraw = 1;
            break;
        case '\\':
            if (!fm_build_panel_cache(p, errmsg, sizeof(errmsg)))
            {
                snprintf(status, sizeof(status), "%s", errmsg);
            }
            else
            {
                fm_mark_clear_all(p, errmsg, sizeof(errmsg));
                snprintf(status, sizeof(status), "Marks cleared");
            }
            need_full_redraw = 1;
            break;
        case '\r':
        case '\n':
            if (p->count > 0 && p->selected >= 0 && p->selected < p->count &&
                fm_get_entry_at(p, p->selected, &entry, errmsg, sizeof(errmsg)))
            {
                if (entry.is_dir)
                {
                    if (strcmp(entry.name, "..") == 0)
                    {
                        fm_parent_path(p->path);
                    }
                    else if (strcmp(entry.name, ".") == 0)
                    {
                        break;
                    }
                    else
                    {
                        char np[FF_MAX_LFN];
                        if (fm_make_child_path(p->path, entry.name, np, sizeof(np)))
                            strcpy(p->path, np);
                    }
                    p->selected = p->top = 0;
                    p->cache_valid = 0;
                    snprintf(status, sizeof(status), "%c:%s", fm_drive_letter(p->filesystem), p->path);
                    need_full_redraw = 1;
                }
                else if (fm_name_is_bas(entry.name))
                {
                    if (!fm_make_full_filename(p->filesystem, p->path, entry.name, run_bas_full, sizeof(run_bas_full)))
                    {
                        snprintf(status, sizeof(status), "Path too long");
                        break;
                    }
                    if (snprintf(run_bas_file, sizeof(run_bas_file), "%s", entry.name) >= (int)sizeof(run_bas_file))
                    {
                        snprintf(status, sizeof(status), "Path too long");
                        break;
                    }
                    snprintf(status, sizeof(status), "Launching %s", entry.name);
                    run_from_fm = 1;
                    done = 1;
                    BreakKey = break_key_save;
                }
                else if (fm_play_audio_file(p, &entry, status, sizeof(status)))
                {
                    need_full_redraw = 1;
                }
                else if (fm_show_image_file(p, &entry, status, sizeof(status), oldmode_local, oldfont_local))
                {
                    need_full_redraw = 1;
                }
            }
            break;
        case 'A':
        case 'B':
        case 'C':
        {
            int newfs = c - 'A';
#if !HAS_USB_MSC
            if (newfs == 2)
            {
                snprintf(status, sizeof(status), "C: is only available on RP2350 USB builds");
                break;
            }
#endif
            if (!fm_drive_available(newfs))
            {
                if (newfs != 0 && fm_drive_available(0))
                {
                    newfs = 0;
                    snprintf(status, sizeof(status), "Drive not ready, switched to A:");
                }
                else
                {
                    snprintf(status, sizeof(status), "Drive %c: not ready", (char)('A' + (c - 'A')));
                    break;
                }
            }
            p->filesystem = newfs;
            p->selected = p->top = 0;
            p->count = 0;
            p->cache_valid = 0;
            fm_set_panel_path_from_cwd(p);
            if (strncmp(status, "Drive not ready", 15) != 0)
                snprintf(status, sizeof(status), "Switched to %c:", fm_drive_letter(p->filesystem));
            need_full_redraw = 1;
            break;
        }
        case 'G':
            fm_go_to_path(p, status, sizeof(status));
            p->cache_valid = 0;
            need_full_redraw = 1;
            break;
        case 'S':
            p->sortorder++;
            if (p->sortorder > FM_SORT_TYPE)
                p->sortorder = FM_SORT_NAME;
            p->cache_valid = 0;
            p->selected = p->top = 0;
            if (p->sortorder == FM_SORT_TIME)
                snprintf(status, sizeof(status), "Sort: datetime");
            else if (p->sortorder == FM_SORT_TYPE)
                snprintf(status, sizeof(status), "Sort: type then name");
            else
                snprintf(status, sizeof(status), "Sort: name");
            need_full_redraw = 1;
            break;
        case 'N':
        {
            char newname[FF_MAX_LFN + 1];
            int name_limit;

            if (!fm_prompt_text("New file name: ", "", newname, sizeof(newname)))
            {
                snprintf(status, sizeof(status), "Create file cancelled");
                need_full_redraw = 1;
                break;
            }
            if (!fm_is_valid_leaf_name(newname))
            {
                snprintf(status, sizeof(status), "Invalid file name");
                need_full_redraw = 1;
                break;
            }

            if (!fm_build_panel_cache(p, errmsg, sizeof(errmsg)))
            {
                snprintf(status, sizeof(status), "%s", errmsg);
                need_full_redraw = 1;
                break;
            }
            name_limit = fm_panel_name_limit(p);
            if ((int)strlen(newname) > name_limit)
            {
                snprintf(status, sizeof(status), "Name too long (max %d chars here)", name_limit);
                need_full_redraw = 1;
                break;
            }

            if (!fm_create_empty_file(p->filesystem, p->path, newname, status, sizeof(status)))
            {
                need_full_redraw = 1;
                break;
            }
            if (!fm_make_full_filename(p->filesystem, p->path, newname, run_bas_full, sizeof(run_bas_full)))
            {
                snprintf(status, sizeof(status), "Path too long");
                need_full_redraw = 1;
                break;
            }
            if (snprintf(run_edit_file, sizeof(run_edit_file), "\"%s\"", run_bas_full) >= (int)sizeof(run_edit_file))
            {
                snprintf(status, sizeof(status), "Path too long");
                need_full_redraw = 1;
                break;
            }

            fm_pending_edit_seek_valid = 0;
            snprintf(status, sizeof(status), "Created %s", newname);
            edit_from_fm = 1;
            done = 1;
            break;
        }
        case 'D':
            fm_duplicate_selected(p, status, sizeof(status));
            p->count = 0;
            p->cache_valid = 0;
            need_full_redraw = 1;
            break;
        case 'M':
            fm_move_selected(&panels[active], &panels[active ^ 1], status, sizeof(status));
            panels[active].count = 0;
            panels[active ^ 1].count = 0;
            panels[active].cache_valid = 0;
            panels[active ^ 1].cache_valid = 0;
            need_full_redraw = 1;
            break;
        case '/':
            if (p->sortorder != FM_SORT_NAME)
            {
                p->sortorder = FM_SORT_NAME;
                p->cache_valid = 0;
                p->count = 0;
            }
            type_select_active = 1;
            type_select_dirty = 0;
            type_select_prefix[0] = 0;
            snprintf(status, sizeof(status), "Type-select: type filename prefix (ENTER=open, ESC=cancel)");
            need_full_redraw = 1;
            break;
        case 'L':
        case F9:
            if (p->count <= 0 || p->selected < 0 || p->selected >= p->count ||
                !fm_get_entry_at(p, p->selected, &entry, errmsg, sizeof(errmsg)))
            {
                snprintf(status, sizeof(status), "No file selected");
            }
            else
            {
                PrintString("\0337\033[2J\033[H\033[?25l");
                fm_list_file(p, &entry, status, sizeof(status));
            }
            need_full_redraw = 1;
            break;
        case CTRLKEY('R'):
        case F3:
            if (fm_prompt_filter(p))
            {
                p->selected = p->top = 0;
                p->count = 0;
                p->cache_valid = 0;
                snprintf(status, sizeof(status), "Filter set to %s", p->filter);
            }
            else
                snprintf(status, sizeof(status), "Filter unchanged");
            need_full_redraw = 1;
            break;
        case CTRLKEY('Q'):
        case F1:
            fm_show_help_screen();
            need_full_redraw = 1;
            break;
        case CTRLKEY('W'):
        case F2:
            if (p->count <= 0 || p->selected < 0 || p->selected >= p->count ||
                !fm_get_entry_at(p, p->selected, &entry, errmsg, sizeof(errmsg)))
            {
                snprintf(status, sizeof(status), "No file selected");
                need_full_redraw = 1;
                break;
            }
            if (entry.is_dir)
            {
                snprintf(status, sizeof(status), "Select a file to edit");
                need_full_redraw = 1;
                break;
            }
            if (!fm_make_full_filename(p->filesystem, p->path, entry.name, run_bas_full, sizeof(run_bas_full)))
            {
                snprintf(status, sizeof(status), "Path too long");
                need_full_redraw = 1;
                break;
            }
            if (snprintf(run_edit_file, sizeof(run_edit_file), "\"%s\"", run_bas_full) >= (int)sizeof(run_edit_file))
            {
                snprintf(status, sizeof(status), "Path too long");
                need_full_redraw = 1;
                break;
            }
            fm_pending_edit_seek_valid = 0;
            if (fm_error_location_valid && fm_error_file[0] && fm_stricmp(run_bas_full, fm_error_file) == 0)
            {
                fm_pending_edit_seek_valid = 1;
                fm_pending_edit_seek_line = fm_error_line > 0 ? fm_error_line - 1 : 0;
                fm_pending_edit_seek_char = fm_error_char;
                if (fm_error_char > 0)
                    snprintf(status, sizeof(status), "Editing %s at %d:%d", entry.name, fm_error_line, fm_error_char + 1);
                else
                    snprintf(status, sizeof(status), "Editing %s at line %d", entry.name, fm_error_line);
            }
            else
            {
                snprintf(status, sizeof(status), "Editing %s", entry.name);
            }
            edit_from_fm = 1;
            done = 1;
            break;
        case CTRLKEY('T'):
        case F4:
            strcpy(p->filter, "*");
            p->selected = p->top = 0;
            p->count = 0;
            p->cache_valid = 0;
            snprintf(status, sizeof(status), "Filter cleared");
            need_full_redraw = 1;
            break;
        case CTRLKEY('Y'):
        case F5:
            if (!fm_build_panel_cache(&panels[active], errmsg, sizeof(errmsg)))
                snprintf(status, sizeof(status), "%s", errmsg);
            else if (panels[active].marked_count > 0)
                fm_copy_marked(&panels[active], &panels[active ^ 1], status, sizeof(status));
            else
                fm_copy_selected(&panels[active], &panels[active ^ 1], status, sizeof(status));
            panels[active].cache_valid = 0;
            panels[active ^ 1].cache_valid = 0;
            need_full_redraw = 1;
            break;
        case CTRLKEY('O'):
        case F6:
            if (CurrentlyPlaying != P_NOTHING)
            {
                CloseAudio(1);
                snprintf(status, sizeof(status), "Audio stopped");
            }
            else
            {
                snprintf(status, sizeof(status), "No audio playing");
            }
            need_full_redraw = 1;
            break;
        case F7:
        case '-':
            fm_adjust_volume(-FM_VOLUME_STEP, status, sizeof(status));
            fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);
            break;
        case F8:
        case '+':
        case '=':
            fm_adjust_volume(FM_VOLUME_STEP, status, sizeof(status));
            fm_draw_field(0, Option.Height - 1, Option.Width, status, FM_ANSI_STATUS, YELLOW, BLACK);
            break;
        case CTRLKEY('N'):
            fm_rename_selected(p, status, sizeof(status));
            p->cache_valid = 0;
            fm_sync_other_panel_cache(panels, active);
            need_full_redraw = 1;
            break;
        case CTRLKEY(']'):
        case DEL:
            if (!fm_build_panel_cache(p, errmsg, sizeof(errmsg)))
                snprintf(status, sizeof(status), "%s", errmsg);
            else if (p->marked_count > 0)
                fm_delete_marked(p, 0, status, sizeof(status));
            else
                fm_delete_selected(p, status, sizeof(status));
            p->cache_valid = 0;
            fm_sync_other_panel_cache(panels, active);
            need_full_redraw = 1;
            break;
        case 'X':
            if (!fm_build_panel_cache(p, errmsg, sizeof(errmsg)))
                snprintf(status, sizeof(status), "%s", errmsg);
            else if (p->marked_count > 0)
                fm_delete_marked(p, 1, status, sizeof(status));
            else
                fm_delete_selected_recursive(p, status, sizeof(status));
            p->cache_valid = 0;
            fm_sync_other_panel_cache(panels, active);
            need_full_redraw = 1;
            break;
        case CTRLKEY('B'):
        case F10:
            fm_make_directory(p, status, sizeof(status));
            p->cache_valid = 0;
            fm_sync_other_panel_cache(panels, active);
            need_full_redraw = 1;
            break;
        default:
            break;
        }
    }

    fm_save_context(panels, active);

    PrintString("\033[0m\033[?25h\0338\033[2J\033[H");
    gui_fcolour = oldfc_local;
    gui_bcolour = oldbc_local;
    MX470Cursor(0, 0);
    MX470Display(DISPLAY_CLS);

    // Restore font unconditionally on the final exit (not just when FM changed
    // it) so that a BASIC program's font change does not persist after FM.
    int final_exit = !run_from_fm && !edit_from_fm;
#ifdef PICOMITEVGA
    if (mode_changed || font_changed)
    {
        if (mode_changed && DISPLAY_TYPE != oldmode_local)
        {
            /* Clear current-mode framebuffer before switching back so the
             * leftover FM pixels do not appear as garbage in the restored
             * mode.                                                        */
            ClearScreen(BLACK);
        }
        DISPLAY_TYPE = oldmode_local;
        ResetDisplay();
        if (mode_changed)
            ClearScreen(gui_bcolour);
        SetFont(oldfont_local);
        PromptFont = oldfont_local;
    }
    else if (final_exit)
    {
        SetFont(oldfont_local);
        PromptFont = oldfont_local;
    }
#else
    if (font_changed || final_exit)
    {
        SetFont(oldfont_local);
        PromptFont = oldfont_local;
    }
#endif

    if (final_exit)
        fm_saved_valid = 0;

    clearrepeat();
    cmdlinebuff[0] = 0;
    memset(inpbuf, 0, STRINGSIZE);
    for (pi = 0; pi < 2; pi++)
        fm_free_panel_cache(&panels[pi]);
    fm_local_cache_release();

    BreakKey = break_key_save;

fm_check_run:
    if (run_from_fm)
    {
        if (CurrentlyPlaying != P_NOTHING)
            CloseAudio(1);
        fm_set_default_drive_dir(&panels[active]);
        MMAbort = false;
        ConsoleRxBufHead = ConsoleRxBufTail;
        fm_flush_keyboard_buffer();
        routinechecks();
        fm_flush_keyboard_buffer();
        MMAbort = false;
        ConsoleRxBufHead = ConsoleRxBufTail;
        fm_error_location_valid = 0;
        fm_error_file[0] = 0;
        fm_error_line = 0;
        fm_error_char = 0;
        fm_pending_edit_seek_valid = 0;
        fm_sanitize_next_console_input = 1;
        strncpy(fm_last_launched_bas, run_bas_full, sizeof(fm_last_launched_bas) - 1);
        fm_last_launched_bas[sizeof(fm_last_launched_bas) - 1] = 0;
        fm_program_launched_from_fm = 1;
        if (!fm_run_bas_program(run_bas_file, run_bas_full))
        {
            fm_program_launched_from_fm = 0;
            fm_last_launched_bas[0] = 0;
            fm_sanitize_next_console_input = 0;
        }
    }
    if (edit_from_fm)
    {
        unsigned char *saved_cmdline = cmdline;
        fm_set_default_drive_dir(&panels[active]);
        fm_edit_wants_run = 0;
        edit((unsigned char *)run_edit_file, false);
        // FM calls edit() directly; release command-temporary buffers here.
        ClearTempMemory();
        cmdline = (unsigned char *)"";
        cmdline = saved_cmdline;

        if (fm_edit_wants_run && fstrstr(run_bas_full, ".bas"))
        {
            // F2 pressed on a .bas file — run it instead of returning to FM.
            // run_bas_full is still valid; derive run_bas_file from it.
            const char *bn = strrchr(run_bas_full, '/');
            strncpy(run_bas_file, bn ? bn + 1 : run_bas_full, sizeof(run_bas_file) - 1);
            run_bas_file[sizeof(run_bas_file) - 1] = 0;
            run_edit_file[0] = 0;
            type_select_prefix[0] = 0;
            type_select_active = 0;
            type_select_dirty = 0;
            type_select_last_input_us = 0;
            edit_from_fm = 0;
            run_from_fm = 1;
            goto fm_check_run;
        }
        else
        {
            // ESC/F1 or non-.bas file — return to FM as before.
            run_bas_file[0] = 0;
            run_bas_full[0] = 0;
            run_edit_file[0] = 0;
            type_select_prefix[0] = 0;
            type_select_active = 0;
            type_select_dirty = 0;
            type_select_last_input_us = 0;
            done = 0;
            run_from_fm = 0;
            edit_from_fm = 0;
            need_full_redraw = 1;
            goto fm_relaunch;
        }
    }
}

#endif

int find_longest_line_length(const char *text, int *linein)
{
    int current_length = 0;
    int max_length = 0;
    const char *ptr = text;
    int line = 0;
    while (*ptr)
    {
        if (*ptr == '\n')
        {
            line++;
            if (ptr > text && *(ptr - 1) == '_' && *(ptr - 2) == ' ' && Option.continuation)
            {
                // Line continuation, do not reset length
            }
            else
            {
                // If this line exceeds the max, update
                if (current_length > max_length)
                {
                    max_length = current_length;
                    *linein = line;
                }
                current_length = 0; // Reset for a new line
            }
        }
        else
        {
            // Increase length for this segment of the line
            current_length++;
        }

        ptr++;
    }

    // Final check in case the last line was the longest
    if (current_length > max_length)
    {
        max_length = current_length;
    }

    return max_length;
}
/*
 * @cond
 * The following section will be excluded from the documentation.
 */
#ifndef PICOMITE
#ifndef PICOMITEWEB
static short lastx1 = 9999, lasty1 = 9999;
static uint16_t lastfc, lastbc;
static bool leftpushed = false, rightpushed = false, middlepushed = false;
#endif
#endif

#ifndef PICOMITEMIN
/* ----------------------------------------------------------------------------
 * editBeautify - Re-indent the editor buffer with 2-space block indentation.
 *
 * Recognised block structures:
 *   IF ... THEN  /  ELSE / ELSEIF / END IF / ENDIF       (multi-line only)
 *   FOR ... NEXT
 *   DO[/WHILE/UNTIL] ... LOOP[/WHILE/UNTIL]
 *   SELECT CASE ... CASE / CASE ELSE ... END SELECT
 *   FUNCTION ... END FUNCTION
 *   SUB ... END SUB
 *   TYPE ... END TYPE                                    (STRUCTENABLED only)
 *
 * Single-line IF (with statements after THEN) is left at the prevailing
 * indent without opening a block.  Blank lines are emitted with no indent.
 * Comment-only lines are indented at the current level.
 * --------------------------------------------------------------------------*/
static int beautify_token_match(const char *up, int idx, const char *kw)
{
    int klen = (int)strlen(kw);
    for (int i = 0; i < klen; i++)
    {
        if (up[idx + i] != kw[i])
            return 0;
    }
    char nxt = up[idx + klen];
    return (nxt == 0 || nxt == ' ' || nxt == '\t' || nxt == ':');
}

static void editBeautify(int edit_buff_size)
{
    /* ---- Pass 1: remove all blank/whitespace-only lines ---- */
    {
        unsigned char *src = EdBuff;
        unsigned char *dst = EdBuff;
        while (*src)
        {
            unsigned char *line_start = src;
            unsigned char *eol = src;
            while (*eol && *eol != '\n')
                eol++;
            unsigned char *t = line_start;
            while (t < eol && (*t == ' ' || *t == '\t'))
                t++;
            int blank = (t >= eol);
            int line_len = (int)(eol - line_start) + (*eol == '\n' ? 1 : 0);
            if (!blank)
            {
                if (dst != line_start)
                    memmove(dst, line_start, line_len);
                dst += line_len;
            }
            src = line_start + line_len;
        }
        *dst = 0;
    }

    /* ---- Pass 2: insert one blank line before each SUB / FUNCTION
     *               (except at start of buffer).                       */
    {
        unsigned char *p = EdBuff;
        int first_line = 1;
        while (*p)
        {
            unsigned char *line_start = p;
            unsigned char *eol = p;
            while (*eol && *eol != '\n')
                eol++;

            unsigned char *s = line_start;
            while (s < eol && (*s == ' ' || *s == '\t'))
                s++;

            char up[16] = {0};
            int ulen = 0;
            for (unsigned char *q = s; q < eol && ulen < (int)sizeof(up) - 1; q++)
            {
                char ch = (char)*q;
                if (ch >= 'a' && ch <= 'z')
                    ch = (char)(ch - 32);
                up[ulen++] = ch;
            }

            int is_sub_or_func = (beautify_token_match(up, 0, "SUB") ||
                                  beautify_token_match(up, 0, "FUNCTION"));

            if (is_sub_or_func && !first_line)
            {
                /* Insert a single '\n' at line_start.  Need 1 byte of room. */
                unsigned char *bufend = line_start;
                while (*bufend)
                    bufend++;
                if ((bufend - EdBuff) + 1 >= edit_buff_size - 10)
                {
                    editDisplayMsg((unsigned char *)" BEAUTIFY: BUFFER FULL ");
                    return;
                }
                int tail_len = (int)(bufend - line_start) + 1; /* incl. NUL */
                memmove(line_start + 1, line_start, tail_len);
                *line_start = '\n';
                /* Skip past the inserted newline; recompute eol */
                line_start++;
                eol = line_start;
                while (*eol && *eol != '\n')
                    eol++;
            }

            first_line = 0;
            p = eol + (*eol == '\n' ? 1 : 0);
        }
    }

    /* ---- Pass 3: re-indent each line based on block structure ---- */
    int level = 0;
    unsigned char *p = EdBuff;

    while (*p)
    {
        unsigned char *line_start = p;

        /* Find current end-of-line and the existing leading whitespace */
        unsigned char *eol = p;
        while (*eol && *eol != '\n')
            eol++;

        unsigned char *s = line_start;
        while (s < eol && (*s == ' ' || *s == '\t'))
            s++;

        /* Build a small uppercase token buffer for keyword sniffing */
        char up[40] = {0};
        int ulen = 0;
        for (unsigned char *q = s; q < eol && ulen < (int)sizeof(up) - 1; q++)
        {
            char ch = (char)*q;
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 32);
            up[ulen++] = ch;
        }

        int dedent_self = 0;
        int indent_after = 0;
        int is_blank = (s >= eol);
        int is_comment = 0;
        if (!is_blank)
        {
            if (*s == '\'')
                is_comment = 1;
            else if (ulen >= 3 && up[0] == 'R' && up[1] == 'E' && up[2] == 'M' &&
                     (ulen == 3 || up[3] == ' ' || up[3] == '\t'))
                is_comment = 1;
        }

        if (!is_blank && !is_comment)
        {
            if (beautify_token_match(up, 0, "ENDIF"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "END") && beautify_token_match(up, 4, "IF"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "NEXT"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "LOOP"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "END") && beautify_token_match(up, 4, "SELECT"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "END") && beautify_token_match(up, 4, "FUNCTION"))
                dedent_self = 1;
            else if (beautify_token_match(up, 0, "END") && beautify_token_match(up, 4, "SUB"))
                dedent_self = 1;
#ifdef STRUCTENABLED
            else if (beautify_token_match(up, 0, "END") && beautify_token_match(up, 4, "TYPE"))
                dedent_self = 1;
#endif
            else if (beautify_token_match(up, 0, "ELSE") ||
                     beautify_token_match(up, 0, "ELSEIF"))
            {
                dedent_self = 1;
                indent_after = 1;
            }
            else if (beautify_token_match(up, 0, "CASE"))
            {
                dedent_self = 1;
                indent_after = 1;
            }

            if (beautify_token_match(up, 0, "FOR"))
                indent_after = 1;
            else if (beautify_token_match(up, 0, "DO"))
                indent_after = 1;
            else if (beautify_token_match(up, 0, "SELECT") && beautify_token_match(up, 7, "CASE"))
                indent_after = 1;
            else if (beautify_token_match(up, 0, "FUNCTION"))
                indent_after = 1;
            else if (beautify_token_match(up, 0, "SUB"))
                indent_after = 1;
#ifdef STRUCTENABLED
            else if (beautify_token_match(up, 0, "TYPE"))
                indent_after = 1;
#endif
            else if (beautify_token_match(up, 0, "IF"))
            {
                int found_after = -1;
                for (unsigned char *r = s + 2; r + 4 < eol; r++)
                {
                    if ((r[0] == ' ' || r[0] == '\t') &&
                        (r[1] == 'T' || r[1] == 't') && (r[2] == 'H' || r[2] == 'h') &&
                        (r[3] == 'E' || r[3] == 'e') && (r[4] == 'N' || r[4] == 'n'))
                    {
                        unsigned char *after = r + 5;
                        if (after == eol || *after == ' ' || *after == '\t')
                        {
                            found_after = (int)(after - s);
                            break;
                        }
                    }
                }
                if (found_after >= 0)
                {
                    unsigned char *a = s + found_after;
                    while (a < eol && (*a == ' ' || *a == '\t'))
                        a++;
                    if (a >= eol || *a == '\'')
                        indent_after = 1;
                }
            }
        }

        int line_indent = level - dedent_self;
        if (line_indent < 0)
            line_indent = 0;
        int want_spaces = is_blank ? 0 : (line_indent * 2);
        int have_spaces = (int)(s - line_start);

        /* Adjust leading whitespace in place by shifting the tail of the buffer */
        if (want_spaces != have_spaces)
        {
            int diff = want_spaces - have_spaces; /* >0 grow, <0 shrink */

            /* Find end of buffer (NUL terminator) */
            unsigned char *bufend = s;
            while (*bufend)
                bufend++;
            int tail_len = (int)(bufend - s); /* bytes from s through, not incl. NUL */

            if (diff > 0)
            {
                /* Need to grow: ensure there's room (10-byte slack like editInsertChar) */
                if ((bufend - EdBuff) + diff >= edit_buff_size - 10)
                {
                    editDisplayMsg((unsigned char *)" BEAUTIFY: BUFFER FULL ");
                    return;
                }
                /* Shift tail (including NUL) right by diff */
                memmove(s + diff, s, tail_len + 1);
            }
            else
            {
                /* Shrink: shift tail (including NUL) left by -diff */
                memmove(s + diff, s, tail_len + 1);
            }

            /* Fill new leading spaces */
            for (int i = 0; i < want_spaces; i++)
                line_start[i] = ' ';

            /* Recompute eol after shift */
            eol = line_start + want_spaces;
            while (*eol && *eol != '\n')
                eol++;
        }
        else
        {
            /* Same width - just rewrite spaces (in case some were tabs) */
            for (int i = 0; i < want_spaces; i++)
                line_start[i] = ' ';
        }

        /* Advance to the character after the newline (or end) */
        p = eol;
        if (*p == '\n')
            p++;

        if (dedent_self)
            level--;
        if (indent_after)
            level++;
        if (level < 0)
            level = 0;
    }
}
#endif /* !PICOMITEMIN */

void FullScreenEditor(int xx, int yy, char *fname, int edit_buff_size, bool cmdfile)
{
    edit_is_bas = (fname == NULL || fstrstr(fname, ".bas") != NULL);
    int c = -1, i;
    unsigned char buf[MAXCLIP + 2], clipboard[MAXCLIP + 2];
    unsigned char *p, *tp, BreakKeySave;
    char currdel = 0, nextdel = 0, lastdel = 0;
    char multi = false;
    bool foundit = false;
#ifdef PICOMITEVGA
    int fontinc = gui_font_width / 8; // use to decide where to position mouse cursor
    int OptionY_TILESave;
    int ytileheightsave;
    ytileheightsave = ytileheight;
    OptionY_TILESave = Y_TILE;
    ytileheight = gui_font_height;
    Y_TILE = VRes / ytileheight;
    if (VRes % ytileheight)
        Y_TILE++;
#else
    char RefreshSave = Option.Refresh;
#if PICOMITERP2350
    if (!(Option.DISPLAY_TYPE >= NEXTGEN))
#endif
        Option.Refresh = 0;
#endif
    printScreen(); // draw the screen
    SCursor(xx, yy);
    drawstatusline = true;
    unsigned char lastkey = 0;
    int y, statuscount;
    clipboard[0] = 0;
    buf[0] = 0;
    insert = true;
    TextChanged = false;
    BreakKeySave = BreakKey;
    BreakKey = 0;
    while (1)
    {
        statuscount = 0;
        do
        {
#ifndef PICOMITE
#ifndef PICOMITEWEB
            c = -1;
#ifdef USBKEYBOARD
            if (HID[1].Device_type == 2 && DISPLAY_TYPE == SCREENMODE1)
            {
#else
            if (mouse0 && DISPLAY_TYPE == SCREENMODE1)
            {
#endif
                if (!nunstruct[2].L)
                    leftpushed = false;
                if (!nunstruct[2].R)
                    rightpushed = false;
                if (!nunstruct[2].C)
                    middlepushed = false;
                if (nunstruct[2].y1 != lasty1 || nunstruct[2].x1 != lastx1)
                {
                    if (lastx1 != 9999)
                    {
#ifdef HDMI
                        if (FullColour)
                        {
#endif
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilefcols[lasty1 * X_TILE + i] = lastfc;
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilebcols[lasty1 * X_TILE + i] = lastbc;
#ifdef HDMI
                        }
                        else
                        {
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilefcols_w[lasty1 * X_TILE + i] = lastfc;
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilebcols_w[lasty1 * X_TILE + i] = lastbc;
                        }
#endif
                    }
                    lastx1 = nunstruct[2].x1;
                    lasty1 = nunstruct[2].y1;
                    if (lasty1 >= VHeight)
                        lasty1 = VHeight - 1;
#ifdef HDMI
                    if (FullColour)
                    {
#endif
                        lastfc = tilefcols[lasty1 * X_TILE + lastx1 * fontinc];
                        lastbc = tilebcols[lasty1 * X_TILE + lastx1 * fontinc];
#ifdef HDMI
                    }
                    else
                    {
                        lastfc = tilefcols_w[lasty1 * X_TILE + lastx1 * fontinc];
                        lastbc = tilebcols_w[lasty1 * X_TILE + lastx1 * fontinc];
                    }
                    if (FullColour)
                    {
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilefcols[lasty1 * X_TILE + i] = RGB555(RED);
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilebcols[lasty1 * X_TILE + i] = RGB555(WHITE);
                    }
                    else
                    {
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilefcols_w[lasty1 * X_TILE + i] = RGB332(RED);
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilebcols_w[lasty1 * X_TILE + i] = RGB332(WHITE);
                    }
#else
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilefcols[lasty1 * X_TILE + i] = RGB121pack(RED);
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilebcols[lasty1 * X_TILE + i] = RGB121pack(WHITE);
#endif
                }
                if ((nunstruct[2].L && leftpushed == false && rightpushed == false && middlepushed == false) ||
                    (nunstruct[2].R && leftpushed == false && rightpushed == false && middlepushed == false) ||
                    (nunstruct[2].C && leftpushed == false && rightpushed == false && middlepushed == false))
                {
                    if (nunstruct[2].L)
                        leftpushed = true;
                    else if (nunstruct[2].R)
                        rightpushed = true;
                    else
                        middlepushed = true;
                    if (lastx1 >= 0 && lastx1 < VWidth && lasty1 >= 0 && lasty1 < VHeight)
                    { // c == ' ' means mouse down and no shift, ctrl, etc
                        ShowCursor(false);
                        // first position on the y axis
                        while (*txtp != 0 && lasty1 > cury) // assume we have to move down the screen
                            if (*txtp++ == '\n')
                                cury++;
                        while (txtp != EdBuff && lasty1 < cury) // assume we have to move up the screen
                            if (*--txtp == '\n')
                                cury--;
                        while (txtp != EdBuff && *(txtp - 1) != '\n')
                            txtp--; // move to the beginning of the line
                        for (curx = 0; curx < lastx1 && *txtp && *txtp != '\n'; curx++)
                            txtp++; // now position on the x axis
                        PositionCursor(txtp);
                        PrintStatus();
                        ShowCursor(true);
#ifdef HDMI
                        if (FullColour)
                        {
#endif
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilefcols[lasty1 * X_TILE + i] = lastfc;
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilebcols[lasty1 * X_TILE + i] = lastbc;
#ifdef HDMI
                        }
                        else
                        {
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilefcols_w[lasty1 * X_TILE + i] = lastfc;
                            for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                                tilebcols_w[lasty1 * X_TILE + i] = lastbc;
                        }
#endif
                    }
                    if (rightpushed == true)
                    {
                        c = F4;
                    }
                    else if (middlepushed == true)
                    {
                        c = F5;
                    }
                }
            }
            ShowCursor(true);
            if (!((rightpushed == true && c == F4) || (middlepushed == true && c == F5)))
#else
            ShowCursor(true);
#endif
#else
            ShowCursor(true);
#endif

                c = MMInkey();

            if (statuscount++ == 5000)
                PrintStatus();
        } while (c == -1);
        ShowCursor(false);

        if (drawstatusline)
            PrintFunctKeys(EDIT);
        drawstatusline = false;
        if (c == TAB)
        {
            strcpy((char *)buf, "        ");
            buf[Option.Tab - ((edx + curx) % Option.Tab)] = 0;
        }
        else
        {
            buf[0] = c;
            buf[1] = 0;
        }
        if (!(buf[0] == SHIFT_F5 || buf[0] == SHIFT_F4 || buf[0] == F7 || buf[0] == F8))
            foundit = false;
        do
        {
            if (buf[0] == BreakKeySave)
                buf[0] = ESC; // if the user tried to break turn it into an escape
            switch (buf[0])
            {

            case '\r':
            case '\n': // first count the spaces at the beginning of the line
                if (txtp != EdBuff && (*txtp == '\n' || *txtp == 0))
                { // we only do this if we are at the end of the line
                    for (tp = txtp - 1, i = 0; *tp != '\n' && tp >= EdBuff; tp--)
                        if (*tp != ' ')
                            i = 0; // not a space
                        else
                            i++; // potential space at the start
                    if (tp == EdBuff && *tp == ' ')
                        i++; // correct for a counting error at the start of the buffer
                    if (buf[1] != 0)
                        i = 0; // do not insert spaces if buffer too small or has something in it
                    else
                        buf[i + 1] = 0; // make sure that the end of the buffer is zeroed
                    while (i)
                        buf[i--] = ' '; // now, place our spaces in the typeahead buffer
                }
                if (!editInsertChar('\n', &multi, edit_buff_size))
                    break; // insert the newline
                TextChanged = true;
                nbrlines++;
                if (!(cury < VHeight - 1)) // if we are NOT at the bottom
                    edy++;                 // otherwise scroll
                edx = 0;                   // reset horizontal scroll for the new line
                printScreen();             // redraw everything
                PositionCursor(txtp);
                break;

            case CTRLKEY('E'):
            case UP:
                if (cury == 0 && edy == 0)
                    break;
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                if (*txtp == '\n')
                    txtp--; // step back over the terminator if we are right at the end of the line
                while (txtp != EdBuff && *txtp != '\n')
                    txtp--; // move to the beginning of the line
                if (txtp != EdBuff)
                {
                    txtp--; // step over the terminator to the end of the previous line
                    while (txtp != EdBuff && *txtp != '\n')
                        txtp--; // move to the beginning of that line
                    if (*txtp == '\n')
                        txtp++; // and position at the start
                }
                for (i = 0; i < edx + tempx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column

                if (cury > 2 || edy == 0)
                { // if we are more that two lines from the top
                    if (cury > 0)
                        SCursor(i, cury - 1); // just move the cursor up
                }
                else if (edy > 0)
                { // if we are two lines or less from the top
                    curx = i;
                    ScrollDown();
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('X'):
            case DOWN:
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                p = txtp;
                while (*p != 0 && *p != '\n')
                    p++; // move to the end of this line
                if (*p == 0)
                    break; // skip if it is at the end of the file
                p++;       // step over the line terminator to the start of the next line
                for (i = 0; i < edx + tempx && *p != 0 && *p != '\n'; i++, p++)
                    ; // move the cursor to the column
                txtp = p;

                if (cury < VHeight - 3 || edy + VHeight == nbrlines)
                {
                    if (cury < VHeight - 1)
                        SCursor(i, cury + 1);
                }
                else if (edy + VHeight < nbrlines)
                {
                    curx = i;
                    Scroll();
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('S'):
            case LEFT:
                if (txtp == EdBuff)
                    break;
                if (*(txtp - 1) == '\n')
                { // if at the beginning of the line wrap around
                    buf[1] = UP;
                    buf[2] = END;
                    buf[3] = 1;
                    buf[4] = 0;
                }
                else
                {
                    txtp--;
                    if (edx > 0 && curx <= SCROLLCHARS)
                    {
                        edx--;
                        printLine(edy + cury);
                    }
                    PositionCursor(txtp);
                }
                break;

            case CTRLKEY('D'):
            case RIGHT:
                if (*txtp == '\n')
                { // if at the end of the line wrap around
                    buf[1] = HOME;
                    buf[2] = DOWN;
                    buf[3] = 0;
                    break;
                }
                if (*txtp != 0)
                {
                    txtp++;
                    if (curx >= VWidth - 1 - SCROLLCHARS)
                    {
                        edx++;
                        printLine(edy + cury);
                    }
                }
                PositionCursor(txtp);
                break;

            // backspace
            case BKSP:
                if (txtp == EdBuff)
                    break;
                if (*(txtp - 1) == '\n')
                { // if at the beginning of the line wrap around
                    buf[1] = UP;
                    buf[2] = END;
                    buf[3] = DEL;
                    buf[4] = 0;
                    break;
                }
                // find how many spaces are between the cursor and the start of the line
                for (p = txtp - 1; *p == ' ' && p != EdBuff; p--)
                    ;
                if ((p == EdBuff || *p == '\n') && txtp - p > 1)
                {
                    i = txtp - p - 1;
                    // we have have the number of continuous spaces between the cursor and the start of the line
                    // now figure out the number of backspaces to the nearest tab stop

                    i = (i % Option.Tab);
                    if (i == 0)
                        i = Option.Tab;
                    // load the corresponding number of deletes in the type ahead buffer
                    buf[i + 1] = 0;
                    while (i--)
                    {
                        buf[i + 1] = DEL;
                        txtp--;
                    }
                    // and let the delete case take care of deleting the characters
                    PositionCursor(txtp);
                    break;
                }
                // this is just a normal backspace (not a tabbed backspace)
                txtp--;
                if (edx > 0 && curx <= SCROLLCHARS)
                    edx--;
                PositionCursor(txtp);
                // fall through to delete the char

            case CTRLKEY(']'):
            case DEL:
                if (*txtp == 0)
                    break;
                p = txtp;
                c = *p;
                currdel = *p;
                if (p != EdBuff + edit_buff_size - 1)
                    nextdel = p[1];
                else
                    nextdel = 0;
                if (p != EdBuff)
                {
                    lastdel = *(--p);
                    p++;
                }
                else
                    lastdel = 0;
                while (*p)
                {
                    p[0] = p[1];
                    p++;
                }
                if (c == '\n')
                {
                    // Measure the cursor's column (= length of original line n).
                    // txtp still points to the now-deleted '\n' position, so count
                    // chars back to the start of the line.
                    int abs_col = 0;
                    {
                        unsigned char *q = txtp;
                        while (q > EdBuff && *(q - 1) != '\n')
                        {
                            abs_col++;
                            q--;
                        }
                    }
                    int saved_cury = cury;
                    int cur_ln = edy + saved_cury;
                    // Reset horizontal scroll before full redraw so every line
                    // is drawn left-justified, not offset by edx from the END key.
                    edx = 0;
                    printScreen();
                    nbrlines--;
                    // If the join point is off-screen to the right, scroll only
                    // the current (joined) line to bring the cursor into view.
                    if (abs_col > VWidth - 1)
                    {
                        edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                        SCursor(0, saved_cury);
                        printLine(cur_ln);
                    }
                }
                else
                    printLine(edy + cury);
                TextChanged = true;
                PositionCursor(txtp);
                if (currdel == '/' && nextdel == '*' && Option.ColourCode)
                    printScreen();
                if (currdel == '*' && nextdel == '/' && Option.ColourCode)
                    printScreen();
                if (currdel == '/' && lastdel == '*' && Option.ColourCode)
                    printScreen();
                if (currdel == '*' && lastdel == '/' && Option.ColourCode)
                    printScreen();
                break;

            case CTRLKEY('N'):
            case INSERT:
                insert = !insert;
                break;

            case CTRLKEY('U'):
            case HOME:
                if (txtp == EdBuff)
                    break;
                if (lastkey == HOME || lastkey == CTRLKEY('U'))
                {
                    edx = edy = curx = cury = 0;
                    txtp = EdBuff;
                    PrintString("\033[2J\033[H"); // vt100 clear screen and home cursor
                    MX470Display(DISPLAY_CLS);    // clear screen on the MX470 display only
                    printScreen();
                    PrintFunctKeys(EDIT);
                    PositionCursor(txtp);
                    break;
                }
                if (*txtp == '\n')
                    txtp--; // step back over the terminator if we are right at the end of the line
                while (txtp != EdBuff && *txtp != '\n')
                    txtp--; // move to the beginning of the line
                if (*txtp == '\n')
                    txtp++; // skip if no more lines above this one
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('K'):
            case END:
                if (*txtp == 0)
                    break; // already at the end
                if (lastkey == END || lastkey == CTRLKEY('K'))
                { // jump to the end of the file
                    i = 0;
                    p = txtp = EdBuff;
                    while (*txtp != 0)
                    {
                        if (*txtp == '\n')
                        {
                            p = txtp + 1;
                            i++;
                        }
                        txtp++;
                    }

                    if (i >= VHeight)
                    {
                        edy = i - VHeight + 1;
                        printScreen();
                        cury = VHeight - 1;
                    }
                    else
                    {
                        cury = i;
                    }
                    txtp = p;
                    curx = 0;
                }

                while (*txtp != 0 && *txtp != '\n')
                    txtp++;
                {
                    int abs_col = 0;
                    unsigned char *q = txtp;
                    while (q > EdBuff && *(q - 1) != '\n')
                    {
                        abs_col++;
                        q--;
                    }
                    if (abs_col > VWidth - 1)
                    {
                        edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                        SCursor(0, cury);
                        printLine(edy + cury);
                    }
                    else if (edx != 0)
                    {
                        edx = 0;
                        SCursor(0, cury);
                        printLine(edy + cury);
                    }
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('P'):
            case PUP:
                if (edy == 0)
                {                  // if we are already showing the top of the text
                    buf[1] = HOME; // force the editing point to the start of the text
                    buf[2] = HOME;
                    buf[3] = 0;
                    break;
                }
                else if (edy >= VHeight - 1)
                { // if we can scroll a full screenfull
                    i = VHeight + 1;
                    edy -= VHeight;
                }
                else
                { // if it is less than a full screenfull
                    i = edy + 1;
                    edy = 0;
                }
                while (i--)
                {
                    if (*txtp == '\n')
                        txtp--; // step back over the terminator if we are right at the end of the line
                    while (txtp != EdBuff && *txtp != '\n')
                        txtp--; // move to the beginning of the line
                    if (txtp == EdBuff)
                        break; // skip if no more lines above this one
                }
                if (txtp != EdBuff)
                    txtp++; // and position at the start of the line
                for (i = 0; i < edx + curx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column
                printScreen();
                PositionCursor(txtp);
                break;

            case CTRLKEY('L'):
            case PDOWN:
                if (nbrlines <= edy + VHeight + 1)
                {                 // if we are already showing the end of the text
                    buf[1] = END; // force the editing point to the end of the text
                    buf[2] = END;
                    buf[3] = 0;
                    break; // cursor to the top line
                }
                else if (nbrlines - edy - VHeight >= VHeight)
                { // if we can scroll a full screenfull
                    edy += VHeight;
                    i = VHeight;
                }
                else
                { // if it is less than a full screenfull
                    i = nbrlines - VHeight - edy;
                    edy = nbrlines - VHeight;
                }
                if (*txtp == '\n')
                    i--; // compensate if we are right at the end of a line
                while (i--)
                {
                    if (*txtp == '\n')
                        txtp++; // step over the terminator if we are right at the start of the line
                    while (*txtp != 0 && *txtp != '\n')
                        txtp++; // move to the end of the line
                    if (*txtp == 0)
                        break; // skip if no more lines after this one
                }
                if (txtp != EdBuff)
                    txtp++; // and position at the start of the line
                for (i = 0; i < edx + curx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column
                // y = cury;
                printScreen();
                PositionCursor(txtp);
                break;

            // Abort without saving
            case ESC:
                uSec(50000); // wait 50ms to see if anything more is coming
                routinechecks();
                if (MMInkey() == '[' && MMInkey() == 'M')
                {
                    // received escape code for Tera Term reporting a mouse click or scroll wheel movement
                    int c, x, y;
                    c = MMInkey();
                    x = MMInkey() - '!';
                    y = MMInkey() - '!';
                    if (c == 'e' || c == 'a')
                    { // Tera Term - SHIFT + mouse-wheel-rotate-down
                        buf[1] = UP;
                        buf[2] = 0;
                    }
                    else if (c == 'd' || c == '`')
                    { // Tera Term - SHIFT + mouse-wheel-rotate-up
                        buf[1] = DOWN;
                        buf[2] = 0;
                    }
                    else if (c == ' ' && x >= 0 && x < VWidth && y >= 0 && y < VHeight)
                    { // c == ' ' means mouse down and no shift, ctrl, etc
                        // first position on the y axis
                        while (*txtp != 0 && y > cury) // assume we have to move down the screen
                            if (*txtp++ == '\n')
                                cury++;
                        while (txtp != EdBuff && y < cury) // assume we have to move up the screen
                            if (*--txtp == '\n')
                                cury--;
                        while (txtp != EdBuff && *(txtp - 1) != '\n')
                            txtp--; // move to the beginning of the line
                        for (curx = 0; curx < x && *txtp && *txtp != '\n'; curx++)
                            txtp++; // now position on the x axis
                        PositionCursor(txtp);
                    }
                    break;
                }
                // this must be an ordinary escape (not part of an escape code)
                if (TextChanged)
                {
                    GetInputString((unsigned char *)"Exit and discard all changes (Y/N): ");
                    if (mytoupper(*inpbuf) != 'Y')
                        break;
                }
                // fall through to the normal exit

            case CTRLKEY('Q'): // Save and exit
            case F1:           // Save and exit
            case CTRLKEY('W'): // Save, exit and run
            case F2:           // Save, exit and run
#ifndef USBKEYBOARD
                     // fflush(stdout);
                tud_cdc_write_flush();
#endif
                int line = 0;
                int i = find_longest_line_length((char *)EdBuff, &line);
                if (i > 255 && (fname == NULL || fstrstr(fname, ".bas")))
                {
                    char buff[20] = {};
                    sprintf(buff, " LINE %d TOO LONG", line);
                    editDisplayMsg((unsigned char *)buff);
                    break;
                }
                //                            }
                c = buf[0];
#ifdef MMBASIC_FM
                /* Capture the exit key now: the file-write loop below reuses
                 * `c` as the byte being written and ends with c == NUL, which
                 * loses the F2 / Ctrl-W "save, exit and run" indication that
                 * FM relies on after edit() returns.                         */
                int fm_exit_key = c;
#endif
                PrintString("\033[?1000l");        // Tera Term turn off mouse click report in vt200 mode
                PrintString("\033[?7h");           // restore autowrap (was disabled by setterminal on entry)
                PrintString("\0338\033[2J\033[H"); // vt100 clear screen and home cursor
                gui_fcolour = GUI_C_NORMAL;
                PrintString(VT100_C_NORMAL);
                gui_fcolour = PromptFC;
                gui_bcolour = PromptBC;
                MX470Cursor(0, 0);         // home the cursor
                MX470Display(DISPLAY_CLS); // clear screen on the MX470 display only
                PrintString("\033[0m");
                BreakKey = BreakKeySave;
                Option.ColourCode = optioncolourcodesave;
#ifdef PICOMITEVGA
                editactive = 0;
                Y_TILE = OptionY_TILESave;
                ytileheight = ytileheightsave;
                // Always restore mode and font on exit
                DISPLAY_TYPE = oldmode;
                ResetDisplay();
                SetFont(oldfont);
                PromptFont = oldfont;
                MX470Display(DISPLAY_CLS); // clear screen on the MX470 display only
#ifdef HDMI
                while (v_scanline != 0)
                {
                }
                mapreset();
                if (DISPLAY_TYPE == SCREENMODE1)
                {
                    if (FullColour)
                    {
                        tilefcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3));
                        tilebcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3) + (MODE1SIZE >> 1));
                        X_TILE = MODE_H_ACTIVE_PIXELS / 8;
                        Y_TILE = MODE_V_ACTIVE_LINES / 8;
                        for (int x = 0; x < X_TILE; x++)
                        {
                            for (int y = 0; y < Y_TILE; y++)
                            {
                                tilefcols[y * X_TILE + x] = RGB555(Option.DefaultFC);
                                tilebcols[y * X_TILE + x] = RGB555(Option.DefaultBC);
                            }
                        }
                    }
                    else
                    {
                        tilefcols_w = (uint8_t *)DisplayBuf + MODE1SIZE;
                        tilebcols_w = tilefcols_w + (MODE_H_ACTIVE_PIXELS / 8) * (MODE_V_ACTIVE_LINES / 8); // minimum tilesize is 8x8
                        memset(tilefcols_w, RGB332(Option.DefaultFC), (MODE_H_ACTIVE_PIXELS / 8) * (MODE_V_ACTIVE_LINES / 8) * sizeof(uint8_t));
                        memset(tilebcols_w, RGB332(Option.DefaultBC), (MODE_H_ACTIVE_PIXELS / 8) * (MODE_V_ACTIVE_LINES / 8) * sizeof(uint8_t));
                        X_TILE = MODE_H_ACTIVE_PIXELS / 8;
                        Y_TILE = MODE_V_ACTIVE_LINES / ytileheight;
                    }
                }
#endif
#else
                Option.Refresh = RefreshSave;
#endif
                if (c != ESC && TextChanged && fname == NULL)
                {
                    gui_bcolour = OriginalBC; // *EB*
                    gui_fcolour = OriginalFC; // *EB*
                    ClearScreen(gui_bcolour); // *EB*
                    SaveToProgMemory();
                }
                if (c != ESC && TextChanged && fname)
                {
                    int fnbr1;
                    if (ExistsFile(fname))
                    {
                        char backup[FF_MAX_LFN];
                        strcpy(backup, fname);
                        strcat(backup, ".bak");
                        fnbr1 = FindFreeFileNbr();
                        BasicFileOpen(fname, fnbr1, FA_READ);
                        int fnbr2 = FindFreeFileNbr();
                        BasicFileOpen(backup, fnbr2, FA_WRITE | FA_CREATE_ALWAYS);
                        while (!FileEOF(fnbr1))
                        { // while waiting for the end of file
                            FilePutChar(FileGetChar(fnbr1), fnbr2);
                        }
                        FileClose(fnbr1);
                        FileClose(fnbr2);
                    }
                    fnbr1 = FindFreeFileNbr();
                    BasicFileOpen(fname, fnbr1, FA_WRITE | FA_CREATE_ALWAYS);
                    p = EdBuff;
                    if (Option.continuation)
                    {
                        unsigned char *q = p;
                        while (*p)
                        {
                            if (*p == Option.continuation && p[1] == '\n')
                                p += 2; // step over the continuation characters
                            else
                                *q++ = *p++;
                        }
                        *q = 0;
                        p = EdBuff;
                    }
                    do
                    { // while waiting for the end of file
                        c = *p++;
                        if (c == '\n')
                            FilePutChar('\r', fnbr1);
                        FilePutChar(c, fnbr1);
                    } while (*p);
                    FileClose(fnbr1);
                }
                if (cmdfile == false)
                {
#ifdef MMBASIC_FM
                    fm_edit_wants_run = (fm_exit_key == F2 || fm_exit_key == CTRLKEY('W')) ? 1 : 0;
#endif
                    RestoreContext(false);
                    return;
                }
                if (c == ESC || c == CTRLKEY('Q') || c == F1 || fname)
                {
                    cmdline = NULL;
                    do_end(false);
#ifdef PICOMITEVGA
                    // do_end calls setmode/ResetDisplay which overwrites font - restore it
                    SetFont(oldfont);
                    PromptFont = oldfont;
#endif
                    longjmp(mark, 1); // jump back to the input prompt
                }
                // this must be save, exit and run.  We have done the first two, now do the run part.
                ClearRuntime(true);
                //                            WatchdogSet = false;
                if (PrepareProgram(true))
                {
                    PrintPreprogramError();
                    return;
                }
                // Create a global constant MM.CMDLINE$ containing the empty string.
                //                            (void) findvar((unsigned char *)"MM.CMDLINE$", V_FIND | V_DIM_VAR | T_CONST);
                if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE)
                    ExecuteProgram(LibMemory); // run anything that might be in the library
                if (*ProgMemory != T_NEWLINE)
                    return; // no program to run
#ifdef PICOMITEWEB
                cleanserver();
#endif
                nextstmt = ProgMemory;
                clearrepeat();
                return;

            // Search
            case CTRLKEY('R'):
            case F3:
                GetInputString((unsigned char *)"Find (Use SHIFT-F3 to repeat): ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                if (!(*inpbuf == 0xb3 || *inpbuf == F3))
                    strcpy((char *)tknbuf, (char *)inpbuf);
                // fall through

            case CTRLKEY('G'):
            case SHIFT_F3:
            case F6:
                p = txtp;
                if (*p == 0)
                    p = EdBuff - 1;
                i = strlen((char *)tknbuf);
                while (1)
                {
                    p++;
                    if (p == txtp)
                        break;
                    if (*p == 0)
                        p = EdBuff;
                    if (p == txtp)
                        break;
                    if (mem_equal(p, tknbuf, i))
                        break;
                }
                if (p == txtp)
                {
                    editDisplayMsg((unsigned char *)" NOT FOUND ");
                    break;
                }
                for (y = 0, txtp = EdBuff; txtp != p; txtp++)
                { // find the line and column of the string
                    if (*txtp == '\n')
                    {
                        y++; // y is the line
                    }
                }
                edy = y - VHeight / 2; // edy is the line displayed at the top
                if (edy < 0)
                    edy = 0; // compensate if we are near the start
                printScreen();
                PositionCursor(txtp);
                foundit = true;
                // SCursor(x, y);
                break;

            // Mark
            case CTRLKEY('T'):
            case F4:
                MarkMode(clipboard, &buf[1]);
                printScreen();
                PrintFunctKeys(EDIT);
                PositionCursor(txtp);
                break;
            case SHIFT_F4:
            case F7:
            case CTRLKEY('F'):
                GetInputString((unsigned char *)"Replace: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                if (!(*inpbuf == 0xb3 || *inpbuf == F3))
                    strcpy((char *)clipboard, (char *)inpbuf);
            case CTRLKEY('I'):
            case SHIFT_F5:
            case F8:
                if (!foundit)
                {
                    editDisplayMsg((unsigned char *)" NOTHING TO REPLACE ");
                    break;
                }
                if (*clipboard == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }
                for (i = 0; i < strlen((char *)tknbuf); i++)
                {
                    buf[i + 1] = DEL;
                }
                buf[i + 1] = F5;
                break;
            case CTRLKEY('Y'):
            case CTRLKEY('V'):
            case F5:
                if (*clipboard == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }
                for (i = 0; clipboard[i]; i++)
                    buf[i + 1] = clipboard[i];
                buf[i + 1] = 0;
                break;

            // F9 - Import file at current position
            case CTRLKEY('O'):
            case F9:
            {
                // Get filename from user
                GetInputString((unsigned char *)"Import file: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;

                // Copy filename to local buffer
                static char importfile[FF_MAX_LFN];
                strcpy(importfile, (char *)inpbuf);

                // Add .bas extension if no extension and file not found
                if (!ExistsFile(importfile))
                {
                    if (strchr(importfile, '.') == NULL)
                        strcat(importfile, ".bas");
                }

                // Check file exists and get size
                if (!ExistsFile(importfile))
                {
                    editDisplayMsg((unsigned char *)" FILE NOT FOUND ");
                    break;
                }

                int fsize = FileSize(importfile);
                unsigned char *endp;
                for (endp = EdBuff; *endp; endp++)
                    ; // find end of current text
                int current_size = endp - EdBuff;

                if (current_size + fsize >= edit_buff_size - 10)
                {
                    editDisplayMsg((unsigned char *)" FILE TOO LARGE ");
                    break;
                }

                // Save current cursor position
                unsigned char *saved_txtp = txtp;

                // Pre-process filename same way ExistsFile does
                // Must set FatFSFileSystem correctly before BasicFileOpen
                int waste = 0;
                char *openname = importfile;
                int t = drivecheck(importfile, &waste);
                openname += waste;
                FatFSFileSystem = t - 1;

                // Open the file
                int fnbr = FindFreeFileNbr();
                if (!BasicFileOpen(openname, fnbr, FA_READ))
                {
                    editDisplayMsg((unsigned char *)" CANNOT OPEN FILE ");
                    break;
                }

                // Read file and insert each character
                char ch;
                int insert_count = 0;
                while (!FileEOF(fnbr))
                {
                    ch = FileGetChar(fnbr);
                    if (ch == '\r')
                        continue; // skip carriage returns
                    if (ch == '\n')
                        nbrlines++;
                    if (!editInsertChar((unsigned char)ch, &multi, edit_buff_size))
                    {
                        FileClose(fnbr);
                        editDisplayMsg((unsigned char *)" OUT OF MEMORY ");
                        break;
                    }
                    insert_count++;
                }
                FileClose(fnbr);

                // Restore cursor to original position (before inserted text)
                txtp = saved_txtp;

                TextChanged = true;
                printScreen();
                PositionCursor(txtp);

                // Display success message
                char msg[40];
                sprintf(msg, " IMPORTED %d CHARS ", insert_count);
                editDisplayMsg((unsigned char *)msg);
            }
            break;

            // F10 - Export clipboard to file
            case CTRLKEY('B'):
            case F10:
            {
                // Check if clipboard has content
                if (clipboard[0] == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }

                // Get filename from user
                GetInputString((unsigned char *)"Export to file: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;

                // Copy filename to local buffer
                static char exportfile[FF_MAX_LFN];
                strcpy(exportfile, (char *)inpbuf);

                // Add .bas extension if no extension specified
                if (strchr(exportfile, '.') == NULL)
                    strcat(exportfile, ".bas");

                // Pre-process filename same way as F9 - set FatFSFileSystem correctly
                int waste = 0;
                char *openname = exportfile;
                int t = drivecheck(exportfile, &waste);
                openname += waste;
                FatFSFileSystem = t - 1;

                // Open file for writing
                int fnbr = FindFreeFileNbr();
                if (!BasicFileOpen(openname, fnbr, FA_WRITE | FA_CREATE_ALWAYS))
                {
                    editDisplayMsg((unsigned char *)" CANNOT CREATE FILE ");
                    break;
                }

                // Write clipboard contents to file
                int export_count = 0;
                unsigned char *cp = clipboard;
                while (*cp)
                {
                    if (*cp == '\n')
                        FilePutChar('\r', fnbr); // Add CR before LF for proper line endings
                    FilePutChar(*cp++, fnbr);
                    export_count++;
                }
                FileClose(fnbr);

                // Display success message
                char msg[40];
                sprintf(msg, " EXPORTED %d CHARS ", export_count);
                editDisplayMsg((unsigned char *)msg);
            }
            break;

            // F11 - reserved
            case F11:
                break;

#ifndef PICOMITEMIN
            // F12 - Beautify (re-indent block structures)
            case F12:
                editBeautify(edit_buff_size);
                TextChanged = true;
                /* After buffer rewrite, restore cursor to start of buffer */
                txtp = EdBuff;
                edx = edy = curx = cury = 0;
                printScreen();
                PositionCursor(txtp);
                PrintStatus();
                editDisplayMsg((unsigned char *)" BEAUTIFIED ");
                break;
#else
            case F12:
                break;
#endif

            // a normal character
            default:
                c = buf[0];
                if (c < ' ' || c > '~')
                    break; // make sure that this is valid
                TextChanged = true;
                if (insert || *txtp == '\n' || *txtp == 0)
                {
                    if (!editInsertChar(c, &multi, edit_buff_size))
                        break; // insert it
                }
                else
                    *txtp++ = c; // or just overtype
                if (curx >= VWidth - 1 - SCROLLCHARS)
                    edx++;
                printLine(edy + cury); // redraw the whole line so that colour coding will occur
                PositionCursor(txtp);
                // SCursor(x, cury);
                tempx = cury; // used to track the preferred cursor position
                if (multi && Option.ColourCode)
                    printScreen();
                PrintStatus();
                break;
            }
            lastkey = buf[0];
            if (buf[0] != UP && buf[0] != DOWN && buf[0] != CTRLKEY('E') && buf[0] != CTRLKEY('X'))
                tempx = curx;
            buf[MAXCLIP + 1] = 0;
            for (i = 0; i < MAXCLIP + 1; i++)
                buf[i] = buf[i + 1]; // suffle down the buffer to get the next char
        } while (*buf);
    }
}

/*******************************************************************************************************************
  UTILITY FUNCTIONS USED BY THE FULL SCREEN EDITOR
*******************************************************************************************************************/

void PositionCursor(unsigned char *curp)
{
    int ln, col;
    unsigned char *p;

    for (p = EdBuff, ln = col = 0; p < curp; p++)
    {
        if (*p == '\n')
        {
            ln++;
            col = 0;
        }
        else
            col++;
    }
    if (ln < edy || ln >= edy + VHeight)
        return;
    SCursor(col - edx, ln - edy);
}

// mark mode
// implement the mark mode (when the user presses F4)
void MarkMode(unsigned char *cb, unsigned char *buf)
{
    unsigned char *p, *mark, *oldmark;
    int c = -1, x, y, i, oldx, oldy, txtpx, txtpy, errmsg = false;
    int edx_save = edx, edy_save = edy;
#ifdef PICOMITEVGA
    int fontinc = gui_font_width / 8;
#endif
    PrintFunctKeys(MARK);
    oldmark = mark = txtp;
    txtpx = oldx = curx;
    txtpy = oldy = cury;
    while (1)
    {
        c = -1;
#ifndef PICOMITE
#ifndef PICOMITEWEB
#ifdef USBKEYBOARD
        if (HID[1].Device_type == 2 && DISPLAY_TYPE == SCREENMODE1)
        {
#else
        if (mouse0 && DISPLAY_TYPE == SCREENMODE1)
        {
#endif
            if (!nunstruct[2].L)
                leftpushed = false;
            if (!nunstruct[2].R)
                rightpushed = false;
            if (!nunstruct[2].C)
                middlepushed = false;
            if (nunstruct[2].y1 != lasty1 || nunstruct[2].x1 != lastx1)
            {
                if (lastx1 != 9999)
                {
#ifdef HDMI
                    if (FullColour)
                    {
#endif
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilefcols[lasty1 * X_TILE + i] = lastfc;
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilebcols[lasty1 * X_TILE + i] = lastbc;
#ifdef HDMI
                    }
                    else
                    {
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilefcols_w[lasty1 * X_TILE + i] = lastfc;
                        for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                            tilebcols_w[lasty1 * X_TILE + i] = lastbc;
                    }
#endif
                }
                lastx1 = nunstruct[2].x1;
                lasty1 = nunstruct[2].y1;
                if (lasty1 >= VHeight)
                    lasty1 = VHeight - 1;
#ifdef HDMI
                if (FullColour)
                {
#endif
                    lastfc = tilefcols[lasty1 * X_TILE + lastx1 * fontinc];
                    lastbc = tilebcols[lasty1 * X_TILE + lastx1 * fontinc];
#ifdef HDMI
                }
                else
                {
                    lastfc = tilefcols_w[lasty1 * X_TILE + lastx1 * fontinc];
                    lastbc = tilebcols_w[lasty1 * X_TILE + lastx1 * fontinc];
                }
                if (FullColour)
                {
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilefcols[lasty1 * X_TILE + i] = RGB555(RED);
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilebcols[lasty1 * X_TILE + i] = RGB555(WHITE);
                }
                else
                {
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilefcols_w[lasty1 * X_TILE + i] = RGB332(RED);
                    for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                        tilebcols_w[lasty1 * X_TILE + i] = RGB332(WHITE);
                }
#else
                for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                    tilefcols[lasty1 * X_TILE + i] = RGB121pack(RED);
                for (int i = lastx1 * fontinc; i < (lastx1 + 1) * fontinc; i++)
                    tilebcols[lasty1 * X_TILE + i] = RGB121pack(WHITE);
#endif
            }
            if ((nunstruct[2].L && leftpushed == false && rightpushed == false && middlepushed == false) ||
                (nunstruct[2].R && leftpushed == false && rightpushed == false && middlepushed == false) ||
                (nunstruct[2].C && leftpushed == false && rightpushed == false && middlepushed == false))
            {
                if (nunstruct[2].L)
                    leftpushed = true;
                else if (nunstruct[2].R)
                    rightpushed = true;
                else
                    middlepushed = true;
                if (lastx1 >= 0 && lastx1 < VWidth && lasty1 >= 0 && lasty1 < VHeight)
                { // c == ' ' means mouse down and no shift, ctrl, etc
                    //        unsigned char * mtxtp=txtp;
                    p = txtp;
                    // first position on the y axis
                    while (*p != 0 && lasty1 > cury) // assume we have to move down the screen
                        if (*p++ == '\n')
                            cury++;
                    while (p != EdBuff && lasty1 < cury) // assume we have to move up the screen
                        if (*--p == '\n')
                            cury--;
                    while (p != EdBuff && *(p - 1) != '\n')
                        p--; // move to the beginning of the line
                    for (curx = 0; curx < lastx1 && *p && *p != '\n'; curx++)
                        p++; // now position on the x axis
                    PositionCursor(p);
                    mark = p;
                }
                if (rightpushed == true)
                {
                    c = F4;
                }
                if (middlepushed == true)
                {
                    c = F5;
                }
                if (leftpushed == true)
                {
                    c = 9999;
                }
            }
        }
        if (!((rightpushed == true && c == F4) || (middlepushed == true && c == F5) || (leftpushed == true && c == 9999)))
#endif
#endif
            c = MMInkey();
        if (c != -1 && errmsg)
        {
            PrintFunctKeys(MARK);
            errmsg = false;
        }
        switch (c)
        {
        case ESC:
            uSec(50000); // wait 50ms to see if anything more is coming
            if (MMInkey() == '[' && MMInkey() == 'M')
            {
                // received escape code for Tera Term reporting a mouse click.  in mark mode we ignore it
                MMInkey();
                MMInkey();
                MMInkey();
                break;
            }
            curx = txtpx;
            cury = txtpy; // just an escape key
            edx = edx_save;
            edy = edy_save;
            SCursor(curx, cury);
            return;

        case CTRLKEY('E'):
        case UP:
            if (cury <= 0 && edy == 0)
                continue; // at very top of file
            p = mark;
            if (*p == '\n')
                p--; // step back over the terminator if we are right at the end of the line
            while (p != EdBuff && *p != '\n')
                p--; // move to the beginning of the line
            if (p != EdBuff)
            {
                p--; // step over the terminator to the end of the previous line
                for (i = 0; p != EdBuff && *p != '\n'; p--, i++)
                    ; // move to the beginning of that line
                if (*p == '\n')
                    p++; // and position at the start
            }
            mark = p;
            for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                ; // move the cursor to the preferred absolute column
            {
                int need_redraw = 0;
                if (i >= edx + VWidth - SCROLLCHARS)
                {
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                    need_redraw = 1;
                }
                else if (edx > 0 && i < edx + SCROLLCHARS)
                {
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                    need_redraw = 1;
                }
                curx = i - edx;
                if (cury > 0)
                    cury--;
                else if (edy > 0)
                {
                    edy--;
                    need_redraw = 1;
                }
                if (need_redraw)
                {
                    printScreen();
                    PrintFunctKeys(MARK);
                }
            }
            break;

        case CTRLKEY('X'):
        case DOWN:
            for (p = mark; *p != 0 && *p != '\n'; p++)
                ; // move to the end of this line
            if (*p == 0)
                continue; // skip if it is at the end of the file
            mark = p + 1; // step over the line terminator to the start of the next line
            for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                ; // move the cursor to the preferred absolute column
            {
                int need_redraw = 0;
                if (i >= edx + VWidth - SCROLLCHARS)
                {
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                    need_redraw = 1;
                }
                else if (edx > 0 && i < edx + SCROLLCHARS)
                {
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                    need_redraw = 1;
                }
                curx = i - edx;
                if (cury < VHeight - 1)
                    cury++;
                else if (edy + VHeight < nbrlines)
                {
                    edy++;
                    need_redraw = 1;
                }
                if (need_redraw)
                {
                    printScreen();
                    PrintFunctKeys(MARK);
                }
            }
            break;

        case CTRLKEY('S'):
        case LEFT:
            if (curx == 0 && edx == 0)
                continue;
            mark--;
            if (edx > 0 && curx <= SCROLLCHARS)
            {
                edx--;
                printLine(edy + cury);
            }
            else
                curx--;
            break;

        case CTRLKEY('D'):
        case RIGHT:
            if (*mark == 0 || *mark == '\n')
                continue;
            mark++;
            if (curx >= VWidth - 1 - SCROLLCHARS)
            {
                edx++;
                printLine(edy + cury);
            }
            else
                curx++;
            break;

        case CTRLKEY('U'):
        case HOME:
            if (mark == EdBuff)
                break;
            if (*mark == '\n')
                mark--; // step back over the terminator if we are right at the end of the line
            while (mark != EdBuff && *mark != '\n')
                mark--; // move to the beginning of the line
            if (*mark == '\n')
                mark++; // skip if no more lines above this one
            if (edx != 0)
            {
                edx = 0;
                printScreen();
                PrintFunctKeys(MARK);
            }
            curx = 0;
            break;

        case CTRLKEY('K'):
        case END:
            if (*mark == 0)
                break;
            for (p = mark; *p != 0 && *p != '\n'; p++)
                ; // move to the end of this line
            mark = p;
            {
                int abs_col = 0;
                unsigned char *q = mark;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    abs_col++;
                    q--;
                }
                if (abs_col > VWidth - 1)
                {
                    edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                    printScreen();
                    PrintFunctKeys(MARK);
                }
                else if (edx != 0)
                {
                    edx = 0;
                    printScreen();
                    PrintFunctKeys(MARK);
                }
                curx = abs_col - edx;
            }
            break;

        case CTRLKEY('P'):
        case PUP:
            if (edy == 0)
            {
                // Already at top, move mark to beginning of file
                mark = EdBuff;
                cury = 0;
                curx = 0;
            }
            else
            {
                // Scroll up by VHeight lines
                int scrollamt = (edy >= VHeight) ? VHeight : edy;
                edy -= scrollamt;
                // Move mark up by scrollamt lines
                for (i = 0; i < scrollamt && mark != EdBuff; i++)
                {
                    if (*mark == '\n')
                        mark--;
                    while (mark != EdBuff && *mark != '\n')
                        mark--;
                    if (*mark == '\n' && mark != EdBuff)
                        mark--;
                    while (mark != EdBuff && *mark != '\n')
                        mark--;
                    if (*mark == '\n')
                        mark++;
                }
                // Position at start of line
                while (mark != EdBuff && *(mark - 1) != '\n')
                    mark--;
                for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                    ;
                if (i >= edx + VWidth - SCROLLCHARS)
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                else if (edx > 0 && i < edx + SCROLLCHARS)
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                curx = i - edx;
                printScreen();
                PrintFunctKeys(MARK);
            }
            break;

        case CTRLKEY('L'):
        case PDOWN:
            if (edy + VHeight >= nbrlines)
            {
                // Already showing end, move mark to end of file
                while (*mark)
                    mark++;
                cury = VHeight - 1;
            }
            else
            {
                // Scroll down by VHeight lines
                int scrollamt = VHeight;
                if (edy + VHeight + scrollamt > nbrlines)
                    scrollamt = nbrlines - edy - VHeight;
                edy += scrollamt;
                // Move mark down by scrollamt lines
                for (i = 0; i < scrollamt && *mark; i++)
                {
                    while (*mark && *mark != '\n')
                        mark++;
                    if (*mark == '\n')
                        mark++;
                }
                // Position at column
                for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                    ;
                if (i >= edx + VWidth - SCROLLCHARS)
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                else if (edx > 0 && i < edx + SCROLLCHARS)
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                curx = i - edx;
                printScreen();
                PrintFunctKeys(MARK);
            }
            break;

        case CTRLKEY('Y'):
        case CTRLKEY('T'):
        case F5:
        case F4:
        case CTRLKEY('B'):
        case F10:
            if (c != F10 && (txtp - mark > MAXCLIP || mark - txtp > MAXCLIP))
            {
                editDisplayMsg((unsigned char *)" MARKED TEXT EXCEEDS BUFFER SIZE");
                errmsg = true;
                SCursor(curx, cury);
                continue;
            }
            if (c != F10)
            {
                if (mark <= txtp)
                {
                    p = mark;
                    while (p < txtp)
                        *cb++ = *p++;
                }
                else
                {
                    p = txtp;
                    while (p <= mark - 1)
                        *cb++ = *p++;
                }
                *cb = 0;
            }
            if (c == F5 || c == CTRLKEY('Y') || c == F10)
            {
                // For F10, also export to file before returning
                if (c == F10)
                {
                    // Get filename from user
                    GetInputString((unsigned char *)"Export to file: ");
                    if (*inpbuf != 0 && *inpbuf != ESC)
                    {
                        // Copy filename to local buffer
                        static char exportfile[FF_MAX_LFN];
                        strcpy(exportfile, (char *)inpbuf);

                        // Add .bas extension if no extension specified
                        if (strchr(exportfile, '.') == NULL)
                            strcat(exportfile, ".bas");

                        // Pre-process filename - set FatFSFileSystem correctly
                        int waste = 0;
                        char *openname = exportfile;
                        int t = drivecheck(exportfile, &waste);
                        openname += waste;
                        FatFSFileSystem = t - 1;

                        // Open file for writing
                        int fnbr = FindFreeFileNbr();
                        if (BasicFileOpen(openname, fnbr, FA_WRITE | FA_CREATE_ALWAYS))
                        {
                            // Write clipboard contents to file
                            // Note: cb has been incremented, so use buf parameter which points to original clipboard
                            int export_count = 0;
                            unsigned char *cp = buf; // buf points to start of clipboard passed to MarkMode
                            // Actually we need the original clipboard start - recalculate
                            cp = (mark <= txtp) ? mark : txtp;
                            unsigned char *cpend = (mark <= txtp) ? txtp : mark;
                            while (cp < cpend)
                            {
                                if (*cp == '\n')
                                    FilePutChar('\r', fnbr);
                                FilePutChar(*cp++, fnbr);
                                export_count++;
                            }
                            FileClose(fnbr);

                            char msg[40];
                            sprintf(msg, " EXPORTED %d CHARS ", export_count);
                            editDisplayMsg((unsigned char *)msg);
                        }
                        else
                        {
                            editDisplayMsg((unsigned char *)" CANNOT CREATE FILE ");
                        }
                    }
                }
                // Calculate line number of txtp and adjust edy if needed
                int ln;
                unsigned char *pp;
                for (pp = EdBuff, ln = 0; pp < txtp; pp++)
                    if (*pp == '\n')
                        ln++;
                // If txtp is off-screen, adjust edy to center it
                if (ln < edy || ln >= edy + VHeight)
                {
                    edy = ln - VHeight / 2;
                    if (edy < 0)
                        edy = 0;
                    if (edy + VHeight > nbrlines)
                        edy = (nbrlines > VHeight) ? nbrlines - VHeight : 0;
                }
                cury = ln - edy;
                PositionCursor(txtp);
#ifndef PICOMITE
#ifndef PICOMITEWEB
#ifdef USBKEYBOARD
                if (HID[1].Device_type == 2 && DISPLAY_TYPE == SCREENMODE1)
                {
#else
                if (mouse0 && DISPLAY_TYPE == SCREENMODE1)
                {
#endif
                    nunstruct[2].ax = curx * FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
                    nunstruct[2].ay = cury * FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
                    lastx1 = 9999;
                    lasty1 = 9999;
                }
#endif
#endif
                return;
            }
            // fall through

        case CTRLKEY(']'):
        case DEL:
            if (mark < txtp)
            {
                p = txtp;
                txtp = mark;
                mark = p; // swap txtp and mark
            }
            for (p = txtp; p < mark; p++)
                if (*p == '\n')
                    nbrlines--;
            for (p = txtp; *mark;)
                *p++ = *mark++;
            *p++ = 0;
            *p++ = 0;
            TextChanged = true;
            // Calculate line number of txtp and adjust edy if needed
            {
                int ln;
                unsigned char *pp;
                for (pp = EdBuff, ln = 0; pp < txtp; pp++)
                    if (*pp == '\n')
                        ln++;
                // If txtp is off-screen, adjust edy to center it
                if (ln < edy || ln >= edy + VHeight)
                {
                    edy = ln - VHeight / 2;
                    if (edy < 0)
                        edy = 0;
                    if (edy + VHeight > nbrlines)
                        edy = (nbrlines > VHeight) ? nbrlines - VHeight : 0;
                }
                cury = ln - edy;
            }
            PositionCursor(txtp);
#ifndef PICOMITE
#ifndef PICOMITEWEB
#ifdef USBKEYBOARD
            if (HID[1].Device_type == 2 && DISPLAY_TYPE == SCREENMODE1)
            {
#else
            if (mouse0 && DISPLAY_TYPE == SCREENMODE1)
            {
#endif
                nunstruct[2].ax = curx * FontTable[gui_font >> 4][0] * (gui_font & 0b1111);
                nunstruct[2].ay = cury * FontTable[gui_font >> 4][1] * (gui_font & 0b1111);
                lastx1 = 9999;
                lasty1 = 9999;
            }
#endif
#endif
            return;
        case 9999:
            break;
        default:
            continue;
        }

        x = curx;
        y = cury;
        markmode = true;

        // Calculate visible line range
        unsigned char *visStart, *visEnd;
        int ln;
        for (visStart = EdBuff, ln = 0; ln < edy && *visStart; visStart++)
            if (*visStart == '\n')
                ln++;
        for (visEnd = visStart, ln = 0; ln < VHeight && *visEnd; visEnd++)
            if (*visEnd == '\n')
                ln++;

        // Determine the marked range (low to high)
        unsigned char *markLow = (mark < txtp) ? mark : txtp;
        unsigned char *markHigh = (mark < txtp) ? txtp : mark;

        // first unmark the area not marked as a result of the keystroke (only visible portion)
        // col tracks the absolute text column so we clip output to [edx, edx+VWidth)
        if (oldmark < mark)
        {
            p = oldmark;
            int col = 0;
            {
                unsigned char *q = p;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    col++;
                    q--;
                }
            }
            if (p >= visStart && p < visEnd)
            {
                while (p < mark && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (p < mark && p >= visStart && p < visEnd && *p != '\n')
                {
                    PositionCursor(p);
                    if (Option.ColourCode)
                        restoreColourFromLineStart(p);
                }
            }
            while (p < mark)
            {
                if (p >= visStart && p < visEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (p < mark && p < visEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (p < mark && p >= visStart && p < visEnd && *p != '\n')
                        {
                            PositionCursor(p);
                            if (Option.ColourCode)
                                restoreColourFromLineStart(p);
                        }
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        if (Option.ColourCode)
                            SetColour(p, true);
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                }
                if (*p == '\n')
                    col = 0;
                else
                    col++;
                p++;
            }
        }
        else if (oldmark > mark)
        {
            p = mark;
            int col = 0;
            {
                unsigned char *q = p;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    col++;
                    q--;
                }
            }
            if (p >= visStart && p < visEnd)
            {
                while (oldmark > p && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (oldmark > p && p >= visStart && p < visEnd && *p != '\n')
                {
                    PositionCursor(p);
                    if (Option.ColourCode)
                        restoreColourFromLineStart(p);
                }
            }
            while (oldmark > p)
            {
                if (p >= visStart && p < visEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (oldmark > p && p < visEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (oldmark > p && p >= visStart && p < visEnd && *p != '\n')
                        {
                            PositionCursor(p);
                            if (Option.ColourCode)
                                restoreColourFromLineStart(p);
                        }
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        if (Option.ColourCode)
                            SetColour(p, true);
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                }
                if (*p == '\n')
                    col = 0;
                else
                    col++;
                p++;
            }
        }
#ifndef USBKEYBOARD
        tud_cdc_write_flush();
#endif
        oldmark = mark;
        oldx = x;
        oldy = y;

        // now draw the marked area (only visible portion)
        if (markLow < markHigh)
        {
            // Find where to start drawing (intersection of marked range and visible range)
            unsigned char *drawStart = (markLow > visStart) ? markLow : visStart;
            unsigned char *drawEnd = (markHigh < visEnd) ? markHigh : visEnd;

            if (drawStart < drawEnd)
            {
                PrintString(Option.ColourCode ? "\033[44m" : "\033[0m\033[7m");
                MX470Display(REVERSE_VIDEO);
                p = drawStart;
                int col = 0;
                {
                    unsigned char *q = p;
                    while (q > EdBuff && *(q - 1) != '\n')
                    {
                        col++;
                        q--;
                    }
                }
                while (p < drawEnd && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (p < drawEnd && *p != '\n')
                    PositionCursor(p);
                while (p < drawEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (p < drawEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (p < drawEnd && *p != '\n')
                            PositionCursor(p);
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                    col++;
                    p++;
                }
                MX470Display(REVERSE_VIDEO);
            }
        }
        markmode = false;
        PrintString("\033[0m"); // normal video

        oldx = x;
        oldy = y;
        oldmark = mark;
        PositionCursor(mark);
    }
}

// search through the text in the editing buffer looking for a specific line
// enters with ln = the line required
// exits pointing to the start of the line or pointing to a zero char if not that many lines in the buffer
// inmulti return values:
//   0 = normal
//   1 = inside /* */ multi-line comment
//   2 = (internal) */ found at start of line
//   3 = continuation line inside a quoted string
//   4 = continuation line inside a single-line comment
char *findLine(int ln, int *inmulti)
{
    unsigned char *p, *q;
    *inmulti = false;
    int inquote = false;
    int incomment = false;
    p = q = EdBuff;
    skipspace(q);
    if (q[0] == '/' && q[1] == '*')
        *inmulti = true;
    if (q[0] == '*' && q[1] == '/')
        *inmulti = false;
    while (ln && *p)
    {
        if (*p == '\n')
        {
            // Check if this line continues to the next via continuation character
            if (Option.continuation && p >= EdBuff + 2 &&
                *(p - 1) == Option.continuation && *(p - 2) == ' ')
            {
                // Continuation line - carry quote/comment state to next line
            }
            else
            {
                // Not a continuation - reset quote/comment state
                inquote = false;
                incomment = false;
            }
            if (*inmulti == 2)
                *inmulti = false;
            ln--;
            q = &p[1];
            skipspace(q);
            if (q[0] == '/' && q[1] == '*')
                *inmulti = true;
            if (q[0] == '*' && q[1] == '/')
                *inmulti = 2;
        }
        else if (!*inmulti)
        {
            // Track quote and comment state through the text
            if (*p == '\'' && !inquote)
                incomment = true;
            else if (*p == '"' && !incomment)
                inquote = !inquote;
        }
        p++;
    }
    // Report continuation colour state via inmulti
    if (!*inmulti && inquote)
        *inmulti = 3;
    else if (!*inmulti && incomment)
        *inmulti = 4;
    return (char *)p;
}

int EditCompStr(char *p, char *tkn)
{
    while (*tkn && (mytoupper(*tkn) == mytoupper(*p)))
    {
        if (*tkn == '(' && *p == '(')
            return true;
        if (*tkn == '$' && *p == '$')
            return true;
        tkn++;
        p++;
    }
    if (*tkn == 0 && !isnamechar((unsigned char)*p))
        return true; // return the string if successful

    return false; // or NULL if not
}

// this function does the syntax colour coding
// p = pointer to the current character to be printed
//     or NULL if the colour coding is to be cmdfile to normal
//
// it keeps track of where it is in the line using static variables
// so it must be fed all chars from the start of the line
void SetColour(unsigned char *p, int DoVT100)
{
    int i;
    unsigned char **pp;
    static int intext = false;
    static int incomment = false;
    static int inkeyword = false;
    static unsigned char *twokeyword = NULL;
    static int inquote = false;
    static int innumber = false;

    if (!Option.ColourCode)
        return;

    // this is a list of keywords that can come after the OPTION and GUI commands
    // the list must be terminated with a NULL
    char *twokeywordtbl[] = {
        "BASE", "EXPLICIT", "DEFAULT", "BREAK", "AUTORUN", "BAUDRATE", "DISPLAY",
#if defined(GUICONTROLS)
        "BUTTON", "SWITCH", "CHECKBOX", "RADIO", "LED", "FRAME", "NUMBERBOX", "SPINBOX", "TEXTBOX", "DISPLAYBOX", "CAPTION", "DELETE",
        "DISABLE", "HIDE", "ENABLE", "SHOW", "FCOLOUR", "BCOLOUR", "REDRAW", "BEEP", "INTERRUPT",
#endif
        NULL};

    // this is a list of common keywords that should be highlighted as such
    // the list must be terminated with a NULL
    char *specialkeywords[] = {
        "SELECT", "INTEGER", "FLOAT", "STRING", "DISPLAY", "SDCARD", "OUTPUT", "APPEND", "WRITE", "SLAVE", "TARGET", "PROGRAM",
        //        ".PROGRAM", ".END PROGRAM", ".SIDE", ".LABEL" , ".LINE",".WRAP", ".WRAP TARGET",
        NULL};

    // cmdfile everything back to normal
    if (p == NULL)
    {
        innumber = inquote = inkeyword = incomment = intext = false;
        twokeyword = NULL;
        if (!multilinecomment)
        {
            gui_fcolour = GUI_C_NORMAL;
            if (DoVT100)
                PrintString(VT100_C_NORMAL);
        }
        return;
    }

    // Special init for continuation line colour state
    if (p == (unsigned char *)1)
    {
        // Init in quote mode (for continuation lines split inside a string)
        inquote = true;
        gui_fcolour = GUI_C_QUOTE;
        if (DoVT100)
            PrintString(VT100_C_QUOTE);
        return;
    }
    if (p == (unsigned char *)2)
    {
        // Init in comment mode (for continuation lines split inside a comment)
        incomment = true;
        gui_fcolour = GUI_C_COMMENT;
        if (DoVT100)
            PrintString(VT100_C_COMMENT);
        return;
    }

    if (*p == '*' && p[1] == '/' && !inquote)
    {
        multilinecomment = 2;
        return;
    }

    if (*p == '/' && !inquote && multilinecomment == 2)
    {
        multilinecomment = false;
        return;
    }

    // check for a comment char
    if (*p == '\'' && !inquote)
    {
        gui_fcolour = GUI_C_COMMENT;
        if (DoVT100)
            PrintString(VT100_C_COMMENT);
        incomment = true;
        return;
    }
    if (*p == '/' && p[1] == '*' && !inquote)
    {
        if (p == EdBuff || p[-1] == (unsigned char)'\n')
        {
            gui_fcolour = GUI_C_COMMENT;
            if (DoVT100)
                PrintString(VT100_C_COMMENT);
            multilinecomment = true;
        }
        return;
    }

    // once in a comment all following chars must be comments also
    if (incomment || multilinecomment)
        return;

    // check for a quoted string
    if (*p == '\"')
    {
        if (!inquote)
        {
            inquote = true;
            gui_fcolour = GUI_C_QUOTE;
            if (DoVT100)
                PrintString(VT100_C_QUOTE);
            return;
        }
        else
        {
            inquote = false;
            return;
        }
    }

    if (inquote)
        return;

    // if we are displaying a keyword check that it is still actually in the keyword and cmdfile if not
    if (inkeyword)
    {
        if (isnamechar(*p) || *p == '$')
            return;
        gui_fcolour = GUI_C_NORMAL;
        if (DoVT100)
            PrintString(VT100_C_NORMAL);
        inkeyword = false;
        return;
    }

    // if we are displaying a number check that we are still actually in it and cmdfile if not
    // this is complicated because numbers can be in hex or scientific notation
    if (innumber)
    {
        if (!isdigit(*p) && !(toupper(*p) >= 'A' && toupper(*p) <= 'F') && toupper(*p) != 'O' && toupper(*p) != 'H' && *p != '.')
        {
            gui_fcolour = GUI_C_NORMAL;
            if (DoVT100)
                PrintString(VT100_C_NORMAL);
            innumber = false;
            return;
        }
        else
        {
            return;
        }
        // check if we are starting a number
    }
    else if (!intext)
    {
        if (isdigit(*p) || *p == '&' || ((*p == '-' || *p == '+' || *p == '.') && isdigit(p[1])))
        {
            gui_fcolour = GUI_C_NUMBER;
            if (DoVT100)
                PrintString(VT100_C_NUMBER);
            innumber = true;
            return;
        }
        // check if this is an 8 digit hex number as used in CFunctions
        for (i = 0; i < 8; i++)
            if (!isxdigit(p[i]))
                break;
        if (i == 8 && (p[8] == ' ' || p[8] == '\'' || p[8] == 0))
        {
            gui_fcolour = GUI_C_NUMBER;
            if (DoVT100)
                PrintString(VT100_C_NUMBER);
            innumber = true;
            return;
        }
    }

    // check if this is the start of a keyword
    if (isnamechar(*p) && !intext)
    {
        for (i = 0; i < CommandTableSize - 1; i++)
        { // check the command table for a match
            if (EditCompStr((char *)p, (char *)commandtbl[i].name) != 0 ||
                ((EditCompStr((char *)&p[1], (char *)&commandtbl[i].name[1]) != 0) && *p == '.' && *commandtbl[i].name == '_'))
            {
                if (EditCompStr((char *)p, "REM") != 0)
                { // special case, REM is a comment
                    gui_fcolour = GUI_C_COMMENT;
                    if (DoVT100)
                        PrintString(VT100_C_COMMENT);
                    incomment = true;
                }
                else
                {
                    gui_fcolour = GUI_C_KEYWORD;
                    if (DoVT100)
                        PrintString(VT100_C_KEYWORD);
                    inkeyword = true;
                    if (EditCompStr((char *)p, "GUI") ||
                        EditCompStr((char *)p, "OPTION"))
                    {
                        twokeyword = p;
                        while (isalnum(*twokeyword))
                            twokeyword++;
                        while (*twokeyword == ' ')
                            twokeyword++;
                    }
                    return;
                }
            }
        }
        for (i = 0; i < TokenTableSize - 1; i++)
        { // check the token table for a match
            if (EditCompStr((char *)p, (char *)tokentbl[i].name) != 0)
            {
                gui_fcolour = GUI_C_KEYWORD;
                if (DoVT100)
                    PrintString(VT100_C_KEYWORD);
                inkeyword = true;
                return;
            }
        }

        // check for the second keyword in two keyword commands
        if (p == twokeyword)
        {
            for (pp = (unsigned char **)twokeywordtbl; *pp; pp++)
                if (EditCompStr((char *)p, (char *)*pp))
                    break;
            if (*pp)
            {
                gui_fcolour = GUI_C_KEYWORD;
                if (DoVT100)
                    PrintString(VT100_C_KEYWORD);
                inkeyword = true;
                return;
            }
        }
        if (p >= twokeyword)
            twokeyword = NULL;

        // check for a range of common keywords
        for (pp = (unsigned char **)specialkeywords; *pp; pp++)
            if (EditCompStr((char *)p, (char *)*pp))
                break;
        if (*pp)
        {
            gui_fcolour = GUI_C_KEYWORD;
            if (DoVT100)
                PrintString(VT100_C_KEYWORD);
            inkeyword = true;
            return;
        }
    }

    // try to keep track of if we are in general text or not
    // this is to avoid recognising keywords or numbers inside variables
    if (isnamechar(*p))
    {
        intext = true;
    }
    else
    {
        intext = false;
        gui_fcolour = GUI_C_NORMAL;
        if (DoVT100)
            PrintString(VT100_C_NORMAL);
    }
}

// Set up SetColour state for the character at 'target' and emit the matching
// VT100 colour escape.  Used by the unmark loops so the terminal colour is
// correct when visible characters are output after an edx-skip.
void restoreColourFromLineStart(unsigned char *target)
{
    int inmulti = 0;
    int ln = 0;
    unsigned char *q;
    for (q = EdBuff; q < target && *q; q++)
        if (*q == '\n')
            ln++;
    SetColour(NULL, false);
    unsigned char *ls = (unsigned char *)findLine(ln, &inmulti);
    if (inmulti == 3)
        SetColour((unsigned char *)1, false);
    else if (inmulti == 4)
        SetColour((unsigned char *)2, false);
    for (q = ls; q < target && *q && *q != '\n'; q++)
        if (!inmulti || inmulti == 3)
            SetColour(q, false);
    if (gui_fcolour == GUI_C_COMMENT)
        PrintString(VT100_C_COMMENT);
    else if (gui_fcolour == GUI_C_QUOTE)
        PrintString(VT100_C_QUOTE);
    else if (gui_fcolour == GUI_C_KEYWORD)
        PrintString(VT100_C_KEYWORD);
    else if (gui_fcolour == GUI_C_NUMBER)
        PrintString(VT100_C_NUMBER);
    else
        PrintString(VT100_C_NORMAL);
}

// print a line starting at the current column (edx) at the current cursor.
// if the line is beyond the end of the text then just clear to the end of line
// enters with the line number to be printed
void printLine(int ln)
{
    char *p;
    int i;
    int inmulti = false;
    // we always colour code the output to the LCD panel on the MX470 (when used as the console)
    if (Option.DISPLAY_CONSOLE)
    {
        MX470PutC('\r'); // print on the MX470 display
        p = findLine(ln, &inmulti);
        // Init colour state for continuation lines split inside a string or comment
        if (inmulti == 3)
            SetColour((unsigned char *)1, false); // init quote state
        else if (inmulti == 4)
            SetColour((unsigned char *)2, false); // init comment state
        // skip edx characters, advancing colour state without displaying
        for (i = edx; i > 0 && *p && *p != '\n'; i--)
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, false);
            p++;
        }
        i = VWidth;
        while (i && *p && *p != '\n')
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, false); // set the colour for the LCD display only
            else
                gui_fcolour = GUI_C_COMMENT;
            MX470PutC(*p++); // print on the MX470 display
            i--;
        }
        MX470Display(CLEAR_TO_EOL); // clear to the end of line on the MX470 display only
    }
    SetColour(NULL, false);

    p = findLine(ln, &inmulti);
    if (Option.ColourCode || edx > 0)
    {
        if (Option.ColourCode)
        {
            // Init colour state for continuation lines split inside a string or comment
            if (inmulti == 3)
                SetColour((unsigned char *)1, true); // init quote state
            else if (inmulti == 4)
                SetColour((unsigned char *)2, true); // init comment state
            // skip edx characters, advancing colour state without emitting codes
            for (i = edx; i > 0 && *p && *p != '\n'; i--)
            {
                if (!inmulti || inmulti == 3)
                    SetColour((unsigned char *)p, false);
                p++;
            }
            // Re-emit the current colour after skipping off-screen chars so the
            // terminal reflects state set by syntax elements that are now off-screen
            // (e.g. a comment ' that has scrolled left of the visible area).
            if (edx > 0)
            {
                if (gui_fcolour == GUI_C_COMMENT)
                    PrintString(VT100_C_COMMENT);
                else if (gui_fcolour == GUI_C_QUOTE)
                    PrintString(VT100_C_QUOTE);
                else if (gui_fcolour == GUI_C_KEYWORD)
                    PrintString(VT100_C_KEYWORD);
                else if (gui_fcolour == GUI_C_NUMBER)
                    PrintString(VT100_C_NUMBER);
                else
                    PrintString(VT100_C_NORMAL);
            }
        }
        else
        {
            // no colour coding: just skip edx characters
            for (i = edx; i > 0 && *p && *p != '\n'; i--)
                p++;
        }
        SSputchar('\r', 0); // go to start of line on the VT100 emulator
        i = VWidth;
    }
    else
    {
        // if we are NOT colour coding we can start drawing at the current cursor position
        i = curx;
        while (i-- && *p && *p != '\n')
            p++; // find the editing point in the buffer
        i = VWidth - curx;
    }

    while (i && *p && *p != '\n')
    {
        if (Option.ColourCode)
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, true); // if colour coding is used set the colour for the VT100 emulator
            else
            {
                gui_fcolour = GUI_C_COMMENT;
                PrintString(VT100_C_COMMENT);
            }
        }
        SSputchar(*p++, 0); // display the chars after the editing point
        i--;
    }

    PrintString("\033[K"); // all done, clear to the end of the line on a vt100 emulator
    if (Option.ColourCode)
        SetColour(NULL, true);
    curx = VWidth - 1;
}

// print a full screen starting with the top left corner specified by edx, edy
// this draws the full screen including blank areas so there is no need to clear the screen first
// it then returns the cursor to its original position
void printScreen(void)
{
    int i;

    SCursor(0, 0);
    for (i = 0; i < VHeight; i++)
    {
        printLine(i + edy);
        PrintString("\r\n");
        MX470PutS("\r\n", gui_fcolour, gui_bcolour);
        curx = 0;
        cury = i + 1;
    }
    while (getConsole() != -1)
        ; // consume any keystrokes accumulated while redrawing the screen
}

// position the cursor on the screen
void SCursor(int x, int y)
{
    char s[12];

    PrintString("\033[");
    IntToStr(s, y + 1, 10);
    PrintString(s);
    PrintString(";");
    IntToStr(s, x + 1, 10);
    PrintString(s);
    PrintString("H");
    MX470Cursor(x * gui_font_width, y * gui_font_height); // position the cursor on the MX470 display only
    curx = x;
    cury = y;
}

// move the text down by one char starting at the current position in the text
// and insert a character
int editInsertChar(unsigned char c, char *multi, int edit_buff_size)
{
    unsigned char *p;

    for (p = EdBuff; *p; p++)
        ; // find the end of the text in memory
    if (p >= EdBuff + edit_buff_size - 10)
    { // and check that we have the space (allow 10 bytes for slack)
        editDisplayMsg((unsigned char *)" OUT OF MEMORY ");
        return false;
    }
    for (; p >= txtp; p--)
        *(p + 1) = *p; // shift everything down
    *multi = 0;
    if (txtp > EdBuff)
    {
        unsigned char prev = *(txtp - 1);
        if ((c == '/' && prev == '*') || (c == '*' && prev == '/'))
            *multi = 1;
    }
    {
        unsigned char next = *(txtp + 1);
        if ((c == '/' && next == '*') || (c == '*' && next == '/'))
            *multi = 1;
    }
    *txtp++ = c; // and insert our char
    return true;
}

// print the function keys at the bottom of the screen
void PrintFunctKeys(int typ)
{
    int i, x, y;
    char *p;

    if (typ == EDIT)
    {
        if (VWidth > 80)
            p = "ESC:Exit F1:Save F2:Run F3/6:Find/r F4:Mrk F5:Paste F7/8:Rpl/r F9:In F10:Out F12:Beautify";
        else if (VWidth >= 70)
            p = "F1:Save F2:Run F3:Find F4:Mark F5:Paste F7:Repl F7/8:Rpl/r F12:Btfy";
        else if (VWidth >= 55)
            p = "F1:Save F2:Run F3:Find F4:Mrk F5:Paste F9:In F10:Out F12:Btfy";
        else
            p = "EDIT MODE";
    }
    else
    {
        if (VWidth >= 70)
            p = "MARK MODE  ESC=Exit DEL:Delete F4:Cut F5:Copy F10:Export";
        else if (VWidth >= 49)
            p = "MARK MODE  ESC:Exit DEL:Del F4:Cut F5:Cpy F10:Out";
        else
            p = "MARK MODE";
    }

    MX470Display(DRAW_LINE);                 // on the MX470 display draw the line
    MX470PutS(p, GUI_C_STATUS, gui_bcolour); // display the string on the display attached to the MX470
    MX470Display(CLEAR_TO_EOL);              // clear to the end of line on the MX470 display only

    x = curx;
    y = cury;
    SCursor(0, VHeight);
    if (Option.ColourCode)
        PrintString(VT100_C_LINE);
    PrintString("\033[4m"); // underline on
    for (i = 0; i < VWidth; i++)
        SSputchar(' ', 0);
    PrintString("\033[0m\r\n"); // underline off
    if (Option.ColourCode)
        PrintString(VT100_C_STATUS);
    PrintString(p);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);
    PrintString("\033[K"); // clear to the end of the line on a vt100 emulator
    SCursor(x, y);
}

// return the character length of the line that txtp sits on
static int current_line_length(void)
{
    unsigned char *p = txtp;
    while (p > EdBuff && *(p - 1) != '\n')
        p--;
    int len = 0;
    while (*p && *p != '\n')
    {
        p++;
        len++;
    }
    return len;
}

// print the current status
void PrintStatus(void)
{
    int tx, col;
    char s[MAXSTRLEN], temp[40];

    tx = edx + curx + 1;
    if (edit_is_bas && current_line_length() > MAXSTRLEN)
        sprintf(temp, "L:%d C:%d %s LONG", edy + cury + 1, tx, insert ? "INS" : "OVR");
    else
        sprintf(temp, "L:%d C:%d %s", edy + cury + 1, tx, insert ? "INS" : "OVR");
    sprintf(s, "%20s", temp);
    col = VWidth - (int)strlen(s);
    if (col < 0)
        col = 0;
    MX470Cursor(col * gui_font_width, (VRes / gui_font_height) * gui_font_height - gui_font_height);
    MX470PutS(s, GUI_C_STATUS, gui_bcolour); // display the string on the display attached to the MX470

    SCursor(col, VHeight + 1);
    if (Option.ColourCode)
        PrintString(VT100_C_STATUS);
    PrintString(s);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);

    PositionCursor(txtp);
}

// display a message in the status line
void editDisplayMsg(unsigned char *msg)
{
    SCursor(0, VHeight + 1);
    if (Option.ColourCode)
        PrintString(VT100_C_ERROR);
    PrintString("\033[7m");
    MX470Cursor(0, (VRes / gui_font_height) * gui_font_height - gui_font_height);
    PrintString((char *)msg);
    MX470PutS((char *)msg, BLACK, RED);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);
    PrintString("\033[0m");
    PrintString("\033[K");      // clear to the end of the line on a vt100 emulator
    MX470Display(CLEAR_TO_EOL); // clear to the end of line on the MX470 display only
    PositionCursor(txtp);
    drawstatusline = true;
}

// save the program in the editing buffer into the program memory
void SaveToProgMemory(void)
{
    SaveProgramToFlash(EdBuff, true);
    ClearProgram(true);
    StartEditPoint = (unsigned char *)(edy + cury); // record out position in case the editor is invoked again
    StartEditChar = edx + curx;
    // bugfix for when the edit point is a space
    // the space could be at the end of a line which will be trimmed in SaveProgramToFlash() leaving StartEditChar referring to something not there
    // this is not a serious issue so fix the bug in the MX470 only because it has plenty of flash
    while (StartEditChar > 0 && txtp > EdBuff && *(--txtp) == ' ')
    {
        StartEditChar--;
    }
}

// get an input string from the user and save into inpbuf
void GetInputString(unsigned char *prompt)
{
    int i;
    SCursor(0, VHeight + 1);
    PrintString((char *)prompt);
    MX470Cursor(0, (VRes / gui_font_height) * gui_font_height - gui_font_height);
    MX470PutS((char *)prompt, gui_fcolour, gui_bcolour);
    for (i = 0; i < VWidth - strlen((char *)prompt); i++)
    {
        SSputchar(' ', 1);
        MX470PutC(' ');
    }
    SCursor(strlen((char *)prompt), VHeight + 1);
    MX470Cursor(strlen((char *)prompt) * gui_font_width, (VRes / gui_font_height) * gui_font_height - gui_font_height);
    int len = 0;
    int maxlen = STRINGSIZE - 1;
    while (1)
    { // get the input
        unsigned char ch = MMgetchar();
        if (ch == '\r')
            break;
        if (ch == 0xb3 || ch == F3 || ch == ESC)
        {
            if (len < maxlen)
                inpbuf[len++] = ch;
            break;
        } // return if it is SHIFT-F3, F3 or ESC
        if (ch == '\b')
        {
            if (len > 0)
            {
                len--;
                PrintString("\b \b");                         // erase on the screen
                MX470PutS("\b \b", gui_fcolour, gui_bcolour); // erase on the MX470 display
            }
            continue;
        }
        if (isprint(ch))
        {
            if (len < maxlen)
            {
                inpbuf[len++] = ch;
                SSputchar(ch, 1); // echo the char
                MX470PutC(ch);    // echo the char on the MX470 display
            }
        }
    }
    inpbuf[len] = 0; // terminate the input string
    PrintFunctKeys(EDIT);
    PositionCursor(txtp);
}

// scroll up the video screen
void Scroll(void)
{
    edy++;
    SCursor(0, VHeight);
    PrintString("\033[J\033[99B\n"); // clear to end of screen, move to the end of the screen and force a scroll of one line
    MX470Cursor(0, VHeight * gui_font_height);
    MX470Scroll(gui_font_height);
    SCursor(0, VHeight);
    curx = 0;
    cury = VHeight - 1;
    PrintFunctKeys(EDIT);
    printLine(VHeight - 1 + edy);
    PositionCursor(txtp);
    while (getConsole() != -1)
        ; // consume any keystrokes accumulated while redrawing the screen
}

// scroll down the video screen
void ScrollDown(void)
{
    SCursor(0, VHeight);   // go to the end of the editing area
    PrintString("\033[J"); // clear to end of screen
    edy--;
    SCursor(0, 0);
    PrintString("\033M"); // scroll window down one line
    MX470Scroll(-gui_font_height);
    printLine(edy);
    PrintFunctKeys(EDIT);
    PositionCursor(txtp);

    while (getConsole() != -1)
        routinechecks();
    ; // consume any keystrokes accumulated while redrawing the screen
}

#endif
/*  @endcond */
