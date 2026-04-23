/*
 * ports/pico/port_config.h — COMPILE=PICO and COMPILE=PICOUSB (rp2040).
 *
 * Port-scoped compile-time constants. Every value is a plain #define — no
 * #ifdef gates live in this file. Core may use these constants as *values*
 * in C expressions and array sizes; never as preprocessor gates. See
 * docs/real-hal-fixup-plan.md.
 *
 * The build system selects this file by adding ports/pico/ to the include
 * path for COMPILE=PICO and COMPILE=PICOUSB. Every other port directory
 * has its own port_config.h with the values appropriate to that target.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Chip-level: RP2040. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_HAS_PIO2                0
#define HAL_PORT_HAS_FAST_TIMER          0
#define HAL_PORT_HAS_INT5                0
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0

/* Chip-feature: RP2040 variants don't ship PSRAM, upng, or the DEFINES
 * compile-time dictionary. */
#define HAL_PORT_HAS_PSRAM               0
#define HAL_PORT_HAS_UPNG                0
#define HAL_PORT_HAS_DEFINES             0

/* Board-level features. HAL_PORT_HAS_HEARTBEAT is set on boards that
 * expose a user-configurable heartbeat LED pin. PICOMITEWEB omits it
 * because the CYW43 wireless chip claims the onboard LED. */
#define HAL_PORT_HAS_HEARTBEAT           1

/* ADC channel count exposed to ADC OPEN. WEB claims GP29 for the CYW43
 * radio pin, so max 3; every other board has 4. */
#define HAL_PORT_ADC_CHANNEL_MAX         4

/* SSD1963 backlight helper is compiled for PicoMite + WebMite variants;
 * VGA and HDMI boards don't link SSD1963.c, so the call has to be
 * compile-time dead on those targets. Used as a value in `if`. */
#define HAL_PORT_HAS_SSD1963             1

/* Hot-path placement: SPI-LCD RP2040 has spare RAM for the GPIO hot loops
 * so we force them into SRAM via __not_in_flash_func. */

/* FLAC decoder: base max sample rate, scaled by CPU_Speed/126MHz. RP2040
 * caps at 44.1 kHz; RP2350 can drive 48 kHz at stock CPU speed. Used as
 * a value in a runtime check in Audio.c flaccallback. */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100

/* MOD file buffer size. RP2040 is RAM-tight, so the MOD ring uses 3/4 of
 * WAV_BUFFER_SIZE (8192 * 3/4 = 6144); RP2350 gets the full 8192. */
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144

/* HAL_PORT_HAS_MP3: software MP3 decode via dr_mp3 is RP2350-only; RP2040
 * routes PLAY MP3 through VS1053 hardware (Option.AUDIO_MISO_PIN). Used
 * as a value in runtime branches in Audio.c. */
#define HAL_PORT_HAS_MP3                 0
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA                  0

#define HAL_PORT_HAS_HDMI                0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar — called hundreds of times per BASIC statement). Keep in
 * RAM on every port except the most RAM-constrained (rp2040 WEB). */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)  __not_in_flash_func(name)

/* Placement for the DefinedSubFun dispatch (called once per user
 * SUB/FUNCTION invocation, and it's ~800 lines of code). Flash on
 * rp2040 WEB AND rp2040 VGA — both are tight enough that the extra
 * page of RAM for this one function matters. */
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

/* Memory-layout port knobs:
 *   TRAILER_BYTES = extra bytes appended to AllMemory[] after the
 *                   heap + safety pad. VGA ports pack the scanout
 *                   framebuffer there; non-VGA ports have 0 and keep
 *                   AllMemory lean.
 *   ALLMEMORY_ALIGN = alignment boundary for AllMemory[]. 4096 on
 *                     rp2040 VGA so the heap fronts a flash-page
 *                     boundary for USB MSC; 256 everywhere else. */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN           256


/* SPI-LCD clock-pin field: rp2040 PICOMITE shares SYSTEM_CLK for the
 * LCD; rp2350 PICOMITE breaks it out as Option.LCD_CLK. Ports without
 * an SPI LCD at all (VGA/HDMI/WEB) never read it at runtime but need
 * the macro defined so Draw.c compiles. */
#define HAL_PORT_LCD_SPI_CLK_PIN         Option.SYSTEM_CLK


/* Console medium font (FontTable[2]) — VGA ports at QVGA
 * resolution use the narrower arial_bold so an 80-column console
 * fits; every other port (including HDMI, which runs true VGA or
 * higher) uses the wider Hom_16x24_LE.  Value is a symbol name, the
 * actual const arrays are included by Draw.c. */
#define HAL_PORT_CONSOLE_FONT_MEDIUM Hom_16x24_LE

/* RANDOMIZE default seed when the BASIC program passes 0 (or omits
 * the seed). Device ports use the hardware timer for entropy; host
 * ports have their own policy (see host/port_config.h). */
#define HAL_PORT_RANDOMIZE_DEFAULT_SEED() ((int64_t)hal_time_us_64())

/* BCCrashInfo storage placement. On device it lives in an
 * uninitialized section (.uninitialized_data) that survives soft /
 * watchdog reset so the next-boot bc_crash_dump_if_any can read
 * register values stored before the fault. Host has no such
 * section — plain BSS. */
#define HAL_PORT_BC_CRASH_INFO_ATTR __attribute__((section(".uninitialized_data.bc_crash_info")))

#endif /* PORT_CONFIG_H */
