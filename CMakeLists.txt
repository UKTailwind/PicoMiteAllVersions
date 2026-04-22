cmake_minimum_required(VERSION 3.13)
# Valid builds are PICO, PICOUSB, VGA, VGAUSB, HDMI(rp2350 only), HDMIUSB(rp2350 only), WEB

set(PICOCALC true)
# For dynamic loading from SD card:
# SDBOOT requires the bootloader to be in the first 256KB of flash
# PicoMite will need to be shifted by this amount, and all references to
# positions in flash updated to accomodate. THIS OPTION IS NOT READY YET.
# details: https://github.com/adwuard/Picocalc_SD_Boot
#set(SDBOOT true)

# Compile for PICO 1 Board
set(COMPILE PICO)

# Compile for PICO 2 Board
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
	SPI-LCD.c
	BmpDecoder.c
	GPS.c
	Audio.c
	CFunction.c
	picomite_gpio_irq.c
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
target_sources(PicoMite PRIVATE ${PICOMITE_CORE_STATE_SOURCES})
target_sources(PicoMite PRIVATE ${PICOMITE_PICO_SDK_HAL_SOURCES})
# The project's headers (MMBasic_Includes.h, Draw.h, etc.) live in the
# repo root. Sources in subdirectories (core/state/, future hal/ + drivers/
# + ports/) need the root on the include path so their #includes resolve.
target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# ports/pico_sdk_common/ supplies port-specific Tier-B inline headers
# (hal_pin_inlines.h) that hal/*.h pulls in via a plain "" include.
target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/pico_sdk_common)
if (COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB"  OR COMPILE STREQUAL "VGARP2350"  OR COMPILE STREQUAL "VGAUSBRP2350"  OR COMPILE STREQUAL "PICORP2350"  OR COMPILE STREQUAL "PICOUSBRP2350" )
    target_sources(PicoMite PRIVATE psram.c)
endif()
if (COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350" )
    target_sources(PicoMite PRIVATE
	SSD1963.c
	Touch.c
	GUI.c
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
	)
	set_source_files_properties(cJSON.c PROPERTIES COMPILE_FLAGS -Os)
	target_include_directories(PicoMite PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
	${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
	)
	Pico_enable_stdio_usb(PicoMite 1)
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
#set the PICOCALC flag
if(PICOCALC STREQUAL "true")
	target_compile_options(PicoMite PRIVATE -DPICOCALC
											)
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

if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
	target_link_libraries(PicoMite
	pico_rand
	)
endif()

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
