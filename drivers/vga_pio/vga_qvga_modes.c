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
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/structs/bus_ctrl.h"
#include "PicoMiteVGA.pio.h"
#include "PicoMiteI2S.pio.h"
#include "Include.h"
#include "hal/hal_main_init.h"

extern void ResetDisplay(void);
extern void ClearScreen(int colour);
#ifdef rp2350
extern uint64_t piomap[];
#endif

void port_main_launch_core1(void) {
#ifdef rp2350
    piomap[QVGA_PIO_NUM]  = (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.VGA_BLUE].GPno);
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 1));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 2));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_BLUE].GPno + 3));
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.VGA_HSYNC].GPno);
    piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.VGA_HSYNC].GPno + 1));
    if (Option.audio_i2s_bclk) {
        piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.audio_i2s_data].GPno);
        piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[Option.audio_i2s_bclk].GPno);
        piomap[QVGA_PIO_NUM] |= (uint64_t)((uint64_t)1 << (uint64_t)(PinDef[Option.audio_i2s_bclk].GPno + 1));
    }
#endif
    X_TILE = Option.X_TILE;
    Y_TILE = Option.Y_TILE;
    ytileheight = (X_TILE == 80 || X_TILE == 106) ? 12 : 16;
    bus_ctrl_hw->priority = 0x100;
    multicore_launch_core1_with_stack(QVgaCore, core1stack, 512);
    core1stack[0] = 0x12345678;
    memset((void *)WriteBuf, 0, 38400);
    ResetDisplay();
    ClearScreen(Option.DefaultBC);
}

void port_video_validate_boot_options(void) {
    if (Option.VGA_HSYNC == 0) {
        Option.VGA_HSYNC = 21;
        Option.VGA_BLUE  = 24;
        SaveOptions();
    }
}

unsigned port_video_sys_clock_khz(unsigned cpu_khz) {
    return cpu_khz;
}

void port_video_post_clock_init(void) {
    if (Option.CPU_Speed == Freq252P || Option.CPU_Speed == Freq480P  ||
        Option.CPU_Speed == Freq848  || Option.CPU_Speed == Freq400   ||
        Option.CPU_Speed == FreqSVGA) QVGA_CLKDIV = 2;
    else if (Option.CPU_Speed == 378000) QVGA_CLKDIV = 3;
    else QVGA_CLKDIV = 1;
#ifdef rp2350
    if (Option.CPU_Speed == Freq848) {
        framebuffersize  = 424 * 240 * 2;
        heap_memory_size = HEAP_MEMORY_SIZE - framebuffersize + 320 * 240 * 2;
        FRAMEBUFFER      = AllMemory + heap_memory_size + 256;
        MODE1SIZE = MODE1SIZE_8;
        MODE2SIZE = MODE2SIZE_8;
        HRes = 848;
    }
    if (Option.CPU_Speed == FreqSVGA) {
        framebuffersize  = 400 * 300 * 2;
        heap_memory_size = HEAP_MEMORY_SIZE - framebuffersize + 320 * 240 * 2;
        FRAMEBUFFER      = AllMemory + heap_memory_size + 256;
        MODE1SIZE = MODE1SIZE_V;
        MODE2SIZE = MODE2SIZE_V;
        MODE3SIZE = MODE3SIZE_V;
        MODE5SIZE = MODE5SIZE_V;
        HRes = 800;
        VRes = 600;
    }
#endif
}

#include "hal/hal_option_setters.h"

/* SSD1963 backlight setter — SSD1963.c is not linked on VGA family.
 * Provide a stub so External.c's setBacklight() compiles on every
 * port; the runtime DISPLAY_TYPE>=SSDPANEL guard ensures it's never
 * actually called on VGA. */
void SetBacklightSSD1963(int intensity) { (void)intensity; }

/* OPTION LIST display section — VGA-family side (resolution + HDMI
 * PINS). Pure VGA never sets HDMIclock to non-default so the HDMI
 * PINS print just self-skips on those ports. */
#include "hal/hal_print_options.h"
extern void PO(char *s1);
extern void PO2Int(char *s1, int n1);
extern void PO2StrInt(char *s1, char *s2, int n1);
extern void PO3Int(char *s1, int n1, int n2);
extern void PInt(int64_t n);
extern void PIntComma(int64_t n);
extern void PRet(void);

