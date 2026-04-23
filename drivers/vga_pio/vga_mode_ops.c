/*
 * drivers/vga_pio/vga_mode_ops.c — VGA-only MMBasic commands + helpers
 * extracted from Draw.c.
 *
 * Includes:
 *   - cmd_colourmap   : remap of 16-colour RGB121 palette
 *   - fun_map / cmd_map : SCREENMODE-specific colour quantisation
 *   - settiles / cmd_mode / setmode : screen-mode dispatch and init
 *   - cmd_tile / fun_getscanline / cmd_tile_write helpers
 *
 * Linked only into PICOMITEVGA targets (VGA + HDMI). HDMI vs VGA
 * internal dispatch kept as local `#ifdef HDMI` — port files are
 * permitted local target-macro ifdefs.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_vga_ops.h"
#include "hal/hal_keyboard.h"

/* File-scope globals defined in Draw.c / PicoMite.c. */
extern volatile int QVgaScanLine;
extern const int CMM1map[16];
extern uint8_t remap[256];
extern uint32_t remap555[256];
extern uint32_t remap332[256];
extern uint16_t remap256[256];
extern int ScreenSize;

/* fun_getscanline — BASIC function "GetScanLine". Lifted from Draw.c's
 * PICOMITEVGA block. */
#ifdef HDMI
extern volatile int32_t v_scanline;
void fun_getscanline(void) {
    if (Option.CPU_Speed == Freq720P) {
        iret = v_scanline - 30;
        if (iret < 0) iret += 750;
        targ = T_INT;
    } else if (Option.CPU_Speed == Freq480P) {
        iret = v_scanline - 20;
        if (iret < 0) iret += 500;
        targ = T_INT;
    } else if (Option.CPU_Speed == FreqXGA) {
        iret = v_scanline - 38;
        if (iret < 0) iret += 806;
        targ = T_INT;
    } else if (Option.CPU_Speed == FreqSVGA) {
        iret = v_scanline - 25;
        if (iret < 0) iret += 625;
        targ = T_INT;
    } else if (Option.CPU_Speed == Freq252P || Option.CPU_Speed == Freq378P) {
        iret = v_scanline - 45;
        if (iret < 0) iret += 525;
        targ = T_INT;
    } else if (Option.CPU_Speed == Freq848) {
        iret = v_scanline - 37;
        if (iret < 0) iret += 517;
        targ = T_INT;
    }
}
#else
void fun_getscanline(void) {
    iret = QVgaScanLine;
    targ = T_INT;
}
#endif

#ifdef PICOMITEVGA
void cmd_colourmap(void){
    long long int *cptr=NULL, *fptr=NULL;
    MMFLOAT *cfptr=NULL, *ffptr=NULL;
    int nf,n,i;
    int map[16];
    getargs(&cmdline,5,(unsigned char *)",");
    memcpy((void *)map,(void *)RGB121map,16*sizeof(int));
    if(!(argc==3 || argc==5))error("Argument count");
    n=parsenumberarray(argv[0],&cfptr,&cptr,1,1,NULL,true);
    if(argc==5){ //user defined mapping
        MMFLOAT* a3float = NULL;
        int64_t* a3int = NULL;
        if(parsenumberarray(argv[4],&a3float,&a3int,3,1,NULL,true)!=16)error("Array size not 16 elements");
        if(a3int!=NULL){
            for(i=0;i<16;i++) {
                map[i]=a3int[i];
                if(map[i]<0 || map[i]>0xFFFFFF)error("Invalid colour");
            }
        } else {
            for(i=0;i<16;i++) {
                map[i]=a3float[i];
                if(map[i]<0 || map[i]>0xFFFFFF)error("Invalid colour");
            }
        }
    }
    nf=parsenumberarray(argv[2],&ffptr,&fptr,1,1,NULL,false);
    if(nf!=n)error("Array size mismatch %, %",n,nf);
    for(int i=0;i<n;i++){
        int in=(cptr == NULL ? (int)cfptr[i] : cptr[i]);
        if(in>=16)error("Input range error on element %",i);
        if(fptr==NULL)ffptr[i]=map[in];
        else fptr[i]=map[in];
    }
}
#endif

