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

/* VIRTUAL-mode mode-2 draw primitives — defined in Draw.c. */
extern void DrawRectangle2(int x1, int y1, int x2, int y2, int c);
extern void DrawBitmap2(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void ScrollLCD2(int lines);
extern void DrawBuffer2(int x1, int y1, int x2, int y2, unsigned char *p);
extern void ReadBuffer2(int x1, int y1, int x2, int y2, unsigned char *p);
extern void DrawBuffer2Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
extern void ReadBuffer2Fast(int x1, int y1, int x2, int y2, unsigned char *p);
extern void DrawPixel2(int x, int y, int c);

/* FRAMEBUFFER 4-bit-mode draw primitives — defined in Draw.c. */
extern void DrawRectangle16(int x1, int y1, int x2, int y2, int c);
extern void DrawBitmap16(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
extern void ScrollLCD16(int lines);
extern void DrawBuffer16(int x1, int y1, int x2, int y2, unsigned char *p);
extern void ReadBuffer16(int x1, int y1, int x2, int y2, unsigned char *p);
extern void DrawBuffer16Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
extern void ReadBuffer16Fast(int x1, int y1, int x2, int y2, unsigned char *p);
extern void DrawPixel16(int x, int y, int c);

/* VGA ResetDisplay body — mode-specific HRes/VRes/function-pointer
 * setup. Real impl below; non-VGA gets a stub in vga_ops_stub.c. */
void hal_vga_ops_reset_display_vga(void) {
#ifdef rp2350
    if(Option.CPU_Speed==Freq848)HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 848: 424);
    else if(Option.CPU_Speed==Freq400)HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 720: 360);
    else if(Option.CPU_Speed==FreqSVGA || Option.CPU_Speed==FreqY )HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 800: 400);
    else HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 640: 320);
#else
    if(Option.CPU_Speed==Freq400)HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 720: 360);
    else HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 640: 320);
#endif
    if(Option.CPU_Speed==Freq400)VRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 400: 200);
#ifdef rp2350
    else if(Option.CPU_Speed==FreqSVGA)VRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 600: 300);
#endif
    else VRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 480: 240);
#ifdef HDMI
        if(Option.CPU_Speed==Freq720P){
            HRes=(DISPLAY_TYPE == SCREENMODE1 ? 1280 : ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 320 : 640));
            VRes=(DISPLAY_TYPE == SCREENMODE1 ? 720 :  ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 180 : 360));
        } else if(Option.CPU_Speed==FreqXGA){
            HRes=(DISPLAY_TYPE == SCREENMODE1 ? 1024 : ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 256 : 512));
            VRes=(DISPLAY_TYPE == SCREENMODE1 ? 768 :  ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 192 : 384));
        } else if(Option.CPU_Speed==FreqSVGA){
            HRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 800: 400);
            VRes=((DISPLAY_TYPE == SCREENMODE1 ||  DISPLAY_TYPE == SCREENMODE3) ? 600: 300);
        } else if(Option.CPU_Speed==FreqX){
            HRes=(DISPLAY_TYPE == SCREENMODE1 ? 1024 : ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 256 : 512));
            VRes=(DISPLAY_TYPE == SCREENMODE1 ? 600 :  ((DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE5) ? 150 : 300));
        } 
