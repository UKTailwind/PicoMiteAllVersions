/*
 * ports/pico_sdk_common/misc_option_setters.c — peripheral OPTION
 * setters whose validity depends on rp2350 features (HDMI PINS,
 * KEYBOARD BACKLIGHT, PSRAM PIN) or PS/2 vs USB keyboard backends
 * (KEYBOARD REPEAT, PS2 PINS / KEYBOARD PINS, MOUSE).
 *
 * Single entry point port_misc_option_setter() returns 1 if the
 * cmdline matches a setter (which then SaveOptions / SoftReset /
 * error() before falling out), 0 otherwise. Internal #ifdef gating
 * lives here in the port file so MM_Misc.c carries no PICOMITE /
 * rp2350 / HDMI / USBKEYBOARD ifdefs around these blocks.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"
#include "hardware/pio.h"

#if !defined(MMBASIC_HOST)

extern int KeyboardlightSlice, KeyboardlightChannel;
extern void disable_lcdspi(void);
extern void disable_systemspi(void);

int MIPS16 port_misc_option_setter(unsigned char *cmdline)
{
    unsigned char *tp;

#ifdef rp2350
#if HAL_PORT_HAS_HDMI
    tp = checkstring(cmdline, (unsigned char *)"HDMI PINS");
    if (tp) {
        getargs(&tp, 7, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (argc != 7) error("Syntax");
        uint8_t clock = getint(argv[0], 0, 7);
        uint8_t d0 = getint(argv[2], 0, 7);
        uint8_t d1 = getint(argv[4], 0, 7);
        uint8_t d2 = getint(argv[6], 0, 7);
        if ((clock & 0x6) == (d0 & 0x6) || (clock & 0x6) == (d1 & 0x6) ||
            (clock & 0x6) == (d2 & 0x6) || (d0 & 0x6) == (d1 & 0x6) ||
            (d0 & 0x6) == (d2 & 0x6) || (d1 & 0x6) == (d2 & 0x6))
            error("Channels not unique");
        Option.HDMIclock = clock;
        Option.HDMId0 = d0;
        Option.HDMId1 = d1;
        Option.HDMId2 = d2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
#if defined(PICOMITE) && defined(rp2350)
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD BACKLIGHT");
    if (tp) {
        if (!Option.LOCAL_KEYBOARD) error("Invalid option");
        Option.KeyboardBrightness = getint(tp, 0, 100);
        setpwm(PINMAP[43], &KeyboardlightChannel, &KeyboardlightSlice, 50000.0, Option.KeyboardBrightness);
        SaveOptions();
        return 1;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PSRAM PIN");
    if (tp) {
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            Option.PSRAM_CS_PIN = 0;
            SaveOptions();
            _excep_code = RESET_COMMAND;
            SoftReset();
            return 1;
        }
        int pin1;
        unsigned char code;
        getargs(&tp, 1, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (!(code = codecheck(argv[0]))) argv[0] += 2;
        pin1 = getinteger(argv[0]);
        if (!code) pin1 = codemap(pin1);
        if (IsInvalidPin(pin1)) error("Invalid pin");
        if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin | is in use", pin1);
        if (!(pin1 == 1 || pin1 == 11 || pin1 == 25 || pin1 == 62))
            error("Invalid pin for PSRAM chip select (GP0,GP8,GP19,GP47)");
        Option.PSRAM_CS_PIN = pin1;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif

#ifdef USBKEYBOARD
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        Option.RepeatStart = getint(argv[0], 100, 2000);
        Option.RepeatRate = getint(argv[2], 25, 2000);
        SaveOptions();
        return 1;
    }
#else
#if defined(PICOMITE) && defined(rp2350)
    tp = checkstring(cmdline, (unsigned char *)"KEYBOARD REPEAT");
    if (tp) {
        getargs(&tp, 3, (unsigned char *)",");
        if (!Option.LOCAL_KEYBOARD) error("Syntax");
        Option.RepeatStart = getint(argv[0], 100, 2000);
        Option.RepeatRate = getint(argv[2], 25, 2000);
        SaveOptions();
        return 1;
    }
#endif
    tp = checkstring(cmdline, (unsigned char *)"PS2 PINS");
    if (tp == NULL) tp = checkstring(cmdline, (unsigned char *)"KEYBOARD PINS");
    if (tp) {
        int pin1, pin2;
        unsigned char code;
        getargs(&tp, 3, (unsigned char *)",");
        if (CurrentLinePtr) error("Invalid in a program");
        if (Option.KEYBOARD_CLOCK) error("Keyboard must be disabled to change pins");
        if (argc != 3) error("Syntax");
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
        Option.KEYBOARD_CLOCK = pin1;
        Option.KEYBOARD_DATA = pin2;
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
    tp = checkstring(cmdline, (unsigned char *)"MOUSE");
    if (tp) {
        if (CurrentLinePtr) error("Invalid in a program");
        if (checkstring(tp, (unsigned char *)"DISABLE")) {
            Option.MOUSE_CLOCK = 0;
            Option.MOUSE_DATA = 0;
        } else {
            int pin1, pin2;
            unsigned char code;
            getargs(&tp, 3, (unsigned char *)",");
            if (Option.MOUSE_CLOCK) error("Mouse must be disabled to change pins");
            if (argc != 3) error("Syntax");
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
            Option.MOUSE_CLOCK = pin1;
            Option.MOUSE_DATA = pin2;
        }
        SaveOptions();
        _excep_code = RESET_COMMAND;
        SoftReset();
        return 1;
    }
#endif
    (void)tp;
    return 0;
}

/* OPTION PICO ON/OFF — exposes/hides CYW43-shadow pins (41/42/44).
 * Disabled on WEB (CYW43 actually owns those pins). RP2350B not
 * supported (no shadow needed). */
