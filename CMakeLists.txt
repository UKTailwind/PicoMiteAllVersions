cmake_minimum_required(VERSION 3.13)
# Valid builds are PICO, PICOUSB, VGA, VGAUSB, HDMI(rp2350 only), HDMIUSB(rp2350 only), WEB

if(NOT DEFINED PICOCALC)
  set(PICOCALC true)
endif()
# For dynamic loading from SD card:
# SDBOOT requires the bootloader to be in the first 256KB of flash
# PicoMite will need to be shifted by this amount, and all references to
# positions in flash updated to accomodate. THIS OPTION IS NOT READY YET.
# details: https://github.com/adwuard/Picocalc_SD_Boot
#set(SDBOOT true)

# Compile for PICO 1 Board — overridden by -DCOMPILE=... on the cmake command line.
if(NOT DEFINED COMPILE)
  set(COMPILE PICO)
endif()

# Compile for PICO 2 Board (uncomment one to override the default above)
#set(COMPILE PICORP2350)
#set(COMPILE WEBRP2350)

if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB"  OR COMPILE STREQUAL "VGARP2350"  OR COMPILE STREQUAL "VGAUSBRP2350"  OR COMPILE STREQUAL "PICORP2350"  OR COMPILE STREQUAL "PICOUSBRP2350" )
	set(PICO_PLATFORM rp2350)
	if (COMPILE STREQUAL "WEBRP2350")
		set(PICO_BOARD pico2_w)
	else()
		set(PICO_BOARD pimoroni_pga2350)
	endif()
else()
	set(PICO_PLATFORM rp2040)
	if (COMPILE STREQUAL "WEB")
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
	ports/pico_sdk_common/hal_keyboard_pico.c
	ports/pico_sdk_common/hal_fast_timer_pico.c
	ports/pico_sdk_common/psram_cache.c
	ports/pico_sdk_common/cmd_psram.c
	ports/pico_sdk_common/defines_loader.c
	ports/pico_sdk_common/mem_writeblock.c
	ports/pico_sdk_common/load_png.c
	ports/pico_sdk_common/cmd_files_hooks.c
	ports/pico_sdk_common/port_load_overrides.c
	ports/pico_sdk_common/picocalc_features.c
	ports/pico_sdk_common/print_display_options.c
	ports/pico_sdk_common/spi_lcd_options.c
	ports/pico_sdk_common/misc_option_setters.c
	ports/pico_sdk_common/vm_sys_time_pico.c
	ports/pico_sdk_common/bc_crash_pico.c
	ports/pico_sdk_common/bc_runtime_pico.c
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
# ports/<board>/port_config.h defines compile-time port-scoped constants
# (HAL_PORT_PWM_SLICE_COUNT, HAL_PORT_GPIO_COUNT, HAL_PORT_HAS_*, …). Each
# port directory holds a single file with plain #defines and no #ifdef
# gates inside; the build system picks exactly one per COMPILE variant by
# putting that directory on the include path. Core uses these as *values*
# in expressions and array sizes; using them as preprocessor gates is
# forbidden (see docs/real-hal-fixup-plan.md F2).
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico)
	target_sources(PicoMite PRIVATE
		ports/pico/pin_tables.c
		ports/pico/port_defaults.c
	)
elseif (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/vga)
	target_sources(PicoMite PRIVATE
		ports/vga/pin_tables.c
		ports/vga/port_defaults.c
	)
elseif (COMPILE STREQUAL "WEB")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/web)
	target_sources(PicoMite PRIVATE
		ports/web/pin_tables.c
		ports/web/port_defaults.c
	)
elseif (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico_rp2350)
	target_sources(PicoMite PRIVATE
		ports/pico_rp2350/pin_tables.c
		ports/pico_rp2350/picocalc_keypad.c
		ports/pico_rp2350/port_defaults.c
	)
elseif (COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/vga_rp2350)
	target_sources(PicoMite PRIVATE
		ports/vga_rp2350/pin_tables.c
		ports/vga_rp2350/port_defaults.c
	)
elseif (COMPILE STREQUAL "WEBRP2350")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/web_rp2350)
	target_sources(PicoMite PRIVATE
		ports/web_rp2350/pin_tables.c
		ports/web_rp2350/port_defaults.c
	)
elseif (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/hdmi_rp2350)
	target_sources(PicoMite PRIVATE
		ports/hdmi_rp2350/pin_tables.c
		ports/hdmi_rp2350/port_defaults.c
	)
