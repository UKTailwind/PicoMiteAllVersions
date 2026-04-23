/*
 * drivers/vga_pio/vga_blit_ops.c — VGA cmd_blit COPY fast path.
 *
 * Lifted verbatim from Draw.c's `#ifdef PICOMITEVGA` block inside
 * cmd_blit. Covers the byte/nibble moves for SCREENMODE2/3, HDMI
 * direct mem copy for SCREENMODE4/5, a SCREENMODE1 tile-copy path,
 * and a generic ReadBLITBuffer/DrawBLITBuffer fallback.
 *
 * Linked only on PICOMITEVGA (VGA + HDMI); non-VGA targets link
 * drivers/vga_pio/vga_ops_stub.c which returns 0 and the caller does
 * nothing.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "port_config.h"
#include "hal/hal_vga_ops.h"

int hal_vga_ops_handle_blit_move(int x1, int y1, int x2, int y2, int w, int h) {
        if(DISPLAY_TYPE==SCREENMODE2 || DISPLAY_TYPE==SCREENMODE3){
            if((w & 1)==0 && (x1 & 1)==0 && (x2 & 1)==0){ //Easiest case - byte move in the x direction with w even
                if(y1<y2){
                    for(int y=h-1; y>=0;y--){
                        volatile uint8_t *in=WriteBuf + ((y+y1)*HRes + x1)/2;
                        volatile uint8_t *out=WriteBuf + ((y+y2)*HRes + x2)/2;
                        memcpy((void *)out,(void *)in,w/2);
                    }
                } else if(y1>y2){
                    for(int y=0;y<h;y++){
                        volatile uint8_t *in=WriteBuf + ((y+y1)*HRes + x1)/2;
                        volatile uint8_t *out=WriteBuf + ((y+y2)*HRes + x2)/2;
                        memcpy((void *)out,(void *)in,w/2);
                    }
                } else {
                    for(int y=0;y<h;y++){
                        volatile uint8_t *in=WriteBuf + ((y+y1)*HRes + x1)/2;
                        volatile uint8_t *out=WriteBuf + ((y+y2)*HRes + x2)/2;
                        memmove((void *)out,(void *)in,w/2);
                    }
                }
                return 1;
            } else { //nibble move not as easy
                uint8_t *inbuff=GetTempMemory(HRes/2);
                int intoggle=x1 & 1;
                int outtoggle=x2 & 1;
                int n=w/2;
                if(w & 1)n++;
                if(y1>y2){
                    for(int y=0;y<h;y++){
                        if(!intoggle)memcpy(inbuff,(void *)WriteBuf + ((y+y1)*HRes + x1)/2, n);
                        else {
                            int toggle=1;
                            volatile uint8_t *in=WriteBuf + ((y+y1)*HRes + x1)/2;
                            volatile uint8_t *out=inbuff;
                            for(int x=0;x<w;x++){
                                if(toggle){
                                    uint8_t t=*in >>4 ;
                                    *out =t ;
                                    in++;
                                } else {
                                    uint8_t t=(*in & 0xf)<<4;
                                    *out|= t;
                                    out++;
                                }
                                toggle ^=1;
                            }
                        }
                        if(!outtoggle){
                            memcpy((void *)WriteBuf + ((y+y2)*HRes + x2)/2, inbuff, w/2);
                            if(w & 1){
                                volatile uint8_t *lastnibble=WriteBuf + ((y+y2) * HRes + x2 + w)/2;
                                *lastnibble  &= 0xf0;
                                *lastnibble |= (inbuff[w/2] & 0xf);
                            }
                        } else {
                            int toggle=1;
                            volatile uint8_t *in=inbuff;
                            volatile uint8_t *out=WriteBuf + ((y+y2) * HRes + x2)/2;
                            for(int x=0;x<w;x++){
                                if(toggle){
                                    uint8_t t=(*in & 0xf)<<4;
                                    *out &=0x0f; //clear the top byte of the output
                                    *out |=t;
                                    out++;
                                } else {
                                    uint8_t t=(*in >>4);
                                    *out &=0xf0;
                                    *out|= t;
                                    in++;
                                }
                                toggle ^=1;
                            }
                        }
                    }
                } else {
                    for(int y=h-1;y>=0;y--){
                        if(!intoggle)memcpy(inbuff,(void *)WriteBuf + ((y+y1)*HRes + x1)/2, n);
                        else {
                            int toggle=1;
                            volatile uint8_t *in=WriteBuf + ((y+y1)*HRes + x1)/2;
                            volatile uint8_t *out=inbuff;
                            for(int x=0;x<w;x++){
                                if(toggle){
                                    uint8_t t=*in >>4 ;
                                    *out =t ;
                                    in++;
                                } else {
                                    uint8_t t=(*in & 0xf)<<4;
                                    *out|= t;
                                    out++;
                                }
                                toggle ^=1;
                            }
                        }
                        if(!outtoggle){
                            memcpy((void *)WriteBuf + ((y+y2)*HRes + x2)/2, inbuff, w/2);
                            if(w & 1){
                                volatile uint8_t *lastnibble=WriteBuf + ((y+y2) * HRes + x2 + w)/2;
                                *lastnibble  &= 0xf0;
                                *lastnibble |= (inbuff[w/2] & 0xf);
                            }
                        } else {
                            int toggle=1;
                            volatile uint8_t *in=inbuff;
                            volatile uint8_t *out=WriteBuf + ((y+y2) * HRes + x2)/2;
                            for(int x=0;x<w;x++){
                                if(toggle){
                                    uint8_t t=(*in & 0xf)<<4;
                                    *out &=0x0f; //clear the top byte of the output
                                    *out |=t;
                                    out++;
                                } else {
                                    uint8_t t=(*in >>4);
                                    *out &=0xf0;
                                    *out|= t;
                                    in++;
                                }
                                toggle ^=1;
                            }
                        }
                    }
                }
                return 1;
            }
        } else if(DISPLAY_TYPE && (DISPLAY_TYPE==SCREENMODE4 || DISPLAY_TYPE==SCREENMODE5)){
            unsigned char *buff = NULL;
            int max_x;
            if(x1 >= x2) {
                max_x = 1;
                buff = GetMemory((max_x * h) * (DISPLAY_TYPE==SCREENMODE4 ? 2 : 1));
                while(w > max_x){
                    ReadBufferFast(x1, y1, x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(x2, y2, x2 + max_x - 1, y2 + h - 1, -1, buff);
                    x1 += max_x;
                    x2 += max_x;
                    w -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
            }
            if(x1 < x2) {
                int start_x1, start_x2;
                max_x = 1;
                buff = GetMemory((max_x * h) * (DISPLAY_TYPE==SCREENMODE4 ? 2 : 1));
                start_x1 = x1 + w - max_x;
                start_x2 = x2 + w - max_x;
                while(w > max_x){
                    ReadBufferFast(start_x1, y1, start_x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(start_x2, y2, start_x2 + max_x - 1, y2 + h - 1, -1, buff);
                    w -= max_x;
                    start_x1 -= max_x;
                    start_x2 -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
            }
       } else if(DISPLAY_TYPE && DISPLAY_TYPE==SCREENMODE1){
            unsigned char *buff = NULL;
            int max_x, ww;
            ww=w;
            if(x1 >= x2) {
                max_x = 1;
                buff = GetMemory((max_x * h)>>1);
                while(w > max_x){
                    ReadBufferFast(x1, y1, x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(x2, y2, x2 + max_x - 1, y2 + h - 1, -1, buff);
                    x1 += max_x;
                    x2 += max_x;
                    w -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
                if((x1 % 8==0) && (x2 % 8 ==0) && (y1 % ytileheight==0) && (y2 % ytileheight ==0) && (ww % 8==0) && (h % ytileheight==0)){
                    int tx1=x1/8;
                    int xc=ww/8;
                    int ty1=y1/ytileheight;
                    int yc=h/ytileheight;
                    int tx2=x2/8;
                    int ty2=y2/ytileheight;
                    for (int x=0;x<xc;x++){
                        for(int y=0;y<yc;y++){
                            int s=(y+ty1)*X_TILE+x+tx1;
                            int d=(y+ty2)*X_TILE+x+tx2;
                            tilefcols[d]=tilefcols[s];
                            tilebcols[d]=tilebcols[s];
                        }
                    }
                }
                    return 1;
            }
            if(x1 < x2) {
                int start_x1, start_x2;
                max_x = 1;
                buff = GetMemory(max_x * h);
                start_x1 = x1 + w - max_x;
                start_x2 = x2 + w - max_x;
                while(w > max_x){
                    ReadBufferFast(start_x1, y1, start_x1 + max_x - 1, y1 + h - 1, buff);
                    DrawBufferFast(start_x2, y2, start_x2 + max_x - 1, y2 + h - 1, -1, buff);
                    w -= max_x;
                    start_x1 -= max_x;
                    start_x2 -= max_x;
                }
                ReadBufferFast(x1, y1, x1 + w - 1, y1 + h - 1, buff);
                DrawBufferFast(x2, y2, x2 + w - 1, y2 + h - 1, -1, buff);
                FreeMemory(buff);
                if((x1 % 8==0) && (x2 % 8 ==0) && (y1 % ytileheight==0) && (y2 % ytileheight ==0) && (ww % 8==0) && (h % ytileheight==0)){
                    int tx1=x1/8;
                    int xc=ww/8;
                    int ty1=y1/ytileheight;
                    int yc=h/ytileheight;
                    int tx2=x2/8;
                    int ty2=y2/ytileheight;
                    for (int x=xc-1;x>=0;x--){
                        for(int y=0;y<yc;y++){
                            int s=(y+ty1)*X_TILE+x+tx1;
                            int d=(y+ty2)*X_TILE+x+tx2;
                            tilefcols[d]=tilefcols[s];
                            tilebcols[d]=tilebcols[s];
                        }
                    }
                }
                return 1;
            }
        }
        return 1;
    }
