/*
 * drivers/spi_lcd/spi_lcd_mem332.c — real implementations for the
 * MEM332 buffered SPI-LCD family (ILI9488WBUFF, ST7796SPBUFF,
 * ILI9341BUFF, ST7796SBUFF, ILI9488BUFF, ILI9488PBUFF, ST7789C).
 *
 * These display controllers run an 8-bit RGB332 shadow framebuffer
 * in RAM. Linked only on ports with the RAM budget for that buffer
 * (currently rp2350 PicoMite). Other ports link
 * spi_lcd_mem332_stub.c.
 *
 * Extracted from drivers/spi_lcd/spi_lcd.c's
 * `#if HAL_PORT_HAS_PICOMITE && defined(rp2350)` blocks.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_spi_lcd_mem332.h"
#include "pico/multicore.h"
#include "hardware/dma.h"

/* DefineRegionSPI / DrawBitmapSPISCR / ScrollStart /
 * low_x/low_y/high_x/high_y / silly_low/silly_high / RGB565 are all
 * declared in SPI-LCD.h, Draw.h, or Hardware_Includes.h above. */

int hal_spi_lcd_mem332_match_option(unsigned char *name) {
    if (checkstring(name, (unsigned char *)"ST7796SPBUFF"))    return ST7796SPBUFF;
    if (checkstring(name, (unsigned char *)"ST7796SBUFF"))     return ST7796SBUFF;
    if (checkstring(name, (unsigned char *)"ILI9341BUFF"))     return ILI9341BUFF;
    if (checkstring(name, (unsigned char *)"ILI9488BUFF"))     return ILI9488BUFF;
    if (checkstring(name, (unsigned char *)"ILI9488PBUFF"))    return ILI9488PBUFF;
    if (checkstring(name, (unsigned char *)"ILI9488WBUFF"))    return ILI9488WBUFF;
    if (checkstring(name, (unsigned char *)"ST7789_320BUFF"))  return ST7789C;
    return 0;
}

void hal_spi_lcd_mem332_init_display(int display_type) {
    (void)display_type;
    /* MEM332-specific init runs through the existing
     * Draw* function-pointer dispatch in spi_lcd.c restorepanel().
     * Nothing extra needed here for now. */
}

void hal_spi_lcd_mem332_init_luts(void) {
    init_RGB332_to_RGB565_LUT();
}

unsigned char hal_spi_lcd_read_response_byte(void) {
    unsigned char q;
    /* MEM332 path uses the DMA-multi reader; first byte is a dummy. */
    lcd_rcvr_byte_multi(&q, 1);
    lcd_rcvr_byte_multi(&q, 1);
    return q;
}

void ScrollLCDMEM332(int lines){
    if(lines==0)return;
	if(Option.DISPLAY_ORIENTATION==PORTRAIT){
		int t = ScrollStart;
		if(lines >= 0) {
			DrawRectangle(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the line to be scrolled off
			multicore_fifo_push_blocking(6);
			multicore_fifo_push_blocking((uint32_t)low_x | (high_x<<16));
			multicore_fifo_push_blocking((uint32_t)low_y | (high_y<<16));
			low_x=silly_low; high_y=silly_high; low_y=silly_low; high_x=silly_high;
			while(lines--) {
				if(++t >= VRes) t = 0;
			}
		} else {
			while(lines++) {
				if(--t < 0) t = VRes - 1;
			}
		}
		multicore_fifo_push_blocking(7);
		multicore_fifo_push_blocking(t);
        DrawRectangle(0, VRes-lines, HRes - 1, VRes - 1, gui_bcolour); // erase the lines to be scrolled off
		ScrollStart = t;
	} else {
		unsigned char *screen=(unsigned char *)(ScreenBuffer);
		if(lines >= 0) {
			DrawRectangle(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the line to be scrolled off
			unsigned char *p=screen+lines*HRes;
			memmove(screen,p,(VRes-lines)*HRes);
        	DrawRectangle(0, VRes-lines, HRes - 1, VRes - 1, gui_bcolour); // erase the lines to be scrolled off
		} else {
			lines=-lines;
			unsigned char *p=screen+lines*HRes;
			memmove(p,screen,(VRes-lines)*HRes);
        	DrawRectangle(0, 0, HRes - 1, lines - 1, gui_bcolour); // erase the lines introduced at the top
		}
	}
}

void DrawBufferMEM332(int x1, int y1, int x2, int y2, unsigned char* p) {
    int x,y; 
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
    for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
			c.rgbbytes[0]=*p++; //this order swaps the bytes to match the .BMP file
			c.rgbbytes[1]=*p++;
			c.rgbbytes[2]=*p++;
			c.rgbbytes[3]=0;
			DrawPixel(x,y,c.rgb);
		}
	}
}

