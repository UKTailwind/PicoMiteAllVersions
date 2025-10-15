/*
 * @cond
 * The following section will be excluded from the documentation.
 */
/* *********************************************************************************************************************
PicoMite MMBasic

Draw.h

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
/* All other tokens (keywords, functions, operators) should be inserted in this table
 * Format:
 *    TEXT      TYPE                P  FUNCTION TO CALL
 * where type is T_NA, T_FUN, T_FNA or T_OPER augmented by the types T_STR and/or T_NBR
 * and P is the precedence (which is only used for operators)
 */

/* Examples (currently commented out):
 * { (unsigned char *)"MM.FontWidth",   T_FNA | T_INT, 0, fun_mmcharwidth  },
 * { (unsigned char *)"MM.FontHeight",  T_FNA | T_INT, 0, fun_mmcharheight },
 */
#endif

/* ============================================================================
 * Main header content
 * ============================================================================ */
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
#ifndef DRAW_H_INCL
#define DRAW_H_INCL

/* ============================================================================
 * Color and utility macros
 * ============================================================================ */
#define RGB(red, green, blue) \
    (unsigned int)(((red & 0b11111111) << 16) | ((green & 0b11111111) << 8) | (blue & 0b11111111))

#define swap(a, b)   \
    {                \
        int t = (a); \
        (a) = (b);   \
        (b) = t;     \
    }

/* ============================================================================
 * Predefined color constants
 * ============================================================================ */
#define WHITE RGB(255, 255, 255)  // 0b1111
#define YELLOW RGB(255, 255, 0)   // 0b1110
#define LILAC RGB(255, 128, 255)  // 0b1101
#define BROWN RGB(255, 128, 0)    // 0b1100
#define FUCHSIA RGB(255, 64, 255) // 0b1011
#define RUST RGB(255, 64, 0)      // 0b1010
#define MAGENTA RGB(255, 0, 255)  // 0b1001
#define RED RGB(255, 0, 0)        // 0b1000
#define CYAN RGB(0, 255, 255)     // 0b0111
#define GREEN RGB(0, 255, 0)      // 0b0110
#define CERULEAN RGB(0, 128, 255) // 0b0101
#define MIDGREEN RGB(0, 128, 0)   // 0b0100
#define COBALT RGB(0, 64, 255)    // 0b0011
#define MYRTLE RGB(0, 64, 0)      // 0b0010
#define BLUE RGB(0, 0, 255)       // 0b0001
#define BLACK RGB(0, 0, 0)        // 0b0000

/* Additional colors */
#define GRAY RGB(128, 128, 128)
#define LITEGRAY RGB(210, 210, 210)
#define ORANGE RGB(0xFF, 0xA5, 0x00)
#define PINK RGB(0xFF, 0xA0, 0xAB)
#define GOLD RGB(0xFF, 0xD7, 0x00)
#define SALMON RGB(0xFA, 0x80, 0x72)
#define BEIGE RGB(0xF5, 0xF5, 0xDC)

/* ============================================================================
 * Text justification constants
 * ============================================================================ */
#define JUSTIFY_LEFT 0
#define JUSTIFY_CENTER 1
#define JUSTIFY_RIGHT 2

#define JUSTIFY_TOP 0
#define JUSTIFY_MIDDLE 1
#define JUSTIFY_BOTTOM 2

/* ============================================================================
 * Text orientation constants
 * ============================================================================ */
#define ORIENT_NORMAL 0
#define ORIENT_VERT 1
#define ORIENT_INVERTED 2
#define ORIENT_CCW90DEG 3
#define ORIENT_CW90DEG 4

/* ============================================================================
 * Display orientation constants
 * ============================================================================ */
#define LANDSCAPE 1
#define PORTRAIT 2
#define RLANDSCAPE 3
#define RPORTRAIT 4

#define DISPLAY_LANDSCAPE (Option.DISPLAY_ORIENTATION & 1)

/* ============================================================================
 * Display mode and resolution constants
 * ============================================================================ */
#define VMaxV 480
#define VMaxH 640

#define DISPLAY_RGB121 0
#define DISPLAY_RGB332 1

/* ============================================================================
 * Dithering mode constants
 * ============================================================================ */
#define DITHER_FLOYD_STEINBERG 0
#define DITHER_ATKINSON 1

/* ============================================================================
 * Touch and reset constants
 * ============================================================================ */
#define TOUCH_NOT_CALIBRATED -999999
#define RESET_COMMAND 9999     // Reset caused by the RESET command
#define WATCHDOG_TIMEOUT 9998  // Reset caused by the watchdog timer
#define PIN_RESTART 9997       // Reset caused by entering 0 at the PIN prompt
#define RESTART_NOAUTORUN 9996 // Reset required after changing the LCD or touch config

/* ============================================================================
 * Font constants
 * ============================================================================ */