void port_print_display_resolution_hdmi(void) {
    if(Option.CPU_Speed==Freq720P)PO2StrInt("RESOLUTION", "1280x720",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqXGA)PO2StrInt("RESOLUTION", "1024x768",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqSVGA)PO2StrInt("RESOLUTION", "800x600",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq848)PO2StrInt("RESOLUTION", "848x480",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq400)PO2StrInt("RESOLUTION", "720x400",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqX)PO2StrInt("RESOLUTION", "1024x600",Option.CPU_Speed);
    if(Option.CPU_Speed==FreqY)PO2StrInt("RESOLUTION", "800x480",Option.CPU_Speed);
    if(Option.CPU_Speed==Freq480P || Option.CPU_Speed==Freq252P || Option.CPU_Speed==Freq378P)
        PO2StrInt("RESOLUTION", "640x480",Option.CPU_Speed);
    if(Option.DISPLAY_TYPE!=SCREENMODE1)PO2Int("DEFAULT MODE", Option.DISPLAY_TYPE-SCREENMODE1+1);
    if(Option.Height != 40 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
    if (Option.HDMIclock != 2 || Option.HDMId0 != 0 ||
        Option.HDMId1   != 6 || Option.HDMId2 != 4) {
        PO("HDMI PINS ");
        PInt(Option.HDMIclock); PIntComma(Option.HDMId0);
        PIntComma(Option.HDMId1); PIntComma(Option.HDMId2); PRet();
    }
}

void port_print_display_panel_touch(void) {
    /* VGA family: panel/touch lines are non-VGA-only. */
}

void port_print_sdcard_system_spi_share(void) {
    /* VGA reuses SYSTEM_CLK/MOSI/MISO when SD has no dedicated pins. */
    MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);
    MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname);
    MMPrintString(", "); MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname);
}

void port_print_vga_pins(void) {
    if(Option.VGA_BLUE!=24 || Option.VGA_HSYNC!=21 ){
        PO("VGA PINS"); MMPrintString((char *)PinDef[Option.VGA_HSYNC].pinname);
        MMputchar(',',1); MMPrintString((char *)PinDef[Option.VGA_BLUE].pinname); PRet();
    }
}

/* MM_Misc.c batch-18 hooks — VGA-side. */
void port_print_system_spi(void) { /* VGA prints VGA PINS in port_print_display_options instead. */ }

