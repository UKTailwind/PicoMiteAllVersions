#include "pico_runtime_internal.h"

lfs_t lfs;
lfs_dir_t lfs_dir;
struct lfs_info lfs_info;
#ifdef rp2350
extern unsigned int psmap[];
extern const unsigned int psmap_size_bytes;

static void psram_boot_init(void)
{
    if (!Option.PSRAM_CS_PIN) return;

    /*
     * Initialize PSRAM after boot flash/filesystem activity has finished.
     * Initializing earlier proved insufficient: the final prompt state could
     * report PSRAM while raw PSRAM writes did not stick.
     */
    PSRAMpin = PinDef[Option.PSRAM_CS_PIN].GPno;
    psram_setup();
    size_t detected = psram_size();
    if (!detected) {
        PSRAMsize = 0;
        PSRAMbase = 0;
        Option.PSRAM_CS_PIN = 0;
        SaveOptions();
        return;
    }
    PSRAMsize = detected > (2u * 1024u * 1024u)
        ? detected - (2u * 1024u * 1024u)
        : detected;
    /* HAL_PORT_PSRAM_BASE = 0x11000000 on every RP2350 port with PSRAM
     * (XIP cache region). Publish it now that the runtime detect has
     * confirmed a module is present. */
    PSRAMbase = (uintptr_t)HAL_PORT_PSRAM_BASE;
    memset(psmap, 0, psmap_size_bytes);
}

/* hal_psram_init wrapper so the HAL contract is satisfied uniformly
 * across ports. Pico boot path still calls psram_boot_init() directly
 * for now to preserve the existing init ordering; this entry point
 * exists so future shared-init code can fan out through the HAL. */
void hal_psram_init(void) { psram_boot_init(); }
#endif

void MIPS16 updatebootcount(void){
    lfs_file_t lfs_file;
    pico_lfs_cfg.block_count = (Option.FlashSize-RoundUpK4(TOP_OF_SYSTEM_FLASH)-(Option.modbuff ? 1024*Option.modbuffsize : 0))/4096;
    int err,boot_count=0;
    err= lfs_mount(&lfs, &pico_lfs_cfg);
    // reformat if we can't mount the filesystem
    // this should only happen on the first boot
    if (err) {
        err=lfs_format(&lfs, &pico_lfs_cfg);
        err=lfs_mount(&lfs, &pico_lfs_cfg);
    }

    err=lfs_file_open(&lfs, &lfs_file, "bootcount", LFS_O_RDWR | LFS_O_CREAT);;
    int dt=get_fattime();
    err=lfs_setattr(&lfs, "bootcount", 'A', &dt,   4);
    err=lfs_file_read(&lfs, &lfs_file, &boot_count, sizeof(boot_count));;
    boot_count+=1;
    err=lfs_file_rewind(&lfs, &lfs_file);
    err=lfs_file_write(&lfs, &lfs_file, &boot_count, sizeof(boot_count));
    err=lfs_file_close(&lfs, &lfs_file);
}
/**
 * @brief Transforms input beginning with * into a corresponding RUN command.
 *
 * e.g.
 *   *foo              =>  RUN "foo"
 *   *"foo bar"        =>  RUN "foo bar"
 *   *foo --wombat     =>  RUN "foo", "--wombat"
 *   *foo "wom"        =>  RUN "foo", Chr$(34) + "wom" + Chr$(34)
 *   *foo "wom" "bat"  =>  RUN "foo", Chr$(34) + "wom" + Chr$(34) + " " + Chr$(34) + "bat" + Chr$(34)
 *   *foo --wom="bat"  =>  RUN "foo", "--wom=" + Chr$(34) + "bat" + Chr$(34)
 */

/* WebConnect body relocated to MMsetwifi.c (WiFi ports) +
 * MMweb_stubs.c (non-WiFi). Hardware_Includes.h declares the symbol
 * unconditionally so call sites stay clean. */

