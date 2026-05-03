/*
 * drivers/spi_lcd/spi_lcd_framebuffer.c — SPI-LCD FRAMEBUFFER
 * subsystem.
 *
 * Extracted from Draw.c's `#if !defined(PICOMITEVGA) && !defined(
 * MMBASIC_HOST)` block. Contains:
 *   - restorepanel           rewire Draw* function pointers after a
 *                            FRAMEBUFFER close
 *   - closeframebuffer       release FrameBuf / LayerBuf / ShadowBuf
 *   - setframebuffer         wire Draw* pointers to the 4-bit
 *                            framebuffer-mode helpers
 *   - copyframetoscreen      push a scanline range to the LCD
 *                            via SPI / SSD1963 / IPS_4_16 / NEXTGEN
 *   - blitmerge / merge      layer-compositing onto the frame
 *   - cmd_framebuffer        BASIC FRAMEBUFFER command dispatcher
 *   - DrawRectangle16 / DrawBitmap16 / DrawBuffer16 / DrawBuffer16Fast
 *     / ReadBuffer16 / ReadBuffer16Fast / ScrollLCD16 / DrawPixel16
 *     the 4-bit framebuffer-mode draw primitives
 *
 * Linked into every target except PICOMITEVGA (VGA has its own
 * framebuffer subsystem in drivers/vga_pio/) and host (which
 * simulates framebuffer in host_stubs_legacy.c).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "bc_alloc.h"
#include "hal/hal_display_merge.h"
#include "hardware/dma.h"

/* File-scope globals defined in Draw.c / SSD1963.c. */
extern int map[16];
extern int SSD1963data;

/* Host build: restorepanel / closeframebuffer / setframebuffer /
 * copyframetoscreen / blitmerge / merge / cmd_framebuffer all live in
 * host_stubs_legacy.c (write into host_framebuffer directly instead of
 * DMAing bytes to a physical LCD controller). This block closes at the
 * original PICOMITEVGA #endif further down. */
