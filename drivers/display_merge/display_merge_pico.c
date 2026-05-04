/*
 * drivers/display_merge/display_merge_pico.c — real merge-pipeline
 * control for the PicoMite (SPI-LCD) device family.
 *
 * Linked only into PICOMITE variants (rp2040 + rp2350). The pipeline
 * is driven through the rp2040 inter-core FIFO; core1 runs UpdateCore
 * (defined at the bottom of this file), which receives the command
 * words posted by the hooks below. See hal/hal_display_merge.h for the
 * contract.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_display_merge.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/dma.h"

extern bool mergerunning;
extern volatile bool mergedone;
extern uint32_t mergetimer;
extern uint32_t _excep_code;
mutex_t frameBufferMutex;
extern unsigned char *ShadowBuf;
extern int fb_dma_chan;
extern void fastgfx_swap_core1(void);

void hal_display_merge_abort(void) {
    if (!mergerunning) return;
    multicore_fifo_push_blocking(0xFF);
    busy_wait_ms(mergetimer + 200);
    if (mergerunning) {
        /* Core1 failed to ack the stop within mergetimer+200 ms. Hard
         * reset — the BASIC program is about to run with a half-dead
         * display pipeline otherwise. Matches the legacy inline
         * pattern that lived in Draw.c before the HAL extraction. */
        _excep_code = RESET_COMMAND;
        SoftReset();
    }
}

void hal_display_merge_check_busy(void) {
    if (mergerunning) error("Display in use for merged operation");
}

void hal_display_merge_lock_fb(void) {
    mutex_enter_blocking(&frameBufferMutex);
}

void hal_display_merge_unlock_fb(void) {
    mutex_exit(&frameBufferMutex);
}

void hal_display_merge_init_fb_mutex(void) {
    mutex_init(&frameBufferMutex);
}

void hal_display_merge_mark_done(void) {
    mergedone = true;
    __dmb();
}

void hal_display_fast_dma_alloc(unsigned bytes) {
    ShadowBuf = GetMemory(bytes);
    memset(ShadowBuf, 0, bytes);
    fb_dma_chan = dma_claim_unused_channel(true);
}

void hal_display_fast_dma_free(void) {
    if (ShadowBuf) { FreeMemory(ShadowBuf); ShadowBuf = NULL; }
    if (fb_dma_chan >= 0) { dma_channel_unclaim(fb_dma_chan); fb_dma_chan = -1; }
}

void hal_display_nextgen_refresh_rect(int x_lo, int y_lo, int x_hi, int y_hi) {
    multicore_fifo_push_blocking(6);
    multicore_fifo_push_blocking((uint32_t)x_lo | ((uint32_t)x_hi << 16));
    multicore_fifo_push_blocking((uint32_t)y_lo | ((uint32_t)y_hi << 16));
}

void hal_display_nextgen_scroll_reset(void) {
    multicore_fifo_push_blocking(7);
    multicore_fifo_push_blocking((uint32_t)0);
}

int hal_display_merge_has_pipeline(void) { return 1; }

void hal_display_merge_sync_wait(void) {
    mergedone = false;
    __dmb();
    while (mergedone == false) CheckAbort();
}

void hal_display_merge_post_fill(unsigned colour) {
    multicore_fifo_push_blocking(2);
    multicore_fifo_push_blocking((uint32_t)colour);
}

void hal_display_merge_post_bg(unsigned colour, unsigned timer_us) {
    multicore_fifo_push_blocking(3);
    multicore_fifo_push_blocking((uint32_t)colour);
    multicore_fifo_push_blocking((uint32_t)timer_us);
}

void hal_display_merge_post_copy(const void *src) {
    multicore_fifo_push_blocking(1);
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)src);
}

void hal_display_merge_post_blit_fill(int x, int y, int w, int h, unsigned colour) {
    multicore_fifo_push_blocking(4);
    multicore_fifo_push_blocking((uint32_t)x);
    multicore_fifo_push_blocking((uint32_t)y);
    multicore_fifo_push_blocking((uint32_t)w);
    multicore_fifo_push_blocking((uint32_t)h);
    multicore_fifo_push_blocking((uint32_t)colour);
}

