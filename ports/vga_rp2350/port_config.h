/*
 * ports/vga_rp2350/port_config.h — COMPILE=VGARP2350 and VGAUSBRP2350.
 *
 * Port-scoped compile-time constants. See ports/pico/port_config.h for
 * the mechanism; values below are plain #defines with no #ifdef gates.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: RP2350. */
#define HAL_PORT_PWM_SLICE_COUNT         12
#define HAL_PORT_GPIO_COUNT              48
#define HAL_PORT_PIO_COUNT               3
#define HAL_PORT_HAS_PIO2                1
#define HAL_PORT_HAS_FAST_TIMER          1
#define HAL_PORT_HAS_INT5                1
#define HAL_PORT_PULLDOWN_NEEDS_RESET    1

#define HAL_PORT_HAS_PSRAM               1
#define HAL_PORT_HAS_UPNG                1
#define HAL_PORT_HAS_DEFINES             1
#define HAL_PORT_HAS_HEARTBEAT           1
#define HAL_PORT_ADC_CHANNEL_MAX         4
#define HAL_PORT_HAS_SSD1963             0

/* RP2350 has the RAM budget to place GPIO hot loops in SRAM even on VGA. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

#define HAL_PORT_HAS_HDMI                0

/* Stage-A palette flags (decascade plan). PICOMITEVGA on rp2350 is built
 * without GUICONTROLS today; the touchscreen widget family lives on
 * PICOMITE/HDMI/WEBRP2350 only. */
#define HAL_PORT_HAS_WIFI                0
#define HAL_PORT_HAS_VGA_PIO             1
#define HAL_PORT_HAS_GUICONTROLS         0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* core1stack[] size in words. VGA runs the QVGA scanout PIO loop on core1
 * (vga_qvga_modes::QVgaCore, 512-byte stack). */
#define HAL_PORT_CORE1_STACK_WORDS       128

/* FLAC decoder base sample-rate cap (RP2350 → 48 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  48000
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   8192
#define HAL_PORT_HAS_MP3                 1

#define HAL_PORT_RAM_FUNC(name)          __not_in_flash_func(name)

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar) and the big DefinedSubFun dispatch. rp2350 VGA has plenty
 * of RAM; put both in SRAM. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    __not_in_flash_func(name)
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) __not_in_flash_func(name)

/* rp2350 VGA framebuffer trailer: 320*240*2 = 153600 bytes. */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES (320*240*2)
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
#define HAL_PORT_CONSOLE_FONT_MEDIUM arial_bold

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
