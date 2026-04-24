#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#if defined(PICOMITEMIN)
static void WriteColorDisabled(unsigned int c)
{
    (void)c;
}

void (*WriteColor)(unsigned int c) = WriteColorDisabled;

void InitSSD1963(void)
{
}

void InitDisplaySSD(void)
{
}

void ConfigDisplaySSD(unsigned char *p)
{
    (void)p;
    error("SSD1963 displays not enabled");
}

void SetBacklightSSD1963(int intensity)
{
    (void)intensity;
}

void SetTearingCfg(int state, int mode)
{
    (void)state;
    (void)mode;
}

void ScrollSSD1963(int lines)
{
    (void)lines;
}

void WriteComand(int cmd)
{
    (void)cmd;
}

void WriteData(int data)
{
    (void)data;
}

void WriteData16bit(int data)
{
    (void)data;
}

void Write16bitCommand(int cmd)
{
    (void)cmd;
}

void SetAreaSSD1963(int x1, int y1, int x2, int y2)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
}

void SetAreaILI9341(int xstart, int ystart, int xend, int yend, int rw)
{
    (void)xstart;
    (void)ystart;
    (void)xend;
    (void)yend;
    (void)rw;
}

void SetAreaIPS_4_16(int xstart, int ystart, int xend, int yend, int rw)
{
    (void)xstart;
    (void)ystart;
    (void)xend;
    (void)yend;
    (void)rw;
}

void WriteCmdDataIPS_4_16(int cmd, int n, int data)
{
    (void)cmd;
    (void)n;
    (void)data;
}

void DrawBitmapSSD1963(int x1, int y1, int width, int height, int scale, int fg, int bg, unsigned char *bitmap)
{
    (void)x1;
    (void)y1;
    (void)width;
    (void)height;
    (void)scale;
    (void)fg;
    (void)bg;
    (void)bitmap;
}

void DrawBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void DrawBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void DrawRectangleSSD1963(int x1, int y1, int x2, int y2, int c)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)c;
}

void ReadBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void ReadBLITBufferSSD1963(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void DrawBitmap320(int x1, int y1, int width, int height, int scale, int fg, int bg, unsigned char *bitmap)
{
    (void)x1;
    (void)y1;
    (void)width;
    (void)height;
    (void)scale;
    (void)fg;
    (void)bg;
    (void)bitmap;
}

void DrawBuffer320(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void DrawBLITBuffer320(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void DrawRectangle320(int x1, int y1, int x2, int y2, int c)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)c;
}

void ReadBuffer320(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}

void ReadBLITBuffer320(int x1, int y1, int x2, int y2, unsigned char *p)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)p;
}
#endif