void hal_display_merge_post_blit_bg(int x, int y, int w, int h,
                                    unsigned colour, unsigned timer_us) {
    multicore_fifo_push_blocking(5);
    multicore_fifo_push_blocking((uint32_t)x);
    multicore_fifo_push_blocking((uint32_t)y);
    multicore_fifo_push_blocking((uint32_t)w);
    multicore_fifo_push_blocking((uint32_t)h);
    multicore_fifo_push_blocking((uint32_t)colour);
    multicore_fifo_push_blocking((uint32_t)timer_us);
}

/* UpdateCore is the receiver counterpart to the post_* functions above:
 * it spins on the inter-core FIFO, decoding the command word + payloads
 * posted by core0 and driving the SPI-LCD merge path on core1.
 *
 * core1stack[] is owned by ports/pico_sdk_common/core1_runtime.c — sized
 * per port via HAL_PORT_CORE1_STACK_WORDS. */
void __not_in_flash_func(UpdateCore)(void) {
    while (true) {
        __dmb();
        if (!multicore_fifo_rvalid()) continue;
        int command = multicore_fifo_pop_blocking();
        if (command == 3) {
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            uint32_t timer = (uint32_t)multicore_fifo_pop_blocking();
            uint64_t delaytime = 0;
            if (timer) delaytime = time_us_64() + timer;
            mergerunning = true;
            while (1) {
                if (multicore_fifo_rvalid()) {
                    int a = multicore_fifo_pop_blocking();
                    if (a == 0xff) { mergerunning = false; break; }
                }
                if (timer) {
                    busy_wait_until(delaytime);
                    delaytime = time_us_64() + timer;
                }
                if (ShadowBuf) merge_optimized(colour);
                else            merge(colour);
            }
        } else if (command == 2) {
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            if (ShadowBuf) merge_optimized(colour);
            else           merge(colour);
        } else if (command == 4) {
            int x1 = multicore_fifo_pop_blocking();
            int y1 = multicore_fifo_pop_blocking();
            int w  = multicore_fifo_pop_blocking();
            int h  = multicore_fifo_pop_blocking();
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            blitmerge(x1, y1, w, h, colour);
        } else if (command == 5) {
            int x1 = multicore_fifo_pop_blocking();
            int y1 = multicore_fifo_pop_blocking();
            int w  = multicore_fifo_pop_blocking();
            int h  = multicore_fifo_pop_blocking();
            uint8_t colour = (uint8_t)multicore_fifo_pop_blocking();
            uint32_t timer = (uint32_t)multicore_fifo_pop_blocking();
            uint64_t delaytime = 0;
            if (timer) delaytime = time_us_64() + timer;
            mergerunning = true;
            while (1) {
                if (multicore_fifo_rvalid()) {
                    int a = multicore_fifo_pop_blocking();
                    if (a == 0xff) { mergerunning = false; break; }
                }
                if (timer) {
                    busy_wait_until(delaytime);
                    delaytime = time_us_64() + timer;
                }
                blitmerge(x1, y1, w, h, colour);
            }
#ifdef rp2350
        } else if (command == 6) {
            int x_low = (int)multicore_fifo_pop_blocking();
            int y_low = (int)multicore_fifo_pop_blocking();
            int x_high = x_low >> 16; x_low &= 0xFFFF;
            int y_high = y_low >> 16; y_low &= 0xFFFF;
            mutex_enter_blocking(&frameBufferMutex);
            copybuffertoscreen((uint8_t *)ScreenBuffer, x_low, y_low, x_high, y_high);
            mutex_exit(&frameBufferMutex);
        } else if (command == 7) {
            int t = (int)multicore_fifo_pop_blocking();
            spi_write_command(CMD_SET_SCROLL_START);
            spi_write_data(t >> 8);
            spi_write_data(t);
#endif
        } else if (command == 1) {
            uint8_t *s = (uint8_t *)multicore_fifo_pop_blocking();
            mutex_enter_blocking(&frameBufferMutex);
            copyframetoscreen(s, 0, HRes - 1, 0, VRes - 1, 0);
            mutex_exit(&frameBufferMutex);
        } else if (command == 8) {
            fastgfx_swap_core1();
        }
    }
}


/* PicoMite SPI-LCD core1 launch — runs UpdateCore (the merge
 * receiver) on core1. SPI-LCD ResetDisplay was already done in the
 * boot sequence before InitDisplaySSD/SPI fired, so no display
 * reset here. */
#include "hal/hal_main_init.h"
#include "hardware/structs/bus_ctrl.h"

