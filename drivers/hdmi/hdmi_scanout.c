/*
 * drivers/hdmi/hdmi_scanout.c — HDMI TMDS scanout engine for RP2350
 * HDMI variants (HDMI / HDMIUSB).
 *
 * Extracted as a file-level relocation from PicoMite.c's
 * `#ifdef PICOMITEVGA / #ifndef HDMI ... #else [HDMI body] #endif /
 * #endif` block. Pure move — no behaviour change. The HDMI target's
 * CMakeLists.txt wiring links this file in place of the
 * drivers/vga_pio/ PIO scanout code.
 *
 * Owns:
 *   - HSTX / TMDS encoder state, sync-line constants, pixel-line DMA.
 *   - dma_irq_handler0 : the vsync/active-line IRQ, chains v_scanline.
 *   - HDMIloop0 / HDMIloop1 / HDMIloop2 / HDMIloop3 / HDMIloopX :
 *     per-CPU-speed scanout inner loops running on core1.
 *   - settiles : mode-1 tile-metadata reset (HDMI flavour).
 *
 * Symbols consumed by other modules through Hardware_Includes.h:
 *   v_scanline, settiles. Everything else stays internal.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/exception.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/systick.h"
#include "hardware/structs/timer.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sysinfo.h"
#include "hardware/regs/powman.h"
#include "hardware/vreg.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "configuration.h"
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "bytecode.h"

extern int vgaloop1, vgaloop2, vgaloop4, vgaloop8, vgaloop16, vgaloop32;

extern uint16_t HDMIlines[2][800];
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_S_SYNC_POLARITY 0
#define MODE_H_S_FRONT_PORCH   (Option.CPU_Speed % 126000==0 ? 16 : 16)
#define MODE_H_S_SYNC_WIDTH    (Option.CPU_Speed % 126000==0 ? 96 : 64)
#define MODE_H_S_BACK_PORCH    (Option.CPU_Speed % 126000==0 ? 48 : 120)

#define MODE_V_S_SYNC_POLARITY 0
#define MODE_V_S_FRONT_PORCH   (Option.CPU_Speed % 126000==0 ? 10 : 1)
#define MODE_V_S_SYNC_WIDTH    (Option.CPU_Speed % 126000==0 ? 2 : 3)
#define MODE_V_S_BACK_PORCH    (Option.CPU_Speed % 126000==0 ? 33 : 16)

#define MODE_H_F_SYNC_POLARITY 0
#define MODE_H_F_ACTIVE_PIXELS 640
#define MODE_H_F_FRONT_PORCH   16
#define MODE_H_F_SYNC_WIDTH    96
#define MODE_H_F_BACK_PORCH    48

#define MODE_V_F_SYNC_POLARITY 0
#define MODE_V_F_ACTIVE_LINES 480
#define MODE_V_F_FRONT_PORCH   10
#define MODE_V_F_SYNC_WIDTH    2
#define MODE_V_F_BACK_PORCH    33

#define MODE_H_8_SYNC_POLARITY 1
#define MODE_H_8_FRONT_PORCH   16
#define MODE_H_8_SYNC_WIDTH    112
#define MODE_H_8_BACK_PORCH    112

#define MODE_V_8_SYNC_POLARITY 1
#define MODE_V_8_FRONT_PORCH   8
#define MODE_V_8_SYNC_WIDTH    6
#define MODE_V_8_BACK_PORCH    23

#define MODE_H_4_SYNC_POLARITY 0
#define MODE_H_4_FRONT_PORCH   18
#define MODE_H_4_SYNC_WIDTH    108
#define MODE_H_4_BACK_PORCH    54

#define MODE_V_4_SYNC_POLARITY 0
#define MODE_V_4_FRONT_PORCH   12
#define MODE_V_4_SYNC_WIDTH    2
#define MODE_V_4_BACK_PORCH    35

#define MODE_H_W_SYNC_POLARITY 1
#define MODE_H_W_FRONT_PORCH   110
#define MODE_H_W_SYNC_WIDTH    40
#define MODE_H_W_BACK_PORCH    220

#define MODE_V_W_SYNC_POLARITY 1
#define MODE_V_W_FRONT_PORCH   5
#define MODE_V_W_SYNC_WIDTH    5
#define MODE_V_W_BACK_PORCH    20


#define MODE_H_L_SYNC_POLARITY 0
#define MODE_H_L_FRONT_PORCH   24
#define MODE_H_L_SYNC_WIDTH    136
#define MODE_H_L_BACK_PORCH    144

#define MODE_V_L_SYNC_POLARITY 0
#define MODE_V_L_FRONT_PORCH   3
#define MODE_V_L_SYNC_WIDTH    6
#define MODE_V_L_BACK_PORCH    29

#define MODE_H_V_SYNC_POLARITY 1
#define MODE_H_V_FRONT_PORCH   24
#define MODE_H_V_SYNC_WIDTH    72
#define MODE_H_V_BACK_PORCH    128

#define MODE_V_V_SYNC_POLARITY 1
#define MODE_V_V_FRONT_PORCH   1
#define MODE_V_V_SYNC_WIDTH    2
#define MODE_V_V_BACK_PORCH    22

#define MODE_H_X_SYNC_POLARITY 1
#define MODE_H_X_FRONT_PORCH   24
#define MODE_H_X_SYNC_WIDTH    136
#define MODE_H_X_BACK_PORCH    160

#define MODE_V_X_SYNC_POLARITY 0
#define MODE_V_X_FRONT_PORCH   1
#define MODE_V_X_SYNC_WIDTH    4
#define MODE_V_X_BACK_PORCH    23

#define MODE_H_Y_SYNC_POLARITY 0
#define MODE_H_Y_FRONT_PORCH   32
#define MODE_H_Y_SYNC_WIDTH    80
#define MODE_H_Y_BACK_PORCH    112

#define MODE_V_Y_SYNC_POLARITY 1
#define MODE_V_Y_FRONT_PORCH   3
#define MODE_V_Y_SYNC_WIDTH    10
#define MODE_V_Y_BACK_PORCH    7

#define MODE_H_S_TOTAL_PIXELS ( \
    MODE_H_S_FRONT_PORCH + MODE_H_S_SYNC_WIDTH + \
    MODE_H_S_BACK_PORCH  + MODE_H_S_ACTIVE_PIXELS \
)
#define MODE_V_S_TOTAL_LINES  ( \
    MODE_V_S_FRONT_PORCH + MODE_V_S_SYNC_WIDTH + \
    MODE_V_S_BACK_PORCH  + MODE_V_S_ACTIVE_LINES \
)
#define MODE_H_F_TOTAL_PIXELS ( \
    MODE_H_F_FRONT_PORCH + MODE_H_F_SYNC_WIDTH + \
    MODE_H_F_BACK_PORCH  + MODE_H_F_ACTIVE_PIXELS \
)
#define MODE_V_F_TOTAL_LINES  ( \
    MODE_V_F_FRONT_PORCH + MODE_V_F_SYNC_WIDTH + \
    MODE_V_F_BACK_PORCH  + MODE_V_F_ACTIVE_LINES \
)
#define MODE_H_W_TOTAL_PIXELS ( \
    MODE_H_W_FRONT_PORCH + MODE_H_W_SYNC_WIDTH + \
    MODE_H_W_BACK_PORCH  + MODE_H_W_ACTIVE_PIXELS \
)
#define MODE_V_W_TOTAL_LINES  ( \
    MODE_V_W_FRONT_PORCH + MODE_V_W_SYNC_WIDTH + \
    MODE_V_W_BACK_PORCH  + MODE_V_W_ACTIVE_LINES \
)
#define MODE_H_L_TOTAL_PIXELS ( \
    MODE_H_L_FRONT_PORCH + MODE_H_L_SYNC_WIDTH + \
    MODE_H_L_BACK_PORCH  + MODE_H_L_ACTIVE_PIXELS \
)
#define MODE_V_L_TOTAL_LINES  ( \
    MODE_V_L_FRONT_PORCH + MODE_V_L_SYNC_WIDTH + \
    MODE_V_L_BACK_PORCH  + MODE_V_L_ACTIVE_LINES \
)
#define MODE_H_V_TOTAL_PIXELS ( \
    MODE_H_V_FRONT_PORCH + MODE_H_V_SYNC_WIDTH + \
    MODE_H_V_BACK_PORCH  + MODE_H_V_ACTIVE_PIXELS \
)
#define MODE_V_V_TOTAL_LINES  ( \
    MODE_V_V_FRONT_PORCH + MODE_V_V_SYNC_WIDTH + \
    MODE_V_V_BACK_PORCH  + MODE_V_V_ACTIVE_LINES \
)
#define MODE_H_8_TOTAL_PIXELS ( \
    MODE_H_8_FRONT_PORCH + MODE_H_8_SYNC_WIDTH + \
    MODE_H_8_BACK_PORCH  + MODE_H_8_ACTIVE_PIXELS \
)
#define MODE_V_8_TOTAL_LINES  ( \
    MODE_V_8_FRONT_PORCH + MODE_V_8_SYNC_WIDTH + \
    MODE_V_8_BACK_PORCH  + MODE_V_8_ACTIVE_LINES \
)
#define MODE_H_4_TOTAL_PIXELS ( \
    MODE_H_4_FRONT_PORCH + MODE_H_4_SYNC_WIDTH + \
    MODE_H_4_BACK_PORCH  + MODE_H_4_ACTIVE_PIXELS \
)
#define MODE_V_4_TOTAL_LINES  ( \
    MODE_V_4_FRONT_PORCH + MODE_V_4_SYNC_WIDTH + \
    MODE_V_4_BACK_PORCH  + MODE_V_4_ACTIVE_LINES \
)
#define MODE_H_X_TOTAL_PIXELS ( \
    MODE_H_X_FRONT_PORCH + MODE_H_X_SYNC_WIDTH + \
    MODE_H_X_BACK_PORCH  + MODE_H_X_ACTIVE_PIXELS \
)
#define MODE_V_X_TOTAL_LINES  ( \
    MODE_V_X_FRONT_PORCH + MODE_V_X_SYNC_WIDTH + \
    MODE_V_X_BACK_PORCH  + MODE_V_X_ACTIVE_LINES \
)
#define MODE_H_Y_TOTAL_PIXELS ( \
    MODE_H_Y_FRONT_PORCH + MODE_H_Y_SYNC_WIDTH + \
    MODE_H_Y_BACK_PORCH  + MODE_H_Y_ACTIVE_PIXELS \
)
#define MODE_V_Y_TOTAL_LINES  ( \
    MODE_V_Y_FRONT_PORCH + MODE_V_Y_SYNC_WIDTH + \
    MODE_V_Y_BACK_PORCH  + MODE_V_Y_ACTIVE_LINES \
)

volatile int mode = 1;
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)
#define DMACH_PING 0
#define DMACH_PONG 1
const uint32_t MAP256DEF[256] =
{
0x0,0x55,0xAA,0xFF,0x2400,0x2455,0x24AA,0x24FF,
0x4900,0x4955,0x49AA,0x49FF,0x6D00,0x6D55,0x6DAA,0x6DFF,
0x9200,0x9255,0x92AA,0x92FF,0xB600,0xB655,0xB6AA,0xB6FF,
0xDB00,0xDB55,0xDBAA,0xDBFF,0xFF00,0xFF55,0xFFAA,0xFFFF,
0x240000,0x240055,0x2400AA,0x2400FF,0x242400,0x242455,0x2424AA,0x2424FF,
0x244900,0x244955,0x2449AA,0x2449FF,0x246D00,0x246D55,0x246DAA,0x246DFF,
0x249200,0x249255,0x2492AA,0x2492FF,0x24B600,0x24B655,0x24B6AA,0x24B6FF,
0x24DB00,0x24DB55,0x24DBAA,0x24DBFF,0x24FF00,0x24FF55,0x24FFAA,0x24FFFF,
0x490000,0x490055,0x4900AA,0x4900FF,0x492400,0x492455,0x4924AA,0x4924FF,
0x494900,0x494955,0x4949AA,0x4949FF,0x496D00,0x496D55,0x496DAA,0x496DFF,
0x499200,0x499255,0x4992AA,0x4992FF,0x49B600,0x49B655,0x49B6AA,0x49B6FF,
0x49DB00,0x49DB55,0x49DBAA,0x49DBFF,0x49FF00,0x49FF55,0x49FFAA,0x49FFFF,
0x6D0000,0x6D0055,0x6D00AA,0x6D00FF,0x6D2400,0x6D2455,0x6D24AA,0x6D24FF,
0x6D4900,0x6D4955,0x6D49AA,0x6D49FF,0x6D6D00,0x6D6D55,0x6D6DAA,0x6D6DFF,
0x6D9200,0x6D9255,0x6D92AA,0x6D92FF,0x6DB600,0x6DB655,0x6DB6AA,0x6DB6FF,
0x6DDB00,0x6DDB55,0x6DDBAA,0x6DDBFF,0x6DFF00,0x6DFF55,0x6DFFAA,0x6DFFFF,
0x920000,0x920055,0x9200AA,0x9200FF,0x922400,0x922455,0x9224AA,0x9224FF,
0x924900,0x924955,0x9249AA,0x9249FF,0x926D00,0x926D55,0x926DAA,0x926DFF,
0x929200,0x929255,0x9292AA,0x9292FF,0x92B600,0x92B655,0x92B6AA,0x92B6FF,
0x92DB00,0x92DB55,0x92DBAA,0x92DBFF,0x92FF00,0x92FF55,0x92FFAA,0x92FFFF,
0xB60000,0xB60055,0xB600AA,0xB600FF,0xB62400,0xB62455,0xB624AA,0xB624FF,
0xB64900,0xB64955,0xB649AA,0xB649FF,0xB66D00,0xB66D55,0xB66DAA,0xB66DFF,
0xB69200,0xB69255,0xB692AA,0xB692FF,0xB6B600,0xB6B655,0xB6B6AA,0xB6B6FF,
0xB6DB00,0xB6DB55,0xB6DBAA,0xB6DBFF,0xB6FF00,0xB6FF55,0xB6FFAA,0xB6FFFF,
0xDB0000,0xDB0055,0xDB00AA,0xDB00FF,0xDB2400,0xDB2455,0xDB24AA,0xDB24FF,
0xDB4900,0xDB4955,0xDB49AA,0xDB49FF,0xDB6D00,0xDB6D55,0xDB6DAA,0xDB6DFF,
0xDB9200,0xDB9255,0xDB92AA,0xDB92FF,0xDBB600,0xDBB655,0xDBB6AA,0xDBB6FF,
0xDBDB00,0xDBDB55,0xDBDBAA,0xDBDBFF,0xDBFF00,0xDBFF55,0xDBFFAA,0xDBFFFF,
0xFF0000,0xFF0055,0xFF00AA,0xFF00FF,0xFF2400,0xFF2455,0xFF24AA,0xFF24FF,
0xFF4900,0xFF4955,0xFF49AA,0xFF49FF,0xFF6D00,0xFF6D55,0xFF6DAA,0xFF6DFF,
0xFF9200,0xFF9255,0xFF92AA,0xFF92FF,0xFFB600,0xFFB655,0xFFB6AA,0xFFB6FF,
0xFFDB00,0xFFDB55,0xFFDBAA,0xFFDBFF,0xFFFF00,0xFFFF55,0xFFFFAA,0xFFFFFF
};
const uint32_t MAP16DEF[16] = {0x00,0xFF,0x5500,0x55ff,0xAA00,0xAAff,0xff00,0xffff,0xff0000,0xff00FF,0xff5500,0xff55ff,0xffAA00,0xffAAff,0xffff00,0xffffff};
const uint32_t MAP4DEF[4] = {0,0xFF,0xFF00,0xFF0000};
uint16_t map256[256];
static uint32_t vblank_line_vsync_off[7] ;
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];
static bool dma_pong = false;
int MODE_H_SYNC_POLARITY, MODE_V_TOTAL_LINES, MODE_ACTIVE_LINES, MODE_ACTIVE_PIXELS;
int MODE_H_ACTIVE_PIXELS, MODE_H_FRONT_PORCH, MODE_H_SYNC_WIDTH, MODE_H_BACK_PORCH;
int MODE_V_SYNC_POLARITY ,MODE_V_ACTIVE_LINES ,MODE_V_FRONT_PORCH, MODE_V_SYNC_WIDTH, MODE_V_BACK_PORCH;
int PIXELS_PER_WORD, TRANSFER_COUNT, BLANKING_COUNT;
// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
volatile int32_t v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;
void MIPS64 __not_in_flash_func(dma_irq_handler0)() {
    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;
 
    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)HDMIlines[v_scanline & 1];
        ch->transfer_count = TRANSFER_COUNT;
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    } 
}

// ----------------------------------------------------------------------------
// Main program


void MIPS32 __not_in_flash_func(HDMIloopX)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_X_TOTAL_LINES - MODE_V_X_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_X_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1024x768x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*MODE_H_X_ACTIVE_PIXELS/8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*MODE_H_X_ACTIVE_PIXELS/8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<MODE_H_X_ACTIVE_PIXELS/32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //256 x 192 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_X_ACTIVE_PIXELS/8;
                        for(int i=0; i<MODE_H_X_ACTIVE_PIXELS/8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //512 x 384 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*MODE_H_X_ACTIVE_PIXELS/4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<MODE_H_X_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //256 x 192 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_X_ACTIVE_PIXELS/4;
                        for(int i=0; i<MODE_H_X_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void MIPS32 __not_in_flash_func(HDMIloop1)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_W_TOTAL_LINES - MODE_V_W_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_W_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1280x720x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*MODE_H_W_ACTIVE_PIXELS/8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*MODE_H_W_ACTIVE_PIXELS/8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //320 x 180 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_W_ACTIVE_PIXELS/8;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //640 x 360 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*MODE_H_W_ACTIVE_PIXELS/4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //320 x 180 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_W_ACTIVE_PIXELS/4;
                        for(int i=0; i<MODE_H_W_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void MIPS32 __not_in_flash_func(HDMIloop2)(void){
    int last_line=2,load_line, line_to_load, Line_dup, Line_quad;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_L_TOTAL_LINES - MODE_V_L_ACTIVE_LINES);
            Line_dup=load_line>>1;
            Line_quad=load_line>>2;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_L_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //1024x768x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint32_t *pp=(uint32_t *)&DisplayBuf[load_line*MODE_H_L_ACTIVE_PIXELS/8];
                        uint32_t *qq=(uint32_t *)&LayerBuf[load_line*MODE_H_L_ACTIVE_PIXELS/8];
                        uint32_t d=*pp | *qq;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/32 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //256 x 192 x 4bit-colour mapped to 256
                    {
                        uint32_t *p=(uint32_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_L_ACTIVE_PIXELS/8;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/8 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparents){
                                *p++=map16quads[s&0xf];
                            } else {
                                if((l&0xf)!=transparent){
                                    *p++=map16quads[l&0xf];
                                } else {
                                    *p++=map16quads[d&0xf];
                                }
                            }
                        }
                    }
                    break;
            case SCREENMODE3: //512 x 384 x 4bit-colour mapped to 256
                    {
                        int pp= (Line_dup)*MODE_H_L_ACTIVE_PIXELS/4;
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //256 x 192 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d,s;
                        int pp= (Line_quad)*MODE_H_L_ACTIVE_PIXELS/4;
                        for(int i=0; i<MODE_H_L_ACTIVE_PIXELS/4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];s=SecondLayer[pp+i];
                            if(s!=transparents){
                                *p++=s;
                                *p++=s;
                                *p++=s;
                                *p++=s;
                            } else {
                                if(l!=transparent){
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                    *p++=l;
                                } else {
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                    *p++=d;
                                }
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void MIPS32 __not_in_flash_func(HDMIloop3)(void){
    int last_line=2,load_line, line_to_load, Line_dup;
    while(1){
        if(v_scanline!=last_line){
            last_line=v_scanline;
            load_line=v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            Line_dup=load_line>>1;
            line_to_load = last_line & 1;
            if(load_line>=0 && load_line<MODE_V_ACTIVE_LINES){
                __dmb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //800x600x2 or 848/800x480x2 colour with tiles
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t *fcol_w=tilefcols_w+load_line/ytileheight*X_TILE, *bcol_w=tilebcols_w+load_line/ytileheight*X_TILE; //get the relevant tile
                        uint16_t *pp=(uint16_t *)&DisplayBuf[load_line*vgaloop8];
                        uint16_t *qq=(uint16_t *)&LayerBuf[load_line*vgaloop8];
                        uint16_t d=*pp | *qq;
                        for(int i=0; i<vgaloop16 ; i++){
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            *p++ = (d&0x1) ? *fcol_w : *bcol_w;
                            d>>=1;
                            fcol_w++;
                            bcol_w++;
                            d=*(++pp) | *(++qq) ;
                        }
                    }
                    break;
                case SCREENMODE2: //400 X 300 x 4bit-colour mapped to 256 or 424/400 X 240 x 4bit-colour mapped to 256
                    {
                        uint16_t *p=(uint16_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        int pp= (Line_dup)*vgaloop4;
                        for(int i=0; i<vgaloop4 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE3: //800 x 600 x 4bit-colour mapped to 256 or 848/800 x 480 x 4bit-colour mapped to 256
                    {
                        int pp= load_line*vgaloop2;
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        for(int i=0; i<vgaloop2 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                            d>>=4;l>>=4;
                            if((l&0xf)!=transparent){
                                *p++=map16quads[l&0xf];
                            } else {
                                *p++=map16quads[d&0xf];
                            }
                        }
                    }
                    break;
                case SCREENMODE5: //400 x 300 x 8bit-colour or 424/400 x 240 x 8bit-colour 
                    {
                        uint8_t *p=(uint8_t *)HDMIlines[line_to_load];
                        uint8_t l,d;
                        int pp= (Line_dup)*vgaloop2;
                        for(int i=0; i<vgaloop2 ; i++){
                            l=LayerBuf[pp+i];d=DisplayBuf[pp+i];
                            if(l!=transparent){
                                *p++=l;
                                *p++=l;
                            } else {
                                *p++=d;
                                *p++=d;
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}

void MIPS32 __not_in_flash_func(HDMIloop0)(void){
    int last_line=2,load_line, line_to_load, line;
    while (1){
        if(v_scanline!=last_line){
            uint8_t transparent16=(uint8_t)transparent;
            uint8_t transparent16s=(uint8_t)transparents;
            last_line=v_scanline;
            load_line=line=v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
            if(HRes==vgaloop2)line>>=1;
            line_to_load = last_line & 1;
            uint8_t l,d,s;
            register unsigned char *dd;
            register unsigned char *ll;
            int pp;
            uint16_t *p=HDMIlines[line_to_load];
            if(load_line>=0 && load_line<MODE_V_ACTIVE_LINES){
                __dsb();
                switch(DISPLAY_TYPE){
                case SCREENMODE1: //720x400x2 colour
                    uint16_t *fcol=tilefcols+line/ytileheight*X_TILE, *bcol=tilebcols+line/ytileheight*X_TILE; //get the relevant tile
                    pp= line*vgaloop8;
                    dd=&DisplayBuf[pp];
                    ll=&LayerBuf[pp];
                    for(int i=0; i<vgaloop8 ; i++){
                        d=*dd | *ll;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        d>>=1;
                        *p++ = (d&0x1) ? fcol[i] : bcol[i];
                        ll++;dd++;
                    }
                    break;
                case SCREENMODE2: //360x200x16 colour with support for a top layer
                    {
                        pp= (line)*vgaloop4;
                        uint32_t *up=(uint32_t *)p;
                        dd=&DisplayBuf[pp];
                        ll=&LayerBuf[pp];
                        volatile register unsigned char *ss=&SecondLayer[pp];
                        if(ss==dd){
                            ss=ll;
                            transparent16s=transparent16;
                        }
                        for(int i=0; i<vgaloop4 ; i++){
                            l=ll[i];d=dd[i];s=ss[i];
                            if((s&0xf)!=transparent16s){
                                *up++=map16pairs[s&0xf];
                            } else {
                                if((l&0xf)!=transparent16){
                                    *up++=map16pairs[l&0xf];
                                } else {
                                    *up++=map16pairs[d&0xf];
                                }
                            }
                            d>>=4;l>>=4;s>>=4;
                            if((s&0xf)!=transparent16s){
                                *up++=map16pairs[s&0xf];
                            } else {
                                if((l&0xf)!=transparent16){
                                    *up++=map16pairs[l&0xf];
                                } else {
                                    *up++=map16pairs[d&0xf];
                                }
                            }
                        }
                    }
                    break;
                case SCREENMODE3: //720x400x16 colour
                    pp= line*vgaloop2;
                    for(int i=0;i<vgaloop2;i++){
                        d=DisplayBuf[pp+i];
                        l=LayerBuf[pp+i];
                        if((l&0xf)!=transparent16){
                            *p++=map16pairs[l&0xf];
                        } else {
                            *p++=map16pairs[d&0xf];
                        }
                        d>>=4;l>>=4;
                        if((l&0xf)!=transparent16){
                            *p++=map16pairs[l&0xf];
                        } else {
                            *p++=map16pairs[d&0xf];
                        }
                    }
                    break;
                case SCREENMODE4: //360x200xRGB555 colour
                    pp=line*vgaloop1;
                    uint16_t* d=(uint16_t *)&DisplayBuf[pp];
                    uint16_t* l=(uint16_t *)&LayerBuf[pp];
                    for(int i=0; i<vgaloop2 ; i++){
                        if(*l!=RGBtransparent){
                            *p++=*l;
                            *p++=*l;
                        } else {
                            *p++=*d;
                            *p++=*d;
                        }
                        l++;d++;
                    }
                    break;
                case SCREENMODE5: //360x200x256 colour
                    pp=line*vgaloop2;
                    dd=&DisplayBuf[pp];
                    ll=&LayerBuf[pp];
                    register unsigned char *ss=&SecondLayer[pp];
                    if(ss==dd){
                        ss=ll;
                        transparent16s=transparent16;
                    }
                    for(int i=0; i<vgaloop2 ; i++){
                        int d=dd[i];
                        int l=ll[i];
                        int s=ss[i];
                        if(s!=transparent16s){
                            *p++=map256[s];
                            *p++=map256[s];
                        } else {
                            if(l!=transparent16){
                                *p++=map256[l];
                                *p++=map256[l];
                            } else {
                                *p++=map256[d];
                                *p++=map256[d];
                            }
                        }
                    }
                    break;
                default:
                }
            }
        }
    }
}
void mapreset(void){
    for(int i=0;i<256;i++)map256[i]=remap256[i]=RGB555(MAP256DEF[i]);
    for(int i=0;i<16;i++){
        map16pairs[i]=remap555[i]=(RGB555(MAP16DEF[i]) | (RGB555(MAP16DEF[i])<<16));
        map16quads[i]=remap332[i]=((RGB332(MAP16DEF[i])<<24) | (RGB332(MAP16DEF[i])<<16) | (RGB332(MAP16DEF[i])<<8) | RGB332(MAP16DEF[i]));
    }
}
/* HDMICore is launched on core1 by PicoMite.c's boot path. core1stack[]
 * is owned by ports/pico_sdk_common/core1_runtime.c — sized per port via
 * HAL_PORT_CORE1_STACK_WORDS. */
