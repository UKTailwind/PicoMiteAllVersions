/***********************************************************************************************************************
PicoMite MMBasic

SPI-LCD.c

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

#include <stdarg.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

const uint32_t maskSPI111[32]={0x00000001,0x00000003,0x00000007,0x0000000f,0x0000001f,0x0000003f,0x0000007f,0x000000ff,
                         0x000001ff,0x000003ff,0x000007ff,0x00000fff,0x00001fff,0x00003fff,0x00007fff,0x0000ffff,
                         0x0001ffff,0x0003ffff,0x0007ffff,0x000fffff,0x001fffff,0x003fffff,0x007fffff,0x00ffffff,
                         0x01ffffff,0x03ffffff,0x07ffffff,0x0fffffff,0x1fffffff,0x3fffffff,0x7fffffff,0xffffffff};
const uint32_t rmaskSPI111[32]={0x80000000,0xC0000000,0xe0000000,0xf0000000,0xf8000000,0xfc000000,0xfe000000,0xff000000,
                          0xff800000,0xffC00000,0xffe00000,0xfff00000,0xfff80000,0xfffc0000,0xfffe0000,0xffff0000,
                          0xffff8000,0xffffC000,0xffffe000,0xfffff000,0xfffff800,0xfffffc00,0xfffffe00,0xffffff00,
                          0xffffff80,0xffffffC0,0xffffffe0,0xfffffff0,0xfffffff8,0xfffffffc,0xfffffffe,0xffffffff};


uint32_t *VideoBufRed = NULL;										// Image buffer for red
uint32_t *VideoBufGrn = NULL;										// Image buffer for green
uint32_t *VideoBufBlu = NULL;										// Image buffer for blue
volatile int lcnt;
volatile int VCount;												// counter for the number of lines in a frame
volatile int VState;												// the state of the state machine
volatile int showbuffer = 0;										// the next counter table (initialise in initVideo() below)
void SPI111hline(int x0, int x1, int y, int con, int which);
inline void plot(int x, int y, int con) ;

void SPI111init(void){
//    if(Option.DISPLAY_TYPE !=SPI111) return;
    DrawRectangle = DrawRectangleSPI111;
    DrawBitmap = DrawBitmapSPI111;
    ScrollLCD = ScrollSPI111;
    DrawBuffer=DrawBufferSPI111;
    ReadBuffer=ReadBufferSPI111;
}
void DrawRectangleSPI111(int x1, int y1, int x2, int y2, int c){
    int i, t;
    int col=0;
    if(c & 0xFF0000)col =1;
    if(c & 0xFF00)col +=2;
    if(c & 0xFF)col +=4;
    // make sure the coordinates are kept within the display area
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
    for(i=y1;i<=y2;i++){
        SPI111hline(x1,x2,i,col,0);
    }

}

void DrawBitmapSPI111(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m;
    unsigned char fcol,bcol;
    int vertCoord, horizCoord, XStart, XEnd, YEnd;

    // adjust when part of the bitmap is outside the displayable coordinates
    vertCoord = y1; if(y1 < 0) y1 = 0;                                 // the y coord is above the top of the screen
    XStart = x1; if(XStart < 0) XStart = 0;                            // the x coord is to the left of the left marginn
    XEnd = x1 + (width * scale) - 1; if(XEnd >= HRes) XEnd = HRes - 1; // the width of the bitmap will extend beyond the right margin
    YEnd = y1 + (height * scale) - 1; if(YEnd >= VRes) YEnd = VRes - 1;// the height of the bitmap will extend beyond the bottom margin
    fcol=0; bcol=0;
    if(fc & 0xFF0000)fcol =1;
    if(fc & 0xFF00)fcol +=2;
    if(fc & 0xFF)fcol +=4;
    if(bc!=-1){
        if(bc & 0xFF0000)bcol =1;
        if(bc & 0xFF00)bcol +=2;
        if(bc & 0xFF)bcol +=4;
    } else bcol=0xff;

    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            if(vertCoord++ < 0) continue;                           // we are above the top of the screen
            if(vertCoord > VRes) return;                            // we have extended beyond the bottom of the screen
            horizCoord = x1;
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    if(horizCoord++ < 0) continue;                  // we have not reached the left margin
                    if(horizCoord > HRes) continue;                 // we are beyond the right margin
                    if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
                        plot(horizCoord-1,vertCoord-1,fcol);
                    } else {
                        if(bcol!=0xff)plot(horizCoord-1,vertCoord-1,bcol);
                    }

                }
            }
        }
    }

}

void SPI111hline(int x0, int x1, int y, int con, int which) { //draw a horizontal line
    uint32_t w1, xx1, w0, xx0, x, xn, i;
    const uint32_t a[]={0xFFFFFFFF,0x7FFFFFFF,0x3FFFFFFF,0x1FFFFFFF,0xFFFFFFF,0x7FFFFFF,0x3FFFFFF,0x1FFFFFF,
                        0xFFFFFF,0x7FFFFF,0x3FFFFF,0x1FFFFF,0xFFFFF,0x7FFFF,0x3FFFF,0x1FFFF,
                        0xFFFF,0x7FFF,0x3FFF,0x1FFF,0xFFF,0x7FF,0x3FF,0x1FF,
                        0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01};
    const uint32_t b[]={0x80000000,0xC0000000,0xe0000000,0xf0000000,0xf8000000,0xfc000000,0xfe000000,0xff000000,
                        0xff800000,0xffC00000,0xffe00000,0xfff00000,0xfff80000,0xfffc0000,0xfffe0000,0xffff0000,
                        0xffff8000,0xffffC000,0xffffe000,0xfffff000,0xfffff800,0xfffffc00,0xfffffe00,0xffffff00,
                        0xffffff80,0xffffffC0,0xffffffe0,0xfffffff0,0xfffffff8,0xfffffffc,0xfffffffe,0xffffffff};
    uint32_t *br, *bg, *bb;
    br=VideoBufRed;
    bg=VideoBufGrn;
    bb=VideoBufBlu;
    
    w0 = y * (SPIints_per_line) + x0/32;
    xx0 = (x0 & 0x1F);
    w1 = y * (SPIints_per_line) + x1/32;
    xx1 = (x1 & 0x1F);
    if(w1==w0){ //special case both inside same word
        x=(a[xx0] & b[xx1]);
        xn=~x;
        if(con & 1) br[w0] |= x; else  br[w0] &= xn;                       // turn on the red pixel
        if(con & 2) bg[w0] |= x; else  bg[w0] &= xn;                       // turn on the green pixel
        if(con & 4) bb[w0] |= x; else  bb[w0] &= xn;                        // turn on the blue pixel
    } else {
        if(w1-w0>1){ //first deal with full words
            for(i=w0+1;i<w1;i++){
            // draw the pixel
                br[i] = 0;                         // turn off the red pixels
                bg[i] = 0;                         // turn off the green pixels
                bb[i] = 0;                         // turn off the blue pixels
                if(con & 1) br[i] = 0xFFFFFFFF;          // turn on the red pixels
                if(con & 2) bg[i] = 0xFFFFFFFF;          // turn on the green pixels
                if(con & 4) bb[i] = 0xFFFFFFFF;          // turn on the blue pixels
            }
        }
        x=~a[xx0];
        br[w0] &= x;                        // turn off the red pixel
        bg[w0] &= x;                      // turn off the green pixel
        bb[w0] &= x;                       // turn off the blue pixel
        x=~x;
        if(con & 1) br[w0] |= x;                         // turn on the red pixel
        if(con & 2) bg[w0] |= x;                       // turn on the green pixel
        if(con & 4) bb[w0] |= x;                        // turn on the blue pixel
        x=~b[xx1];
        br[w1] &= x;                        // turn off the red pixel
        bg[w1] &= x;                      // turn off the green pixel
        bb[w1] &= x;                       // turn off the blue pixel
        x=~x;
        if(con & 1) br[w1] |= x;                         // turn on the red pixel
        if(con & 2) bg[w1] |= x;                       // turn on the green pixel
        if(con & 4) bb[w1] |= x;                        // turn on the blue pixel            
    }
}

void DrawBufferSPI111(int x1, int y1, int x2, int y2, unsigned char* p) {
    int t,x,y,w, xd, c1;
    unsigned int xx, xxn;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    // make sure the coordinates are kept within the display area
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    for(y=y1;y<=y2;y++){
        c1=y * (SPIints_per_line);
        for(x=x1;x<=x2;x++){
            xx = (0x80000000>>(x & 0x1f));
            xxn=~xx;
            xd=x>>5;
            w = c1 + xd;
            // draw the pixel
            if(y>=0 && y<VRes && x>=0 && x<HRes){
                if(*p++ > 0x80){
                    bb[w] |= xx;
                } else {
                    bb[w] &= xxn; 
                }// turn on the blue pixel
                if(*p++ > 0x80){
                     bg[w] |= xx;
                } else {
                    bg[w] &= xxn; 
                }// turn on the green pixel
                if(*p++ > 0x80){
                     br[w] |= xx;
                } else {
                    br[w] &= xxn; 
                }// turn on the red pixel
            } else p+=3;
        }
   }            
}
void ReadBufferSPI111(int x1, int y1, int x2, int y2, unsigned char* p) {
    int t,x,y, w, xx, xd, c1;
    // make sure the coordinates are kept within the display area
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    for(y=y1;y<=y2;y++){
        c1=y * (SPIints_per_line);
        for(x=x1;x<=x2;x++){
            xx=(0x80000000>>(x & 0x1f));
            xd=x>>5;
            w = c1 + xd;
            if(y>=0 && y<VRes && x>=0 && x<HRes){
            *p++ = ((VideoBufBlu[w]) & xx) ? 0xff : 0;
            *p++ = ((VideoBufGrn[w]) & xx) ? 0xff : 0;
            *p++ = ((VideoBufRed[w]) & xx) ? 0xff : 0;
            } else p+=3;
        }
    }        
}
void plot121(int x, int y, int con) {
    int w, xx,xxn, xd;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    xx = (0x80000000>>(x & 0x1f));
    xd=x>>5;
    w = y * (SPIints_per_line) + xd;
    // draw the pixel
    xxn = ~xx;
    if(con & 1) br[w] |= xx;  else  br[w] &= xxn;                    // turn on the red pixel
    if(con & 6) bg[w] |= xx;  else  bg[w] &= xxn;                       // turn on the green pixel
    if(con & 8) bb[w] |= xx;  else  bb[w] &= xxn;                         // turn on the blue pixel
}

inline void plot(int x, int y, int con) {
    int w, xx,xxn, xd;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    xx = (0x80000000>>(x & 0x1f));
    xd=x>>5;
    w = y * (SPIints_per_line) + xd;
    // draw the pixel
    xxn = ~xx;
    if(con & 1) br[w] |= xx;  else  br[w] &= xxn;                    // turn on the red pixel
    if(con & 2) bg[w] |= xx;  else  bg[w] &= xxn;                       // turn on the green pixel
    if(con & 4) bb[w] |= xx;  else  bb[w] &= xxn;                         // turn on the blue pixel
}

void copyframetoscreen(uint8_t *s,int xstart, int xend, int ystart, int yend, int odd){
	int c;
	int i=(xend-xstart+1)*(yend-ystart+1);
	int x=xstart,y=ystart;
	if(odd){
		c=(*s & 0xF0)>>4;
		plot121(x,y,c);
		if(x==xend){
			x=xstart;
			y++;
		} else x++;


		s++;
		i--;
	}
	while(i>0){
		c=*s & 0xF;
		plot121(x,y,c);
		if(x==xend){
			x=xstart;
			y++;
		} else x++;
		if(i>1){
			c=(*s & 0xF0)>>4;
			plot121(x,y,c);
			if(x==xend){
				x=xstart;
				y++;
			} else x++;
			s++;
			i-=2;
		}
	}
}

void ScrollSPI111sideways(int lines){
    int i, j;
    uint32_t k, l, prevr=0, prevg=0, prevb=0;
	uint32_t *pdr,*pdg,*pdb;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    int m1=0x0;
    int m2=0x0;
    if(lines > 0) {
        for(j=0; j<VRes;j++){
            pdr = br + (SPIints_per_line * j);
            pdg = bg + (SPIints_per_line * j);
            pdb = bb + (SPIints_per_line * j);
            for(i=0;i<=HRes/32;i++){
                l = *pdr;
                k=l & maskSPI111[lines-1];
                l = l >> lines;
                l |= prevr;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdr++ = l;
                prevr=k<<(32-lines);
                
                l = *pdg;
                k= l & maskSPI111[lines-1];
                l = l >> lines;
                l |= prevg;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdg++ = l;
                prevg=k<<(32-lines);

                l = *pdb;
                k=l & maskSPI111[lines-1];
                l = l >>lines;
                l |= prevb;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdb++ = l;
                prevb=k<<(32-lines);
             }
        }
    } else {
        int m=-lines;
        for(j=0; j<VRes;j++){
            pdr = br + HRes/32 + (SPIints_per_line * j);
            pdg = bb + HRes/32 + (SPIints_per_line * j);
            pdb = bg + HRes/32 + (SPIints_per_line * j);
            for(i=HRes/32;i>=0;i--){
                l = *pdr;
                k=l & rmaskSPI111[m];
                l = l << m;
                l |= prevr;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdr-- = l;
                prevr=k>>(32-m);
                
                l = *pdg;
                k= l & rmaskSPI111[m];
                l = l << m;
                l |= prevg;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdg-- = l;
                prevg=k>>(32-m);

                l = *pdb;
                k=l & rmaskSPI111[m];
                l = l << m;
                l |= prevb;
                if(i==0)l &= m1;
                if(i==HRes/32)l &= m2;
                *pdb-- = l;
                prevb=k>>(32-m);
             }
        }
    }
}
void ScrollSPI111(int lines) {
    int i,amount;
	uint32_t *pdr,*pdg,*pdb;
   	uint32_t *psr,*psg,*psb;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    if(lines >= 0) {
        amount = SPIints_per_line * (VRes - lines); 
		pdr = br;
       	psr = pdr + SPIints_per_line * lines;
		pdg = bg;
       	psg = pdg + SPIints_per_line * lines;
		pdb = bb;
       	psb = pdb + SPIints_per_line * lines;
       	for(i=0; i < amount; i+=8) {
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
        }
        DrawRectangle(0, VRes-lines, HRes - 1, VRes - 1, gui_bcolour); // erase the lines introduced at the bottom
    } else {
        lines = -lines;
        amount = SPIints_per_line * (VRes - lines); 
		psr = br + SPIints_per_line * (VRes  - lines);
       	pdr = psr + SPIints_per_line * lines;
		psg = bg + SPIints_per_line * (VRes  - lines);
       	pdg = psg + SPIints_per_line * lines;
		psb = bb + SPIints_per_line * (VRes  - lines);
       	pdb = psb + SPIints_per_line * lines;
       	for(i=0; i < amount; i+=8){
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
        }
        DrawRectangle(0, 0, HRes - 1, lines-1 , gui_bcolour); // erase the lines introduced at the top
    }
}
void ScrollSPI111vertical(int lines, int blank) {
    int i,amount;
	uint32_t *pdr,*pdg,*pdb;
   	uint32_t *psr,*psg,*psb;
    uint32_t *br, *bg, *bb;
    br=VideoBufRed, bg=VideoBufGrn, bb=VideoBufBlu;
    if(lines >= 0) {
        amount = SPIints_per_line * (VRes - lines); 
		pdr = br;
       	psr = pdr + SPIints_per_line * lines;
		pdg = bg;
       	psg = pdg + SPIints_per_line * lines;
		pdb = bb;
       	psb = pdb + SPIints_per_line * lines;
       	for(i=0; i < amount; i+=8) {
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdr++ = *psr++;	                                    // scroll up
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdg++ = *psg++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
            *pdb++ = *psb++;	
        }
    } else {
        lines = -lines;
        amount = SPIints_per_line * (VRes - lines); 
		psr = br + SPIints_per_line * (VRes  - lines);
       	pdr = psr + SPIints_per_line * lines;
		psg = bg + SPIints_per_line * (VRes  - lines);
       	pdg = psg + SPIints_per_line * lines;
		psb = bb + SPIints_per_line * (VRes  - lines);
       	pdb = psb + SPIints_per_line * lines;
       	for(i=0; i < amount; i+=8){
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdr = *--psr;	                                    // scroll down
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdg = *--psg;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
             *--pdb = *--psb;	                                    
        }
    }
}