void port_main_launch_core1(void) {
    bus_ctrl_hw->priority = 0x100;
    multicore_launch_core1_with_stack(UpdateCore, core1stack, 2048);
    core1stack[0] = 0x12345678;
}

void port_video_validate_boot_options(void) { }

unsigned port_video_sys_clock_khz(unsigned cpu_khz) { return cpu_khz; }

void port_video_post_clock_init(void) { }

/* OPTION LIST display section — non-VGA side (CPUSPEED + LCDPANEL +
 * TOUCH calibration). */
#include "hal/hal_print_options.h"
#include "hal/hal_gui_controls.h"
#include "hal/hal_i2c_keypad.h"
extern const char *OrientList[];
extern void port_web_print_options(void);
extern void PO(char *s1);
extern void PInt(int64_t n);
extern void PIntComma(int64_t n);
/* PIntHC declared in Hardware_Includes.h. */
extern void PRet(void);
extern void PO2Int(char *s1, int n1);
extern void PO3Int(char *s1, int n1, int n2);
extern short HRes;
extern short VRes;

void port_print_display_resolution_hdmi(void) { /* VGA-family only. */ }

void port_print_display_panel_touch(void) {
    int i = 0;
    PO2Int("CPUSPEED (KHz)", Option.CPU_Speed);
    if(Option.DISPLAY_CONSOLE == true) {
        PO("LCDPANEL CONSOLE");
        if(Option.DefaultFont != (Option.DISPLAY_TYPE==SCREENMODE2? (6<<4) | 1 : 0x01 ))PInt((Option.DefaultFont>>4) +1);
        else if(!(Option.DefaultFC==WHITE && Option.DefaultBC==BLACK && Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.DefaultFC!=WHITE)PIntHC(Option.DefaultFC);
        else if(!(Option.DefaultBC==BLACK && Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.DefaultBC!=BLACK)PIntHC(Option.DefaultBC);
        else if(!(Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.BackLightLevel != 100)PIntComma(Option.BackLightLevel);
        else if(!(Option.BackLightLevel == 100 && Option.NoScroll==0))MMputchar(',',1);
        if(Option.NoScroll!=0)MMPrintString(",NOSCROLL");
        PRet();
    }
    if(Option.Height != 24 || Option.Width != 80) PO3Int("DISPLAY", Option.Height, Option.Width);
    if(Option.DISPLAY_TYPE == DISP_USER) PO3Int("LCDPANEL USER", HRes, VRes);
    {
        int in_range = (Option.DISPLAY_TYPE > I2C_PANEL &&
                        (Option.DISPLAY_TYPE < DISP_USER ||
                         Option.DISPLAY_TYPE >= NEXTGEN));
        if (in_range) {
            i=Option.DISPLAY_ORIENTATION;
            if(Option.DISPLAY_TYPE==ST7789 || Option.DISPLAY_TYPE == ST7789A)i=(i+2) % 4;
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CD].pinname);
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_Reset].pinname);
            if(Option.DISPLAY_TYPE!=ST7920){
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.LCD_CS].pinname);
            }
            int buffered_range = (Option.DISPLAY_TYPE <= I2C_PANEL ||
                                  (Option.DISPLAY_TYPE >= BufferedPanel &&
                                   Option.DISPLAY_TYPE <  NEXTGEN));
            if(!buffered_range && Option.DISPLAY_BL){
                MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
            } else if(Option.BGR)MMputchar(',',1);
            if(!buffered_range && Option.BGR){
                MMputchar(',',1);MMPrintString((char *)"INVERT");
            }
            if(Option.DISPLAY_TYPE==SSD1306SPI && Option.I2Coffset)PIntComma(Option.I2Coffset);
            if(Option.DISPLAY_TYPE==N5110 && Option.LCDVOP!=0xC8)PIntComma(Option.LCDVOP);
            MMPrintString("\r\n");
        }
    }
    if(Option.DISPLAY_TYPE > 0 && Option.DISPLAY_TYPE <= I2C_PANEL) {
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", "); MMPrintString((char *)OrientList[(int)i - 1]);
        if(Option.DISPLAY_TYPE==SSD1306I2C && Option.I2Coffset)PIntComma(Option.I2Coffset);
        MMPrintString("\r\n");
    }
    if(Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE<VIRTUAL) {
        PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); MMPrintString(", ");
        MMPrintString((char *)OrientList[(int)i - 1]);
        if(Option.DISPLAY_BL){
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.DISPLAY_BL].pinname);
        } else if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13) || Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
        if(Option.SSD_DC!=(Option.DISPLAY_TYPE> SSD_PANEL_8 ? 16: 13)){
            MMputchar(',',1);MMPrintString((char *)PinDef[PINMAP[Option.SSD_DC]].pinname);
        } else if(Option.SSD_RESET!=(Option.DISPLAY_TYPE > SSD_PANEL_8 ? 19: 16) || (Option.SSD_DATA!=1))MMputchar(',',1);
        if(Option.SSD_RESET==-1){
            MMputchar(',',1);MMPrintString("NORESET");
        } else if( (Option.SSD_DATA!=1))MMputchar(',',1);
        if(Option.SSD_DATA!=1){
            MMputchar(',',1);
            MMPrintString((char *)PinDef[Option.SSD_DATA].pinname);
        }
        PRet();
    }
    {
        int is_virtual = (Option.DISPLAY_TYPE >= VIRTUAL &&
                          Option.DISPLAY_TYPE <  NEXTGEN);
        if (is_virtual) {
            PO("LCDPANEL"); MMPrintString((char *)display_details[Option.DISPLAY_TYPE].name); PRet();
        }
    }
    if(Option.BackLightLevel!=100)PO2Int("LCD BACKLIGHT", Option.BackLightLevel);
    hal_gui_controls_print_options();
    hal_i2c_keypad_print_options();
    port_web_print_options();
    if(Option.TOUCH_CS) {
        PO("TOUCH");
        if(Option.TOUCH_CAP==1)(MMPrintString("FT6336 "));
        MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_IRQ : Option.TOUCH_CS].pinname);MMputchar(',',1);
        MMPrintString((char *)PinDef[Option.TOUCH_CAP==1 ? Option.TOUCH_CS : Option.TOUCH_IRQ].pinname);
        if(Option.TOUCH_Click) {
            MMputchar(',',1);MMPrintString((char *)PinDef[Option.TOUCH_Click].pinname);
        } else if(Option.TOUCH_CAP)MMputchar(',',1);
        if(Option.TOUCH_CAP){
            MMputchar(',',1);PInt(Option.THRESHOLD_CAP);
        }
        MMPrintString("\r\n");
        if(Option.TOUCH_XZERO != 0 || Option.TOUCH_YZERO != 0) {
            MMPrintString("GUI CALIBRATE "); PInt(Option.TOUCH_SWAPXY); PIntComma(Option.TOUCH_XZERO); PIntComma(Option.TOUCH_YZERO);
            PIntComma(Option.TOUCH_XSCALE * 10000); PIntComma(Option.TOUCH_YSCALE * 10000); MMPrintString("\r\n");
        }
    }
}