void HDMICore(void){
    mapreset();
    if(Option.CPU_Speed==FreqXGA){
        MODE_H_SYNC_POLARITY=MODE_H_L_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_L_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_L_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_L_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_L_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_L_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_L_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_L_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_L_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_L_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_L_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_L_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_L_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_L;
        MODE2SIZE=MODE2SIZE_L;
        MODE3SIZE=MODE3SIZE_L;
        MODE4SIZE=0L;
        MODE5SIZE=MODE5SIZE_L;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq720P){
        MODE_H_SYNC_POLARITY=MODE_H_W_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_W_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_W_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_W_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_W_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_W_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_W_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_W_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_W_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_W_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_W_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_W_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_W_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_W;
        MODE2SIZE=MODE2SIZE_W;
        MODE3SIZE=MODE3SIZE_W;
        MODE4SIZE=0L;
        MODE5SIZE=MODE5SIZE_W;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==FreqSVGA){
        MODE_H_SYNC_POLARITY=MODE_H_V_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_V_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_V_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_V_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_V_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_V_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_V_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_V_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_V_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_V_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_V_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_V_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_V_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_V;
        MODE2SIZE=MODE2SIZE_V;
        MODE3SIZE=MODE3SIZE_V;
        MODE5SIZE=MODE5SIZE_V;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq848){
        MODE_H_SYNC_POLARITY=MODE_H_8_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_8_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_8_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_8_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_8_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_8_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_8_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_8_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_8_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_8_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_8_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_8_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_8_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_8;
        MODE2SIZE=MODE2SIZE_8;
        MODE3SIZE=MODE3SIZE_8;
        MODE5SIZE=MODE5SIZE_8;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==Freq400){
        MODE_H_SYNC_POLARITY=MODE_H_4_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_4_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_4_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_4_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_4_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_4_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_4_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_4_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_4_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_4_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_4_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_4_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_4_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_4;
        MODE2SIZE=MODE2SIZE_4;
        MODE3SIZE=MODE3SIZE_4;
        MODE4SIZE=MODE3SIZE_4;
        MODE5SIZE=MODE5SIZE_4;
        PIXELS_PER_WORD=2;
    } else if(Option.CPU_Speed==FreqX){
        MODE_H_SYNC_POLARITY=MODE_H_X_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_X_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_X_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_X_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_X_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_X_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_X_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_X_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_X_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_X_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_X_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_X_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_X_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_X;
        MODE2SIZE=MODE2SIZE_X;
        MODE3SIZE=MODE3SIZE_X;
        MODE4SIZE=0L;
        MODE5SIZE=MODE5SIZE_X;
        PIXELS_PER_WORD=4;
    } else if(Option.CPU_Speed==FreqY){
        MODE_H_SYNC_POLARITY=MODE_H_Y_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_Y_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_Y_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_Y_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_Y_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_Y_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_Y_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_Y_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_Y_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_Y_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_Y_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_Y_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_Y_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_Y;
        MODE2SIZE=MODE2SIZE_Y;
        MODE3SIZE=MODE3SIZE_Y;
        MODE5SIZE=MODE5SIZE_Y;
        PIXELS_PER_WORD=4;
    }  else {
        MODE_H_SYNC_POLARITY=MODE_H_S_SYNC_POLARITY;
        MODE_ACTIVE_LINES=MODE_V_S_ACTIVE_LINES;
        MODE_ACTIVE_PIXELS=MODE_H_S_ACTIVE_PIXELS;
        MODE_V_TOTAL_LINES=MODE_V_S_TOTAL_LINES;
        MODE_H_ACTIVE_PIXELS=MODE_H_S_ACTIVE_PIXELS;
        MODE_H_FRONT_PORCH=MODE_H_S_FRONT_PORCH;
        MODE_H_SYNC_WIDTH=MODE_H_S_SYNC_WIDTH;
        MODE_H_BACK_PORCH=MODE_H_S_BACK_PORCH;
        MODE_V_SYNC_POLARITY=MODE_V_S_SYNC_POLARITY;
        MODE_V_ACTIVE_LINES=MODE_V_S_ACTIVE_LINES;
        MODE_V_FRONT_PORCH=MODE_V_S_FRONT_PORCH;
        MODE_V_SYNC_WIDTH=MODE_V_S_SYNC_WIDTH;
        MODE_V_BACK_PORCH=MODE_V_S_BACK_PORCH;
        MODE1SIZE=MODE1SIZE_S;
        MODE2SIZE=MODE2SIZE_S;
        MODE3SIZE=MODE3SIZE_S;
        MODE4SIZE=MODE4SIZE_S;
        MODE5SIZE=MODE5SIZE_S;
        PIXELS_PER_WORD=2;
    }
    vgaloop1=MODE_H_ACTIVE_PIXELS;
    vgaloop2=MODE_H_ACTIVE_PIXELS/2;
    vgaloop4=MODE_H_ACTIVE_PIXELS/4;
    vgaloop8=MODE_H_ACTIVE_PIXELS/8;
    vgaloop16=MODE_H_ACTIVE_PIXELS/16;
    vgaloop32=MODE_H_ACTIVE_PIXELS/32;
    TRANSFER_COUNT=MODE_H_ACTIVE_PIXELS/PIXELS_PER_WORD;
    BLANKING_COUNT=MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH;

    vblank_line_vsync_off[0] =       HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vblank_line_vsync_off[1] =       SYNC_V1_H1;
    vblank_line_vsync_off[2] =       HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vblank_line_vsync_off[3] =       SYNC_V1_H0;
    vblank_line_vsync_off[4] =       HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
    vblank_line_vsync_off[5] =       SYNC_V1_H1;
    vblank_line_vsync_off[6] =       HSTX_CMD_NOP;

    vblank_line_vsync_on[0] =    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vblank_line_vsync_on[1] =    SYNC_V0_H1;
    vblank_line_vsync_on[2] =    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vblank_line_vsync_on[3] =    SYNC_V0_H0,
    vblank_line_vsync_on[4] =    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
    vblank_line_vsync_on[5] =    SYNC_V0_H1;
    vblank_line_vsync_on[6] =    HSTX_CMD_NOP;

    vactive_line[0] =    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    vactive_line[1] =    SYNC_V1_H1;
    vactive_line[2] =    HSTX_CMD_NOP;
    vactive_line[3] =    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH;
    vactive_line[4] =    SYNC_V1_H0;
    vactive_line[5] =    HSTX_CMD_NOP;
    vactive_line[6] =    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH;
    vactive_line[7] =    SYNC_V1_H1;
    vactive_line[8] =    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS;
        // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds =
        ((FullColour) ? 
            (29 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            2 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            7 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            4  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB) 
            :
            (2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
            0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
            2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
            29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
            1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
            26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB));

    // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
    // entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        ((FullColour) ? 
            (2 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            16 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB)
            :
            (4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
            8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
            1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
            0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB));


        // Serial output config: clock period of 5 cycles, pop from command
        // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS; 

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.
    // Pinout on Pico DVI sock:
    //
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-

    // Assign clock pair to two neighbouring pins:
    if(Option.HDMIclock & 1){
        hstx_ctrl_hw->bit[Option.HDMIclock] = HSTX_CTRL_BIT0_CLK_BITS;
        hstx_ctrl_hw->bit[Option.HDMIclock-1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;

    } else {
        hstx_ctrl_hw->bit[Option.HDMIclock] = HSTX_CTRL_BIT0_CLK_BITS;
        hstx_ctrl_hw->bit[Option.HDMIclock+1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    }
    int lane_to_output_bit[3];
    lane_to_output_bit[0]=Option.HDMId0;
    lane_to_output_bit[1]=Option.HDMId1;
    lane_to_output_bit[2]=Option.HDMId2;
        
    for (uint lane = 0; lane < 3; ++lane) {
        // For each TMDS lane, assign it to the correct GPIO pair based on the
        // desired pinout:
        int bit = lane_to_output_bit[lane];
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        if(bit & 1){
            hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
            hstx_ctrl_hw->bit[bit - 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
        } else {
            hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
            hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
        }
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX
        gpio_set_drive_strength (i, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(i,GPIO_SLEW_RATE_FAST);
        gpio_set_input_enabled(i, false);
        gpio_set_pulls(i,false,false);
        gpio_set_input_hysteresis_enabled(i,false);
    }

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler0);
    irq_set_enabled(DMA_IRQ_0, true);

//    bus_ctrl_hw->priority = 1;
    dma_channel_start(DMACH_PING);
    if(Option.CPU_Speed==Freq480P || Option.CPU_Speed==Freq378P  || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq400)HDMIloop0();
    else if(Option.CPU_Speed==Freq720P)HDMIloop1();
    else if(Option.CPU_Speed==FreqXGA)HDMIloop2();
    else if(Option.CPU_Speed==FreqX)HDMIloopX();
    else HDMIloop3();
}
void settiles(void){
    if(DISPLAY_TYPE!=SCREENMODE1)return;
    if(FullColour){
        tilefcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3));
        tilebcols=(uint16_t *)((uint32_t)FRAMEBUFFER+(MODE1SIZE*3)+(MODE1SIZE>>1));
        ytileheight=gui_font_height;
        Y_TILE=VRes/ytileheight;
        X_TILE=HRes/8;
        if(VRes % ytileheight)Y_TILE++;
            for(int x=0;x<X_TILE;x++){
            for(int y=0;y<Y_TILE;y++){
                tilefcols[y*X_TILE+x]=RGB555(gui_fcolour);
                tilebcols[y*X_TILE+x]=RGB555(gui_bcolour);
            }
        }
    } else {
        tilefcols_w=(uint8_t *)DisplayBuf+MODE1SIZE;
        tilebcols_w=tilefcols_w+(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8); //minimum tilesize is 8x8
        memset(tilefcols_w,RGB332(gui_fcolour),(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8)*sizeof(uint8_t));
        memset(tilebcols_w,RGB332(gui_bcolour),(MODE_H_ACTIVE_PIXELS/8)*(MODE_V_ACTIVE_LINES/8)*sizeof(uint8_t));
        ytileheight=(MediumRes? 12:24);
        X_TILE=MODE_H_ACTIVE_PIXELS/8;Y_TILE=MODE_V_ACTIVE_LINES/ytileheight;
    }
}