#ifdef PICOMITEVGA
void fun_map(void){
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
#ifndef HDMI
void cmd_map(void){
	unsigned char *p;
//    if(Option.CPU_Speed==126000)error("CPUSPEED >= 252000 for colour mapping");
    if(!(DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE3 ))error("Invalid for this screen mode");
    if((p=checkstring(cmdline, (unsigned char *)"RESET"))) {
        while(QVgaScanLine!=0){}
        for(int i=0;i<16;i++)remap[i]=RGB121map[i];
        for(int i=0;i<16;i++)map16[i]=RGB121(remap[i]);
     } else if((p=checkstring(cmdline, (unsigned char *)"MAXIMITE"))) {
        while(QVgaScanLine!=0){}
        for(int i=0;i<16;i++)remap[i]=CMM1map[i];
        for(int i=0;i<16;i++)map16[i]=RGB121(remap[i]);
    } else if((p=checkstring(cmdline, (unsigned char *)"SET"))) {
        while(QVgaScanLine!=0){}
        for(int i=0;i<16;i++)map16[i]=RGB121(remap[i]);
    } else {
        static bool first=true;
    	int cl = getinteger(cmdline);
		while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
		if(!*cmdline) error("Invalid syntax");
		++cmdline;
		if(!*cmdline) error("Invalid syntax");
		int col=getColour((char *)cmdline,0);
        if(first){
            for(int i=0;i<16;i++)remap[i]=RGB121map[i];
            first=false;
        }
		remap[cl]=col;
    }
}

void cmd_tile(void){
    unsigned char *tp;
    uint32_t bcolour=0xFFFFFFFF,fcolour=0xFFFFFFFF;
    int xlen=1,ylen=1;
    if(DISPLAY_TYPE!=SCREENMODE1)error("Invalid for this screen mode");
    if(checkstring(cmdline,(unsigned char *)"RESET")){
        for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
                tilefcols[y*X_TILE+x]=RGB121pack(gui_fcolour);
                tilebcols[y*X_TILE+x]=RGB121pack(gui_bcolour);
            }
        }
    } else if((tp=checkstring(cmdline,(unsigned char *)"HEIGHT"))){
        if(!(WriteBuf==DisplayBuf))error("Not available when write is set to a buffer");
        ytileheight=getint(tp,12,VRes);
        Y_TILE=VRes/ytileheight;
        if(VRes % ytileheight)Y_TILE++;
        ClearScreen(Option.DefaultBC);
    } else {
        getargs(&cmdline, 11, (unsigned char *)",");
        if(!(DISPLAY_TYPE==SCREENMODE1))return;
        if(argc<5)error("Syntax");
        int x=getint(argv[0],0,X_TILE);
        int y=getint(argv[2],0,Y_TILE);
        int tilebcolour, tilefcolour ;
        if(*argv[4]){
            tilefcolour = getColour((char *)argv[4], 0);
            fcolour = RGB121pack(tilefcolour);
        }
        if(argc>=7 && *argv[6]){
            tilebcolour = getColour((char *)argv[6], 0);
            bcolour = RGB121pack(tilebcolour);
        }
        if(argc>=9 && *argv[8]){
            xlen=getint(argv[8],1,X_TILE-x);
        }
        if(argc>=11 && *argv[10]){
            ylen=getint(argv[10],1,Y_TILE-y);
        }
        for(int xp=x;xp<x+xlen;xp++){
            for(int yp=y;yp<y+ylen;yp++){
                if(fcolour!=0xFFFFFFFF) tilefcols[yp*X_TILE+xp]=(uint16_t)fcolour;
                if(bcolour!=0xFFFFFFFF) tilebcols[yp*X_TILE+xp]=(uint16_t)bcolour;
            }
        }
    }
}
#else
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
void DrawRectangle555(int x1, int y1, int x2, int y2, int c){
    int x,y,t;
    uint16_t col=((c & 0xf8)>>3) | ((c& 0xf800)>>6) | ((c & 0xf80000)>>9);
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
    for(y=y1;y<=y2;y++){
        uint16_t *p=(uint16_t *)((uint8_t *)(WriteBuf+((y*HRes+x1)*2)));
        for(x=x1;x<=x2;x++){
            *p++=col;
       }
    }
}
void DrawBitmap555(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m, x, y;
//    unsigned char mask;
    if(x1>=HRes || y1>=VRes || x1+width*scale<0 || y1+height*scale<0)return;
    uint16_t fcolour = RGB555(fc);
    uint16_t bcolour = RGB555(bc);
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    x=x1 + k * scale + m ;
                    y=y1 + i * scale + j ;
                    if(x >= 0 && x < HRes && y >= 0 && y < VRes) {  // if the coordinates are valid
	                    uint16_t *p=(uint16_t *)(((uint32_t) WriteBuf)+(y*(HRes<<1))+(x<<1));
                        if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
                            *p=fcolour;
                        } else {
                            if(bc>=0){
                                *p=bcolour;
                            }
                        }
                   }
                }
            }
        }
    }
}