#endif
        
        switch(DISPLAY_TYPE){
            case SCREENMODE1:
                ScreenSize= MODE1SIZE;
                break;
            case SCREENMODE2:
                ScreenSize=MODE2SIZE;
                break;
            case SCREENMODE3:
                ScreenSize=MODE3SIZE;
                break;
            case SCREENMODE4:
                ScreenSize=MODE4SIZE;
                break;
            case SCREENMODE5:
                ScreenSize=MODE5SIZE;
                break;
        }
        if(DISPLAY_TYPE == SCREENMODE2 || DISPLAY_TYPE == SCREENMODE3){
            DrawRectangle=DrawRectangle16;
            DrawBitmap= DrawBitmap16;
            ScrollLCD=ScrollLCD16;
            DrawBuffer=DrawBuffer16;
            ReadBuffer=ReadBuffer16;
            DrawBufferFast=DrawBuffer16Fast;
            ReadBufferFast=ReadBuffer16Fast;
            DrawPixel=DrawPixel16;
#ifdef HDMI
        } else if(DISPLAY_TYPE == SCREENMODE4){
            /* HDMI SCREENMODE4/5 dispatchers live in
             * drivers/vga_pio/vga_mode_ops.c (PICOMITEVGA+HDMI real
             * impl). Extern forward decls here match the target
             * function-pointer signatures. */
            extern void DrawRectangle555(int x1, int y1, int x2, int y2, int c);
            extern void DrawBitmap555(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
            extern void ScrollLCD555(int lines);
            extern void DrawBuffer555(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void ReadBuffer555(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void DrawBuffer555Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
            extern void ReadBuffer555Fast(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void DrawPixel555(int x, int y, int c);
            DrawRectangle=DrawRectangle555;
            DrawBitmap= DrawBitmap555;
            ScrollLCD=ScrollLCD555;
            DrawBuffer=DrawBuffer555;
            ReadBuffer=ReadBuffer555;
            DrawBufferFast=DrawBuffer555Fast;
            ReadBufferFast=ReadBuffer555Fast;
            DrawPixel=DrawPixel555;
        } else if(DISPLAY_TYPE == SCREENMODE5){
            extern void DrawRectangle256(int x1, int y1, int x2, int y2, int c);
            extern void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
            extern void ScrollLCD256(int lines);
            extern void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
            extern void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char *p);
            extern void DrawPixel256(int x, int y, int c);
            DrawRectangle=DrawRectangle256;
            DrawBitmap= DrawBitmap256;
            ScrollLCD=ScrollLCD256;
            DrawBuffer=DrawBuffer256;
            ReadBuffer=ReadBuffer256;
            DrawBufferFast=DrawBuffer256Fast;
            ReadBufferFast=ReadBuffer256Fast;
            DrawPixel=DrawPixel256;
#endif
        } else {
            DrawRectangle=DrawRectangle2;
            DrawBitmap= DrawBitmap2;
            ScrollLCD=ScrollLCD2;
            DrawBuffer=DrawBuffer2;
            ReadBuffer=ReadBuffer2;
            DrawBufferFast=DrawBuffer2Fast;
            ReadBufferFast=ReadBuffer2Fast;
            DrawPixel=DrawPixel2;
            PromptFC = gui_fcolour= Option.DefaultFC;
            PromptBC = gui_bcolour= Option.DefaultBC;
        }
#ifdef HDMI
        settiles();
#else
#ifdef rp2350
        if(DISPLAY_TYPE==SCREENMODE1){
            tilefcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3));
            tilebcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3)+(MODE1SIZE>>1));
        }
#endif
        for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
                tilefcols[y*X_TILE+x]=RGB121pack(Option.DefaultFC);
                tilebcols[y*X_TILE+x]=RGB121pack(Option.DefaultBC);
           }
        }
#endif
}

/* VGA closeframebuffer — mode-aware teardown of the framebuffer /
 * layer buffers. Directly named (not via a hook) because the non-
 * VGA closeframebuffer lives in Draw.c inside the `#if !PICOMITEVGA
 * && !MMBASIC_HOST` block — only one of the two reaches the linker
 * per target. */
void closeframebuffer(char layer) {
    if(layer=='A')WriteBuf=DisplayBuf;
    if(FrameBuf!=DisplayBuf && (layer=='A' || layer=='F')){
        if(WriteBuf==FrameBuf)WriteBuf=DisplayBuf;
        switch(DISPLAY_TYPE){
            case SCREENMODE1:
            case SCREENMODE2:
#ifdef rp2350
                if(ScreenSize<framebuffersize/3)FrameBuf=DisplayBuf;
                else FreeMemory((void *)FrameBuf);
#else
                FreeMemory((void *)FrameBuf);
#endif
                break;
#ifdef rp2350
            case SCREENMODE3:
                FreeMemory((void *)FrameBuf);
                break;
#ifdef HDMI
            case SCREENMODE4:
            case SCREENMODE5:
                FreeMemory((void *)FrameBuf);
                break;
#endif
#endif
        }
    } 
    if(LayerBuf!=DisplayBuf &&  (layer=='A' || layer=='L')){
        if(WriteBuf==LayerBuf)WriteBuf=DisplayBuf;
        volatile unsigned char *temp= LayerBuf;
        switch(DISPLAY_TYPE){
            case SCREENMODE2:
                transparent=0;
            case SCREENMODE1:
#ifdef rp2350
                if(ScreenSize<framebuffersize/2)LayerBuf=DisplayBuf;
                else {
                    LayerBuf=DisplayBuf;
                    FreeMemory((void *)temp);
                }
#else
                LayerBuf=DisplayBuf;
                FreeMemory((void *)temp);
#endif
                break;
#ifdef rp2350
            case SCREENMODE3:
                LayerBuf=DisplayBuf;
                FreeMemory((void *)temp);
                break;
#ifdef HDMI
            case SCREENMODE4:
                LayerBuf=DisplayBuf;
                FreeMemory((void *)temp);
                break;
            case SCREENMODE5:
                LayerBuf=DisplayBuf;
                transparent=0;
                break;
#endif
#endif
        }
    }
    if(SecondFrame!=DisplayBuf &&  (layer=='A' || layer=='2')){
        FreeMemory((void *)SecondFrame);
    }
    if(SecondLayer!=DisplayBuf &&  (layer=='A' || layer=='T')){
        if(WriteBuf==LayerBuf)WriteBuf=DisplayBuf;
        volatile unsigned char *temp= SecondLayer;
        switch(DISPLAY_TYPE){
            case SCREENMODE2:
                transparents=0;
                SecondLayer=DisplayBuf;
                break;
            case SCREENMODE1:
                SecondLayer=DisplayBuf;
                FreeMemory((void *)temp);
                break;
#ifdef rp2350
            case SCREENMODE3:
                SecondLayer=DisplayBuf;
                FreeMemory((void *)temp);
                break;
#ifdef HDMI
            case SCREENMODE4:
                SecondLayer=DisplayBuf;
                FreeMemory((void *)temp);
                break;
            case SCREENMODE5:
                SecondLayer=DisplayBuf;
                transparents=0;
                break;
#endif
#endif
        }
    }
	WriteBuf=(unsigned char *)FRAMEBUFFER;
	DisplayBuf=(unsigned char *)FRAMEBUFFER;
	LayerBuf=(unsigned char *)FRAMEBUFFER;
	FrameBuf=(unsigned char *)FRAMEBUFFER;
    SecondLayer=(unsigned char *)FRAMEBUFFER;
    SecondFrame=(unsigned char *)FRAMEBUFFER;
    transparent=0;
}
/*  @endcond */