int MIPS16 main(){
    int i=0;
    /* ErrorInPrompt + savewatchdog moved with the prompt loop to
     * MMBasic_REPL.c. */
        i=watchdog_caused_reboot();
#ifdef rp2350
    restart_reason=powman_hw->chip_reset | i;
    rp2350a=(*((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1);
#else
    restart_reason=vreg_and_chip_reset_hw->chip_reset | i;
#endif
    if(_excep_code == SOFT_RESET || _excep_code == SCREWUP_TIMEOUT )restart_reason=0xFFFFFFFF;
    if((_excep_code == WATCHDOG_TIMEOUT) & i) restart_reason=0xFFFFFFFE;
    if((_excep_code == POSSIBLE_WATCHDOG) & i)restart_reason=0xFFFFFFFD;
    LoadOptions();
#ifdef rp2350
    if(rom_get_last_boot_type()==BOOT_TYPE_FLASH_UPDATE)restart_reason=0xFFFFFFFC;
#else
    if(restart_reason==0x10001 || restart_reason==0x101)restart_reason=0xFFFFFFFC;
#endif
    uint32_t excep=_excep_code;
    if(  Option.Baudrate == 0 ||
        !(Option.Tab==2 || Option.Tab==3 || Option.Tab==4 ||Option.Tab==8) ||
        !(Option.Autorun>=0 && Option.Autorun<=MAXFLASHSLOTS+1) ||
        Option.CPU_Speed<MIN_CPU || Option.CPU_Speed>MAX_CPU ||
        Option.PROG_FLASH_SIZE!=MAX_PROG_SIZE ||
        (Option.heartbeatpin==0 && Option.NoHeartbeat==0) ||
        !(Option.Magic==MagicKey)
        ){
        ResetAllFlash();              // init the options if this is the very first startup
        _excep_code=0;
        watchdog_enable(1, 1);
        while(1);
    }
    port_video_validate_boot_options();
    m_alloc(M_PROG);                                           // init the variables for program memory
    LibMemory = (uint8_t *)flash_libmemory;
    uSec(100);
    if(_excep_code == RESET_CLOCKSPEED) {
        /* HAL_PORT_DEFAULT_CPU_SPEED_KHZ is set per port in
         * port_config.h (200 MHz for SPI-LCD, 252 MHz for pure VGA,
         * 315 MHz for HDMI HSTX). */
        Option.CPU_Speed = HAL_PORT_DEFAULT_CPU_SPEED_KHZ;
        SaveOptions();
        _excep_code=INVALID_CLOCKSPEED;
        watchdog_enable(1, 1);
        while(1);
    } else {
        _excep_code=RESET_CLOCKSPEED;
        watchdog_enable(1000, 1);
    }
#ifdef rp2350
    if(!rp2350a){
        if(!Option.AllPins){
            Option.AllPins=true;
            SaveOptions();
        }
    }
#endif
    vreg_disable_voltage_limit ();
    if(Option.CPU_Speed<=200000)vreg_set_voltage(VREG_VOLTAGE_1_15);
    else if(Option.CPU_Speed>200000  && Option.CPU_Speed<=320000 )vreg_set_voltage(VREG_VOLTAGE_1_30);  // Std default @ boot is 1_10
#ifdef rp2350
    else if(Option.CPU_Speed>320000  && Option.CPU_Speed<=360000 )vreg_set_voltage(VREG_VOLTAGE_1_40);  // Std default @ boot is 1_10
    else vreg_set_voltage(VREG_VOLTAGE_1_60);  // Std default @ boot is 1_10
#else
    else vreg_set_voltage(VREG_VOLTAGE_1_30);
#endif
    sleep_ms(10);
#ifdef rp2350
    pads_qspi_hw->io[0]=0x67;
    pads_qspi_hw->io[1]=0x67;
    pads_qspi_hw->io[2]=0x67;
    pads_qspi_hw->io[3]=0x6B;
    pads_qspi_hw->io[4]=0x6B;
    pads_qspi_hw->io[5]=0x6B;
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    sleep_ms(2);
#endif
    set_sys_clock_khz(port_video_sys_clock_khz(Option.CPU_Speed), false);
#ifdef rp2350
    if(Option.CPU_Speed<=288000)qmi_hw->m[0].timing = 0x40006202;
    sleep_ms(2);
#endif
    PWM_FREQ=44100;
    pico_get_unique_board_id_string (id_out,12);
    if(clock_get_hz(clk_usb)!=48000000){
        ResetAllFlash();              // init the options if this is the very first startup
        _excep_code=INVALID_CLOCKSPEED;
        watchdog_enable(1, 1);
        while(1);
    }
    clock_configure(
        clk_adc,
        0,                                                // No glitchless mux
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
        Option.CPU_Speed * 1000,                               // Input frequency
        ADC_CLK_SPEED                                 // Output (must be same as no divider)
    );
    SetADCFreq(500000);
    adc_clk_div=adc_hw->div;
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
    hal_display_merge_init_fb_mutex();           /* SPI-LCD ports only */


#ifndef rp2350
    if(Option.CPU_Speed<=200000)modclock(2);
#else
    /* NEXTGEN displays are MEM332-family SPI-LCD with shadow
     * framebuffer; the runtime guard `Option.DISPLAY_TYPE >= NEXTGEN`
     * is dead on ports whose OPTION setter rejects those values. */
    if (Option.DISPLAY_TYPE >= NEXTGEN) {
        framebuffersize = display_details[Option.DISPLAY_TYPE].horizontal *
                          display_details[Option.DISPLAY_TYPE].vertical;
        heap_memory_size -= framebuffersize;
        FRAMEBUFFER = AllMemory + heap_memory_size + 256;
    }
#endif
    uSec(100);
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    _excep_code=excep;
    port_video_post_clock_init();
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
    ticks_per_second = Option.CPU_Speed*1000;
    // The serial clock won't vary from this point onward, so we can configure
    // the UART etc.
    LoadOptions();
	stdio_init_all();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    mSecTimer=time_us_64()/1000;
    add_repeating_timer_us(-1000, timer_callback, NULL, &timer);
    InitReservedIO();
    ClearExternalIO();
    ConsoleRxBufHead = 0;
    ConsoleRxBufTail = 0;
    ConsoleTxBufHead = 0;
    ConsoleTxBufTail = 0;
    PromptFC=gui_fcolour=Option.DefaultFC;
    PromptBC=gui_bcolour=Option.DefaultBC;
    InitHeap(true);              										// initilise memory allocation
    uSecFunc(1000);
    fileio_flash_write_begin();
    fileio_flash_write_end();
    mSecTimer=time_us_64()/1000;
    DISPLAY_TYPE = Option.DISPLAY_TYPE;
    // negative timeout means exact delay (rather than delay between callbacks)
	OptionErrorSkip = false;
    /* USB-CDC stdio boot setup — runs the translate_crlf reset and
     * the 5-second host-attach wait on PS/2 ports; no-op on USB-host
     * ports (USB-A in host mode). */
    hal_console_usb_cdc_boot_init();
	InitBasic();
    /* Display + keypad init order. SPI-LCD ports run the SSD / SPI /
     * I²C / virtual display + touch helpers; on VGA family these are
     * stubs in spi_lcd_periph_io_stub.c. The PicoCalc keypad MCU
     * shares the I²C bus, so on that port the SSD/I²C display init
     * helpers are skipped (the keypad is busy on those addresses)
     * and a 300 ms settle delay runs after touch-init. Other SPI-LCD
     * boards run the standard sequence with no delay.
     *
     * SSD1963.h / Touch.h are gated in Hardware_Includes.h, so
     * declare InitDisplaySSD / InitTouch here unconditionally. */
    extern void InitDisplaySSD(void);
    extern void InitDisplaySPI(int InitOnly);
    extern void InitDisplayI2C(int InitOnly);
    extern void InitTouch(void);
    if (!hal_i2c_keypad_owns_i2c_bus()) InitDisplaySSD();
    InitDisplaySPI(0);
    if (!hal_i2c_keypad_owns_i2c_bus()) {
        InitDisplayI2C(0);
        InitDisplayVirtual();
    }
    InitTouch();
    hal_i2c_keypad_boot_init();
    if (Option.BackLightLevel) setBacklight(Option.BackLightLevel, 0);
    /* ErrorInPrompt is a static inside MMBasic_RunPromptLoop; initialized there. */
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION,sigbus);
    exception_set_exclusive_handler(SVCALL_EXCEPTION,sigbus);
    exception_set_exclusive_handler(PENDSV_EXCEPTION,sigbus);
    exception_set_exclusive_handler(NMI_EXCEPTION ,sigbus);
    exception_set_exclusive_handler(SYSTICK_EXCEPTION,sigbus);
    while((i=getConsole())!=-1){}

    /* core1 launch + post-launch display prep dispatched per port via
     * hal_main_init.h. */
    port_main_launch_core1();
        strcpy((char *)banner,MES_SIGNON);
#ifdef rp2350
        /* Stamp the package suffix (A/B) over the trailing space of
         * CHIP="RP2350 ". Independent of HAL_PORT_DEVICE_NAME length —
         * which is what the previous per-port position table was
         * compensating for. */
        {
            char *_pkg = strstr((char *)banner, "RP2350");
            if (_pkg) _pkg[6] = (rp2350a ? 'A' : 'B');
        }
#endif
    extern void MMBasic_PrintBanner(void);
    if(!(_excep_code == RESTART_NOAUTORUN || _excep_code == INVALID_CLOCKSPEED || _excep_code == SCREWUP_TIMEOUT || _excep_code == WATCHDOG_TIMEOUT || (_excep_code==POSSIBLE_WATCHDOG && watchdog_caused_reboot()))){
        if(Option.Autorun==0 ){
            if(!(_excep_code == RESET_COMMAND || _excep_code == SOFT_RESET)){
                MMBasic_PrintBanner();
            }
        } else {
            if(Option.Autorun!=MAXFLASHSLOTS+1){
                ProgMemory=(unsigned char *)(flash_target_contents+(Option.Autorun-1)*MAX_PROG_SIZE);
            }
            if(*ProgMemory != 0x01 ) {
                MMBasic_PrintBanner();
            }
        }
    }
    bc_crash_dump_if_any();
    memset(inpbuf,0,STRINGSIZE);
    WatchdogSet = false;
    if(_excep_code == INVALID_CLOCKSPEED) {
        MMPrintString("\r\nInvalid clock speed - reset to default\r\n");
        restart_reason=0xFFFFFFFF;
    }
    if(_excep_code == SCREWUP_TIMEOUT) {
        MMPrintString("\r\nCommand timeout\r\n");
        restart_reason=0xFFFFFFFF;
    }
    if(restart_reason==0xFFFFFFFE) {
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        MMPrintString("\r\nMMBasic Watchdog timeout\r\n");
    }
    if(restart_reason==0xFFFFFFFD){
        MMPrintString("\r\nHW Watchdog timeout\r\n");
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        _excep_code=0;
    }
    if(restart_reason==0xFFFFFFFC) {
        WatchdogSet = true;                                 // remember if it was a watchdog timeout
        MMPrintString("\rFirmware updated\r\n");
    }
    /* savewatchdog is now a local of MMBasic_RunPromptLoop(). */
    if(noRTC){
        noRTC=0;
        Option.RTC=0;
        SaveOptions();
        MMPrintString("RTC not found, OPTION RTC AUTO disabled\r\n");
    }
    if(noI2C){
        noI2C=0;
        Option.KeyboardConfig=NO_KEYBOARD;
        SaveOptions();
        MMPrintString("I2C Keyboard not found, OPTION KEYBOARD disabled\r\n");
    }
    updatebootcount();
#ifdef rp2350
    psram_boot_init();
#endif
    *tknbuf = 0;
     ContinuePoint = nextstmt;                               // in case the user wants to use the continue command
    hal_keyboard_init();        /* USB: TinyUSB init; PS/2: initKeyboard */
    /* PS/2 ports also init mouse0 here. The hal_keyboard_init() PS/2
     * impl already calls initKeyboard(); add the mouse init alongside. */
    hal_keyboard_init_external_mouse();
#ifdef rp2350
    if(PSRAMsize){MMPrintString("Total of ");PInt(PSRAMsize/(1024*1024));MMPrintString(" Mbytes PSRAM available\r\n");}
#endif
    /* HAL_PORT_AUDIO_I2S_PIO_NUM is set per port in port_config.h. */
    start_i2s(HAL_PORT_AUDIO_I2S_PIO_NUM, 1);


    extern void MMBasic_RunPromptLoop(void);
    MMBasic_RunPromptLoop();
}