void DrawBuffer555(int x1, int y1, int x2, int y2, unsigned char *p){
	int x,y, t;
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
    uint16_t fcolour;
    uint16_t *pp;
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
	for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
	        c.rgbbytes[0]=*p++; //this order swaps the bytes to match the .BMP file
	        c.rgbbytes[1]=*p++;
	        c.rgbbytes[2]=*p++;
            fcolour = RGB555(c.rgb);
            pp=(uint16_t *)(((uint32_t) WriteBuf)+(y*(HRes<<1))+(x<<1));
            *pp=fcolour;
        }
    }
}
void DrawBuffer555Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p){
	int x,y,t;
    uint16_t c;
    uint16_t *pp, *qq=(uint16_t *)p;
    // make sure the coordinates are kept within the display area
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
	for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
            if(x>=0 && x<HRes && y>=0 && y<VRes){
                pp=(uint16_t *)(WriteBuf+(y*(HRes<<1))+(x<<1));
                c=*qq++;
                if(c!=sprite_transparent || blank==-1)*pp = c;
            }
        }
    }
}
void DrawPixel555(int x, int y, int c){
    if(x<0 || y<0 || x>=HRes || y>=VRes)return;
    uint16_t colour = RGB555(c);
	uint16_t *p=(uint16_t *)(((uint32_t) WriteBuf)+(y*(HRes<<1))+(x<<1));
    *p=colour;
}
void ReadBuffer555(int x1, int y1, int x2, int y2, unsigned char *c){
    int x,y,t;
    uint16_t *pp;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    int xx1=x1, yy1=y1, xx2=x2, yy2=y2;
    if(x1 < 0) xx1 = 0;
    if(x1 >= HRes) xx1 = HRes - 1;
    if(x2 < 0) xx2 = 0;
    if(x2 >= HRes) xx2 = HRes - 1;
    if(y1 < 0) yy1 = 0;
    if(y1 >= VRes) yy1 = VRes - 1;
    if(y2 < 0) yy2 = 0;
    if(y2 >= VRes) yy2 = VRes - 1;
	for(y=yy1;y<=yy2;y++){
    	for(x=xx1;x<=xx2;x++){
	        pp=(uint16_t *)(((uint32_t) WriteBuf)+(y*(HRes<<1))+(x<<1));
            t=*pp;
            *c++=((t&0x1F)<<3);
            *c++=(((t>>5)&0x1F)<<3);
            *c++=(((t>>10)&0x1F)<<3);
        }
    }
}
void ReadBuffer555Fast(int x1, int y1, int x2, int y2, unsigned char *c){
    int x,y,t;
    uint16_t *pp, *qq=(uint16_t *)c;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    int xx1=x1, yy1=y1, xx2=x2, yy2=y2;
    if(x1 < 0) xx1 = 0;
    if(x1 >= HRes) xx1 = HRes - 1;
    if(x2 < 0) xx2 = 0;
    if(x2 >= HRes) xx2 = HRes - 1;
    if(y1 < 0) yy1 = 0;
    if(y1 >= VRes) yy1 = VRes - 1;
    if(y2 < 0) yy2 = 0;
    if(y2 >= VRes) yy2 = VRes - 1;
	for(y=yy1;y<=yy2;y++){
    	for(x=xx1;x<=xx2;x++){
	        pp=(uint16_t *)(((uint32_t) WriteBuf)+(y*(HRes<<1))+(x<<1));
            *qq++=*pp;
        }
    }
}
void ScrollLCD555(int lines){
    if(lines==0)return;
     if(lines >= 0) {
        for(int i=0;i<VRes-lines;i++) {
            int d=i*(HRes<<1),s=(i+lines)*(HRes<<1); 
            for(int c=0;c<(HRes<<1);c++)WriteBuf[d+c]=WriteBuf[s+c];
        }
        DrawRectangle(0, VRes-lines, HRes - 1, VRes - 1, PromptBC); // erase the lines to be scrolled off
    } else {
    	lines=-lines;
        for(int i=VRes-1;i>=lines;i--) {
            int d=i*(HRes<<1),s=(i-lines)*(HRes<<1); 
            for(int c=0;c<(HRes<<1);c++)WriteBuf[d+c]=WriteBuf[s+c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
void DrawRectangle256(int x1, int y1, int x2, int y2, int c){
    int y,t;
    uint8_t colour =RGB332(c);
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
    for(y=y1;y<=y2;y++){
        volatile uint8_t *p=WriteBuf+(y*HRes+x1);
        memset((void *)p,colour,x2-x1+1);
    }
}
void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap){
    int i, j, k, m, x, y;
//    unsigned char mask;
    if(x1>=HRes || y1>=VRes || x1+width*scale<0 || y1+height*scale<0)return;
    uint8_t fcolour = RGB332(fc);
    uint8_t bcolour = RGB332(bc);
    for(i = 0; i < height; i++) {                                   // step thru the font scan line by line
        for(j = 0; j < scale; j++) {                                // repeat lines to scale the font
            for(k = 0; k < width; k++) {                            // step through each bit in a scan line
                for(m = 0; m < scale; m++) {                        // repeat pixels to scale in the x axis
                    x=x1 + k * scale + m ;
                    y=y1 + i * scale + j ;
                    if(x >= 0 && x < HRes && y >= 0 && y < VRes) {  // if the coordinates are valid
	                    uint8_t *p=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
                        if((bitmap[((i * width) + k)/8] >> (((height * width) - ((i * width) + k) - 1) %8)) & 1) {
                            *p=fcolour;
                        } else {
                            if(bc>=0){
                                *p=bcolour;
                            }
                        }
                   }
                }
            }
        }
    }
}

void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char *p){
	int x,y, t;
    union colourmap
    {
    char rgbbytes[4];
    unsigned int rgb;
    } c;
    uint8_t fcolour;
    uint8_t *pp;
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
	for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
	        c.rgbbytes[0]=*p++; //this order swaps the bytes to match the .BMP file
	        c.rgbbytes[1]=*p++;
	        c.rgbbytes[2]=*p++;
            fcolour = RGB332(c.rgb);
            pp=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
            *pp=fcolour;
        }
    }
}
void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p){
	int x,y,t;
    uint8_t c;
    uint8_t *pp, *qq=(uint8_t *)p;
    // make sure the coordinates are kept within the display area
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
	for(y=y1;y<=y2;y++){
    	for(x=x1;x<=x2;x++){
            if(x>=0 && x<HRes && y>=0 && y<VRes){
                pp=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
                c=*qq++;
                if(c!=sprite_transparent || blank==-1)*pp = c;
            }
        }
    }
}
void DrawPixel256(int x, int y, int c){
    if(x<0 || y<0 || x>=HRes || y>=VRes)return;
    uint8_t colour = RGB332(c);
	uint8_t *p=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
    *p=colour;
}
void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char *c){
    int x,y,t;
    uint8_t *pp;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    int xx1=x1, yy1=y1, xx2=x2, yy2=y2;
    if(x1 < 0) xx1 = 0;
    if(x1 >= HRes) xx1 = HRes - 1;
    if(x2 < 0) xx2 = 0;
    if(x2 >= HRes) xx2 = HRes - 1;
    if(y1 < 0) yy1 = 0;
    if(y1 >= VRes) yy1 = VRes - 1;
    if(y2 < 0) yy2 = 0;
    if(y2 >= VRes) yy2 = VRes - 1;
	for(y=yy1;y<=yy2;y++){
    	for(x=xx1;x<=xx2;x++){
	        pp=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
            t = hal_vga_ops_layer_merge_rgb8(*pp, x, y);
            *c++=((t & 0x3)<<6);
            *c++=(((t>>2) & 0x7)<<5);
            *c++=(((t>>5) & 0x7)<<5);
        }
    }
}
void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char *c){
    int x,y,t;
    uint8_t *pp, *qq=(uint8_t *)c;
    if(x2 <= x1) { t = x1; x1 = x2; x2 = t; }
    if(y2 <= y1) { t = y1; y1 = y2; y2 = t; }
    int xx1=x1, yy1=y1, xx2=x2, yy2=y2;
    if(x1 < 0) xx1 = 0;
    if(x1 >= HRes) xx1 = HRes - 1;
    if(x2 < 0) xx2 = 0;
    if(x2 >= HRes) xx2 = HRes - 1;
    if(y1 < 0) yy1 = 0;
    if(y1 >= VRes) yy1 = VRes - 1;
    if(y2 < 0) yy2 = 0;
    if(y2 >= VRes) yy2 = VRes - 1;
	for(y=yy1;y<=yy2;y++){
    	for(x=xx1;x<=xx2;x++){
	        pp=(uint8_t *)((uint32_t)(WriteBuf+y*HRes+x));
            *qq++=*pp;
        }
    }
}
void ScrollLCD256(int lines){
    if(lines==0)return;
     if(lines >= 0) {
        for(int i=0;i<VRes-lines;i++) {
            int d=i*HRes,s=(i+lines)*HRes; 
            for(int c=0;c<(HRes);c++)WriteBuf[d+c]=WriteBuf[s+c];
        }
        DrawRectangle(0, VRes-lines, HRes - 1, VRes - 1, PromptBC); // erase the lines to be scrolled off
    } else {
    	lines=-lines;
        for(int i=VRes-1;i>=lines;i--) {
            int d=i*HRes,s=(i-lines)*HRes; 
            for(int c=0;c<(HRes<<1);c++)WriteBuf[d+c]=WriteBuf[s+c];
        }
        DrawRectangle(0, 0, HRes - 1, lines - 1, PromptBC); // erase the lines introduced at the top
    }
}
/*  @endcond */