void cmd_framebuffer(void){
/*
RP2040 version support just modes 1 and 2
RP2350 vversions support modes 1 to 5
All modes can have a framebuffer and a layer buffer but only modes 2 and 5 automatically display the layer buffer over the top of the main display
In all other cases it is just another framebuffer that can be used to build up images to be copied to the main display
For VGA/HDMI Layer buffers and framebuffers have exactly the same resolution as the main display (unlike TFT displays)
For RP2350 Modes 1 and 2 both buffers are in the allocated Video Memory (640x240 bytes == 320x240x2)
For RP2350 Mode 5 the layer buffer is in the allocated video memory
All other buffers are allocated out of variable space using GetMemory
NB: for RP2350 with PSRAM buffers may be allocated in the slower external memory
Buffer sizes are:
Normal:
    #define MODE1SIZE_S  VMaxH*VMaxV/8
    #define MODE2SIZE_S  320*240/2
    #define MODE3SIZE_S  VMaxH*VMaxV/2
    #define MODE4SIZE_S  320*240*2
    #define MODE5SIZE_S  320*240

Widescreen:
    #define MODE1SIZE_W  1280 *720 /8
    #define MODE2SIZE_W  (1280/4) * (720/4)/2
    #define MODE3SIZE_W  (1280/2) * (720/2)/2
    #define MODE5SIZE_W  (1280/4) * (720/4)

XGA:
    #define MODE1SIZE_W  `1024 *768 /8
    #define MODE2SIZE_W  (1024/4) * (768/4)/2
    #define MODE3SIZE_W  (1024/2) * (768/2)/2
    #define MODE5SIZE_W  (1024/4) * (768/4)

*/
    unsigned char *p;
#ifdef rp2350
    if((p=checkstring(cmdline, (unsigned char *)"CREATE 2"))) {
        int colour=0;
        if(SecondFrame==DisplayBuf){
            getargs(&p,1,(unsigned char *)",");
            switch(DISPLAY_TYPE){
                case SCREENMODE2:
                case SCREENMODE1:
                    SecondFrame=GetMemory(ScreenSize);
                    break;
#ifdef rp2350
                case SCREENMODE3:
                     SecondFrame=GetMemory(ScreenSize);
                    break;
#ifdef HDMI
                case SCREENMODE4:
                    SecondFrame=GetMemory(ScreenSize);
                    break;
                case SCREENMODE5:
                    SecondFrame=GetMemory(ScreenSize);
                    break;
#endif
#endif
            }
        } else error("Framebuffer 2 already exists");
        memset((void *)SecondFrame,colour,ScreenSize);
    } else 
#endif
    if((p=checkstring(cmdline, (unsigned char *)"CREATE"))) {
        if(FrameBuf==DisplayBuf){
            switch(DISPLAY_TYPE){
                case SCREENMODE1:
                case SCREENMODE2:
#ifdef rp2350
                    if(ScreenSize<framebuffersize/3)FrameBuf=DisplayBuf+2*ScreenSize;
                    else FrameBuf=GetMemory(ScreenSize);
#else
                    FrameBuf=GetMemory(ScreenSize);
#endif
                    break;
#ifdef rp2350
                case SCREENMODE3:
                    FrameBuf=GetMemory(ScreenSize);
                    break;
#ifdef HDMI
                case SCREENMODE4:
                case SCREENMODE5:
                    FrameBuf=GetMemory(ScreenSize);
                    break;
#endif
#endif
            }
        } else error("Framebuffer already exists");
        memset((void *)FrameBuf,0,ScreenSize);

#ifdef rp2350
    } else if((p=checkstring(cmdline, (unsigned char *)"LAYER TOP"))) {
        int colour=0;
        if(SecondLayer==DisplayBuf){
            getargs(&p,1,(unsigned char *)",");
            switch(DISPLAY_TYPE){
                case SCREENMODE2:
                    if(argc==1)transparents=getint(argv[0],0,15);
                    colour=transparents | (transparents<<4);
                    if(ScreenSize<framebuffersize/4)SecondLayer=DisplayBuf+3*ScreenSize;
                    else SecondLayer=GetMemory(ScreenSize);
                    break;
                case SCREENMODE1:
                    SecondLayer=GetMemory(ScreenSize);
                    break;
                case SCREENMODE3:
                    if(argc==1)transparents=getint(argv[0],0,15);
                    SecondLayer=GetMemory(ScreenSize);
                    if(SecondLayer>=(uint8_t *)PSRAMbase && SecondLayer< (uint8_t *)(PSRAMbase + 1024*1024*16)){
                        FreeMemory((void *)SecondLayer);
                        error("Second Layer must be in tightly coupled RAM, declare before other variables");
                    }
                    colour=transparents | (transparents<<4);
                    break;
#ifdef HDMI
                case SCREENMODE4:
                    SecondLayer=GetMemory(ScreenSize);
                    break;
                case SCREENMODE5:
                    SecondLayer=GetMemory(ScreenSize);
                    if(SecondLayer>=(uint8_t *)PSRAMbase && SecondLayer< (uint8_t *)(PSRAMbase + 1024*1024*16)){
                        FreeMemory((void *)SecondLayer);
                        error("Second Layer must be in tightly coupled RAM, declare before other variables");
                    }
                    if(argc==1)transparents=getint(argv[0],0,255);
                    colour=transparents;
                    break;
#endif
            }
        } else error("Framebuffer already exists");
        memset((void *)SecondLayer,colour,ScreenSize);
#endif
    } else if((p=checkstring(cmdline, (unsigned char *)"LAYER"))) {
        int colour=0;
        if(LayerBuf==DisplayBuf){
            getargs(&p,1,(unsigned char *)",");
            switch(DISPLAY_TYPE){
                case SCREENMODE2:
                    if(argc==1)transparent=getint(argv[0],0,15);
                    colour=transparent | (transparent<<4);
                case SCREENMODE1:
#ifdef rp2350
                    if(ScreenSize<framebuffersize/2)LayerBuf=DisplayBuf+ScreenSize;
                    else LayerBuf=GetMemory(ScreenSize);
#else
                    LayerBuf=GetMemory(ScreenSize);
#endif
                    break;
#ifdef rp2350
                case SCREENMODE3:
                    if(argc==1)transparent=getint(argv[0],0,15);
                    LayerBuf=GetMemory(ScreenSize);
                    colour=transparent | (transparent<<4);
                    break;
#ifdef HDMI
                case SCREENMODE4:
                    LayerBuf=GetMemory(ScreenSize);
                    if(argc==1)RGBtransparent=RGB555(getColour((char *)argv[0],0));
                    else RGBtransparent=0;
                    break;
                case SCREENMODE5:
                    if(ScreenSize<framebuffersize/2)LayerBuf=DisplayBuf+ScreenSize;
                    else LayerBuf=GetMemory(ScreenSize);
                    if(argc==1)transparent=getint(argv[0],0,255);
                    colour=transparent;
                    break;
#endif
#endif
                }
#ifdef rp2350
            if(LayerBuf>(uint8_t *)PSRAMbase && LayerBuf< (uint8_t *)(PSRAMbase + 1024*1024*16)){
                FreeMemory((void *)LayerBuf);
                error("Layer Buffer must be in tightly coupled RAM, declare before other variables");
            }
#endif        
        } else error("Framebuffer already exists");
        if(DISPLAY_TYPE!=SCREENMODE4)memset((void *)LayerBuf,colour,ScreenSize);
        else {
            uint16_t *p=(uint16_t *)LayerBuf;
            for(int i=0;i<HRes*VRes;i++)*p++=RGBtransparent;
        }
    } else if((p=checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        if(checkstring(p, (unsigned char *)"F")){
            closeframebuffer('F');
        } else if(checkstring(p, (unsigned char *)"L")){
            closeframebuffer('T');
#ifdef rp2350
        } else if(checkstring(p, (unsigned char *)"T")){
            closeframebuffer('L');
        } else if(checkstring(p, (unsigned char *)"2")){
            closeframebuffer('2');
#endif
        } else  closeframebuffer('A');
    } else if((p=checkstring(cmdline, (unsigned char *)"WRITE"))) {
        if(checkstring(p, (unsigned char *)"N"))WriteBuf=DisplayBuf;
        else if(checkstring(p, (unsigned char *)"L")){
            if(LayerBuf==DisplayBuf)error("Layer not created");
            WriteBuf=LayerBuf;
        }
#ifdef rp2350
        else if(checkstring(p, (unsigned char *)"T")){
            if(SecondLayer==DisplayBuf)error("Layer 2 not created");
            WriteBuf=SecondLayer;
        }
        else if(checkstring(p, (unsigned char *)"2")){
            if(SecondFrame==DisplayBuf)error("Frame 2 not created");
            WriteBuf=SecondFrame;
        }
#endif
        else if(checkstring(p, (unsigned char *)"F")){
            if(FrameBuf==DisplayBuf)error("Frame buffer not created");
             WriteBuf=FrameBuf;
        }
        else {
            getargs(&p,1,(unsigned char *)",");
            char *q=(char *)getCstring(argv[0]);
            if(strcasecmp(q,"N")==0)WriteBuf=DisplayBuf;
            else if(strcasecmp(q,"L")==0){
                if(LayerBuf==DisplayBuf)error("Layer not created");
                WriteBuf=LayerBuf;
            } else if(strcasecmp(q,"F")==0){
                if(FrameBuf==DisplayBuf)error("Frame buffer not created");
                WriteBuf=FrameBuf;
            } else if(strcasecmp(q,"2")==0){
                if(SecondFrame==DisplayBuf)error("Frame buffer 2 not created");
                WriteBuf=SecondFrame;
            } else if(strcasecmp(q,"T")==0){
                if(SecondLayer==DisplayBuf)error("Layer Top not created");
                WriteBuf=SecondLayer;
            }
            else error("Syntax");
        }
    } else if((p=checkstring(cmdline, (unsigned char *)"WAIT"))) {
            #ifdef HDMI
            while(v_scanline!=0){}
            #else
            while(QVgaScanLine!=0){}
            #endif

    } else if((p=checkstring(cmdline, (unsigned char *)"COPY"))) {
        getargs(&p,5,(unsigned char *)",");
        if(!(argc==3 || argc==5))error("Syntax");
        volatile uint8_t *s=NULL,*d=NULL;
        if(checkstring(argv[0],(unsigned char *)"N"))s=DisplayBuf;
        else if(checkstring(argv[0],(unsigned char *)"L"))s=LayerBuf;
        else if(checkstring(argv[0],(unsigned char *)"F"))s=FrameBuf;
        else if(checkstring(argv[0],(unsigned char *)"2"))s=SecondFrame;
        else if(checkstring(argv[0],(unsigned char *)"T"))s=SecondLayer;
        else error("Syntax");
        if(checkstring(argv[2],(unsigned char *)"N"))d=DisplayBuf;
        else if(checkstring(argv[2],(unsigned char *)"L"))d=LayerBuf;
        else if(checkstring(argv[2],(unsigned char *)"F"))d=FrameBuf;
        else if(checkstring(argv[2],(unsigned char *)"2"))d=SecondFrame;
        else if(checkstring(argv[2],(unsigned char *)"T"))d=SecondLayer;
        else error("Syntax");
        if(argc==5){
            if(checkstring(argv[4],(unsigned char *)"B")){
                #ifdef HDMI
                while(v_scanline!=0){} 
                #else
                while(QVgaScanLine!=0){}
            #endif
            } else error("Syntax");
        }
        if(d!=s)
//            #ifdef rp2350
//                _Z10copy_wordsPKmPmm((uint32_t *)s, (uint32_t *)d, ScreenSize>>2);
//            #else
                memcpy((void *)d,(void *)s,ScreenSize);
//            #endif
        else error("Buffer not created");
    } else error("Syntax");
}

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
