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
//	{ (unsigned char *)"MM.FontWidth",   T_FNA | T_INT,		0, fun_mmcharwidth 	},
//	{ (unsigned char *)"MM.FontHeight",  T_FNA | T_INT,		0, fun_mmcharheight },
// the format is:
//    TEXT      	TYPE                P  FUNCTION TO CALL
// where type is T_NA, T_FUN, T_FNA or T_OPER augmented by the types T_STR and/or T_NBR
// and P is the precedence (which is only used for operators)

#endif
#if !defined(INCLUDE_COMMAND_TABLE) && !defined(INCLUDE_TOKEN_TABLE)
  #ifndef DRAW_H_INCL
    #define RGB(red, green, blue) (unsigned int) (((red & 0b11111111) << 16) | ((green  & 0b11111111) << 8) | (blue & 0b11111111))
    #define swap(a, b) {int t = a; a = b; b = t;}

    #define DRAW_H_INCL
    #define WHITE               RGB(255,  255,  255) //0b1111
    #define YELLOW              RGB(255,  255,    0) //0b1110
    #define LILAC               RGB(255,  128,  255) //0b1101
    #define BROWN               RGB(255,  128,    0) //0b1100
    #define FUCHSIA             RGB(255,  64,   255) //0b1011
    #define RUST                RGB(255,  64,     0) //0b1010
    #define MAGENTA             RGB(255,  0,    255) //0b1001
    #define RED                 RGB(255,  0,      0) //0b1000
    #define CYAN                RGB(0,    255,  255) //0b0111
    #define GREEN               RGB(0,    255,    0) //0b0110
    #define CERULEAN            RGB(0,    128,  255) //0b0101 
    #define MIDGREEN            RGB(0,    128,    0) //0b0100
    #define COBALT              RGB(0,    64,   255) //0b0011
    #define MYRTLE              RGB(0,    64,     0) //0b0010
    #define BLUE                RGB(0,    0,    255) //0b0001
    #define BLACK               RGB(0,    0,      0) //0b0000
    #define BROWN               RGB(255,  128,    0)
    #define GRAY                RGB(128,  128,    128)
    #define LITEGRAY            RGB(210,  210,    210)
    #define ORANGE            	RGB(0xff,	0xA5,	0)
	#define PINK				RGB(0xFF,	0xA0,	0xAB)
	#define GOLD				RGB(0xFF,	0xD7,	0x00)
	#define SALMON				RGB(0xFA,	0x80,	0x72)
	#define BEIGE				RGB(0xF5,	0xF5,	0xDC)

    #define JUSTIFY_LEFT        0
    #define JUSTIFY_CENTER      1
    #define JUSTIFY_RIGHT       2

    #define JUSTIFY_TOP         0
    #define JUSTIFY_MIDDLE      1
    #define JUSTIFY_BOTTOM      2

    #define ORIENT_NORMAL       0
    #define ORIENT_VERT         1
    #define ORIENT_INVERTED     2
    #define ORIENT_CCW90DEG     3
    #define ORIENT_CW90DEG      4

 extern short gui_font;
    extern short gui_font_width, gui_font_height;

    extern int gui_fcolour;
    extern int gui_bcolour;

    extern short DisplayHRes, DisplayVRes;        // resolution of the display
    extern short HRes, VRes;                      // the programming charteristics of the display
    extern short low_y, high_y, low_x, high_x;