void port_print_sdcard_system_spi_share(void) { /* Non-VGA: dedicated SD pins. */ }
void port_print_vga_pins(void) { /* Non-VGA: no VGA pins. */ }

/* MM_Misc.c batch-18 hooks — PicoMite SPI-LCD side (non-VGA). */

void port_print_system_spi(void) {
    if (Option.SYSTEM_CLK) {
        PO("SYSTEM SPI");
        MMPrintString((char *)PinDef[Option.SYSTEM_CLK].pinname);  MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.SYSTEM_MOSI].pinname); MMputchar(',', 1);
        MMPrintString((char *)PinDef[Option.SYSTEM_MISO].pinname); MMPrintString("\r\n");
    }
}

void port_disable_sd_release_system_spi(void) {
    /* Non-VGA: SD has dedicated pins, leave SYSTEM_* alone. */
}

int port_setter_sdcard_combined_cs(unsigned char *tp) {
    if (!checkstring(tp, (unsigned char *)"COMBINED CS")) return 0;
    if (Option.SD_CS || Option.CombinedCS) error("SDcard already configured");
    if (!Option.SYSTEM_CLK) error("System SPI not configured");
    if (!Option.TOUCH_CS)   error("Touch CS pin not configured");
    Option.CombinedCS = 1;
    Option.SD_CS = 0;
    SaveOptions();
    _excep_code = RESET_COMMAND;
    SoftReset();
    return 1;
}