endif()
if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB"  OR COMPILE STREQUAL "VGARP2350"  OR COMPILE STREQUAL "VGAUSBRP2350"  OR COMPILE STREQUAL "PICORP2350"  OR COMPILE STREQUAL "PICOUSBRP2350" )
    target_sources(PicoMite PRIVATE psram.c)
endif()

# MP3 decoder (dr_mp3 amalgamated impl) is ~100 KB of code — only RP2350
# targets have the flash + RAM budget. RP2040 variants link no-op stubs so
# Audio.c can include dr_mp3.h unconditionally for types/decls without
# blowing the binary size on the small chip.
if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB"  OR COMPILE STREQUAL "VGARP2350"  OR COMPILE STREQUAL "VGAUSBRP2350"  OR COMPILE STREQUAL "PICORP2350"  OR COMPILE STREQUAL "PICOUSBRP2350" )
    target_sources(PicoMite PRIVATE drivers/audio_mp3/audio_mp3_real.c)
else()
    target_sources(PicoMite PRIVATE drivers/audio_mp3/audio_mp3_stub.c)
endif()

# QSPI PSRAM heap: rp2350 non-WEB variants (PicoMite + VGA + HDMI at
# rp2350) can optionally attach a PSRAM module via Option.PSRAM_CS_PIN.
# Memory.c's GetMemory / TryGetMemory / FreeMemory / MemSize / ... call
# into the psram_heap driver; callers runtime-guard with `if(PSRAMsize)`
# or an address-range check so the stub is safe. rp2040 variants and
# WEBRP2350 (where the QSPI pins are consumed by the CYW43 radio) link
# the no-op stub.
if (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    target_sources(PicoMite PRIVATE drivers/psram_heap/psram_heap_pico.c)
else()
    target_sources(PicoMite PRIVATE drivers/psram_heap/psram_heap_stub.c)
endif()

# Merge pipeline: only PICOMITE (SPI-LCD) variants run the core1 merge
# loop. VGA/HDMI/WEB composite via other means, so they link a no-op
# stub that lets Draw.c / cmd_framebuffer call the HAL hooks
# unconditionally.
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE drivers/display_merge/display_merge_pico.c)
else()
    target_sources(PicoMite PRIVATE drivers/display_merge/display_merge_stub.c)
endif()

# VM FRAMEBUFFER command surface (hal/hal_vm_framebuffer.h): SPI-LCD
# PICOMITE variants get the real impl that drives the merge pipeline;
# VGA/HDMI/WEB get the error stub because FRAMEBUFFER commands don't
# map onto scanout-framebuffer hardware.
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c)
else()
    target_sources(PicoMite PRIVATE drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c)
endif()

# NEXTGEN (MEM332) SPI-LCD display types are rp2350 PICOMITE only. Real
# implementations live in drivers/spi_lcd/spi_lcd.c under
# `#if PICOMITE && rp2350` gates; every other target links a no-op stub
# so Draw.c's restorepanel() can assign the function pointers without
# a target-macro ifdef.
if (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    # real impls already compiled inside drivers/spi_lcd/spi_lcd.c
else()
    target_sources(PicoMite PRIVATE drivers/spi_lcd/spi_lcd_nextgen_stub.c)
endif()

# FASTGFX double-buffered scanline-diff DMA swap path: PICOMITE (SPI-LCD)
# only. VGA/HDMI/WEB get cmd_fastgfx / bc_fastgfx_* stubs that error.
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE drivers/spi_lcd/spi_lcd_fastgfx.c)
else()
    target_sources(PicoMite PRIVATE drivers/spi_lcd/spi_lcd_fastgfx_stub.c)
endif()

