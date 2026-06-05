/*
 * ports/picocalc_rp2040/port_config.h -- ClockworkPi PicoCalc RP2040
 * port-scoped compile-time constants.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* Sentinel for hal/hal_port_assert.h: every TU that uses HAL_PORT_HAS_* in
 * a #if directive can include hal_port_assert.h to turn a missing
 * port_config.h into a build error instead of a silent eval-to-zero. */
#define HAL_PORT_CONFIG_INCLUDED 1

/* Chip-level: RP2040. */
#define HAL_PORT_PWM_SLICE_COUNT 8
#define HAL_PORT_GPIO_COUNT 30
#define HAL_PORT_PIO_COUNT 2
#define HAL_PORT_PULLDOWN_NEEDS_RESET 0
/* Audio I²S PIO instance — rp2040 ports use PIO 0 (shares with QVGA
 * scanout state machines) since only two PIOs exist. */
#define HAL_PORT_AUDIO_I2S_PIO_NUM 0
/* Boot-default sysclk in kHz when an invalid Option.CPU_Speed is
 * detected on a watchdog reset. PicoMite/Web rp2040 default is
 * 200 MHz. */
#define HAL_PORT_DEFAULT_CPU_SPEED_KHZ 200000

#define HAL_PORT_HAS_I2C_KEYPAD 1
#define HAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C 1
#define HAL_PORT_I2C_TIMEOUT_MS 500
#define HAL_PORT_I2C_SLOW_HZ 10000

/* MMInkey placement: PicoMite SPI-LCD has the headroom to pin
 * MMInkey to RAM (no scanout shadow framebuffer eating SRAM). */
#define MMINKEY_DECL(name) __not_in_flash_func(name)

/* Chip-feature: RP2040 variants don't ship PSRAM, upng, or the DEFINES
 * compile-time dictionary. */

/* Board-level features: heartbeat LED selection lives in the
 * drivers/heartbeat/ driver pair (port_sources.cmake links _real on
 * GPIO-LED boards, _stub on CYW43 boards). */

/* ADC channel count exposed to ADC OPEN. WEB claims GP29 for the CYW43
 * radio pin, so max 3; every other board has 4. */
#define HAL_PORT_ADC_CHANNEL_MAX 4

/* SSD1963 backlight helper is compiled for SPI-LCD variants;
 * VGA and HDMI boards don't link SSD1963.c, so the call has to be
 * compile-time dead on those targets. Used as a value in `if`. */

/* Hot-path placement: SPI-LCD RP2040 has spare RAM for the GPIO hot loops
 * so we force them into SRAM via __not_in_flash_func. */

/* FLAC decoder: base max sample rate, scaled by CPU_Speed/126MHz. RP2040
 * caps at 44.1 kHz; RP2350 can drive 48 kHz at stock CPU speed. Used as
 * a value in a runtime check in Audio.c flaccallback. */
#define HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ 44100

/* MOD file buffer size. RP2040 is RAM-tight, so the MOD ring uses 3/4 of
 * WAV_BUFFER_SIZE (8192 * 3/4 = 6144); RP2350 gets the full 8192. */
#define HAL_PORT_AUDIO_MOD_BUFFER_SIZE 6144

/* HAL_PORT_HAS_MP3: software MP3 decode via dr_mp3. Used as a value in
 * runtime branches in Audio.c. */
#define HAL_PORT_HAS_MP3 1
/* cmd_files flist[] cap. Device has the RAM and the SaveContext+InitHeap
 * dance to allocate ~76 KB. Host caps lower in host/port_config.h. */
#define HAL_PORT_FILES_MAX 1000

/* Non-VGA port — used as a value in `if (HAL_PORT_IS_VGA)` runtime branches. */
#define HAL_PORT_IS_VGA 0

#define HAL_PORT_HAS_HDMI 0

/* Stage-A palette flags (decascade plan). Drive what core/CMake gates today
 * with -DPICOMITEWEB, -DGUICONTROLS, and the drivers/vga_pio source-list.
 *   HAS_WIFI         — CYW43 + lwIP/mongoose stack, MMtcpserver.c, WEB cmds.
 *   HAS_VGA_PIO      — drivers/vga_pio scanout family (used by VGA + HDMI).
 *   HAS_GUICONTROLS  — GUI.c widget family + Touch.c + Option.MaxCtrls. */
