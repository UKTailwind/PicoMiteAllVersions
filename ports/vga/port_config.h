/*
 * ports/vga/port_config.h — COMPILE=VGA and COMPILE=VGAUSB (rp2040 VGA).
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

/* Chip-level: RP2040. */
#define HAL_PORT_PWM_SLICE_COUNT         8
#define HAL_PORT_GPIO_COUNT              30
#define HAL_PORT_PIO_COUNT               2
#define HAL_PORT_PULLDOWN_NEEDS_RESET    0
/* Audio I²S shares PIO 0 with the QVGA scanout (rp2040 has only
 * 2 PIOs). */
#define HAL_PORT_AUDIO_I2S_PIO_NUM       0
/* Pure VGA QVGA scanout requires 252 MHz (Freq252P). */
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ   252000

/* MMInkey placement: rp2040 VGA has tight RAM (the QVGA scanout +
 * shadow framebuffer eat most of it), so MMInkey stays in flash. */
#define HAL_PORT_MMINKEY_DECL(name)      name

#define HAL_PORT_ADC_CHANNEL_MAX         4
/* VGA boards don't ship SSD1963 support. */

/* VGA on rp2040 runs scanout from flash via XIP — forcing GPIO hot loops
 * into SRAM would starve the scanout buffer and cause tearing. Leave them
 * in flash; scanout bandwidth takes priority over pin-write latency. */
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX               1000

/* VGA-family build (PICOMITEVGA defined). VGA+HDMI ports share core paths
 * that branch on this flag at runtime — gated as preprocessor #ifdef in
 * the original source. */
#define HAL_PORT_IS_VGA                  1

#define HAL_PORT_HAS_HDMI                0

/* Stage-A palette flags (decascade plan). VGA-PIO scanout drivers are
 * shared with HDMI; this port uses them but does not link the HDMI sink. */
#define HAL_PORT_HAS_WIFI                0
#define HAL_PORT_HAS_GUICONTROLS         0

/* core1stack[] size in words. VGA runs the QVGA scanout PIO loop on core1
 * (vga_qvga_modes::QVgaCore, 512-byte stack). */
#define HAL_PORT_CORE1_STACK_WORDS       128
#define HAL_PORT_HAS_NEXTGEN_DISPLAY    0

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). VGA rp2040 differs from VGAUSB rp2040 in FLASH_TARGET_OFFSET (the
 * USB build's flash image is smaller). */
#define HAL_PORT_HEAP_MEMORY_SIZE        (99 * 1024)
#define HAL_PORT_MAX_CPU                 378000
#define HAL_PORT_MIN_CPU                 252000
#define HAL_PORT_MAX_VARS                480
#define HAL_PORT_MAX_SUBFUN              256
#define HAL_PORT_MAX_MODES               2
#define HAL_PORT_FLASH_TARGET_OFFSET     (864 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (848 * 1024)
#define HAL_PORT_MAGIC_KEY               0xA3349A2F
#define HAL_PORT_MAGIC_KEY_USB           0x4876A715
#define HAL_PORT_HEAP_TOP                0x2003F000
#define HAL_PORT_HEAP_TOP_USB            0x2003F000
#define HAL_PORT_CONSOLE_RX_BUF_SIZE     256
#define HAL_PORT_PIOMAX                  2
#define HAL_PORT_NBR_PINS                44
/* VGA-PIO scanout claims PIO1 on rp2040; PIO0 free for user. */
#define HAL_PORT_PIO0_CLAIMED            false
#define HAL_PORT_PIO1_CLAIMED            true
#define HAL_PORT_PIO2_CLAIMED            false

/* FLAC decoder base sample-rate cap (RP2040 → 44.1 kHz). */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ  44100
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE   6144
#define HAL_PORT_HAS_MP3                 0

#define HAL_PORT_RAM_FUNC(name)          name

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar — per-token hot path). rp2040 VGA tolerates them in RAM. */
#define HAL_PORT_MMBASIC_HOT_FUNC(name)    __not_in_flash_func(name)

/* DefinedSubFun dispatch is large (~800 lines) and called only once
 * per SUB/FUNCTION invocation — flash on rp2040 VGA so the QVGA
 * framebuffer + mode tables have more SRAM. */
#define HAL_PORT_MMBASIC_SUBFUN_FUNC(name) name

/* QVGA scanout framebuffer trailer (640*480/8 = 38400 bytes) and
 * the 4096-byte alignment needed for the USB MSC view of the heap. */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES (640*480/8)
#define HAL_PORT_ALLMEMORY_ALIGN           4096


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