void restorepanel(void){
    if(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel){
        if(Option.DISPLAY_ORIENTATION==PORTRAIT){
            DrawRectangle = DrawRectangleSPISCR;
            DrawBitmap = DrawBitmapSPISCR;
            DrawBuffer = DrawBufferSPISCR;
            DrawPixel = DrawPixelNormal;
            ScrollLCD = ScrollLCDSPISCR;
            DrawBLITBuffer = DrawBufferSPISCR;
            if(Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ILI9488  || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ST7789B){
                ReadBuffer = ReadBufferSPISCR;
                ReadBLITBuffer = ReadBufferSPISCR;
            }
        } else {
            DrawRectangle = DrawRectangleSPI;
            DrawBitmap = DrawBitmapSPI;
            DrawBuffer = DrawBufferSPI;
            DrawBLITBuffer = DrawBufferSPI;
            DrawPixel = DrawPixelNormal;
            if(Option.DISPLAY_TYPE == ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE == ILI9488  || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE == ST7789B){
                ReadBLITBuffer = ReadBufferSPI;
                ReadBuffer = ReadBufferSPI;
                ScrollLCD = ScrollLCDSPI;
            }
        }
    } else if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL){
        if(screen320){
            DrawRectangle = DrawRectangle320;
            DrawBitmap = DrawBitmap320;
            DrawBuffer = DrawBuffer320;
            ReadBuffer = ReadBuffer320;
        } else {
            DrawRectangle= DrawRectangleSSD1963;
            DrawBitmap = DrawBitmapSSD1963;
            DrawBuffer = DrawBufferSSD1963;
            ReadBuffer = ReadBufferSSD1963;
            if(SSD16TYPE || Option.DISPLAY_TYPE==IPS_4_16){
                DrawBLITBuffer= DrawBLITBufferSSD1963;
                ReadBLITBuffer = ReadBLITBufferSSD1963;
            } else {
                DrawBLITBuffer= DrawBufferSSD1963;
                ReadBLITBuffer = ReadBufferSSD1963;
            }
        }
        DrawPixel = DrawPixelNormal;
        if(!(Option.DISPLAY_TYPE == ILI9341_8 || Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == IPS_4_16 ))ScrollLCD = ScrollSSD1963;
        else ScrollLCD=ScrollLCDSPI;
    } else if(Option.DISPLAY_TYPE>=NEXTGEN){
        /* NEXTGEN framebuffer display types are rp2350 PICOMITE only;
         * non-rp2350 targets never reach this branch because the OPTION
         * command won't let the user set a NEXTGEN display type there.
         * MEM332 function symbols are stubbed via
         * drivers/spi_lcd/spi_lcd_nextgen_stub.c on other builds so the
         * assignments below link unconditionally. */
        DrawRectangle = DrawRectangleMEM332;
        DrawBitmap = DrawBitmapMEM332;
        DrawBuffer = DrawBufferMEM332;
        ReadBuffer = ReadBufferMEM332;
        DrawBLITBuffer = DrawBlitBufferMEM332;
        ReadBLITBuffer = ReadBlitBufferMEM332;
        ScrollLCD = ScrollLCDMEM332;
        DrawPixel = DrawPixelNormal;
    }
    WriteBuf=NULL;
}
void setframebuffer(void){
    if(!((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL) || Option.DISPLAY_TYPE>=NEXTGEN))return;
    DrawRectangle=DrawRectangle16;
    DrawBitmap= DrawBitmap16;
    ScrollLCD=ScrollLCD16;
    DrawBuffer=DrawBuffer16;
    ReadBLITBuffer=ReadBuffer16;
    DrawBLITBuffer=DrawBuffer16;
    ReadBuffer=ReadBuffer16;
    DrawBufferFast=DrawBuffer16Fast;
    ReadBufferFast=ReadBuffer16Fast;
    DrawPixel=DrawPixel16;
}
void closeframebuffer(char layer){
    hal_display_merge_abort();
    hal_display_fast_dma_free();
    if(FrameBuf)FreeMemory(FrameBuf);
    if(LayerBuf)FreeMemory(LayerBuf);
    if(FrameBuf || LayerBuf)restorepanel();
    FrameBuf=NULL;
    WriteBuf=NULL;
}
void copyframetoscreen(uint8_t *s,int xstart, int xend, int ystart, int yend, int odd){
    low_x=xstart;low_y=ystart;high_x=xend;high_y=yend;
    unsigned char col[3]={0};
    int c;
    if(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel)DefineRegionSPI(xstart,ystart,xend,yend, 1);
    else if(Option.DISPLAY_TYPE == ILI9341_8){
        SetAreaILI9341(xstart,ystart,xend,yend, 1);
    } else if(Option.DISPLAY_TYPE == ILI9341_16 || Option.DISPLAY_TYPE == ILI9486_16) {
        if(Option.DISPLAY_TYPE == ILI9486_16){
            Write16bitCommand(ILI9341_PIXELFORMAT);
            WriteData16bit(0x55);
        }
    	SetAreaILI9341(xstart,ystart,xend,yend, 1);
    } else if(Option.DISPLAY_TYPE==IPS_4_16) {
    	if(LCDAttrib==1)WriteCmdDataIPS_4_16(0x3A00,1,0x55);
        if(screen320){
           SetAreaIPS_4_16(xstart+80,ystart*2,xend*2-xstart+81,yend*2+1,1);                                // setup the area to be filled
        } else {
            SetAreaIPS_4_16(xstart,ystart,xend,yend, 1);                               // setup the area to be filled
        }
    } else if(Option.DISPLAY_TYPE>=NEXTGEN) {
        //nothing to do (NEXTGEN framebuffer already in RAM; non-rp2350 never reaches here)
    } else {
        if(screen320){
            if(Option.DISPLAY_TYPE!=SSD1963_4_16)SetAreaSSD1963(xstart+80,ystart*2,xend*2-xstart+81,yend*2+1);                                // setup the area to be filled
            else SetAreaSSD1963(xstart+80,ystart+16,xend+80,yend+16);
        } else {
            SetAreaSSD1963(xstart,ystart,xend,yend);                                // setup the area to be filled
        }
        WriteComand(CMD_WR_MEMSTART);
    }
    int i;
    int cnt=2; 
    if(Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE == ILI9488P  || Option.DISPLAY_TYPE==ILI9481IPS || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<=SSD_PANEL_8)){
        cnt=3;
    } 
    if(map[15]==0){
        for(i=0;i<16;i++){
            if(Option.DISPLAY_TYPE==ILI9488  || Option.DISPLAY_TYPE == ILI9488P || Option.DISPLAY_TYPE==ILI9481IPS){
                col[0]=(RGB121map[i]>>16);
                col[1]=(RGB121map[i]>>8) & 0xFF;
                col[2]=(RGB121map[i] & 0xFF);
            } else if(Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<=SSD_PANEL_8){
                col[2]=(RGB121map[i]>>16);
                col[1]=(RGB121map[i]>>8) & 0xFF;
                col[0]=(RGB121map[i] & 0xFF);
            } else if(Option.DISPLAY_TYPE>SSD_PANEL_8 && Option.DISPLAY_TYPE< NEXTGEN){
                map[i]=((RGB121map[i]>>8) & 0xf800) | ((RGB121map[i]>>5) & 0x07e0) | ((RGB121map[i]>>3) & 0x001f);
                continue;
            } else if(Option.DISPLAY_TYPE>=NEXTGEN){
                map[i]=RGB332(RGB121map[i]);
                continue;
            } else {
                col[0]= ((RGB121map[i] >> 16) & 0b11111000) | ((RGB121map[i] >> 13) & 0b00000111);
                col[1] = ((RGB121map[i] >>  5) & 0b11100000) | ((RGB121map[i] >>  3) & 0b00011111);
            }
            if(Option.DISPLAY_TYPE == GC9A01){
                col[0]=~col[0];
                col[1]=~col[1];
            }
            map[i]=col[0]|(col[1]<<8)|(col[2]<<16);
        }
    }
    i=(xend-xstart+1)*(yend-ystart+1);
    if(Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ){
        if(PinDef[HAL_PORT_LCD_SPI_CLK_PIN].mode & SPI0SCK){
            if(odd){
                c=map[(*s & 0xF0)>>4];
                spi_write_fast(spi0,(uint8_t *)&c,cnt);
                s++;
                i--;
            }
            while(i>0){
                c=map[*s & 0xF];
                spi_write_fast(spi0,(uint8_t *)&c,cnt);
                if(i>1){
                    c=map[(*s & 0xF0)>>4];
                    spi_write_fast(spi0,(uint8_t *)&c,cnt);
                }
                s++;
                i-=2;
            }
        } else {
            if(odd){
                c=map[(*s & 0xF0)>>4];
                spi_write_fast(spi1,(uint8_t *)&c,cnt);
                s++;
                i--;
            }
            while(i>0){
                c=map[*s & 0xF];
                spi_write_fast(spi1,(uint8_t *)&c,cnt);
                if(i>1){
                    c=map[(*s & 0xF0)>>4];
                    spi_write_fast(spi1,(uint8_t *)&c,cnt);
                }
                s++;
                i-=2;
            }
        }
        if(PinDef[HAL_PORT_LCD_SPI_CLK_PIN].mode & SPI0SCK)spi_finish(spi0);
        else spi_finish(spi1);
        ClearCS(Option.LCD_CS);                  //set CS high
    } else if(Option.DISPLAY_TYPE>=NEXTGEN){
        unsigned char *screen=(unsigned char *)(ScreenBuffer);
        for(int y=ystart;y<=yend;y++){
            unsigned char *p=screen+(y+ScrollStart<VRes? y+ScrollStart : y+ScrollStart-VRes)*HRes;
            for(int x=xstart;x<=xend;x++){
                if(odd){
                    c=map[(*s & 0xF0)>>4];
                    s++;
                    odd^=1;
                } else {
                    c=map[*s & 0xF];
                    odd^=1;
                }
                p[x]=c;
            }
        }
    } else {
        if(screen320  && Option.DISPLAY_TYPE!=SSD1963_4_16){
            unsigned char *q = buff320;
            HRes=720;
            VRes=480;
                uint16_t *pp=(uint16_t *)q;
                if(odd){ //only used for a single line
                    if(odd){
                        c=map[(*s & 0xF0)>>4];
                        *pp++=c;
                        gpio_put(SSD1963_WR_GPPIN,0);
                        gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                        nop;gpio_put(SSD1963_WR_GPPIN,1);
                        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                        s++;
                        i--;
                        int x=1;
                        while(x<=xend-xstart){
                            c=map[*s & 0xF];
                            *pp++=c;
                            gpio_put(SSD1963_WR_GPPIN,0);
                            gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                            nop;gpio_put(SSD1963_WR_GPPIN,1);
                            nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                            if(i>1){
                                c=map[(*s & 0xF0)>>4];
                                *pp++=c;
                                gpio_put(SSD1963_WR_GPPIN,0);
                                gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                                nop;gpio_put(SSD1963_WR_GPPIN,1);
                                nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                            }
                            s++;
                            i-=2;
                            x+=2;
                        }
                        pp=(uint16_t *)q;
                        for(int x=xstart;x<=xend;x++){
                            gpio_put(SSD1963_WR_GPPIN,0);
                            gpio_put_masked64(0xFFFF<<SSD1963data,(*pp++)<<SSD1963data);
                            nop;gpio_put(SSD1963_WR_GPPIN,1);
                            nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                        }
                    }
                } else {
                    for(int y=ystart;y<=yend;y++){
                        pp=(uint16_t *)q;
                        int x=0;
                        while(x<=xend-xstart){
                            c=map[*s & 0xF];
                            *pp++=c;
                            gpio_put(SSD1963_WR_GPPIN,0);
                            gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                            nop;gpio_put(SSD1963_WR_GPPIN,1);
                            nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                            if(i>1){
                                c=map[(*s & 0xF0)>>4];
                                *pp++=c;
                                gpio_put(SSD1963_WR_GPPIN,0);
                                gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                                nop;gpio_put(SSD1963_WR_GPPIN,1);
                                nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                            }
                            s++;
                            i-=2;
                            x+=2;
                        }
                        pp=(uint16_t *)q;
                        for(int x=xstart;x<=xend;x++){
                            gpio_put(SSD1963_WR_GPPIN,0);
                            gpio_put_masked64(0xFFFF<<SSD1963data,(*pp++)<<SSD1963data);
                            nop;gpio_put(SSD1963_WR_GPPIN,1);
                            nop;gpio_put(SSD1963_WR_GPPIN,0);nop;gpio_put(SSD1963_WR_GPPIN,1);
                        }
                    }
                }
            HRes=320;
            VRes=240;
        } else {
            if(Option.DISPLAY_TYPE>SSD_PANEL_8){
                if(odd){
                    c=map[(*s & 0xF0)>>4];
                    gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    s++;
                    i--;
                }
                while(i>0){
                    c=map[*s & 0xF];
                    gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    if(i>1){
                        c=map[(*s & 0xF0)>>4];
                        gpio_put_masked64(0xFFFF<<SSD1963data,c<<SSD1963data);
                        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    }
                    s++;
                    i-=2;
                }
            } else {
                if(odd){
                    c=map[(*s & 0xF0)>>4];
                    gpio_put_masked64(0b11111111<<SSD1963data,((c >> 16) & 0xff)<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    gpio_put_masked64(0b11111111<<SSD1963data,((c >> 8) & 0xff)<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    nop;gpio_put_masked64(0b11111111<<SSD1963data,(c  & 0xff)<<SSD1963data);
                    gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    s++;
                    i--;
                }
                while(i>0){
                    c=map[*s & 0xF];
                    gpio_put_masked64(0b11111111<<SSD1963data,((c >> 16) & 0xff)<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    gpio_put_masked64(0b11111111<<SSD1963data,((c >> 8) & 0xff)<<SSD1963data);
                    nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    nop;gpio_put_masked64(0b11111111<<SSD1963data,(c  & 0xff)<<SSD1963data);
                    gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    if(i>1){
                        c=map[(*s & 0xF0)>>4];
                        gpio_put_masked64(0b11111111<<SSD1963data,((c >> 16) & 0xff)<<SSD1963data);
                        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                        gpio_put_masked64(0b11111111<<SSD1963data,((c >> 8) & 0xff)<<SSD1963data);
                        nop;gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                        nop;gpio_put_masked64(0b11111111<<SSD1963data,(c  & 0xff)<<SSD1963data);
                        gpio_put(SSD1963_WR_GPPIN,0);nop;nop;gpio_put(SSD1963_WR_GPPIN,1);
                    }
                    s++;
                    i-=2;
                }
            }
        }
    }
}
void blitmerge (int x0, int y0, int w, int h, uint8_t colour){
    if(LayerBuf==NULL || FrameBuf==NULL)return;
    uint8_t *ss,*s=LayerBuf;
    uint8_t *d=FrameBuf;
    uint8_t LineBuf[HRes/2];
    uint8_t highcolour=colour<<4;
    hal_display_merge_lock_fb();
    if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE == ILI9488P ){
        while(GetLineILI9341()!=0){}
    }
    for(int y=y0;y<y0+h;y++){
        if(y>VRes-1)break;
        memcpy(LineBuf,d+y*HRes/2,HRes/2);
        ss=s+y*HRes/2;
        for(int x=0;x<HRes/2;x++){
            uint8_t top=*ss & 0xF0;
            uint8_t bottom=*ss++ &0x0f;
            if(top==highcolour && bottom==colour)continue;
            if(top!=highcolour && bottom!=colour)LineBuf[x]=(top|bottom);
            else if(top!=highcolour){
                LineBuf[x]&=0x0F;
                LineBuf[x]|=top;
            } else {
                LineBuf[x]&=0xF0;
                LineBuf[x]|=bottom;
            }
        }
        copyframetoscreen(&LineBuf[x0/2],x0,x0+w-1,y,y,0);
    }
    hal_display_merge_unlock_fb();
    hal_display_merge_mark_done();
}
void merge(uint8_t colour){
    if(LayerBuf==NULL || FrameBuf==NULL)return;
    uint8_t *ss,*s=LayerBuf;
    uint8_t *d=FrameBuf;
    uint8_t LineBuf[HRes/2];
    uint8_t highcolour=colour<<4;
    hal_display_merge_lock_fb();
    if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE == ILI9488P ){
        while(GetLineILI9341()!=0){}
    }
    for(int y=0;y<VRes;y++){
        memcpy(LineBuf,d+y*HRes/2,HRes/2);
        ss=s+y*HRes/2;
        for(int x=0;x<HRes/2;x++){
            uint8_t top=*ss & 0xF0;
            uint8_t bottom=*ss++ &0x0f;
            if(top==highcolour && bottom==colour)continue;
            if(top!=highcolour && bottom!=colour)LineBuf[x]=(top|bottom);
            else if(top!=highcolour){
                LineBuf[x]&=0x0F;
                LineBuf[x]|=top;
            } else {
                LineBuf[x]&=0xF0;
                LineBuf[x]|=bottom;
            }
        }
        copyframetoscreen(LineBuf,0,HRes-1,y,y,0);
    }
    hal_display_merge_unlock_fb();
    hal_display_merge_mark_done();
    low_x=0;low_y=0;high_x=HRes-1;high_y=VRes-1;
}
/*  @endcond */
void cmd_framebuffer(void){
    unsigned char *p=NULL;
    if((p=checkstring(cmdline, (unsigned char *)"CREATE"))) {
        if(FrameBuf==NULL){
            int fast = 0;
            if(checkstring(p, (unsigned char *)"FAST")) fast = 1;
            FrameBuf=GetMemory(HRes*VRes/2);
            if(fast) hal_display_fast_dma_alloc(HRes*VRes/2);
        }
        else error("Framebuffer already exists");
    } else if((p=checkstring(cmdline, (unsigned char *)"WRITE"))) {
        if(checkstring(p, (unsigned char *)"N")){
            hal_display_merge_check_busy();
            restorepanel();
            return;
        }
        else if(checkstring(p, (unsigned char *)"L")){
            if(!LayerBuf)error("Layer buffer not created");
            WriteBuf=LayerBuf;
            setframebuffer();
            return;           
            }
        else if(checkstring(p, (unsigned char *)"F")){
            if(!FrameBuf)error("Frame buffer not created");
            WriteBuf=FrameBuf;
            setframebuffer();
            return;           
        }
        {
            getargs(&p,1,(unsigned char *)",");
            if(argc!=1)error("Syntax");
            char *q=(char *)getCstring(argv[0]);
            if(strcasecmp(q,"N")==0){
                hal_display_merge_check_busy();
                restorepanel();
            } else if(strcasecmp(q,"L")==0){
                if(!LayerBuf)error("Layer buffer not created");
                WriteBuf=LayerBuf;
                setframebuffer();
            } else if(strcasecmp(q,"F")==0){
                if(!FrameBuf)error("Frame buffer not created");
                WriteBuf=FrameBuf;
                setframebuffer();
            } else error("Syntax");
        }
#if !HAL_PORT_IS_VGA
    } else if((p=checkstring(cmdline, (unsigned char *)"SYNC"))) {
        hal_display_merge_sync_wait();
    } else if((p=checkstring(cmdline, (unsigned char *)"MERGE"))) { //merge the layer onto the physical display
        if(!LayerBuf)error("Layer not created");
        if(!FrameBuf)error("Framebuffer not created");
        uint8_t colour=0;
        getargs(&p,5,(unsigned char *)",");
        if(argc>=1 && *argv[0]){
            colour=getint(argv[0],0,15);
        }
        uint8_t background=0;
        if(argc>=3 && *argv[2]){
            if(checkstring(argv[2],(unsigned char *)"B"))background=1;
            else if(checkstring(argv[2],(unsigned char *)"R"))background=2;
            else if(checkstring(argv[2],(unsigned char *)"A"))background=3;
            else error("Syntax");
        }
        if(background && !hal_display_merge_has_pipeline())
            error("Not available on this display");
        if(background==1){
            if(!(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || Option.DISPLAY_TYPE>=NEXTGEN || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL))))error("Not available on this display");
            if(diskchecktimer<200 && SPIatRisk)diskchecktimer = 200;
            hal_display_merge_post_fill(colour);
        } else if(background==2){
            mergetimer=0;
            if(argc==5)mergetimer=getint(argv[4],0,60*10*1000);
            if(!(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL))))error("Not available on this display");
            if(WriteBuf==NULL)WriteBuf=FrameBuf;
            setframebuffer();
            hal_display_merge_post_bg(colour, mergetimer*1000);
        } else if(background==3){
            hal_display_merge_abort();
        } else {
            merge(colour);
        }