void cmd_tile(void){
   unsigned char *tp;
    uint32_t bcolour=0xFFFFFFFF,fcolour=0xFFFFFFFF;
    int xlen=1,ylen=1;
    if(DISPLAY_TYPE!=SCREENMODE1)error("Invalid for this screen mode");
    if(checkstring(cmdline,(unsigned char *)"RESET")){
        fcolour=(FullColour) ? RGB555(Option.DefaultFC):  RGB332(Option.DefaultFC);
        bcolour=(FullColour) ? RGB555(Option.DefaultBC):  RGB332(Option.DefaultBC);
        for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
#ifdef HDMI
                if(FullColour){
#endif
                    if(fcolour!=0xFFFFFFFF) tilefcols[y*X_TILE+x]=fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols[y*X_TILE+x]=bcolour;
#ifdef HDMI
                } else {
                    if(fcolour!=0xFFFFFFFF) tilefcols_w[y*X_TILE+x]=fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols_w[y*X_TILE+x]=bcolour;
                }
#endif
            }
        }
    } else if((tp=checkstring(cmdline,(unsigned char *)"HEIGHT"))){
        ytileheight=getint(tp,8,VRes);
        Y_TILE=VRes/ytileheight;
        if(VRes % ytileheight)Y_TILE++;
        ClearScreen(Option.DefaultBC);
    } else {
        getargs(&cmdline, 11, (unsigned char *)",");
        if(!(DISPLAY_TYPE==SCREENMODE1))return;
        if(argc<5)error("Syntax");
        int x=getint(argv[0],0,X_TILE-1);
        int y=getint(argv[2],0,Y_TILE-1);
        int tilebcolour, tilefcolour ;
        if(*argv[4]){
            tilefcolour = getColour((char *)argv[4], 0);
            fcolour = (FullColour) ? RGB555(tilefcolour):  RGB332(tilefcolour);
        }
        if(argc>=7 && *argv[6]){
            tilebcolour = getColour((char *)argv[6], 0);
            bcolour = (FullColour) ? RGB555(tilebcolour):  RGB332(tilebcolour);
        }
        if(argc>=9 && *argv[8]){
            xlen=getint(argv[8],1,X_TILE-x);
        }
        if(argc>=11 && *argv[10]){
            ylen=getint(argv[10],1,Y_TILE-y);
        }
        for(int xp=x;xp<x+xlen;xp++){
            for(int yp=y;yp<y+ylen;yp++){
#ifdef HDMI
                if(FullColour){
#endif
                    if(fcolour!=0xFFFFFFFF) tilefcols[yp*X_TILE+xp]=(uint16_t)fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols[yp*X_TILE+xp]=(uint16_t)bcolour;
#ifdef HDMI
                } else {
                    if(fcolour!=0xFFFFFFFF) tilefcols_w[yp*X_TILE+xp]=(uint8_t)fcolour;
                    if(bcolour!=0xFFFFFFFF) tilebcols_w[yp*X_TILE+xp]=(uint8_t)bcolour;
                }
#endif
            }
        }
    }
}
void cmd_map(void){
	unsigned char *p;
    if(!(DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE3  || DISPLAY_TYPE==SCREENMODE5 ))error("Invalid for this screen mode");
    if((p=checkstring(cmdline, (unsigned char *)"RESET"))) {
    mapreset();
    } else if(checkstring(cmdline, (unsigned char *)"GRAYSCALE") || checkstring(cmdline, (unsigned char *)"GREYSCALE")) {
        while(v_scanline!=0){}
        for(int i=1;i<=32;i++){
            int j=i*8-(8-i/4+1);
            if(j<0)j=0;
            map256[i-1]=remap256[i-1]=RGB555(j*65536 + j*256 + j);
            map256[i+32-1]=remap256[i+32-1]=RGB555(j);
            map256[i+64-1]=remap256[i+64-1]=RGB555(j*256 );
            map256[i+96-1]=remap256[i+96-1]=RGB555(j*256 + j);
            map256[i+128-1]=remap256[i+128-1]=RGB555(j*65536);
            map256[i+160-1]=remap256[i+160-1]=RGB555(j*65536 + j);
            map256[i+192-1]=remap256[i+192-1]=RGB555(j*65536 + j*256);
            map256[i+224-1]=remap256[i+224-1]=RGB555(j*65536 + j*256 + j);
        }
        for(int i=1;i<=16;i++){
            int j=i*16-(16-i+1);
            map16quads[i-1]=remap332[i-1]= RGB332(j*65536+j*256+j) | (RGB332(j*65536+j*256+j)<<8) | (RGB332(j*65536+j*256+j)<<16) | (RGB332(j*65536+j*256+j)<<24);
            map16pairs[i-1]=remap555[i-1]= (RGB555(j*65536+j*256+j) | (RGB555(j*65536+j*256+j)<<16));
        }
    } else if((p=checkstring(cmdline, (unsigned char *)"MAXIMITE"))) {
        while(v_scanline!=0){}
        for(int i=0;i<16;i++)map256[i]=remap256[i]=RGB555(CMM1map[i]);
        for(int i=0;i<16;i++){
            map16quads[i]=remap332[i]=RGB332(CMM1map[i]) | (RGB332(CMM1map[i])<<8)| (RGB332(CMM1map[i])<<16)| (RGB332(CMM1map[i])<<24);
            map16pairs[i]=remap555[i]=RGB555(CMM1map[i]) | (RGB555(CMM1map[i])<<8);
        }
    } else if((p=checkstring(cmdline, (unsigned char *)"SET"))) {
        while(v_scanline!=0){}
        for(int i=0;i<256;i++)map256[i]=remap256[i];
        for(int i=0;i<16;i++){
            map16pairs[i]=remap555[i];
            map16quads[i]=remap332[i];
        }
    } else {
    	int cl = getint(cmdline,0,255);
        if(DISPLAY_TYPE!=SCREENMODE5 && cl >15)error("Mode supports 16 colours (0-15)");
		while(*cmdline && tokenfunction(*cmdline) != op_equal) cmdline++;
		if(!*cmdline) error("Invalid syntax");
		++cmdline;
		if(!*cmdline) error("Invalid syntax");
		int col=getColour((char *)cmdline,0);
        remap256[cl]=RGB555(col);
		remap555[cl]=RGB555(col) | (RGB555(col)<<16);
        remap332[cl]=RGB332(col) | (RGB332(col)<<8) | (RGB332(col)<<16) | (RGB332(col)<<24);
    }
}
#endif
void setmode(int mode, bool clear){
    closeframebuffer('A');
    if(clear)memset((void *)FRAMEBUFFER,0,framebuffersize);
#ifdef HDMI
    while(v_scanline!=0){}
#else
    while(QVgaScanLine!=0){}
#endif
    if(mode==5){
        DISPLAY_TYPE=SCREENMODE5; 
        ScreenSize=MODE5SIZE;
    } else if(mode==4){
        if(!(FullColour))error("Mode not available in this resolution");
        DISPLAY_TYPE=SCREENMODE4; 
        ScreenSize=MODE4SIZE;
    } else if(mode==3){
        DISPLAY_TYPE=SCREENMODE3; 
        ScreenSize=MODE3SIZE;
    } else if(mode==2){
        DISPLAY_TYPE=SCREENMODE2; 
        ScreenSize=MODE2SIZE;
    } else { //mode=1
#ifdef rp2350
#ifndef HDMI
        tilefcols=(uint16_t *)((uint8_t*)FRAMEBUFFER+(MODE1SIZE*3));
        tilebcols=(uint16_t *)((uint8_t*)FRAMEBUFFER+(MODE1SIZE*3)+(MODE1SIZE>>1));
#else
        mapreset();
#endif
#endif
        DISPLAY_TYPE=SCREENMODE1;
        ScreenSize=MODE1SIZE;
    }
//    uSec(10000);
    ResetDisplay();
    if(clear){
        memset((void *)WriteBuf, 0, ScreenSize);
        CurrentX = CurrentY =0;
        ClearScreen(Option.DefaultBC);
    }
#ifdef HDMI
    if(FullColour || MediumRes){
#endif
        if(DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE4 || DISPLAY_TYPE==SCREENMODE5){
            SetFont((6<<4) | 1) ;
            PromptFont=(6<<4) | 1;
        } else {
            SetFont(1) ;
            PromptFont = 1;
        }
#ifdef HDMI
    } else {
        if(DISPLAY_TYPE==SCREENMODE1){
            SetFont((2<<4) | 1) ;
            PromptFont=(2<<4) | 1;
        } else if(DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5){
            SetFont((6<<4) | 1) ;
            PromptFont=(6<<4) | 1;
        } else if(DISPLAY_TYPE==SCREENMODE3){
            SetFont(1) ;
            PromptFont = 1;
        }
    }
#endif
if(mode==Option.DISPLAY_TYPE-SCREENMODE1+1){
    SetFont(Option.DefaultFont);
    PromptFont=Option.DefaultFont;
}
if(DISPLAY_TYPE==SCREENMODE1){
    ytileheight=gui_font_height;
    Y_TILE=VRes/ytileheight;
    if(VRes % ytileheight)Y_TILE++;
#ifdef PICOMITEVGA
    if(DISPLAY_TYPE==SCREENMODE1/* && WriteBuf==DisplayBuf*/){
        gui_fcolour=Option.DefaultFC;
        gui_bcolour=Option.DefaultBC;
#ifdef HDMI
        settiles();
#else
        int bcolour = RGB121pack(gui_bcolour);
        int fcolour = RGB121pack(gui_fcolour);
        for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
                tilefcols[y*X_TILE+x]=fcolour;
                tilebcols[y*X_TILE+x]=bcolour;
            } 
        }
#endif
    }
#endif
}

    hal_keyboard_clear_repeat_state();
}


void cmd_mode(void){
    int mode =getint(cmdline,1,MAXMODES);
    setmode(mode, true);
}
#endif