void DrawBlitBufferMEM332(int x1, int y1, int x2, int y2, unsigned char* p) {
	unsigned char *screen=(unsigned char *)(ScreenBuffer);
    for(int y=y1;y<=y2;y++){
		unsigned char *buff=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes;
		for(int x=x1;x<=x2;x++){
			buff[x]=*p++;
		}
	}
    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
}
void ReadBufferMEM332(int x1, int y1, int x2, int y2, unsigned char* buff) {
	unsigned char *screen=(unsigned char *)(ScreenBuffer);
    int x,y,t;
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
    for(y=y1;y<=y2;y++){
		unsigned char *p=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes;
    	for(x=x1;x<=x2;x++){
			*buff++=((p[x] & 3)<<6);
			*buff++=((p[x] & 0b11100)<<3);
			*buff++=(p[x] & 0b11100000);
       	}
    }
}

void ReadBlitBufferMEM332(int x1, int y1, int x2, int y2, unsigned char* buff) {
	unsigned char *screen=(unsigned char *)(ScreenBuffer);
	int t;
    if(x1 < 0) x1 = 0;
    if(x1 >= HRes) x1 = HRes - 1;
    if(x2 < 0) x2 = 0;
    if(x2 >= HRes) x2 = HRes - 1;
    if(y1 < 0) y1 = 0;
    if(y1 >= VRes) y1 = VRes - 1;
    if(y2 < 0) y2 = 0;
    if(y2 >= VRes) y2 = VRes - 1;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
    for(int y=y1;y<=y2;y++){
		unsigned char *p=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes;
	    for(int x=x1;x<=x2;x++){
			*buff++=p[x];
		}
	}
}
void DrawRectangleMEM332(int x1, int y1, int x2, int y2, int c){
	int t;
	unsigned char *screen=(unsigned char *)(ScreenBuffer);
	unsigned char colour=RGB332(c);
	if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
	if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
	if(x1 < 0) x1 = 0;
	if(x1 >= HRes) x1 = HRes - 1;
	if(x2 < 0) x2 = 0;
	if(x2 >= HRes) x2 = HRes - 1;
	if(y1 < 0) y1 = 0;
	if(y1 >= VRes) y1 = VRes - 1;
	if(y2 < 0) y2 = 0;
	if(y2 >= VRes) y2 = VRes - 1;
    if(y1<low_y)low_y=y1;
    if(y2>high_y)high_y=y2;
    if(x1<low_x)low_x=x1;
    if(x2>high_x)high_x=x2;
    for(int y=y1;y<=y2;y++){
		unsigned char *p=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes;
		p+=x1;
		memset(p,colour,x2-x1+1);
    }
}
void DrawBitmapMEM332(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m, x, y;
	unsigned char f=RGB332(fc);
	unsigned char b=RGB332(bc);
	unsigned char *screen=(unsigned char *)(ScreenBuffer);
    if(x1>=HRes || y1>=VRes || x1+width*scale<0 || y1+height*scale<0)return;
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    x=x1 + k * scale + m ;
                    y=y1 + i * scale + j ;
                    if(y<low_y)low_y=y;
                    if(y>high_y)high_y=y;
                    if(x<low_x)low_x=x;
                    if(x>high_x)high_x=x;
					unsigned char *p=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes+x;
                    if(x >= 0 && x < HRes && y >= 0 && y < VRes) {  // if the coordinates are valid
                        if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
							*p=f;
                        } else {
                            if(bc>=0){
                                *p=b;
                            } 
                        }
                   }
                }
            }
        }
    }
}
extern uint16_t __not_in_flash_func(RGB565)(uint32_t c);
static uint32_t RGB332_LUT[256]={0};
static uint32_t remap_LUT[256]={0};
static uint8_t tlen=2;
void init_RGB332_to_RGB565_LUT(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t r = (i >> 5) & 0x07;   // 3-bit red
        uint8_t g = (i >> 2) & 0x07;   // 3-bit green
        uint8_t b = i & 0x03;          // 2-bit blue

        // Stretch components via perceptual LUTs
        static const uint8_t RED_LUT[8]   = { 0, 4, 8, 12, 16, 20, 26, 31 };
        static const uint8_t GREEN_LUT[8] = { 0, 9, 18, 27, 36, 45, 54, 63 };
        static const uint8_t BLUE_LUT[4]  = { 0, 10, 21, 31 };

        uint8_t r5 = RED_LUT[r];
        uint8_t g6 = GREEN_LUT[g];
        uint8_t b5 = BLUE_LUT[b];

        // Your bit order mapping:
        RGB332_LUT[i] = remap_LUT[i]= RGB565(r5<<19 | g6<<10 | b5<<3);
    }
}