int MIPS16 port_pico_pins_option_setter(unsigned char *cmdline)
{
#if HAL_PORT_HAS_WIFI
    (void)cmdline;
    return 0;
#else
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"PICO");
    if (!tp) return 0;
#ifdef rp2350
    if (!rp2350a) error("Invalid for RP2350B");
#endif
    if (checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE"))
        Option.AllPins = 1;
    else if (checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE"))
        Option.AllPins = 0;
    else error("Syntax");
    SaveOptions();
    if (Option.AllPins == 0) {
        if (CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(41, EXT_DIG_OUT, Option.PWM);
        if (CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(42, EXT_DIG_IN, 0);
        if (CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(44, EXT_ANA_IN, 0);
    } else {
        if (CheckPin(41, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(41, EXT_NOT_CONFIG, 0);
        if (CheckPin(42, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(42, EXT_NOT_CONFIG, 0);
        if (CheckPin(44, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) ExtCfg(44, EXT_NOT_CONFIG, 0);
    }
    return 1;
#endif
}

/* OPTION HEARTBEAT — WEB only allows ON/OFF (no pin reassignment);
 * other ports allow pin selection. */
int MIPS16 port_heartbeat_option_setter(unsigned char *cmdline)
{
    unsigned char *tp = checkstring(cmdline, (unsigned char *)"HEARTBEAT");
    if (!tp) return 0;
    if (checkstring(tp, (unsigned char *)"OFF") || checkstring(tp, (unsigned char *)"DISABLE")) {
        Option.NoHeartbeat = 1;
    } else {
#if HAL_PORT_HAS_WIFI
        if (checkstring(tp, (unsigned char *)"ON") || checkstring(tp, (unsigned char *)"ENABLE"))
            Option.NoHeartbeat = 0;
        else error("Syntax");
        SaveOptions();
        return 1;
#else
        unsigned char *p = NULL;
        p = checkstring(tp, (unsigned char *)"ON");
        if (p == NULL) p = checkstring(tp, (unsigned char *)"ENABLE");
        if (p) {
            getargs(&p, 1, (unsigned char *)",");
            if (argc) {
                unsigned char code, pin1;
                if (!(code = codecheck(p))) p += 2;
                pin1 = getinteger(p);
                if (!code) pin1 = codemap(pin1);
                if (IsInvalidPin(pin1)) error("Invalid pin");
                if (ExtCurrentConfig[pin1] != EXT_NOT_CONFIG) error("Pin %/| is in use", pin1, pin1);
                Option.NoHeartbeat = 0;
                Option.heartbeatpin = pin1;
                SaveOptions();
                _excep_code = RESET_COMMAND;
                SoftReset();
            } else Option.NoHeartbeat = 0;
        } else error("Syntax");
#endif
    }
#if !HAL_PORT_HAS_WIFI
    SaveOptions();
    if (CheckPin(HEARTBEATpin, CP_NOABORT | CP_IGNORE_INUSE | CP_IGNORE_RESERVED)) {
        if (Option.NoHeartbeat == 0) {
            hal_pin_set_mode(PinDef[HEARTBEATpin].GPno, HAL_PIN_MODE_OUTPUT);
            ExtCurrentConfig[PinDef[HEARTBEATpin].pin] = EXT_HEARTBEAT;
        } else ExtCfg(HEARTBEATpin, EXT_NOT_CONFIG, 0);
    } else error("Pin %/| is reserved", HEARTBEATpin, HEARTBEATpin);
#endif
    return 1;
}

/* OPTION SYSTEM SPI / OPTION LCD SPI — non-VGA only. PICOMITE+rp2350
 * also gets a separate LCD SPI bus that defaults to mirroring the
 * system bus on first config. */
int MIPS16 port_system_lcd_spi_option_setter(unsigned char *cmdline)
{
#ifdef PICOMITEVGA
    (void)cmdline;
    return 0;
#else
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
#if defined(PICOMITE) && defined(rp2350)
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
#if defined(PICOMITE) && defined(rp2350)
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
#endif
}

/* OPTION AUDIO I2S — PWM-slice selection for the I2S backend.
 *
 * On RP2040 there's only checkslice(); the bclk pin's slice is the
 * one we keep. On RP2350 there's an extra PIO conflict check (the
 * I2S state-machine shares PIO bits with QVGA on PICOMITEVGA-non-HDMI
 * builds; on other rp2350 ports it lands in pio2). RP2350A reserves
 * slice 11 for audio. */
extern int checkslice(int pin1, int pin2, int ignore);
#ifdef rp2350
extern uint64_t piomap[];
#endif

int MIPS16 port_audio_i2s_pio_slice(int pin1, int pin2)
{
#ifdef rp2350
#if defined(PICOMITEVGA) && !HAL_PORT_HAS_HDMI
    int pio = QVGA_PIO_NUM;
#else
    int pio = 2;
#endif
    uint64_t map = piomap[pio];
    map |= (uint64_t)((uint64_t)1 << (uint64_t)PinDef[pin2].GPno);
    map |= ((uint64_t)1 << (uint64_t)PinDef[pin1].GPno);
    map |= ((uint64_t)1 << (uint64_t)(PinDef[pin1].GPno + 1));
    if ((map & (uint64_t)0xFFFF) && (map & (uint64_t)0xFFFF00000000))
        error("Attempt to define incompatible PIO pins");
    if (rp2350a) return 11;
#endif
    (void)pin2;
    return checkslice(pin1, pin1, 1);
}

/* MM.INFO INTERRUPTS — read NVIC ISER. Cortex-M0+ (RP2040) has the
 * register at PPB+M0PLUS_NVIC_ISER_OFFSET; Cortex-M33 (RP2350) uses
 * different offsets (and the SDK header doesn't define the M0+
 * symbol on rp2350), so we just don't expose this on rp2350. */
int MIPS16 port_mminfo_interrupts(int64_t *out_iret)
{
#ifdef rp2350
    (void)out_iret;
    return 0;
#else
    *out_iret = (int64_t)(uint32_t)*((io_rw_32 *) (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET));
    return 1;
#endif
}

/* MM.INFO TOUCH — VGA Option struct lacks TOUCH_XZERO/TOUCH_CS so
 * the field is unavailable. Other ports return the calibration state. */
int MIPS16 port_mminfo_touch_status(unsigned char *out_sret)
{
#ifdef PICOMITEVGA
    (void)out_sret;
    return 0;
#else
    if (Option.TOUCH_CS == false)                       strcpy((char *)out_sret, "Disabled");
    else if (Option.TOUCH_XZERO == TOUCH_NOT_CALIBRATED) strcpy((char *)out_sret, "Not calibrated");
    else                                                 strcpy((char *)out_sret, "Ready");
    return 1;
#endif
}

/* MM.INFO SCROLL / MM.INFO SCREENBUFF — PicoCalc framebuffer accessors
 * (PICOMITE + rp2350 only; ScreenBuffer macro = FRAMEBUFFER on rp2350).
 * ScrollStart's volatile qualifier comes from SSD1963.h, included via
 * Hardware_Includes.h. */
int MIPS16 port_mminfo_scroll_start(int64_t *out_iret)
{
#if defined(PICOMITE) && defined(rp2350)
    *out_iret = ScrollStart;
    return 1;
#else
    (void)out_iret;
    return 0;
#endif
}
int MIPS16 port_mminfo_screenbuff(int64_t *out_iret)
{
#if defined(PICOMITE) && defined(rp2350)
    *out_iret = (int64_t)(uint32_t)ScreenBuffer;
    return 1;
#else
    (void)out_iret;
    return 0;
#endif
}

/* POKE DISPLAY <args> raw command/data byte sequence. Dispatches by
 * panel class (SSD1963 parallel / SPI-LCD / I2C) — none of those
 * drivers exist on PICOMITEVGA. */
int MIPS16 port_poke_display_panel(unsigned char *p)
{
#ifdef PICOMITEVGA
    (void)p;
    return 0;
#else
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
#endif
}

/* PIO instance lookup for the interrupt poll loop. RP2040 has 2 PIOs
 * with the legacy index-0=pio1 ordering; RP2350 has 3 in natural
 * order. PIOMAX (= HAL_PORT_PIO_COUNT) bounds the caller's loop. */
PIO port_pio_for_index(int pio_idx)
{
#ifdef rp2350
    return (pio_idx == 0 ? pio0 : (pio_idx == 1 ? pio1 : pio2));
#else
    return (pio_idx == 0 ? pio1 : pio0);
#endif
}

#endif /* !MMBASIC_HOST */