#define HAL_PORT_HAS_WIFI 0
#define HAL_PORT_HAS_GUICONTROLS 0
#define HAL_PORT_HAS_NEXTGEN_DISPLAY 0

/* core1stack[] size in words. PICOMITE runs the SPI-LCD merge pipeline on
 * core1 (drivers/display_merge/display_merge_pico.c, ~2 KB stack). */
#define HAL_PORT_CORE1_STACK_WORDS 512

/* Stage-D per-port memory + clock + MMBasic-table values (decascade plan
 * D1). Replaces the configuration.h #ifdef PICOMITE cascade. */
#define HAL_PORT_HEAP_MEMORY_SIZE (128 * 1024)
#define HAL_PORT_MAX_CPU 420000
#define HAL_PORT_MIN_CPU 48000
#define HAL_PORT_MAX_VARS 512
#define HAL_PORT_MAX_SUBFUN 256
/* PicoCalc board support pushes this image past the stock PicoMite 1 MB
 * option/program-flash boundary. Keep the writable flash region above the
 * firmware so first-boot option reset cannot erase the running image. */
#define HAL_PORT_FLASH_TARGET_OFFSET (1152 * 1024)
#define HAL_PORT_FLASH_TARGET_OFFSET_USB (1152 * 1024)
#define HAL_PORT_MAGIC_KEY 0xE1799B93
#define HAL_PORT_MAGIC_KEY_USB 0x6210519E
#define HAL_PORT_HEAP_TOP 0x2003EC00
#define HAL_PORT_HEAP_TOP_USB 0x2003F000
#define HAL_PORT_CONSOLE_RX_BUF_SIZE 256
#define HAL_PORT_PIOMAX 2
#define HAL_PORT_NBR_PINS 44
/* Per-PIO claim flags — drives Custom.c's PIO0/PIO1/PIO2. PICOMITE
 * rp2040 claims PIO0+PIO1; PIO2 doesn't exist on rp2040. */
#define HAL_PORT_PIO0_CLAIMED true
#define HAL_PORT_PIO1_CLAIMED true
#define HAL_PORT_PIO2_CLAIMED false

#define PORT_RAM_FUNC(name) __not_in_flash_func(name)

/* Placement for MMBasic's per-expression hot functions (getvalue,
 * findvar — called hundreds of times per BASIC statement). Keep in
 * RAM on every port except the most RAM-constrained (rp2040 WEB). */
#define MMB_HOT_FUNC(name) __not_in_flash_func(name)

/* Placement for the DefinedSubFun dispatch (called once per user
 * SUB/FUNCTION invocation, and it's ~800 lines of code). Flash on
 * rp2040 WEB AND rp2040 VGA — both are tight enough that the extra
 * page of RAM for this one function matters. */
#define MMB_DISPATCH_FUNC(name) __not_in_flash_func(name)

/* Memory-layout port knobs:
 *   TRAILER_BYTES = extra bytes appended to AllMemory[] after the
 *                   heap + safety pad. VGA ports pack the scanout
 *                   framebuffer there; non-VGA ports have 0 and keep
 *                   AllMemory lean.
 *   ALLMEMORY_ALIGN = alignment boundary for AllMemory[]. 4096 on
 *                     rp2040 VGA so the heap fronts a flash-page
 *                     boundary for USB MSC; 256 everywhere else. */
#define HAL_PORT_FRAMEBUFFER_TRAILER_BYTES 0
#define HAL_PORT_ALLMEMORY_ALIGN 256

/* SPI-LCD clock-pin field: rp2040 PICOMITE shares SYSTEM_CLK for the
 * LCD; rp2350 PICOMITE breaks it out as Option.LCD_CLK. Ports without
 * an SPI LCD at all (VGA/HDMI/WEB) never read it at runtime but need
 * the macro defined so Draw.c compiles. */
#define HAL_PORT_LCD_SPI_CLK_PIN Option.SYSTEM_CLK

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
#define BC_CRASH_INFO_ATTR __attribute__((section(".uninitialized_data.bc_crash_info")))

/* Compiler-table sizes. */
#include "../bc_tables_rp2040.h"

#endif /* PORT_CONFIG_H */
