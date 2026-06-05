/*
 * ports/pico_sdk_common/misc_option_setters.c — peripheral OPTION
 * setters whose validity depends on rp2350 features (HDMI PINS,
 * KEYBOARD BACKLIGHT, PSRAM PIN) or PS/2 vs USB keyboard backends
 * (KEYBOARD REPEAT, PS2 PINS / KEYBOARD PINS, MOUSE).
 *
 * Single entry point port_misc_option_setter() returns 1 if the
 * cmdline matches a setter (which then SaveOptions / SoftReset /
 * error() before falling out), 0 otherwise. Per-port-shape branches
 * dispatch through hal/hal_option_setters.h sub-hooks; this file
 * carries no #if HAL_PORT_* gates.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_pin.h"
#include "hal/hal_option_setters.h"
#include "hardware/pio.h"

#if !defined(MMBASIC_HOST)

extern int KeyboardlightSlice, KeyboardlightChannel;
extern void disable_lcdspi(void);
extern void disable_systemspi(void);

int MIPS16 port_misc_option_setter(unsigned char * cmdline) {
    unsigned char * tp;

#ifdef rp2350
    if (port_setter_hdmi_pins(cmdline)) return 1;
    if (port_setter_keyboard_backlight(cmdline)) return 1;
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

    if (port_setter_keyboard_repeat(cmdline)) return 1;
    if (port_setter_ps2_pins(cmdline)) return 1;
    if (port_setter_mouse_pins(cmdline)) return 1;

    (void)tp;
    return 0;
}

/* OPTION PICO ON/OFF — exposes/hides CYW43-shadow pins (41/42/44).
 * Disabled on WEB (CYW43 actually owns those pins). RP2350B not
 * supported (no shadow needed). */
int MIPS16 port_pico_pins_option_setter(unsigned char * cmdline) {
    return port_setter_pico_pins(cmdline);
}

/* OPTION HEARTBEAT — WEB only allows ON/OFF (no pin reassignment);
 * other ports allow pin selection. */
int MIPS16 port_heartbeat_option_setter(unsigned char * cmdline) {
    return port_setter_heartbeat(cmdline);
}

/* OPTION SYSTEM SPI / OPTION LCD SPI — non-VGA only. PICOMITE+rp2350
 * also gets a separate LCD SPI bus that defaults to mirroring the
 * system bus on first config. */
int MIPS16 port_system_lcd_spi_option_setter(unsigned char * cmdline) {
    return port_setter_system_lcd_spi(cmdline);
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

int MIPS16 port_audio_i2s_pio_slice(int pin1, int pin2) {
#ifdef rp2350
    int pio = HAL_PORT_AUDIO_I2S_PIO_NUM;
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

/* OPTION AUDIO PWM slice picker. RP2350A reserves slice 11 for audio
 * (which is rp2350-class only — rp2040's rp2350a is fixed-true but
 * PWM_SLICE_COUNT is 8, so the runtime guard skips). Everyone else
 * uses checkslice(pin) to find a free slice. */
int MIPS16 port_audio_default_pwm_slice(int pin) {
#ifdef rp2350
    if (rp2350a) return 11;
#endif
    return checkslice(pin, pin, 1);
}

/* DEVICE$ chip-variant suffix. rp2350-class ports append " RP2350A"
 * or " RP2350B" depending on rp2350a; rp2040 ports append nothing. */
void MIPS16 port_chip_variant_suffix(char * sret) {
#ifdef rp2350
    strcat(sret, rp2350a ? " RP2350A" : " RP2350B");
#else
    (void)sret;
#endif
}

/* MM.INFO(BOOT) reset-reason decoder for the chip-family-specific
 * watchdog/reset bits. rp2040 uses exact-value matches; rp2350 uses
 * bit-mask matches against PowerMan reset registers. */
const char * MIPS16 port_boot_reason_label(uint32_t restart_reason) {
#ifdef rp2350
    if (restart_reason & 0x30000)
        return "Power On";
    else if (restart_reason & 0x40000)
        return "Reset Switch";
    else if (restart_reason & 0x280000)
        return "Debug";
#else
    if (restart_reason == 0x100)
        return "Power On";
    else if (restart_reason == 0x10000)
        return "Reset Switch";
    else if (restart_reason == 0x100000)
        return "Debug";
#endif
    return NULL;
}

/* MM.INFO INTERRUPTS — read NVIC ISER. Cortex-M0+ (RP2040) has the
 * register at PPB+M0PLUS_NVIC_ISER_OFFSET; Cortex-M33 (RP2350) uses
 * different offsets (and the SDK header doesn't define the M0+
 * symbol on rp2350), so we just don't expose this on rp2350. */
int MIPS16 port_mminfo_interrupts(int64_t * out_iret) {
#ifdef rp2350
    (void)out_iret;
    return 0;
#else
    *out_iret = (int64_t)(uint32_t)*((io_rw_32 *)(PPB_BASE + M0PLUS_NVIC_ISER_OFFSET));
    return 1;
#endif
}

/* MM.INFO TOUCH — VGA Option struct lacks TOUCH_XZERO/TOUCH_CS so
 * the field is unavailable. Other ports return the calibration state. */
int MIPS16 port_mminfo_touch_status(unsigned char * out_sret) {
    return port_setter_touch_status(out_sret);
}

/* MM.INFO SCROLL / MM.INFO SCREENBUFF — PicoCalc framebuffer accessors
 * (PICOMITE + rp2350 only; ScreenBuffer macro = FRAMEBUFFER on rp2350).
 * ScrollStart's volatile qualifier comes from SSD1963.h, included via
 * Hardware_Includes.h. */
int MIPS16 port_mminfo_scroll_start(int64_t * out_iret) {
    return port_setter_scroll_start(out_iret);
}
int MIPS16 port_mminfo_screenbuff(int64_t * out_iret) {
    return port_setter_screenbuff(out_iret);
}

/* POKE DISPLAY <args> raw command/data byte sequence. Dispatches by
 * panel class (SSD1963 parallel / SPI-LCD / I2C) — none of those
 * drivers exist on PICOMITEVGA. */
int MIPS16 port_poke_display_panel(unsigned char * p) {
    return port_setter_poke_display(p);
}

/* PIO instance lookup for the interrupt poll loop. RP2040 has 2 PIOs
 * with the legacy index-0=pio1 ordering; RP2350 has 3 in natural
 * order. PIOMAX (= HAL_PORT_PIO_COUNT) bounds the caller's loop. */
PIO port_pio_for_index(int pio_idx) {
#ifdef rp2350
    return (pio_idx == 0 ? pio0 : (pio_idx == 1 ? pio1 : pio2));
#else
    return (pio_idx == 0 ? pio1 : pio0);
#endif
}

#endif /* !MMBASIC_HOST */
