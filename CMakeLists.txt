cmake_minimum_required(VERSION 3.13)
# Port selector is -DPORT=<port_dir_name>; the legacy -DCOMPILE=<TARGET>
# knob is still recognized for backwards compatibility (buildall.sh +
# build_firmware.sh + the GitHub Actions matrix all still drive
# -DCOMPILE=). When only COMPILE is set, the inference block below picks
# the matching ports/<dir>. New invocations should pass -DPORT=<dir>
# directly.

if(NOT DEFINED PICOCALC)
  set(PICOCALC true)
endif()
# For dynamic loading from SD card:
# SDBOOT requires the bootloader to be in the first 256KB of flash
# PicoMite will need to be shifted by this amount, and all references to
# positions in flash updated to accomodate. THIS OPTION IS NOT READY YET.
# details: https://github.com/adwuard/Picocalc_SD_Boot
#set(SDBOOT true)

# Port selection. PORT names a directory under ports/.
if(NOT DEFINED PORT)
    if(DEFINED COMPILE)
        # Legacy COMPILE → PORT inference. New ports should not extend
        # this list; pass -DPORT=<dir> directly.
        if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB")
            set(PORT pico)
        elseif (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB")
            set(PORT vga)
        elseif (COMPILE STREQUAL "WEB")
            set(PORT web)
        elseif (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
            set(PORT pico_rp2350)
        elseif (COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
            set(PORT vga_rp2350)
        elseif (COMPILE STREQUAL "WEBRP2350")
            set(PORT web_rp2350)
        elseif (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
            set(PORT hdmi_rp2350)
        elseif (COMPILE STREQUAL "VGAWIFIRP2350")
            set(PORT vga_wifi_rp2350)
        elseif (COMPILE STREQUAL "DVIWIFIRP2350")
            set(PORT dvi_wifi_rp2350)
        else()
            message(FATAL_ERROR
                "Unknown legacy COMPILE=${COMPILE}; pass -DPORT=<dir> instead.")
        endif()
    else()
        # Default port: the original rp2040 PicoMite SPI-LCD board.
        set(PORT pico)
    endif()
endif()

# pico-sdk platform / board selection per PORT. PICO_PLATFORM and
# PICO_BOARD have to be set before pico_sdk_import.cmake / pico_sdk_init,
# so the lookup lives here at top-level CMakeLists rather than in the
# per-port port_sources.cmake (which runs after sdk init).
set(_RP2350_PORTS pico_rp2350 vga_rp2350 web_rp2350 hdmi_rp2350
                  vga_wifi_rp2350 dvi_wifi_rp2350)
if (PORT IN_LIST _RP2350_PORTS)
    set(PICO_PLATFORM rp2350)
    if (PORT STREQUAL "web_rp2350" OR PORT STREQUAL "vga_wifi_rp2350")
        set(PICO_BOARD pico2_w)
    elseif (PORT STREQUAL "dvi_wifi_rp2350")
        # Custom board file lives in the repo at boards/include/boards/.
        # pico-sdk picks it up via PICO_BOARD_HEADER_DIRS (set below).
        list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/include/boards)
        set(PICO_BOARD pico_stretch)
    else()
        set(PICO_BOARD pimoroni_pga2350)
    endif()
else()
    set(PICO_PLATFORM rp2040)
    if (PORT STREQUAL "web")
        set(PICO_BOARD pico_w)
    else()
        set(PICO_BOARD pico)
    endif()
endif()
include(pico_sdk_import.cmake)
project(PicoMite C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
#set(PICO_NO_COPRO_DIS 1)
pico_sdk_init()
set(PICOMITE_BASE_COMMON_SOURCES
	regex.c
	re.c
	ff.c
	ffsystem.c
	ffunicode.c
	drivers/sd_spi/mmc_stm32.c
	picojpeg.c
	lfs.c
	lfs_util.c
	hxcmod.c
	aes.c
)

set(PICOMITE_BASE_SOURCES
	${PICOMITE_BASE_COMMON_SOURCES}
	PicoMite.c
	MMBasic_Prompt.c
	MMBasic_Print.c
	MMBasic_REPL.c
	drivers/vs1053/VS1053.c
	Memory.c
)

set(PICOMITE_VM_DEVICE_SOURCES
	vm_device_runtime.c
	vm_device_main.c
	vm_device_fileio.c
)

set(PICOMITE_LEGACY_CORE_SOURCES
	MMBasic.c
	Operators.c
	Custom.c
	Functions.c
	Commands.c
	FileIO.c
	Draw.c
	RGB121.c
	Tilemap.c
	Editor.c
	XModem.c
	MM_Misc.c
	mm_misc_shared.c
	External.c
	MATHS.c
	Onewire.c
	I2C.c
	SPI.c
	Serial.c
	drivers/spi_lcd/spi_lcd.c
	BmpDecoder.c
	GPS.c
	CFunction.c
	picomite_gpio_irq.c
)

# Audio: device body lives in drivers/pwm_synth/pwm_synth.c (PWM/PIO
# tones, WAV/FLAC/MP3/MOD decoders, VS1053 glue). Audio.c itself holds
# only the host cmd_play and compiles on host builds via host/Makefile.
set(PICOMITE_AUDIO_DEVICE_SOURCES
	drivers/pwm_synth/pwm_synth.c
)

set(PICOMITE_VM_SHARED_GFX_SOURCES
	gfx_box_shared.c
	gfx_circle_shared.c
	gfx_line_shared.c
	gfx_pixel_shared.c
	gfx_cls_shared.c
	gfx_text_shared.c
	gfx_console_shared.c
)

# Cross-cutting state (display, pin, Option, audio globals) shared across
# Draw.c / External.c / FileIO.c / Audio.c. Consolidating storage here lets
# per-driver source files coexist without multiple-definition link errors.
set(PICOMITE_CORE_STATE_SOURCES
	core/state/display_state.c
	core/state/pin_state.c
	core/state/option_state.c
	core/state/audio_state.c
)

# HAL implementations shared across every RP2040/RP2350 device target.
set(PICOMITE_PICO_SDK_HAL_SOURCES
	ports/pico_sdk_common/hal_flash_pico.c
	ports/pico_sdk_common/hal_time_pico.c
	ports/pico_sdk_common/hal_pin_pico.c
	ports/pico_sdk_common/hal_storage_pico.c
	ports/pico_sdk_common/hal_filesystem_pico.c
	# hal_keyboard_pico.c was a multiplexer; replaced with per-backend
	# real impls selected by port_sources.cmake below:
	#   drivers/usb_host_kbd/hal_keyboard_usb.c   (HAL_PORT_KEYBOARD_USB_HOST=1)
	#   drivers/ps2_matrix/hal_keyboard_ps2.c     (HAL_PORT_KEYBOARD_USB_HOST=0)
	ports/pico_sdk_common/hal_fast_timer_pico.c
	ports/pico_sdk_common/psram_cache.c
	ports/pico_sdk_common/cmd_psram.c
	ports/pico_sdk_common/defines_loader.c
	ports/pico_sdk_common/mem_writeblock.c
	ports/pico_sdk_common/load_png.c
	ports/pico_sdk_common/cmd_files_hooks.c
	# port_load_overrides + picocalc_features are added by the
	# PICOCALC selection block below — real on PicoCalc, stub elsewhere.
	ports/pico_sdk_common/print_display_options.c
	ports/pico_sdk_common/spi_lcd_options.c
	ports/pico_sdk_common/misc_option_setters.c
	ports/pico_sdk_common/vm_sys_time_pico.c
	ports/pico_sdk_common/bc_crash_pico.c
	ports/pico_sdk_common/bc_runtime_pico.c
	ports/pico_sdk_common/bc_bridge_pico.c
	ports/pico_sdk_common/clear_runtime_port.c
	ports/pico_sdk_common/funtbl_port.c
	ports/pico_sdk_common/core1_runtime.c
	drivers/pico_flash/pico_flash_lfs.c
)

set(PICOMITE_VM_SOURCES
	bc_alloc.c
	bc_bridge.c
	bc_source.c
	bc_compiler_core.c
	bc_vm.c
	bc_runtime.c
	bc_debug.c
	vm_sys_input.c
	vm_sys_graphics.c
	vm_sys_time.c
	vm_sys_pin.c
	vm_sys_file.c
	${PICOMITE_VM_SHARED_GFX_SOURCES}
)

add_executable(PicoMite
	${PICOMITE_BASE_SOURCES}
	${PICOMITE_VM_SOURCES}
)
target_sources(PicoMite PRIVATE ${PICOMITE_LEGACY_CORE_SOURCES})
target_sources(PicoMite PRIVATE ${PICOMITE_AUDIO_DEVICE_SOURCES})
target_sources(PicoMite PRIVATE ${PICOMITE_CORE_STATE_SOURCES})
target_sources(PicoMite PRIVATE ${PICOMITE_PICO_SDK_HAL_SOURCES})
# The project's headers (MMBasic_Includes.h, Draw.h, etc.) live in the
# repo root. Sources in subdirectories (core/state/, future hal/ + drivers/
# + ports/) need the root on the include path so their #includes resolve.
target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# ports/pico_sdk_common/ supplies port-specific Tier-B inline headers
# (hal_pin_inlines.h) that hal/*.h pulls in via a plain "" include.
target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico_sdk_common)
# drivers/console_cdc/ — shared USB-CDC stdio helper used by every
# non-USB-host-keyboard port (PS/2, CDC-only). The keyboard backends
# include "console_cdc.h" to delegate the actual CDC plumbing.
target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/console_cdc)
# ports/<board>/port_config.h defines compile-time port-scoped constants
# (HAL_PORT_PWM_SLICE_COUNT, HAL_PORT_GPIO_COUNT, HAL_PORT_HAS_*, …). Each
# port directory holds a single file with plain #defines and no #ifdef
# gates inside; the build system picks exactly one per port by putting
# that directory on the include path. Core uses these as *values* in
# expressions and array sizes; using them as preprocessor gates is
# forbidden (see docs/real-hal-fixup-plan.md F2).
# Source-list inclusion is per-port composition. Each port directory
# under ports/<port>/ ships a port_sources.cmake snippet that declares
# the source files for that port's hardware backends (SPI-LCD vs VGA-PIO
# vs HDMI vs WiFi, USB-host vs PS/2 keyboard, rp2040 stubs vs rp2350
# real-feature backends). PORT names the directory directly.
include(${CMAKE_SOURCE_DIR}/ports/${PORT}/port_sources.cmake)

if(PICOCALC STREQUAL "true")
    target_sources(PicoMite PRIVATE
        drivers/i2c_picocalc_kbd/i2ckbd.c
        drivers/i2c_picocalc_kbd/i2c_keypad_real.c
        drivers/i2c_picocalc_kbd/picocalc_keypad_matrix.c
        ports/pico_sdk_common/picocalc_features_real.c
        ports/pico_sdk_common/port_load_overrides_picocalc.c
    )
else()
    target_sources(PicoMite PRIVATE
        drivers/i2c_picocalc_kbd/i2c_keypad_stub.c
        ports/pico_sdk_common/picocalc_features_stub.c
        ports/pico_sdk_common/port_load_overrides_stub.c
    )
endif()

set_source_files_properties(drivers/sd_spi/mmc_stm32.c PROPERTIES COMPILE_FLAGS -O2)
set_source_files_properties(ff.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(GUI.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(BmpDecoder.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(GPS.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(I2C.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(lfs.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(picojpeg.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(regex.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(hxcmod.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(MATHS.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(Editor.c PROPERTIES COMPILE_FLAGS -Os)
set_source_files_properties(aes.c PROPERTIES COMPILE_FLAGS -Os)
pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteI2S.pio)

pico_enable_stdio_uart(PicoMite 0)
pico_add_extra_outputs(PicoMite)
pico_set_printf_implementation(PicoMite compiler)

# Universal compile options applied to every device build. Per-port
# heap/stack sizes, chip flags (-Drp2350), board defines (-DPICOMITE,
# -DPICOMITEVGA, -DCYW43_HOST_NAME), USB-keyboard axis, device-name
# string, library linkage (pico_multicore, tinyusb, cyw43_arch), and
# pico_set_float_implementation all live in ports/<port>/port_sources.cmake.
target_compile_options(PicoMite PRIVATE -DNDEBUG
                                        -DPICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
                                        -DPICO_ADC_CLKDIV_ROUND_NEAREST
                                        -DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
                                        -DPICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK
                                        -DPICO_FLASH_SIZE_BYTES=16777216
                                        -DPICO_CORE1_STACK_SIZE=0x00
                                        -DPICO_MALLOC_PANIC
                                        -O2
                                        -Wall)

# PICOCALC global axis (set via -DPICOCALC=true|false). The CMake
# variable PICOCALC drives:
#   -DHAL_PORT_HAS_I2C_KEYPAD            — gates the I²C keypad-controller
#                                           protocol in core/drivers and
#                                           the keypad init delay in
#                                           PicoMite.c boot.
#   -DHAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C  — switches setBacklight() to
#                                           write the keypad's backlight
#                                           register instead of driving
#                                           a PWM slice.
#   drivers/i2c_picocalc_kbd/i2ckbd.c    — linked above when PICOCALC=true.
# configuration.h still consults the CMake-level PICOCALC name for the
# heap-size override (HAL_PORT_HEAP_MEMORY_SIZE_PICOCALC) — that's a
# legacy port-impl shim, kept for the dual-shipping ports/pico/.
if(PICOCALC STREQUAL "true")
    target_compile_options(PicoMite PRIVATE -DPICOCALC
                                            -DHAL_PORT_HAS_I2C_KEYPAD=1
                                            -DHAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C=1
                                            -DHAL_PORT_I2C_TIMEOUT_MS=500
                                            -DHAL_PORT_I2C_SLOW_HZ=10000
                                            )
else()
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_HAS_I2C_KEYPAD=0
                                            -DHAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C=0
                                            -DHAL_PORT_I2C_TIMEOUT_MS=5
                                            -DHAL_PORT_I2C_SLOW_HZ=100000)
endif()

# Library linkage shared across every device port.
target_link_libraries(PicoMite
    pico_stdlib
    pico_rand
    hardware_flash
    hardware_irq
    hardware_adc
    hardware_pwm
    hardware_i2c
    hardware_spi
    hardware_dma
    hardware_exception
    hardware_pio
)
