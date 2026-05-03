/*
 * drivers/vga_pio/vga_qvga_modes.c — QVGA-non-HDMI variants of
 * cmd_map / cmd_tile.
 *
 * Extracted from drivers/vga_pio/vga_mode_ops.c's `#ifndef HDMI`
 * branch. Linked on VGA + VGAUSB + VGARP2350 + VGAUSBRP2350 (the
 * four PICOMITEVGA targets that are NOT HDMI). HDMI ports link
 * drivers/hdmi/hdmi_modes.c — the file that sits in the other half
 * of what used to be the `#ifndef HDMI / #else / #endif` split.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "pico/multicore.h"
#include "hardware/irq.h"
#include "hardware/dma.h"

extern volatile int QVgaScanLine;
/* Pure-VGA palette-remap table (RGB121 16-entry indirection). */
uint8_t remap[256];
extern const int CMM1map[16];
extern void QVgaInit(void);

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

/* QVGA scanout core1 entry + its 128-word stack. Linked only on
 * PICOMITEVGA non-HDMI targets. Runs QVgaInit() once to stand up the
 * PIO/DMA scanout chain, then spins on the inter-core FIFO to accept
 * 0x5555 / 0xAAAA commands that disable/enable DMA_IRQ_0 (used by the
 * audio sub-system to pause VGA scanout during PWM-synth transitions
 * so the scanline timer doesn't jitter during I²S setup).
 *
 * QVgaInit() itself still lives in PicoMite.c because it pulls in a
 * large chain of PIO/DMA init globals local to that file; moving it
 * is a separate refactor.
 *
 * core1stack[] is owned by ports/pico_sdk_common/core1_runtime.c. */
void __not_in_flash_func(QVgaCore)(void) {
    QVgaInit();
    while (true) {
        __dmb();
        if (multicore_fifo_rvalid()) {
            int command = multicore_fifo_pop_blocking();
            if (command == 0x5555) irq_set_enabled(DMA_IRQ_0, false);
            if (command == 0xAAAA) irq_set_enabled(DMA_IRQ_0, true);
        }
    }
}

/* Pure-VGA impl of hal_vga_init_screenmode1_tiles. The RGB121-packed
 * tile-color arrays are written by hand on this scanout path
 * (vga_memory.c owns tilefcols/tilebcols). */
void hal_vga_init_screenmode1_tiles(void) {
    extern int gui_fcolour, gui_bcolour;
    int bcolour = RGB121pack(gui_bcolour);
    int fcolour = RGB121pack(gui_fcolour);
    for (int x = 0; x < X_TILE; x++) {
        for (int y = 0; y < Y_TILE; y++) {
            tilefcols[y * X_TILE + x] = fcolour;
            tilebcols[y * X_TILE + x] = bcolour;
        }
    }
}

/* HDMI-only resolution / screenmode dispatch — pure-VGA stubs.
 * Runtime never reaches HDMI-only CPU_Speed values or
 * SCREENMODE4/5 on these ports (OPTION setter rejects them). */
void hal_vga_apply_hdmi_resolution(int display_type) { (void)display_type; }
int  hal_vga_assign_hdmi_screenmode(int display_type) { (void)display_type; return 0; }

void hal_vga_init_screenmode_tiles(void) {
#ifdef rp2350
    if (DISPLAY_TYPE == SCREENMODE1) {
        tilefcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3));
        tilebcols = (uint16_t *)((uint32_t)FRAMEBUFFER + (MODE1SIZE * 3) + (MODE1SIZE >> 1));
    }
#endif
    for (int x = 0; x < X_TILE; x++) {
        for (int y = 0; y < Y_TILE; y++) {
            tilefcols[y * X_TILE + x] = RGB121pack(Option.DefaultFC);
            tilebcols[y * X_TILE + x] = RGB121pack(Option.DefaultBC);
        }
    }
}

/* Pure-VGA fun_getscanline — read the QVGA scanline counter directly. */
void fun_getscanline(void) {
    iret = QVgaScanLine;
    targ = T_INT;
}

void hal_vga_setmode_mode1_pre_reset(void) {
#ifdef rp2350
    tilefcols = (uint16_t *)((uint8_t *)FRAMEBUFFER + (MODE1SIZE * 3));
    tilebcols = (uint16_t *)((uint8_t *)FRAMEBUFFER + (MODE1SIZE * 3) + (MODE1SIZE >> 1));
#endif
}

int hal_vga_setmode_select_alt_font(int display_type) {
    (void)display_type;
    return 0;       /* common font path always */
}

/* Boot-time scanout-pin recovery from soft reset. Pure-VGA only —
 * HDMI ports use the HSTX peripheral and don't share VGA's
 * GP-pin-driven RGB121 PIO program, so the recovery sequence is a
 * no-op there. */
void hal_vga_ops_reserved_io_recovery(void) {
    VGArecovery(0);
}

/* Wait for the pure-VGA QVGA scanout to reach the top of frame so
 * tile-buffer writes can land between displayed scanlines. */
void hal_vga_ops_wait_scanline_zero(void) {
    while (QVgaScanLine != 0) { }
}