void port_setter_sdcard_argc_check(int argc) {
    /* Non-VGA accepts the 1-arg (CS only) shortcut as well as the
     * full 7-arg form. */
    if (!(argc == 1 || argc == 7)) error("Syntax");
}

int port_setter_sdcard_via_system_spi(int pin1, int pin2, int pin3) {
    /* Non-VGA: SD always uses dedicated SD_*_PIN. */
    (void)pin1; (void)pin2; (void)pin3;
    return 0;
}

int port_mminfo_lcdpanel(unsigned char *ep, unsigned char *sret, int *out_targ) {
    if (!checkstring(ep, (unsigned char *)"LCDPANEL")) return 0;
    strcpy((char *)sret, display_details[Option.DISPLAY_TYPE].name);
    CtoM(sret);
    *out_targ = T_STR;
    return 1;
}

int port_mminfo_lcd320(unsigned char *ep, int64_t *out_iret, int *out_targ) {
    if (!checkstring(ep, (unsigned char *)"LCD320")) return 0;
    *out_iret = (SSD16TYPE || Option.DISPLAY_TYPE == IPS_4_16);
    *out_targ = T_INT;
    return 1;
}

/* PicoMite SPI-LCD post-clear-program housekeeping: SPIatRisk
 * marker (used by SD/Touch dispatch to avoid bus contention) and a
 * full-frame refresh of the panel after an autorun-disabled boot. */
extern unsigned char SPIatRisk;
void port_repl_post_clear_display_refresh(void) {
    SPIatRisk = ((Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < BufferedPanel) &&
                 Option.SD_CLK_PIN == 0);
    low_x = 0; high_x = HRes - 1; low_y = 0; high_y = VRes - 1;
    if (Option.Refresh) Display_Refresh();
}

#include "hal/hal_option_setters.h"
#include "hal/hal_pin.h"

extern int KeyboardlightSlice, KeyboardlightChannel;
extern void disable_lcdspi(void);
extern void disable_systemspi(void);
int port_setter_hdmi_pins(unsigned char *cmdline) { (void)cmdline; return 0; }

int port_setter_keyboard_backlight(unsigned char *cmdline) {
#ifdef rp2350
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"KEYBOARD BACKLIGHT");
    if (!tp) return 0;
    if (!Option.LOCAL_KEYBOARD) error("Invalid option");
    Option.KeyboardBrightness = getint(tp, 0, 100);
    setpwm(PINMAP[43], &KeyboardlightChannel, &KeyboardlightSlice, 50000.0, Option.KeyboardBrightness);
    SaveOptions();
    return 1;
#else
    (void)cmdline;
    return 0;
#endif
}

int port_setter_scroll_start(int64_t *out_iret) {
#ifdef rp2350
    *out_iret = ScrollStart;
    return 1;
#else
    (void)out_iret;
    return 0;
#endif
}

int port_setter_screenbuff(int64_t *out_iret) {
#ifdef rp2350
    *out_iret = (int64_t)(uint32_t)ScreenBuffer;
    return 1;
#else
    (void)out_iret;
    return 0;
#endif
}

int port_setter_system_lcd_spi(unsigned char *cmdline) {
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"SYSTEM SPI");
    if (tp) {
        int pin1, pin2, pin3;
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            if (CurrentLinePtr) error("Invalid in a program");
            if ((Option.SD_CS && Option.SD_CLK_PIN == 0) || Option.TOUCH_CS || Option.LCD_CS || Option.CombinedCS)
                error("In use");
            disable_systemspi();
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return 1;
        }
        getargs(&tp, 5, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (argc != 5) error("Syntax");
        if (Option.SYSTEM_CLK) error("SYSTEM SPI already configured");
        unsigned char code;
        if (!(code = codecheck(argv[0]))) argv[0] += 2;
        pin1 = getinteger(argv[0]);
        if (!code) pin1 = codemap(pin1);
        if (IsInvalidPin(pin1)) error("Invalid pin");
        if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
        if (!(code = codecheck(argv[2]))) argv[2] += 2;
        pin2 = getinteger(argv[2]);
        if (!code) pin2 = codemap(pin2);
        if (IsInvalidPin(pin2)) error("Invalid pin");
        if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin2, pin2);
        if (!(code = codecheck(argv[4]))) argv[4] += 2;
        pin3 = getinteger(argv[4]);
        if (!code) pin3 = codemap(pin3);
        if (IsInvalidPin(pin3)) error("Invalid pin");
        if (ExtCurrentConfig[pin3] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin3, pin3);
        if (!(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) &&
            !(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX))
            error("Not valid SPI pins");
        if (PinDef[pin1].mode & SPI0SCK && SPI0locked) error("SPI channel already configured");
        if (PinDef[pin1].mode & SPI1SCK && SPI1locked) error("SPI channel already configured");
        Option.SYSTEM_CLK = pin1;
        Option.SYSTEM_MOSI = pin2;
        Option.SYSTEM_MISO = pin3;