#define LANDSCAPE       1
#define PORTRAIT        2
#define RLANDSCAPE      3
#define RPORTRAIT       4
#define DISPLAY_LANDSCAPE   (Option.DISPLAY_ORIENTATION & 1)
#define TOUCH_NOT_CALIBRATED    -999999
#define RESET_COMMAND       9999                                // indicates that the reset was caused by the RESET command
#define WATCHDOG_TIMEOUT    9998                                // reset caused by the watchdog timer
#define PIN_RESTART         9997                                // reset caused by entering 0 at the PIN prompt
#define RESTART_NOAUTORUN   9996                                // reset required after changing the LCD or touch config
#define PinRead(a)  gpio_get(PinDef[a].GPno)
#define LCDMaxV 480
#define LCDMaxH 800
#define VMaxV 480
#define VMaxH 640
extern int GetJustification(char *p, int *jh, int *jv, int *jo);
extern void cmd_guiBasic(void);
extern void DrawLine(int x1, int y1, int x2, int y2, int w, int c);
extern void DrawBox(int x1, int y1, int x2, int y2, int w, int c, int fill);
extern void DrawRBox(int x1, int y1, int x2, int y2, int radius, int c, int fill);
extern void DrawCircle(int x, int y, int radius, int w, int c, int fill, MMFLOAT aspect);
extern void ClearScreen(int c);
extern void SetFont(int fnt);
extern void ResetDisplay(void);
extern int GetFontWidth(int fnt);
extern int GetFontHeight(int fnt);
//extern char * spritebuffptr[MAXBLITBUF];                                  //Buffer pointers for the BLIT command
extern int rgb(int r, int g, int b);
extern void (*DrawRectangle)(int x1, int y1, int x2, int y2, int c);
extern void (*DrawBitmap)(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void (*ScrollLCD) (int lines);
extern void (*DrawBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*DrawBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
extern void (*ReadBLITBuffer)(int x1, int y1, int x2, int y2, unsigned char *c);
void DrawPixel16(int x, int y, int c);
void DrawRectangle16(int x1, int y1, int x2, int y2, int c);
void DrawBitmap16(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
void ScrollLCD16(int lines);
void DrawBuffer16(int x1, int y1, int x2, int y2, unsigned char *p);
void DrawBuffer16Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
void ReadBuffer16(int x1, int y1, int x2, int y2, unsigned char *c);
void ReadBuffer16Fast(int x1, int y1, int x2, int y2, unsigned char *c);
void DrawPixelNormal(int x, int y, int c) ;
void ReadBuffer2(int x1, int y1, int x2, int y2, unsigned char *c);
void copyframetoscreen(uint8_t *s,int xstart, int xend, int ystart, int yend, int odd);
void restorepanel(void);
#define FONT_BUILTIN_NBR     8
#define FONT_TABLE_SIZE      16
extern void (*DrawPixel)(int x1, int y1, int c);
extern void (*ReadBufferFast)(int x1, int y1, int x2, int y2, unsigned char *c);
extern uint8_t sprite_transparent;
#ifndef PICOMITEVGA
    extern void DrawRectangleUser(int x1, int y1, int x2, int y2, int c);
    extern void DrawBitmapUser(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
#endif
extern void DisplayPutC(char c);
extern void GUIPrintString(int x, int y, int fnt, int jh, int jv, int jo, int fc, int bc, char *str);
extern void DrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c, int fill) ;
extern void cmd_guiMX170(void);
extern void initFonts(void);
extern void ShowCursor(int show);
extern unsigned char *FontTable[16];
extern short CurrentX, CurrentY;
extern int PrintPixelMode;
extern char CMM1;
extern int ScreenSize;
extern char LCDAttrib;
extern uint32_t remap[];

typedef struct SVD {
	FLOAT3D x;
	FLOAT3D y;
	FLOAT3D z;
}s_vector;
typedef struct t_quaternion {
	FLOAT3D w;
	FLOAT3D x;
	FLOAT3D y;
	FLOAT3D z;
	FLOAT3D m;
}s_quaternion;
struct D3D {
	s_quaternion* q_vertices;//array of original vertices
	s_quaternion* r_vertices; //array of rotated vertices
	s_quaternion* q_centroids;//array of original vertices
	s_quaternion* r_centroids; //array of rotated vertices
	s_vector* normals;
	uint8_t* facecount; //number of vertices for each face
	uint16_t* facestart; //index into the face_x_vert table of the start of a given face
	int32_t* fill; //fill colours
	int32_t* line; //line colours
	int32_t* colours;
	uint16_t* face_x_vert; //list of vertices for each face
	uint8_t* flags;
	FLOAT3D* dots;
	FLOAT3D* depth;
	FLOAT3D distance;
	FLOAT3D ambient;
	int* depthindex;
	s_vector light;
	s_vector current;
	short tot_face_x_vert;
	short xmin, xmax, ymin, ymax;
	uint16_t nv;	//number of vertices to describe the object
	uint16_t nf; // number of faces in the object
	uint8_t dummy;
	uint8_t vmax;  // maximum verticies for any face on the object
	uint8_t camera; // camera to use for the object
	uint8_t nonormals;
};
typedef struct {
	FLOAT3D x;
	FLOAT3D y;
	FLOAT3D z;
	FLOAT3D viewplane;
	FLOAT3D panx;
	FLOAT3D pany;
}s_camera;
extern struct D3D* struct3d[MAX3D + 1];
extern s_camera camera[MAXCAM + 1];
struct spritebuffer {
    char* spritebuffptr; //points to the sprite image, set to NULL if not in use
    short w;
    short h;
    char* blitstoreptr; //points to the stored background, set to NULL if not in use
    char  collisions[MAXCOLLISIONS + 1]; //set to NULL if not in use, otherwise contains current collisions
    int64_t master; //bitmask of which sprites are copies
    uint64_t lastcollisions;
    short x; //set to 1000 if not in use
    short y;
    short next_x; //set to 1000 if not in use
    short next_y;
    signed char layer; //defaults to 1 if not specified. If zero then scrolls with background
    signed char mymaster; //number of master if this is a copy
    char rotation;
    char active;
    char edges;
};
struct blitbuffer {
    char* blitbuffptr; //points to the sprite image, set to NULL if not in use
    short w;
    short h;
};
extern struct spritebuffer spritebuff[MAXBLITBUF+1];
extern struct blitbuffer blitbuff[MAXBLITBUF+1];
//extern int layer_in_use[MAXLAYER + 1];
extern void closeall3d(void);
extern void closeframebuffer(char layer);
extern void closeallsprites(void);
extern char* COLLISIONInterrupt;
extern bool CollisionFound;
extern void InitDisplayVirtual(void);
extern void ConfigDisplayVirtual(unsigned char *p);
extern void merge(uint8_t colour);
extern void blitmerge (int x0, int y0, int w, int h, uint8_t colour);
extern bool mergerunning;
extern uint32_t mergetimer;
extern int RGB121map[16];
#endif
#endif
/*  @endcond */