/* HDMI port impl of hal_vga_init_screenmode1_tiles — delegate to
 * settiles() which knows the HDMI tile-buffer layout. */
void hal_vga_init_screenmode1_tiles(void) {
    settiles();
}

void hal_vga_apply_hdmi_resolution(int display_type) {
    if (Option.CPU_Speed == Freq720P) {
        HRes = (display_type == SCREENMODE1 ? 1280 : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 320 : 640));
        VRes = (display_type == SCREENMODE1 ? 720  : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 180 : 360));
    } else if (Option.CPU_Speed == FreqXGA) {
        HRes = (display_type == SCREENMODE1 ? 1024 : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 256 : 512));
        VRes = (display_type == SCREENMODE1 ? 768  : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 192 : 384));
    } else if (Option.CPU_Speed == FreqSVGA) {
        HRes = ((display_type == SCREENMODE1 || display_type == SCREENMODE3) ? 800 : 400);
        VRes = ((display_type == SCREENMODE1 || display_type == SCREENMODE3) ? 600 : 300);
    } else if (Option.CPU_Speed == FreqX) {
        HRes = (display_type == SCREENMODE1 ? 1024 : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 256 : 512));
        VRes = (display_type == SCREENMODE1 ? 600  : ((display_type == SCREENMODE2 || display_type == SCREENMODE5) ? 150 : 300));
    }
}