#endif
    } else if((p=checkstring(cmdline, (unsigned char *)"LAYER"))) {
        if(LayerBuf==NULL){
            LayerBuf=GetMemory(HRes*VRes/2);
        } else error("Layer already exists");
    } else if((p=checkstring(cmdline, (unsigned char *)"WAIT"))) {
        if(Option.DISPLAY_TYPE==ILI9341 || Option.DISPLAY_TYPE == ST7796SP || Option.DISPLAY_TYPE == ST7796S || Option.DISPLAY_TYPE==ST7789B || Option.DISPLAY_TYPE==ILI9488 || Option.DISPLAY_TYPE == ILI9488P ){
            while(GetLineILI9341()!=0){}
        }
    } else if((p=checkstring(cmdline, (unsigned char *)"CLOSE"))) {
        if(checkstring(p, (unsigned char *)"F")){
            hal_display_merge_abort();
            if(WriteBuf!=LayerBuf)restorepanel();
            if(FrameBuf)FreeMemory(FrameBuf);
            FrameBuf=NULL;
            hal_display_fast_dma_free();
        } else if(checkstring(p, (unsigned char *)"L")){
            hal_display_merge_abort();
            if(WriteBuf!=FrameBuf)restorepanel();
            if(LayerBuf)FreeMemory(LayerBuf);
            LayerBuf=NULL;
        } else  closeframebuffer('A');
    } else if((p=checkstring(cmdline, (unsigned char *)"COPY"))) {
        int complex=0, background=0;
        unsigned char *buff = WriteBuf;
        getargs(&p,5,(unsigned char *)",");
        if(!(argc==3 || argc==5))error("Syntax");
        if(argc==5){
            if(checkstring(argv[4],(unsigned char *)"B"))background=1;
            else error("Syntax");
        }
        if(background && !hal_display_merge_has_pipeline())
            error("Not available on this display");
        uint8_t *s=NULL,*d=NULL;
        if(checkstring(argv[0],(unsigned char *)"N")){
            complex=1;
            if((void *)ReadBuffer == (void *)DisplayNotSet) error("Invalid on this display");
        }
        else if(checkstring(argv[0],(unsigned char *)"L"))s=LayerBuf;
        else if(checkstring(argv[0],(unsigned char *)"F"))s=FrameBuf;
        else error("Syntax");
        if(checkstring(argv[2],(unsigned char *)"N")){
            complex=2;
        }
        else if(checkstring(argv[2],(unsigned char *)"L"))d=LayerBuf;
        else if(checkstring(argv[2],(unsigned char *)"F"))d=FrameBuf;
        else error("Syntax");
        
        if(d!=s){
            if(!complex) memcpy(d,s,HRes*VRes/2);
            else {
                if(complex==1){//copying from the real display
                    char *LCDBuffer=GetTempMemory(HRes*3);
                    int DisplayMode=0;
                    if(DrawBufferSPI==DrawBuffer || DrawBufferSSD1963==DrawBuffer) DisplayMode=1;
                    WriteBuf=d;
                    for(int y=0;y<VRes;y++){
                        restorepanel();   
                        ReadBuffer(0,y,HRes-1,y,(unsigned char *)LCDBuffer);
                        WriteBuf=d;
                        setframebuffer();
                        DrawBuffer(0,y,HRes-1,y,(unsigned char *)LCDBuffer);
                    }
                    if(DisplayMode) restorepanel();  
                } else { //copying to the real display
                    if(background){
                        if(!(((Option.DISPLAY_TYPE>I2C_PANEL && Option.DISPLAY_TYPE<BufferedPanel ) || Option.DISPLAY_TYPE>=NEXTGEN || (Option.DISPLAY_TYPE>=SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL))))error("Not available on this display");
                        if(diskchecktimer<100 && SPIatRisk) diskchecktimer=100;
                        hal_display_merge_post_copy(s);
                    } else {
                        copyframetoscreen(s,0,HRes-1,0,VRes-1,0);
                    }
                }
            }
        }
        WriteBuf=buff;
    } else error("Syntax");
}
