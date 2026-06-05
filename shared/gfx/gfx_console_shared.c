/*
 * gfx_console_shared.c -- Console text path shared between the device and
 * host (--sim) builds. Moved out of Draw.c so the simulator can reuse the
 * exact same cursor-tracking, wrap, scroll, font-lookup and rasterisation
 * logic the device uses. The only per-build difference is the backing
 * pixel output (DrawBitmap / DrawRectangle / ClearScreen / ScrollLCD) —
 * which is already plumbed through function pointers on the device and
 * through equivalent host stubs in the --sim build.
 *
 * Do not diverge the control-flow from Draw.c's original: edits here
 * should land equally in both builds, since that is the whole point of
 * sharing this file.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

__attribute__((weak)) void port_display_render_begin(void) {}
__attribute__((weak)) void port_display_render_end(void) {}

/******************************************************************************************
 Print a char on the LCD display
 Any characters not in the font will print as a space.
 The char is printed at the current location defined by CurrentX and CurrentY
*****************************************************************************************/
void GUIPrintChar(int fnt, int fc, int bc, char c, int orientation) {
    unsigned char *p, *fp, *np = NULL, *AllocatedMemory = NULL;
    int BitNumber, BitPos, x, y, newx, newy, modx, mody, scale = fnt & 0b1111;
    int height, width;
    if (PrintPixelMode == 1) bc = -1;
    if (PrintPixelMode == 2) {
        int s = bc;
        bc = fc;
        fc = s;
    }
    if (PrintPixelMode == 5) {
        fc = bc;
        bc = -1;
    }
    // to get the +, - and = chars for font 6 we fudge them by scaling up font 1
    if ((fnt & 0xf0) == 0x50 && (c == '-' || c == '+' || c == '=')) {
        fp = (unsigned char *)FontTable[0];
        scale = scale * 4;
    } else
        fp = (unsigned char *)FontTable[fnt >> 4];

    height = fp[1];
    width = fp[0];
    modx = mody = 0;
    if (orientation > ORIENT_VERT) {
        AllocatedMemory = np = GetMemory(width * height);
        if (orientation == ORIENT_INVERTED) {
            modx -= width * scale - 1;
            mody -= height * scale - 1;
        } else if (orientation == ORIENT_CCW90DEG) {
            mody -= width * scale;
        } else if (orientation == ORIENT_CW90DEG) {
            modx -= height * scale - 1;
        }
    }

    if (c >= fp[2] && c < fp[2] + fp[3]) {
        p = fp + 4 + (int)(((c - fp[2]) * height * width) / 8);

        if (orientation > ORIENT_VERT) { // non-standard orientation
            if (orientation == ORIENT_INVERTED) {
                for (y = 0; y < height; y++) {
                    newy = height - y - 1;
                    for (x = 0; x < width; x++) {
                        newx = width - x - 1;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1) {
                            BitNumber = ((newy * width) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            } else if (orientation == ORIENT_CCW90DEG) {
                for (y = 0; y < height; y++) {
                    newx = y;
                    for (x = 0; x < width; x++) {
                        newy = width - x - 1;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1) {
                            BitNumber = ((newy * height) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            } else if (orientation == ORIENT_CW90DEG) {
                for (y = 0; y < height; y++) {
                    newx = height - y - 1;
                    for (x = 0; x < width; x++) {
                        newy = x;
                        if ((p[((y * width) + x) / 8] >> (((height * width) - ((y * width) + x) - 1) % 8)) & 1) {
                            BitNumber = ((newy * height) + newx);
                            BitPos = 128 >> (BitNumber % 8);
                            np[BitNumber / 8] |= BitPos;
                        }
                    }
                }
            }
        } else
            np = p;

        if (orientation < ORIENT_CCW90DEG)
            DrawBitmap(CurrentX + modx, CurrentY + mody, width, height, scale, fc, bc, np);
        else
            DrawBitmap(CurrentX + modx, CurrentY + mody, height, width, scale, fc, bc, np);
    } else {
        if (orientation < ORIENT_CCW90DEG)
            DrawRectangle(CurrentX + modx, CurrentY + mody, CurrentX + modx + (width * scale), CurrentY + mody + (height * scale), bc);
        else
            DrawRectangle(CurrentX + modx, CurrentY + mody, CurrentX + modx + (height * scale), CurrentY + mody + (width * scale), bc);
    }

    // to get the . and degree symbols for font 6 we draw a small circle
    if ((fnt & 0xf0) == 0x50) {
        if (orientation > ORIENT_VERT) {
            if (orientation == ORIENT_INVERTED) {
                if (c == '.') DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + 7 * scale, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60) DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + (height * scale) - 9 * scale, 6 * scale, 2 * scale, fc, -1, 1.0);
            } else if (orientation == ORIENT_CCW90DEG) {
                if (c == '.') DrawCircle(CurrentX + modx + (height * scale) - 7 * scale, CurrentY + mody + (width * scale) / 2, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60) DrawCircle(CurrentX + modx + 9 * scale, CurrentY + mody + (width * scale) / 2, 6 * scale, 2 * scale, fc, -1, 1.0);
            } else if (orientation == ORIENT_CW90DEG) {
                if (c == '.') DrawCircle(CurrentX + modx + 7 * scale, CurrentY + mody + (width * scale) / 2, 4 * scale, 0, fc, fc, 1.0);
                if (c == 0x60) DrawCircle(CurrentX + modx + (height * scale) - 9 * scale, CurrentY + mody + (width * scale) / 2, 6 * scale, 2 * scale, fc, -1, 1.0);
            }

        } else {
            if (c == '.') DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + (height * scale) - 7 * scale, 4 * scale, 0, fc, fc, 1.0);
            if (c == 0x60) DrawCircle(CurrentX + modx + (width * scale) / 2, CurrentY + mody + 9 * scale, 6 * scale, 2 * scale, fc, -1, 1.0);
        }
    }

    if (orientation == ORIENT_NORMAL)
        CurrentX += width * scale;
    else if (orientation == ORIENT_VERT)
        CurrentY += height * scale;
    else if (orientation == ORIENT_INVERTED)
        CurrentX -= width * scale;
    else if (orientation == ORIENT_CCW90DEG)
        CurrentY -= width * scale;
    else if (orientation == ORIENT_CW90DEG)
        CurrentY += width * scale;
    if (orientation > ORIENT_VERT) FreeMemory(AllocatedMemory);
}

/******************************************************************************************
 Print a char on the LCD display (SSD1963 and in landscape only).  It handles control chars
 such as newline and will wrap at the end of the line and scroll the display if necessary.

 The char is printed at the current location defined by CurrentX and CurrentY
 *****************************************************************************************/
void DisplayPutC(char c) {

    if (!Option.DISPLAY_CONSOLE) return;
    // if it is printable and it is going to take us off the right hand end of the screen do a CRLF
    if (c >= FontTable[gui_font >> 4][2] && c < FontTable[gui_font >> 4][2] + FontTable[gui_font >> 4][3]) {
        if (CurrentX + gui_font_width > HRes) {
            DisplayPutC('\r');
            DisplayPutC('\n');
        }
    }

    // handle the standard control chars
    switch (c) {
    case '\b':
        CurrentX -= gui_font_width;
        //            if (CurrentX < 0) CurrentX = 0;
        if (CurrentX < 0) {              //Go to end of previous line
            CurrentY -= gui_font_height; //Go up one line
            if (CurrentY < 0) CurrentY = 0;
            CurrentX = (Option.Width - 1) * gui_font_width; //go to last character
        }
        return;
    case '\r':
        CurrentX = 0;
        return;
    case '\n':
        if (CurrentY + 2 * gui_font_height > VRes) {
            if (Option.NoScroll && Option.DISPLAY_CONSOLE) {
                ClearScreen(gui_bcolour);
                CurrentX = 0;
                CurrentY = 0;
            } else {
                ScrollLCD(gui_font_height);
            }
        } else {
            CurrentY += gui_font_height;
        }
        return;
    case '\t':
        do {
            DisplayPutC(' ');
        } while ((CurrentX / gui_font_width) % Option.Tab);
        return;
    }
    port_display_render_begin();
    GUIPrintChar(gui_font, gui_fcolour, gui_bcolour, c, ORIENT_NORMAL); // print it
    port_display_render_end();
    routinechecks();
}

/******************************************************************************************
 Toggle the blinking cursor underline at (CurrentX, CurrentY). Called from
 MMgetchar's input-wait loop; visibility flips based on CursorTimer, which
 is incremented every millisecond by the device timer interrupt (or by the
 host sim's wall-clock sync).
 *****************************************************************************************/
void ShowCursor(int show) {
    static int visible = false;
    int newstate;
    if (!Option.DISPLAY_CONSOLE) return;
    newstate = ((CursorTimer <= CURSOR_ON) && show); // what should be the state of the cursor?
    if (visible == newstate) return;                 // we can skip the rest if the cursor is already in the correct state
    visible = newstate;                              // draw the cursor BELOW the font
    DrawLine(CurrentX, CurrentY + gui_font_height - (gui_font_height <= 12 ? 1 : 2), CurrentX + gui_font_width - 1, CurrentY + gui_font_height - (gui_font_height <= 12 ? 1 : 2), (gui_font_height <= 12 ? 1 : 2), visible ? gui_fcolour : (DISPLAY_TYPE == SCREENMODE1 ? 0 : gui_bcolour));
}