int hal_vga_assign_hdmi_screenmode(int display_type) {
    extern void DrawRectangle555(int x1, int y1, int x2, int y2, int c);
    extern void DrawBitmap555(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
    extern void ScrollLCD555(int lines);
    extern void DrawBuffer555(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void ReadBuffer555(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void DrawBuffer555Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
    extern void ReadBuffer555Fast(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void DrawPixel555(int x, int y, int c);
    extern void DrawRectangle256(int x1, int y1, int x2, int y2, int c);
    extern void DrawBitmap256(int x1, int y1, int width, int height, int scale, int fc, int bc, unsigned char *bitmap);
    extern void ScrollLCD256(int lines);
    extern void DrawBuffer256(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void ReadBuffer256(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void DrawBuffer256Fast(int x1, int y1, int x2, int y2, int blank, unsigned char *p);
    extern void ReadBuffer256Fast(int x1, int y1, int x2, int y2, unsigned char *p);
    extern void DrawPixel256(int x, int y, int c);
    if (display_type == SCREENMODE4) {
        DrawRectangle = DrawRectangle555;
        DrawBitmap = DrawBitmap555;
        ScrollLCD = ScrollLCD555;
        DrawBuffer = DrawBuffer555;
        ReadBuffer = ReadBuffer555;
        DrawBufferFast = DrawBuffer555Fast;
        ReadBufferFast = ReadBuffer555Fast;
        DrawPixel = DrawPixel555;
        return 1;
    }
    if (display_type == SCREENMODE5) {
        DrawRectangle = DrawRectangle256;
        DrawBitmap = DrawBitmap256;
        ScrollLCD = ScrollLCD256;
        DrawBuffer = DrawBuffer256;
        ReadBuffer = ReadBuffer256;
        DrawBufferFast = DrawBuffer256Fast;
        ReadBufferFast = ReadBuffer256Fast;
        DrawPixel = DrawPixel256;
        return 1;
    }
    return 0;
}

void hal_vga_init_screenmode_tiles(void) {
    settiles();
}

void hal_vga_setmode_mode1_pre_reset(void) {
    mapreset();
}

int hal_vga_setmode_select_alt_font(int display_type) {
    if (FullColour || MediumRes) return 0;       /* common path */
    if (display_type == SCREENMODE1) {
        SetFont((2 << 4) | 1);
        PromptFont = (2 << 4) | 1;
    } else if (display_type == SCREENMODE2 || display_type == SCREENMODE5) {
        SetFont((6 << 4) | 1);
        PromptFont = (6 << 4) | 1;
    } else if (display_type == SCREENMODE3) {
        SetFont(1);
        PromptFont = 1;
    }
    return 1;
}

/* HDMI scanout uses the HSTX peripheral, not GP-pin RGB121 PIO, so
 * there is no GPIO state to recover after a soft reset. */
void hal_vga_ops_reserved_io_recovery(void) { }

/* HDMI fun_getscanline impl — per-CPU_Speed offset against the
 * v_scanline counter. */
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


#ifdef __cplusplus
}
#endif