# SPI-LCD FRAMEBUFFER subsystem: PICOMITE + WEB variants. VGA/HDMI
# supply their own cmd_framebuffer / restorepanel / etc. in
# drivers/vga_pio/. Host has its own simulator impl.
if (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    # VGA/HDMI: drivers/vga_pio/vga_mode_ops.c provides closeframebuffer etc.
else()
    target_sources(PicoMite PRIVATE drivers/spi_lcd/spi_lcd_framebuffer.c)
endif()

# VGA framebuffer CLS/layer helpers: PICOMITEVGA (VGA + HDMI) gets the
# real impl, everyone else gets a no-op stub.
if (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    target_sources(PicoMite PRIVATE
        drivers/vga_pio/vga_ops.c
        drivers/vga_pio/vga_mode_ops.c
        drivers/vga_pio/vga_blit_ops.c
        drivers/vga_pio/vga_memory.c
    )
    # VGA-without-HDMI gets QVGA-specific cmd_map/cmd_tile; HDMI gets
    # the HDMI-specific mode-ops (DrawRectangle555/256 family and
    # HDMI-flavoured cmd_map/cmd_tile).
    if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
        target_sources(PicoMite PRIVATE
            drivers/hdmi/hdmi_modes.c
            drivers/hdmi/hdmi_scanout.c
        )
    else()
        target_sources(PicoMite PRIVATE drivers/vga_pio/vga_qvga_modes.c)
    endif()
else()
    target_sources(PicoMite PRIVATE drivers/vga_pio/vga_ops_stub.c)
endif()

# 3D polygon / DRAW3D subsystem: linked on every target except
# PICOMITEWEB (WEB links MMtcpserver.c's closeall3d stub).
if (COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
    # closeall3d stub already in MMtcpserver.c; no-op body
else()
    target_sources(PicoMite PRIVATE drivers/gfx_3d/gfx_3d.c)
endif()

# upng sprite LOADPNG decoder: rp2350 variants get the real impl
# (upng.c already linked there per line 299 of this CMakeLists); every
# other target gets a stub that errors at runtime.
if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE drivers/upng_sprite/upng_sprite.c)
else()
    target_sources(PicoMite PRIVATE drivers/upng_sprite/upng_sprite_stub.c)
endif()

# GUI touch-panel sub-commands (BEEP / RESET LCDPANEL / CALIBRATE /
# TEST TOUCH): SPI-LCD + touch-panel variants (PICOMITE / WEB) get
# the real impl; VGA/HDMI link no-op stubs so cmd_guiMX170 reports
# "Unknown command" for touch-panel commands on those boards.
if (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    target_sources(PicoMite PRIVATE drivers/gui_touch/gui_touch_stub.c)
else()
    target_sources(PicoMite PRIVATE drivers/gui_touch/gui_touch.c)
endif()
if (COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350" )
    target_sources(PicoMite PRIVATE
	SSD1963.c
	Touch.c
	cJSON.c
    mqtt.c
    MMMqtt.c
    MMTCPclient.c
    MMtelnet.c
    MMntp.c
    MMtcpserver.c
    tftp.c
    MMtftp.c
    MMudp.c
    MMsetwifi.c
	)
	set_source_files_properties(cJSON.c PROPERTIES COMPILE_FLAGS -Os)
	target_include_directories(PicoMite PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
	)
	Pico_enable_stdio_usb(PicoMite 1)
else()
    # Non-WEB device builds get the no-op stubs so MM_Misc.c can call
    # WEB-stack symbols (closeMQTT, ProcessWeb, port_web_*) unconditionally.
    target_sources(PicoMite PRIVATE MMweb_stubs.c)
endif()

if ((COMPILE STREQUAL "PICO") OR (COMPILE STREQUAL "PICOUSB") OR (COMPILE STREQUAL "PICORP2350") OR (COMPILE STREQUAL "PICOUSBRP2350") OR (COMPILE STREQUAL "WEBRP2350"))
    target_sources(PicoMite PRIVATE
	SSD1963.c
	Touch.c)
endif()
# GUI.c — widget command family, only compiled where GUICONTROLS is enabled.
if ((COMPILE STREQUAL "PICORP2350") OR (COMPILE STREQUAL "PICOUSBRP2350") OR (COMPILE STREQUAL "WEBRP2350"))
    target_sources(PicoMite PRIVATE GUI.c)
endif()

if (COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "HDMIUSB")
    target_sources(PicoMite PRIVATE
	drivers/usb_host_kbd/USBKeyboard.c
	)
else()
	target_sources(PicoMite PRIVATE
	drivers/ps2_matrix/Keyboard.c
	mouse.c
	)
endif()

if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
	target_sources(PicoMite PRIVATE 
	upng.c
	)
endif()

if(PICOCALC STREQUAL "true")
	target_sources(PicoMite PRIVATE
	drivers/i2c_picocalc_kbd/i2ckbd.c
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
if (COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "WEB")
	pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
	target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
	pico_set_boot_stage2(PicoMite slower_boot2)
endif()
if (COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "VGARP2350")
	pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteVGA.pio)
endif()


if (COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
	Pico_enable_stdio_usb(PicoMite 0)
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/usb_host_files
		)
else()
	Pico_enable_stdio_usb(PicoMite 1)
endif()

pico_enable_stdio_uart(PicoMite 0)
pico_add_extra_outputs(PicoMite)
pico_set_printf_implementation(PicoMite compiler)
if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
	pico_set_float_implementation(PicoMite pico_dcp)
endif()
target_compile_options(PicoMite PRIVATE -DNDEBUG
										-DPICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
										-DPICO_ADC_CLKDIV_ROUND_NEAREST
										-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 
										-DPICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK
										-DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 
										-DPICO_FLASH_SIZE_BYTES=16777216
										-DPICO_CORE1_STACK_SIZE=0x00
										-DPICO_MALLOC_PANIC
										-O2
										-Wall)
# all RP2350 variants
if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
target_compile_options(PicoMite PRIVATE -Drp2350
										-DPICO_FLASH_SPI_CLKDIV=4
										-DPICO_PIO_USE_GPIO_BASE
										)
endif()
# all PicoMite variants - base defines
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
target_compile_options(PicoMite PRIVATE -DPICOMITE
										-DPICO_HEAP_SIZE=0x800
										-DPICO_CORE0_STACK_SIZE=0x1000
										)
endif()
# GUI widget command family — rp2350 PicoMite variants + WEBRP2350. Costs
# ~10 KB of static .bss (CTRLS[200] @ ~52 bytes each); rp2040 PicoCalc has
# no touchscreen and needs that RAM for the MMHeap to compile larger
# programs. WEBRP2350 needs GUICONTROLS because it compiles GUI.c, which
# references Option.MaxCtrls and MAX_CAPTION_LINES unconditionally —
# matches upstream's "#special case WEB RP2350 gets the GUI controls" block.
if (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "WEBRP2350")
target_compile_options(PicoMite PRIVATE -DGUICONTROLS)
endif()
# all VGA variants 
if (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
target_compile_options(PicoMite PRIVATE -DPICOMITEVGA
										-DPICO_HEAP_SIZE=0x1000 
										-DPICO_CORE0_STACK_SIZE=0x2000
										)
endif()
# HDMI variants
if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
target_compile_options(PicoMite PRIVATE -DPICOMITEVGA
										-DHDMI
										-DPICO_HEAP_SIZE=0x2000 
										-DPICO_CORE0_STACK_SIZE=0x2000
										)
endif()
#Web variants need more heap
if(COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
	target_compile_options(PicoMite PRIVATE -DPICOMITEWEB 
											-DPICO_HEAP_SIZE=0x3000 
											-DCYW43_HOST_NAME="WebMite" 
											-DPICO_CYW43_ARCH_POLL
											-DPICO_CORE0_STACK_SIZE=0x4000
											)
endif()
#USB variants
if (COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMIUSB")
target_compile_options(PicoMite PRIVATE -DUSBKEYBOARD
										)
endif()
#special case WEB RP2350 gets the GUI controls
if(COMPILE STREQUAL "WEBRP2350")
	target_compile_options(PicoMite PRIVATE -DGUICONTROLS
											)
endif()
# Per-variant device-name string used by fun_device. Lets MM_Misc.c
# replace its 13-line nested-ifdef block with a single strcpy.
if (COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICORP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMite")
elseif (COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMiteUSB")
elseif (COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGARP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMiteVGA")
elseif (COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMiteVGAUSB")
elseif (COMPILE STREQUAL "HDMI")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMiteHDMI")
elseif (COMPILE STREQUAL "HDMIUSB")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="PicoMiteHDMIUSB")
elseif (COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_DEVICE_NAME="WebMite")
endif()

#set the PICOCALC flag + its port-constant companion
if(PICOCALC STREQUAL "true")
	target_compile_options(PicoMite PRIVATE -DPICOCALC
	                                        -DHAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C=1
											)
else()
	target_compile_options(PicoMite PRIVATE -DHAL_PORT_BACKLIGHT_VIA_KEYPAD_I2C=0)
endif()


target_link_libraries(PicoMite
	pico_stdlib
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

if(COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
	target_link_libraries(PicoMite
	tinyusb_host 
	tinyusb_board
	pico_multicore
	)
endif()

if(COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "PICORP2350")
	target_link_libraries(PicoMite
	pico_multicore
	)
endif()

# pico_rand: available on both rp2040 and rp2350 (lives in rp2_common).
# Linked on every device target so Functions.c::fun_rnd can call
# get_rand_32() unconditionally.
target_link_libraries(PicoMite pico_rand)

if(COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350" )
	target_link_libraries(PicoMite
	pico_cyw43_arch_lwip_poll
	)
endif()

if(SDBOOT STREQUAL "true" AND (COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "WEBRP2350"))
	pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/memmap_default_rp2350.ld)
endif()

if(SDBOOT STREQUAL "true" AND (COMPILE STREQUAL "PICO"))
	pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/memmap_default_rp2040.ld)
endif()