void port_disable_sd_release_system_spi(void) {
    /* VGA reuses SYSTEM_CLK/MOSI/MISO for SD; release them when SD is
     * disabled so the bus is fully free for the next configuration. */
    if (!IsInvalidPin(Option.SYSTEM_CLK))  ExtCurrentConfig[Option.SYSTEM_CLK]  = EXT_DIG_IN;
    if (!IsInvalidPin(Option.SYSTEM_CLK))  ExtCfg(Option.SYSTEM_CLK,  EXT_NOT_CONFIG, 0);
    Option.SYSTEM_CLK  = 0;
    if (!IsInvalidPin(Option.SYSTEM_MISO)) ExtCurrentConfig[Option.SYSTEM_MISO] = EXT_DIG_IN;
    if (!IsInvalidPin(Option.SYSTEM_MISO)) ExtCfg(Option.SYSTEM_MISO, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_MISO = 0;
    if (!IsInvalidPin(Option.SYSTEM_MOSI)) ExtCurrentConfig[Option.SYSTEM_MOSI] = EXT_DIG_IN;
    if (!IsInvalidPin(Option.SYSTEM_MOSI)) ExtCfg(Option.SYSTEM_MOSI, EXT_NOT_CONFIG, 0);
    Option.SYSTEM_MOSI = 0;
}

int port_setter_sdcard_combined_cs(unsigned char *tp) {
    /* VGA uses dedicated SD pins from SDIO lines — COMBINED CS is non-VGA only. */
    (void)tp;
    return 0;
}

void port_setter_sdcard_argc_check(int argc) {
    /* VGA requires the full 7-arg form (no implicit SYSTEM SPI sharing). */
    if (argc != 7) error("Syntax");
}

int port_setter_sdcard_via_system_spi(int pin1, int pin2, int pin3) {
    /* VGA reuses SPI0/SPI1 hw pins when the chosen SD pins match. */
    if (Option.SYSTEM_CLK) return 0;
    if (PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) {
        Option.SYSTEM_CLK  = pin1;
        Option.SYSTEM_MOSI = pin2;
        Option.SYSTEM_MISO = pin3;
        MMPrintString("SPI channel 0 in use for SDcard\r\n");
        return 1;
    }
    if (PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX) {
        Option.SYSTEM_CLK  = pin1;
        Option.SYSTEM_MOSI = pin2;
        Option.SYSTEM_MISO = pin3;
        MMPrintString("SPI channel 1 in use for SDcard\r\n");
        return 1;
    }
    return 0;
}

int port_mminfo_lcdpanel(unsigned char *ep, unsigned char *sret, int *out_targ) {
    /* VGA has no runtime panel switcher. */
    (void)ep; (void)sret; (void)out_targ;
    return 0;
}

int port_mminfo_lcd320(unsigned char *ep, int64_t *out_iret, int *out_targ) {
    (void)ep; (void)out_iret; (void)out_targ;
    return 0;
}

/* VGA-family port: HDMI PINS, KEYBOARD BACKLIGHT, INFO SCROLL,
 * SCREENBUFF, SYSTEM SPI, TOUCH, POKE DISPLAY are not supported. */
int port_setter_hdmi_pins(unsigned char *cmdline)         { (void)cmdline; return 0; }
int port_setter_keyboard_backlight(unsigned char *cmdline){ (void)cmdline; return 0; }
int port_setter_scroll_start(int64_t *out_iret)           { (void)out_iret; return 0; }
int port_setter_screenbuff(int64_t *out_iret)             { (void)out_iret; return 0; }
int port_setter_system_lcd_spi(unsigned char *cmdline)    { (void)cmdline; return 0; }
int port_setter_touch_status(unsigned char *out_sret)     { (void)out_sret; return 0; }
int port_setter_poke_display(unsigned char *p)            { (void)p; return 0; }

/* Per-mode loop counters defined in drivers/vga_pio/vga_memory.c
 * (shared with HDMI scanout). */
extern int vgaloop1, vgaloop2, vgaloop4, vgaloop8, vgaloop16, vgaloop32;

/* I2S PIO program offset — defined in PicoMite.c (shared with the
 * I²S start path in Custom.c). */
extern uint I2SOff;

/* QVGA scanline / VSYNC timing constants — written by QVgaInit()
 * below from QVGA_HACT etc. configured at boot. Pure-VGA only. */
uint8_t map16[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
int QVGA_TOTAL, QVGA_HSYNC, QVGA_BP, QVGA_FP;
int QVGA_VACT, QVGA_VFRONT, QVGA_VSYNC, QVGA_VBACK, QVGA_VTOT, QVGA_HACT;

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

/* -------------------------------------------------------------------
 * QVGA scanout core (relocated from PicoMite.c). The entire core1
 * scanout body — sync/active-line PIO command tables, DMA control
 * buffers, the per-SCREENMODE scanline composer (QVgaLine1), and the
 * QVgaCore / QVgaInit entry points — lives here on every pure-VGA
 * port. HDMI ports use drivers/hdmi/hdmi_scanout.c instead.
 * ------------------------------------------------------------------- */

// ****************************************************************************
//
//                                  QVGA
//
// ****************************************************************************
// VGA resolution:
// - 640x480 pixels
// - vertical frequency 60 Hz
// - horizontal frequency 31.4685 kHz
// - pixel clock 25.175 MHz
//
// QVGA resolution:
// - 320x240 pixels
// - vertical double image scanlines
// - vertical frequency 60 Hz
// - horizontal frequency 31.4685 kHz
// - pixel clock 12.5875 MHz
//
// VGA vertical timings:
// - 525 scanlines total
// - line 1,2: (2) vertical sync
// - line 3..35: (33) dark
// - line 36..515: (480) image lines 0..479
// - line 516..525: (10) dark
//
// VGA horizontal timings:
// - 31.77781 total scanline in [us] (800 pixels, QVGA 400 pixels)
// - 0.63556 H front porch (after image, before HSYNC) in [us] (16 pixels, QVGA 8 pixels)
// - 3.81334 H sync pulse in [us] (96 pixels, QVGA 48 pixels)
// - 1.90667 H back porch (after HSYNC, before image) in [us] (48 pixels, QVGA 24 pixels)
// - 25.42224 H full visible in [us] (640 pixels, QVGA 320 pixels)
// - 0.0397222625 us per pixel at VGA, 0.079444525 us per pixel at QVGA
//
// We want reach 25.175 pixel clock (at 640x480). Default system clock is 125 MHz, which is
// approx. 5x pixel clock. We need 25.175*5 = 125.875 MHz. We use nearest frequency 126 MHz.
//	126000, 1512000, 126, 6, 2,     // 126.00MHz, VC0=1512MHz, FBDIV=126, PD1=6, PD2=2
//	126000, 504000, 42, 4, 1,       // 126.00MHz, VC0=504MHz, FBDIV=42, PD1=4, PD2=1
//	sysclk=126.000000 MHz, vco=504 MHz, fbdiv=42, pd1=4, pd2=1
//	sysclk=126.000000 MHz, vco=504 MHz, fbdiv=42, pd1=2, pd2=2
//	sysclk=126.000000 MHz, vco=756 MHz, fbdiv=63, pd1=6, pd2=1
//	sysclk=126.000000 MHz, vco=756 MHz, fbdiv=63, pd1=3, pd2=2
//	sysclk=126.000000 MHz, vco=1008 MHz, fbdiv=84, pd1=4, pd2=2 !!!!!
//	sysclk=126.000000 MHz, vco=1260 MHz, fbdiv=105, pd1=5, pd2=2
//	sysclk=126.000000 MHz, vco=1512 MHz, fbdiv=126, pd1=6, pd2=2
//	sysclk=126.000000 MHz, vco=1512 MHz, fbdiv=126, pd1=4, pd2=3
// Pixel clock is now:
//      5 system clock ticks per pixel at VGA ... pixel clock = 25.2 MHz, 0.039683 us per pixel
//     10 system clock ticks per pixel at QVGA ... pixel clock = 12.6 MHz, 0.079365 us per pixel
//
// - active image is 640*5=3200 clock ticks = 25.3968 us (QVGA: 1600 clock ticks)
// - total scanline is 126*31.77781=4004 clock ticks (QVGA: 2002 clock ticks)
// - H front porch = 82 clock ticks (QVGA: 41 clock ticks)
// - H sync pulse = 480 clock ticks (QVGA: 240 clock ticks)
// - H back porch = 242 clock ticks (QVGA: 121 clock ticks)

// in case of system clock = 125 MHz
// - PIO clock = system clock / 2
// - 5 PIO clocks per pixel = 10 system clocks per pixel
// - PIO clock = 62.5 MHz
// - pixel clock = 12.5 MHz
// - active image (320 pixels): 320*5 = 1600 PIO clocks = 3200 system ticks = 25.6 us (2.2 pixels stays invisible)
// - total scanline: 125*31.77781 = 3972 system clocks = 1986 PIO clocks
// - H front porch = 34 PIO clk
// - H sync = 238 PIO clk
// - H back = 114 PIO clk

extern volatile int VGAscrolly;

// PIO command (jmp=program address, num=loop counter)
#define QVGACMD(jmp, num) ( ((uint32_t)((jmp)+QVGAOff)<<27) | (uint32_t)(num))

// display frame buffer

// pointer to current frame buffer
uint QVGAOff;	// offset of QVGA PIO program
// Scanline data buffers (commands sent to PIO)
uint32_t ScanLineImg[3];	// image: HSYNC ... back porch ... image command
uint32_t ScanLineFp;		// front porch
uint32_t ScanLineDark[2];	// dark: HSYNC ... back porch + dark + front porch
uint32_t ScanLineSync[2];	// vertical sync: VHSYNC ... VSYNC(back porch + dark + front porch)

// Scanline control buffers
#define CB_MAX 8	// size of one scanline control buffer (1 link to data buffer requires 2x uint32_t)
uint32_t ScanLineCB[2*CB_MAX]; // 2 control buffers
int QVgaBufInx;		// current running control buffer
uint32_t* ScanLineCBNext;	// next control buffer

// handler variables
volatile int QVgaScanLine; // current processed scan line 0... (next displayed scan line)
volatile uint32_t QVgaFrame;	// frame counter
#ifdef rp2350
uint16_t fbuff[2][212]={0};
#else
uint16_t fbuff[2][180]={0};
#endif
volatile int X_TILE=80, Y_TILE=40;
// saved integer divider state
// VGA DMA handler - called on end of every scanline
static int VGAnextbuf=0,VGAnowbuf=1, tile=0, tc=0;

void MIPS32 __not_in_flash_func(QVgaLine1)()
{
    int i,line;
    uint8_t l,d;
    uint8_t transparent16=(uint8_t)transparent;
#ifdef rp2350
    uint8_t s;
    uint8_t transparent16s=(uint8_t)transparents;
#endif
	// Clear the interrupt request for DMA control channel
	dma_hw->ints0 = (1u << QVGA_DMA_PIO);

	// update DMA control channel and run it
	dma_channel_set_read_addr(QVGA_DMA_CB, ScanLineCBNext, true);
	// save integer divider state
//	hw_divider_save_state(&SaveDividerState);

	// prepare control buffer to be processed
	uint32_t* cb = &ScanLineCB[QVgaBufInx*CB_MAX];
	// switch current buffer index (bufinx = current preparing buffer, MiniVgaBufInx = current running buffer)
    QVgaBufInx ^= 1;
	ScanLineCBNext = cb;

	// increment scanline (1..)
	line = QVgaScanLine+1; // current scanline
//	line++; 		// new current scanline
	if (line >= QVGA_VTOT) // last scanline?
	{
		QVgaFrame++;	// increment frame counter
		line = 0; 	// restart scanline
        tile=0;
        tc=0;
	}
	QVgaScanLine = line;	// store new scanline

	// check scanline
	line -= QVGA_VSYNC;
	if (line < 0)
	{
		// VSYNC
		*cb++ = 2;
		*cb++ = (uint32_t)&ScanLineSync[0];
	}
	else
	{
		// front porch and back porch
		line -= QVGA_VBACK;
		if ((line < 0) || (line >= QVGA_VACT))
		{
			// dark line
			*cb++ = 2;
			*cb++ = (uint32_t)&ScanLineDark[0];
		}

		// image scanlines
		else
		{
        // prepare image line
            if(DISPLAY_TYPE==SCREENMODE1){
                uint16_t *q=&fbuff[VGAnextbuf][0];
                volatile unsigned char *p=&DisplayBuf[line * vgaloop8];
                volatile unsigned char *pp=&LayerBuf[line * vgaloop8];
                if(tc==ytileheight){
                    tile++;
                    tc=0;
                }
                tc++;
                register int pos=tile*X_TILE;
                for(i=0;i<vgaloop16;i++){
                    register int d=*p++ | *pp++;
                    register int low= d & 0xF;
                    register int high=d >>4;
                    *q++=(M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]) ;
                    *q++=(M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
                    pos++;
                    d=*p++ | *pp++;
                    low= d & 0xF;
                    high=d++ >>4;
                    *q++=(M_Foreground[low] & tilefcols[pos]) | (M_Background[low] & tilebcols[pos]) ;
                    *q++=(M_Foreground[high]& tilefcols[pos]) | (M_Background[high] & tilebcols[pos]) ;
                    pos++;
                }
#ifdef rp2350
            } else if(DISPLAY_TYPE==SCREENMODE3){
                register unsigned char *p=&DisplayBuf[line * vgaloop2];
                register unsigned char *q=&LayerBuf[line * vgaloop2];
                register int low, high, low2, high2;
                register uint8_t *r=(uint8_t *)fbuff[VGAnextbuf];
                for(int i=0;i<vgaloop2;i++){
                    low= map16[p[i] & 0xF];
                    high=map16[(p[i] & 0xF0)>>4];
                    low2= map16[q[i] & 0xF];
                    high2=map16[(q[i] & 0xF0)>>4];
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
                    *r++=low | (high<<4);
                }
#endif
            } else { //mode 2
                line>>=1;
                register unsigned char *dd=&DisplayBuf[line * vgaloop4];
                register unsigned char *ll=&LayerBuf[line * vgaloop4];
#ifdef rp2350
                register unsigned char *ss=&SecondLayer[line * vgaloop4];
                if(ss==dd){
                    ss=ll;
                    transparent16s=transparent16;
                }
                register int low3, high3;
#endif
                register int low, high, low2, high2;
                register uint16_t *r=(uint16_t *)fbuff[VGAnextbuf];
                for(int i=0;i<vgaloop4;i+=2){
                    d=*dd++;
                    l=*ll++;
                    low= map16[d & 0xF];
                    d>>=4;
                    high=map16[d];
                    low2= map16[l & 0xF];
                    l>>=4;
                    high2=map16[l];
#ifdef rp2350
                    s=*ss++;
                    low3= map16[s & 0xF];
                    s>>=4;
                    high3=map16[s];
#endif
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
#ifdef rp2350
                    if(low3!=transparent16s)low=low3;
                    if(high3!=transparent16s)high=high3;
#endif
                    *r++=(low | (low<<4) | (high<<8) | (high<<12));
                    d=*dd++;
                    l=*ll++;
                    low= map16[d & 0xF];
                    d>>=4;
                    high=map16[d];
                    low2= map16[l & 0xF];
                    l>>=4;
                    high2=map16[l];
#ifdef rp2350
                    s=*ss++;
                    low3= map16[s & 0xF];
                    s>>=4;
                    high3=map16[s];
#endif
                    if(low2!=transparent16)low=low2;
                    if(high2!=transparent16)high=high2;
#ifdef rp2350
                    if(low3!=transparent16s)low=low3;
                    if(high3!=transparent16s)high=high3;
#endif
                    *r++=(low | (low<<4) | (high<<8) | (high<<12));
                }
            }
            VGAnextbuf ^=1;
            VGAnowbuf ^=1;

			// HSYNC ... back porch ... image command
			*cb++ = 3;
			*cb++ = (uint32_t)&ScanLineImg[0];

			// image data
			*cb++ = vgaloop8;
			*cb++ = (uint32_t)fbuff[VGAnowbuf];

			// front porch
			*cb++ = 1;
			*cb++ = (uint32_t)&ScanLineFp;
		}
	}

	// end mark
	*cb++ = 0;
	*cb = 0;

	// restore integer divider state
//	hw_divider_restore_state(&SaveDividerState);
}

void QVgaPioInit()
{
	int i;
    if((Option.CPU_Speed % 126000) ==0){
        QVGA_TOTAL= 4000;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 480;	// horizontal sync clock ticks
        QVGA_HACT = 640;
        QVGA_BP	= 240;	// back porch clock ticks
        QVGA_FP	= 80;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 10;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 33;	// V back porch
        QVGA_VTOT	= 525;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == Freq848){
        QVGA_TOTAL= 1088*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 112*5;	// horizontal sync clock ticks
        QVGA_HACT = 848;
        QVGA_BP	= 112*5;	// back porch clock ticks
        QVGA_FP	= 16*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 8;	// V front porch
        QVGA_VSYNC	= 6;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 23;	// V back porch
        QVGA_VTOT	= 517;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == FreqSVGA){
        QVGA_TOTAL= 1024*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 72*5;	// horizontal sync clock ticks
        QVGA_HACT = 800;
        QVGA_BP	= 128*5;	// back porch clock ticks
        QVGA_FP	= 24*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 600;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 1;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 22;	// V back porch
        QVGA_VTOT	= 625;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else if(Option.CPU_Speed == Freq400){
        QVGA_TOTAL= 900*5;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC = 108*5;	// horizontal sync clock ticks
        QVGA_HACT = 720;
        QVGA_BP	= 54*5;	// back porch clock ticks
        QVGA_FP	= 18*5;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 400;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 12;	// V front porch
        QVGA_VSYNC	= 2;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 35;	// V back porch
        QVGA_VTOT	= 449;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    } else {
        QVGA_TOTAL= 4200;// total clock ticks (= QVGA_HSYNC + QVGA_BP + WIDTH*QVGA_CPP[1600] + QVGA_FP)
        QVGA_HSYNC =	320;	// horizontal sync clock ticks
        QVGA_HACT = 640;
        QVGA_BP	= 600;	// back porch clock ticks
        QVGA_FP	= 80;	// front porch clock ticks
        // QVGA vertical timings
        QVGA_VACT	= 480;	// V active scanlines (= 2*HEIGHT)
        QVGA_VFRONT	= 1;	// V front porch
        QVGA_VSYNC	= 3;	// length of V sync (number of scanlines)
        QVGA_VBACK	= 16;	// V back porch
        QVGA_VTOT	= 500;	// total scanlines (= QVGA_VSYNC + QVGA_VBACK + QVGA_VACT + QVGA_VFRONT)
    }
	// load PIO program
#ifdef rp2350
    if(piomap[QVGA_PIO_NUM] & (uint64_t)0xFFFF00000000)pio_set_gpio_base(QVGA_PIO,16);
#endif
    I2SOff = pio_add_program(QVGA_PIO, &i2s_program);
	QVGAOff = pio_add_program(QVGA_PIO, &qvga_program);
	// configure GPIOs for use by PIO
	for (i = QVGA_GPIO_FIRST; i <= QVGA_GPIO_LAST; i++) pio_gpio_init(QVGA_PIO, i);
	pio_gpio_init(QVGA_PIO, QVGA_GPIO_HSYNC);
	pio_gpio_init(QVGA_PIO, QVGA_GPIO_VSYNC);

	// set pin direction to output
	pio_sm_set_consecutive_pindirs(QVGA_PIO, QVGA_SM, QVGA_GPIO_FIRST, QVGA_GPIO_NUM, true);
	pio_sm_set_consecutive_pindirs(QVGA_PIO, QVGA_SM, QVGA_GPIO_HSYNC, 2, true);

	// negate HSYNC and VSYNC output
    if(!(Option.CPU_Speed==Freq848 || Option.CPU_Speed==FreqSVGA)){
        gpio_set_outover(QVGA_GPIO_HSYNC, GPIO_OVERRIDE_INVERT);
        gpio_set_outover(QVGA_GPIO_VSYNC, GPIO_OVERRIDE_INVERT);
    }

	// prepare default PIO program config
	pio_sm_config cfg = qvga_program_get_default_config(QVGAOff);

	// map state machine's OUT and MOV pins	
	sm_config_set_out_pins(&cfg, QVGA_GPIO_FIRST, QVGA_GPIO_NUM);

	// set sideset pins (HSYNC and VSYNC)
	sm_config_set_sideset_pins(&cfg, QVGA_GPIO_HSYNC);

	// join FIFO to send only
	sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

	// PIO clock divider
	sm_config_set_clkdiv(&cfg, QVGA_CLKDIV);

	// shift right, autopull, pull threshold
	sm_config_set_out_shift(&cfg, true, true, 32);

	// initialize state machine
	pio_sm_init(QVGA_PIO, QVGA_SM, QVGAOff+qvga_offset_entry, &cfg);
}

// initialize scanline buffers
void QVgaBufInit()
{
	// image scanline data buffer: HSYNC ... back porch ... image command
	ScanLineImg[0] = QVGACMD(qvga_offset_hsync, QVGA_HSYNC-3); // HSYNC
	ScanLineImg[1] = QVGACMD(qvga_offset_dark, QVGA_BP-4); // back porch
	ScanLineImg[2] = QVGACMD(qvga_offset_output, QVGA_HACT-2); // image

	// front porch
	ScanLineFp = QVGACMD(qvga_offset_dark, QVGA_FP-4); // front porch

	// dark scanline: HSYNC ... back porch + dark + front porch
	ScanLineDark[0] = QVGACMD(qvga_offset_hsync, QVGA_HSYNC-3); // HSYNC
	ScanLineDark[1] = QVGACMD(qvga_offset_dark, QVGA_TOTAL-QVGA_HSYNC-4); // back porch + dark + front porch

	// vertical sync: VHSYNC ... VSYNC(back porch + dark + front porch)
	ScanLineSync[0] = QVGACMD(qvga_offset_vhsync, QVGA_HSYNC-3); // VHSYNC
	ScanLineSync[1] = QVGACMD(qvga_offset_vsync, QVGA_TOTAL-QVGA_HSYNC-3); // VSYNC(back porch + dark + front porch)

	// control buffer 1 - initialize to VSYNC
	ScanLineCB[0] = 2; // send 2x uint32_t (send ScanLineSync)
	ScanLineCB[1] = (uint32_t)&ScanLineSync[0]; // VSYNC data buffer
	ScanLineCB[2] = 0; // stop mark
	ScanLineCB[3] = 0; // stop mark

	// control buffer 1 - initialize to VSYNC
	ScanLineCB[CB_MAX+0] = 2; // send 2x uint32_t (send ScanLineSync)
	ScanLineCB[CB_MAX+1] = (uint32_t)&ScanLineSync[0]; // VSYNC data buffer
	ScanLineCB[CB_MAX+2] = 0; // stop mark
	ScanLineCB[CB_MAX+3] = 0; // stop mark
}

// initialize QVGA DMA
//   control blocks aliases:
//                  +0x0        +0x4          +0x8          +0xC (Trigger)
// 0x00 (alias 0):  READ_ADDR   WRITE_ADDR    TRANS_COUNT   CTRL_TRIG
// 0x10 (alias 1):  CTRL        READ_ADDR     WRITE_ADDR    TRANS_COUNT_TRIG
// 0x20 (alias 2):  CTRL        TRANS_COUNT   READ_ADDR     WRITE_ADDR_TRIG
// 0x30 (alias 3):  CTRL        WRITE_ADDR    TRANS_COUNT   READ_ADDR_TRIG ... we use this!
void QVgaDmaInit()
{

// ==== prepare DMA control channel
	// prepare DMA default config
	dma_channel_config cfg = dma_channel_get_default_config(QVGA_DMA_CB);

	// increment address on read from memory
	channel_config_set_read_increment(&cfg, true);

	// increment address on write to DMA port
	channel_config_set_write_increment(&cfg, true);

	// each DMA transfered entry is 32-bits
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);

	// write ring - wrap to 8-byte boundary (TRANS_COUNT and READ_ADDR_TRIG of data DMA)
	channel_config_set_ring(&cfg, true, 3);

	// DMA configure
	dma_channel_configure(
		QVGA_DMA_CB,		// channel
		&cfg,			// configuration
		&dma_hw->ch[QVGA_DMA_PIO].al3_transfer_count, // write address
		&ScanLineCB[0],		// read address - as first, control buffer 1 will be sent out
		2,			// number of transfers in uint32_t (number of transfers per one request from data DMA)
		false			// do not start yet
	);

// ==== prepare DMA data channel

	// prepare DMA default config

	cfg = dma_channel_get_default_config(QVGA_DMA_PIO);

	// increment address on read from memory
	channel_config_set_read_increment(&cfg, true);

	// do not increment address on write to PIO
	channel_config_set_write_increment(&cfg, false);

	// each DMA transfered entry is 32-bits
	channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);

	// DMA data request for sending data to PIO
	channel_config_set_dreq(&cfg, pio_get_dreq(QVGA_PIO, QVGA_SM, true));

	// chain channel to DMA control block
	channel_config_set_chain_to(&cfg, QVGA_DMA_CB);

	// raise the IRQ flag when 0 is written to a trigger register (end of chain)
	channel_config_set_irq_quiet(&cfg, true);

	// set high priority
	cfg.ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;

	// DMA configure
	dma_channel_configure(
		QVGA_DMA_PIO,		// channel
		&cfg,			// configuration
		&QVGA_PIO->txf[QVGA_SM], // write address
		NULL,			// read address
		0,			// number of transfers in uint32_t
		false			// do not start immediately
	);

// ==== initialize IRQ0, raised from DMA data channel

	// enable DMA channel IRQ0
	dma_channel_set_irq0_enabled(QVGA_DMA_PIO, true);

	// set DMA IRQ handler
    irq_set_exclusive_handler(DMA_IRQ_0, QVgaLine1);
    vgaloop4=QVGA_HACT/4;
    vgaloop8=QVGA_HACT/8;
    vgaloop16=QVGA_HACT/16;
    vgaloop2=QVGA_HACT/2;
    MODE1SIZE=QVGA_HACT*QVGA_VACT/8;
    MODE2SIZE=(QVGA_HACT/2)*(QVGA_VACT/2)/2;
    MODE3SIZE=QVGA_HACT*QVGA_VACT/2;
    HRes=QVGA_HACT;
    VRes=QVGA_VACT;
// set highest IRQ priority
	irq_set_priority(DMA_IRQ_0, 0);
}

// initialize QVGA (can change system clock)
void QVgaInit()
{
    X_TILE=Option.X_TILE;
    Y_TILE=Option.Y_TILE;
    ytileheight=(X_TILE==80 || X_TILE==90 || X_TILE==106 || X_TILE==100)? 12 : 16;
	// initialize PIO
	QVgaPioInit();

	// initialize scanline buffers
	QVgaBufInit();

	// initialize DMA
	QVgaDmaInit();

	// initialize parameters
	QVgaScanLine = 0; // currently processed scanline
	QVgaBufInx = 0; // at first, control buffer 1 will be sent out
	QVgaFrame = 0; // current frame
	ScanLineCBNext = &ScanLineCB[CB_MAX]; // send control buffer 2 next

	// enable DMA IRQ
	irq_set_enabled(DMA_IRQ_0, true);

	// start DMA
	dma_channel_start(QVGA_DMA_CB);

	// run state machine
	pio_sm_set_enabled(QVGA_PIO, QVGA_SM, true);
}

void (* volatile Core1Fnc)() = NULL; // core 1 remote function

/* QVgaCore (core1 QVGA scanout entry) + its 128-word core1stack live
 * in drivers/vga_pio/vga_qvga_modes.c — see phase-8 notes. The launch
 * site below references the QVgaCore symbol via the extern in
 * Hardware_Includes.h. QVgaInit() still lives in this file and is
 * called from the driver on core1 start-up. */