#define FONT_BUILTIN_NBR 9
#define FONT_TABLE_SIZE 16

/* ============================================================================
 * Hardware access macros
 * ============================================================================ */
#define PinRead(a) gpio_get(PinDef[a].GPno)

/* ============================================================================
 * Type definitions - 3D vector
 * ============================================================================ */
typedef struct SVD
{
    FLOAT3D x;
    FLOAT3D y;
    FLOAT3D z;
} s_vector;

/* ============================================================================
 * Type definitions - Quaternion
 * ============================================================================ */
typedef struct t_quaternion
{
    FLOAT3D w;
    FLOAT3D x;
    FLOAT3D y;
    FLOAT3D z;
    FLOAT3D m;
} s_quaternion;

/* ============================================================================
 * Type definitions - 3D object structure
 * ============================================================================ */
struct D3D
{
    s_quaternion *q_vertices;  // Array of original vertices
    s_quaternion *r_vertices;  // Array of rotated vertices
    s_quaternion *q_centroids; // Array of original centroids
    s_quaternion *r_centroids; // Array of rotated centroids
    s_vector *normals;         // Surface normals
    uint8_t *facecount;        // Number of vertices for each face
    uint16_t *facestart;       // Index into the face_x_vert table of the start of a given face
    int32_t *fill;             // Fill colors
    int32_t *line;             // Line colors
    int32_t *colours;          // Additional colors
    uint16_t *face_x_vert;     // List of vertices for each face
    uint8_t *flags;            // Face flags
    FLOAT3D *dots;             // Dot products
    FLOAT3D *depth;            // Depth values
    FLOAT3D distance;          // Camera distance
    FLOAT3D ambient;           // Ambient lighting
    int *depthindex;           // Depth sorting index
    s_vector light;            // Light direction
    s_vector current;          // Current vector
    short tot_face_x_vert;     // Total face-vertex count
    short xmin, xmax;          // Bounding box X
    short ymin, ymax;          // Bounding box Y
    uint16_t nv;               // Number of vertices
    uint16_t nf;               // Number of faces
    uint8_t dummy;             // Padding
    uint8_t vmax;              // Maximum vertices for any face
    uint8_t camera;            // Camera to use for the object
    uint8_t nonormals;         // Flag for normal calculation
    uint8_t depthmode;         // Depth sorting mode
};

/* ============================================================================
 * Type definitions - Camera structure
 * ============================================================================ */
typedef struct
{
    FLOAT3D x;
    FLOAT3D y;
    FLOAT3D z;
    FLOAT3D viewplane;
    FLOAT3D panx;
    FLOAT3D pany;
} s_camera;

/* ============================================================================
 * Type definitions - Sprite buffer
 * ============================================================================ */
struct spritebuffer
{
    char *spritebuffptr;                // Points to the sprite image, NULL if not in use
    short w;                            // Width
    short h;                            // Height
    char *blitstoreptr;                 // Points to stored background, NULL if not in use
    char collisions[MAXCOLLISIONS + 1]; // Current collisions, NULL if not in use
    int64_t master;                     // Bitmask of which sprites are copies
    uint64_t lastcollisions;            // Previous collision state
    short x;                            // Current X position (1000 if not in use)
    short y;                            // Current Y position
    short next_x;                       // Next X position (1000 if not in use)
    short next_y;                       // Next Y position
    signed char layer;                  // Layer (defaults to 1, 0 scrolls with background)
    signed char mymaster;               // Master sprite number if this is a copy
    char rotation;                      // Rotation state
    char active;                        // Active flag
    char edges;                         // Edge collision flags
};

/* ============================================================================
 * Type definitions - Blit buffer
 * ============================================================================ */
struct blitbuffer
{
    char *blitbuffptr; // Points to the sprite image, NULL if not in use
    short w;           // Width
    short h;           // Height
};

/* ============================================================================
 * External variables - GUI and font configuration
 * ============================================================================ */
extern short gui_font;
extern short gui_font_width, gui_font_height;
extern int gui_fcolour;
extern int gui_bcolour;
extern unsigned char *FontTable[16];

/* ============================================================================
 * External variables - Display resolution
 * ============================================================================ */
extern short DisplayHRes, DisplayVRes; // Resolution of the display
extern short HRes, VRes;               // Programming characteristics of the display
extern volatile short low_y, high_y, low_x, high_x;

/* ============================================================================
 * External variables - Display state
 * ============================================================================ */
extern short CurrentX, CurrentY;
extern int PrintPixelMode;
extern char CMM1;
extern int ScreenSize;
extern char LCDAttrib;

/* ============================================================================
 * External variables - 3D and camera
 * ============================================================================ */
extern struct D3D *struct3d[MAX3D + 1];
extern s_camera camera[MAXCAM + 1];

/* ============================================================================
 * External variables - Sprite and blit buffers
 * ============================================================================ */