#ifdef rp2350
        if (!Option.LCD_CLK) {
            Option.LCD_CLK = Option.SYSTEM_CLK;
            Option.LCD_MOSI = Option.SYSTEM_MOSI;
            Option.LCD_MISO = Option.SYSTEM_MISO;
        }
#endif
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#ifdef rp2350
    tp = checkstring(cmdline, (unsigned char *)"LCD SPI");
    if (tp) {
        int pin1, pin2, pin3;
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            if (CurrentLinePtr) error("Invalid in a program");
            if (Option.LCD_CS) error("In use");
            disable_lcdspi();
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return 1;
        }
        getargs(&tp, 5, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (argc != 5) error("Syntax");
        if (Option.LCD_CLK && !(Option.LCD_CLK == Option.SYSTEM_CLK)) error("LCD SPI already configured");
        unsigned char code;
        if (!(code = codecheck(argv[0]))) argv[0] += 2;
        pin1 = getinteger(argv[0]);
        if (!code) pin1 = codemap(pin1);
        if (IsInvalidPin(pin1)) error("Invalid pin");
        if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
        if (!(code = codecheck(argv[2]))) argv[2] += 2;
        pin2 = getinteger(argv[2]);
        if (!code) pin2 = codemap(pin2);
        if (IsInvalidPin(pin2)) error("Invalid pin");
        if (ExtCurrentConfig[pin2] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin2, pin2);
        if (!(code = codecheck(argv[4]))) argv[4] += 2;
        pin3 = getinteger(argv[4]);
        if (!code) pin3 = codemap(pin3);
        if (IsInvalidPin(pin3)) error("Invalid pin");
        if (ExtCurrentConfig[pin3] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin3, pin3);
        if (!(PinDef[pin1].mode & SPI0SCK && PinDef[pin2].mode & SPI0TX && PinDef[pin3].mode & SPI0RX) &&
            !(PinDef[pin1].mode & SPI1SCK && PinDef[pin2].mode & SPI1TX && PinDef[pin3].mode & SPI1RX))
            error("Not valid SPI pins");
        if (PinDef[pin1].mode & SPI0SCK && SPI0locked) error("SPI channel already configured");
        if (PinDef[pin1].mode & SPI1SCK && SPI1locked) error("SPI channel already configured");
        Option.LCD_CLK = pin1;
        Option.LCD_MOSI = pin2;
        Option.LCD_MISO = pin3;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    return 0;
}

int port_setter_touch_status(unsigned char *out_sret) {
    if (Option.TOUCH_CS == false)                         strcpy((char *)out_sret, "Disabled");
    else if (Option.TOUCH_XZERO == TOUCH_NOT_CALIBRATED)  strcpy((char *)out_sret, "Not calibrated");
    else                                                   strcpy((char *)out_sret, "Ready");
    return 1;
}

int port_setter_poke_display(unsigned char *p) {
    getargs(&p, (MAX_ARG_COUNT * 2) - 3, (unsigned char *)",");
    if (!argc) return 1;
    if (Option.DISPLAY_TYPE >= SSDPANEL && Option.DISPLAY_TYPE < VIRTUAL) {
        WriteComand(getinteger(argv[0]));
        for (int i = 2; i < argc; i += 2) WriteData(getinteger(argv[i]));
        return 1;
    } else if (Option.DISPLAY_TYPE > I2C_PANEL && Option.DISPLAY_TYPE < ST7920) {
        spi_write_command(getinteger(argv[0]));
        for (int i = 2; i < argc; i += 2) spi_write_data(getinteger(argv[i]));
        return 1;
    } else if (Option.DISPLAY_TYPE <= I2C_PANEL) {
        if (argc > 1) error("UNsupported command");
        I2C_Send_Command(getinteger(argv[0]));
        return 1;
    } else {
        error("Display not supported");
    }
    return 1;
}