void init_RGB332_to_RGB888_LUT(void) {
    for (int i = 0; i < 256; ++i) {
        uint8_t r = (i >> 5) & 0x07; // 3 bits
        uint8_t g = (i >> 2) & 0x07; // 3 bits
        uint8_t b = i & 0x03;        // 2 bits

        uint8_t r8 = (r << 5) | (r << 2) | (r >> 1);  // scale to 8 bits
        uint8_t g8 = (g << 5) | (g << 2) | (g >> 1);  // scale to 8 bits
        uint8_t b8 = (b << 6) | (b << 4) | (b << 2) | b; // scale to 8 bits

        RGB332_LUT[i] = remap_LUT[i]= (b8 << 16) | (g8 << 8) | r8;
    }
	tlen=3;
}
void fun_map(void){
    if(!(DISPLAY_TYPE>=NEXTGEN))error("Invalid for this display");
	int cl=getint(ep,0,255);
    switch(DISPLAY_TYPE){
        case SCREENMODE1:
        case SCREENMODE4:
            error("Invalid for Mode");
        break;
        case SCREENMODE2:
        case SCREENMODE3:
            if(cl>15)error("Mode has 16 colours - 0 to 15");
            targ=T_INT;
            iret=((cl & 0b1000)<<20) | ((cl & 0b110)<<13) | ((cl & 0b1)<<7);
            break;
        case SCREENMODE5:
            targ=T_INT;
            iret=((cl & 0b11100000)<<16) | ((cl & 0b00011100)<<11) | ((cl & 0b11)<<6);
            break;
    }
}
void cmd_map(void){
	unsigned char *p;
//    if(Option.CPU_Speed==126000)error("CPUSPEED >= 252000 for colour mapping");
    if(!(DISPLAY_TYPE>=NEXTGEN))error("Invalid for this display");
    if((p=checkstring(cmdline, (unsigned char *)"RESET"))) {
		if(Option.DISPLAY_TYPE==ILI9488BUFF  || Option.DISPLAY_TYPE == ILI9488PBUFF)init_RGB332_to_RGB888_LUT();
		else init_RGB332_to_RGB565_LUT();
    } else if((p=checkstring(cmdline, (unsigned char *)"SET"))) {
         for(int i=0;i<256;i++)RGB332_LUT[i]=remap_LUT[i];
		 low_x=0;low_y=0;high_x=HRes-1;high_y=VRes-1;
    } else {
        union colourmap {
			char rgbbytes[4];
			unsigned int rgb;
		} c;
	int cl = getinteger(cmdline);
		while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
		if(!*cmdline) error("Invalid syntax");
		++cmdline;
		if(!*cmdline) error("Invalid syntax");
		c.rgb=getColour((char *)cmdline,0);
		if(Option.DISPLAY_TYPE==ILI9488BUFF  || Option.DISPLAY_TYPE == ILI9488PBUFF){
			remap_LUT[cl]=(c.rgbbytes[0] << 16) | (c.rgbbytes[1] << 8) | c.rgbbytes[2];
		} else {
			remap_LUT[cl]=RGB565(c.rgb);
		}
    }
}
void copybuffertoscreen(unsigned char *s,int low_x,int low_y,int high_x,int high_y){
	if(RGB332_LUT[255]==0){
		if(Option.DISPLAY_TYPE==ILI9488BUFF  || Option.DISPLAY_TYPE == ILI9488PBUFF)init_RGB332_to_RGB888_LUT();
		else init_RGB332_to_RGB565_LUT();
	}
    int t = high_y - low_y;                                                    // get the distance between the top and bottom
    low_y = (low_y + ScrollStart) % VRes;
    high_y = low_y + t;                                                    // and set y2 to the same
    if(high_y >= VRes) {                                                // if the box splits over the frame buffer boundary
		DefineRegionSPI(low_x, low_y, high_x, VRes-1, 1);
		if(PinDef[Option.LCD_CLK].mode & SPI0SCK){
			for(int y=low_y;y<VRes;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi0,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		} else {
			for(int y=low_y;y<VRes;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi1,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		}
		DefineRegionSPI(low_x, 0, high_x, high_y-VRes, 1);
		if(PinDef[Option.LCD_CLK].mode & SPI0SCK){
			for(int y=0;y<=high_y-VRes;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi0,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		} else {
			for(int y=0;y<=high_y-VRes;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi1,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		}
    } else {
		DefineRegionSPI(low_x, low_y, high_x, high_y, 1);
		if(PinDef[Option.LCD_CLK].mode & SPI0SCK){
			for(int y=low_y;y<=high_y;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi0,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		} else {
			for(int y=low_y;y<=high_y;y++){
				unsigned char *p=(unsigned char *)(ScreenBuffer) + (y * HRes + low_x);
				for(int x=low_x;x<=high_x;x++)spi_write_fast(spi1,(unsigned char *)&RGB332_LUT[*p++],tlen);
			}
		}
	}
	if(PinDef[Option.LCD_CLK].mode & SPI0SCK)spi_finish(spi0);
	else spi_finish(spi1);
	ClearCS(Option.LCD_CS); 
}