extern struct spritebuffer spritebuff[MAXBLITBUF + 1];
extern struct blitbuffer blitbuff[MAXBLITBUF + 1];

/* ============================================================================
 * External variables - Collision detection
 * ============================================================================ */
extern char *COLLISIONInterrupt;
extern bool CollisionFound;

/* ============================================================================
 * External variables - Transparency and color mapping
 * ============================================================================ */
extern uint8_t sprite_transparent;
extern int RGB121map[16];

/* ============================================================================
 * External variables - Merge operations
 * ============================================================================ */
extern bool mergerunning;
extern uint32_t mergetimer;

/* ============================================================================
 * Function pointer declarations - Core drawing functions
 * ============================================================================ */
extern void (*DrawPixel)(int x1, int y1, int c);
extern void (*DrawRectangle)(int x1, int y1, int x2, int y2, int c);
extern void (*DrawBitmap)(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void (*ScrollLCD)(int lines);
extern void (*DrawBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*DrawBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char *c);

/* ============================================================================
 * Function declarations - Basic drawing primitives
 * ============================================================================ */
void DrawLine(int x1, int y1, int x2, int y2, int w, int c);
void DrawBox(int x1, int y1, int x2, int y2, int w, int c, int fill);
void DrawRBox(int x1, int y1, int x2, int y2, int radius, int c, int fill);
void DrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect);
void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill);

/* ============================================================================
 * Function declarations - Pixel operations (16-bit)
 * ============================================================================ */
void DrawPixel16(int x, int y, int c);
void DrawPixelNormal(int x, int y, int c);
void DrawRectangle16(int x1, int y1, int x2, int y2, int c);
void DrawBitmap16(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);

/* ============================================================================
 * Function declarations - Buffer operations
 * ============================================================================ */
void DrawBuffer16(int x1, int y1, int x2, int y2, unsigned char *p);
void DrawBuffer16Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
void ReadBuffer16(int x1, int y1, int x2, int y2, unsigned char *c);
void ReadBuffer16Fast(int x1, int y1, int x2, int y2, unsigned char *c);
void ReadBuffer2(int x1, int y1, int x2, int y2, unsigned char *c);

/* ============================================================================
 * Function declarations - Screen operations
 * ============================================================================ */
void ClearScreen(int c);
void ScrollLCD16(int lines);
void copyframetoscreen(uint8_t *s, int xstart, int xend, int ystart, int yend, int odd);
void copybuffertoscreen(unsigned char *s, int low_x, int low_y, int high_x, int high_y);
void restorepanel(void);

/* ============================================================================
 * Function declarations - Display control
 * ============================================================================ */
void ResetDisplay(void);
void DisplayPutC(char c);
void ShowCursor(int show);
void CheckDisplay(void);
void setmode(int mode, bool clear);

/* ============================================================================
 * Function declarations - Font operations
 * ============================================================================ */
void SetFont(int fnt);
int GetFontWidth(int fnt);
int GetFontHeight(int fnt);
void initFonts(void);

/* ============================================================================
 * Function declarations - Text rendering
 * ============================================================================ */
void GUIPrintString(int x, int y, int fnt, int jh, int jv, int jo, int fc, int bc, char *str);
int GetJustification(char *p, int *jh, int *jv, int *jo);

/* ============================================================================
 * Function declarations - Color operations
 * ============================================================================ */
int rgb(int r, int g, int b);
int getColour(char *c, int minus);

/* ============================================================================
 * Function declarations - Image loading
 * ============================================================================ */
int ReadAndDisplayBMP(int fnbr, int display_mode, int dither_mode, int img_x_offset,
                      int img_y_offset, int x_display, int y_display);

/* ============================================================================
 * Function declarations - GUI commands
 * ============================================================================ */
void cmd_guiBasic(void);
void cmd_guiMX170(void);

/* ============================================================================
 * Function declarations - 3D operations
 * ============================================================================ */
void closeall3d(void);

/* ============================================================================
 * Function declarations - Sprite operations
 * ============================================================================ */
void closeallsprites(void);
void closeframebuffer(char layer);

/* ============================================================================
 * Function declarations - Virtual display
 * ============================================================================ */
void InitDisplayVirtual(void);
void ConfigDisplayVirtual(unsigned char *p);

/* ============================================================================
 * Function declarations - Merge operations
 * ============================================================================ */
void merge(uint8_t colour);
void blitmerge(int x0, int y0, int w, int h, uint8_t colour);

/* ============================================================================
 * Function declarations - User-defined drawing (non-VGA)
 * ============================================================================ */
#ifndef PICOMITEVGA
void DrawRectangleUser(int x1, int y1, int x2, int y2, int c);
void DrawBitmapUser(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
#endif

#endif /* DRAW_H_INCL */
#endif /* !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE) */

/*  @endcond */