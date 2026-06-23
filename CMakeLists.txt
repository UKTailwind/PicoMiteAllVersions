cmake_minimum_required(VERSION 3.13)

# Valid builds are PICO, PICOUSB, PICOMIN, VGA, VGAUSB, WEB,
# PICORP2350, PICOUSBRP2350, VGARP2350, VGAUSBRP2350, WEBRP2350, HDMI, HDMIUSB,
# PICOBTRP2350, PICOBTHRP2350, HDMIBTH, HDMIWEB.
set(VALID_COMPILE_VARIANTS
	PICO
	PICOUSB
	PICOMIN
	VGA
	VGAUSB
	WEB
	PICORP2350
	PICOUSBRP2350
	VGARP2350
	VGAUSBRP2350
	WEBRP2350
	HDMI
	HDMIUSB
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)

# VS Code uses this default when COMPILE is not supplied explicitly.
# Guard restored: without it, this set() shadows the -DCOMPILE=X passed
# by buildpicomite.bat and every variant silently builds as the default.
if(NOT DEFINED COMPILE)
	set(COMPILE HDMIUSB)
endif()

list(FIND VALID_COMPILE_VARIANTS "${COMPILE}" COMPILE_INDEX)
if(COMPILE_INDEX EQUAL -1)
	string(JOIN ", " VALID_COMPILE_VARIANTS_TEXT ${VALID_COMPILE_VARIANTS})
	message(FATAL_ERROR "Unsupported COMPILE value '${COMPILE}'. Valid values: ${VALID_COMPILE_VARIANTS_TEXT}")
endif()

macro(compile_is_in outvar)
	set(_compile_variants ${ARGN})
	list(FIND _compile_variants "${COMPILE}" _compile_index)
	if(_compile_index EQUAL -1)
		set(${outvar} FALSE)
	else()
		set(${outvar} TRUE)
	endif()
endmacro()

set(COMMON_SOURCES
	PicoMite.c
	MMBasic.c
	Operators.c
	Custom.c
	Functions.c
	Commands.c
	FileIO.c
	ff.c
	ffsystem.c
	ffunicode.c
	mmc_stm32.c
	Draw.c
	Draw3D.c
	DrawFill.c
	TileMap.c
	Pointer.c
	Sprite.c
	Blit.c
	FrameBuffer.c
	Editor.c
	XModem.c
	MM_Misc.c
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
	picojpeg.c
	lfs.c
	lfs_util.c
	hxcmod.c
	VS1053.c
	aes.c
	Memory.c
	RGB121.c
	re.c
)
set(CACHE_SOURCES
	MMtrace.c
)
set(DISPLAY_SOURCES
	SSD1963.c
	Touch.c
)

set(WEB_SOURCES
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

set(OPTIMIZED_SOURCES
	ff.c
	GUI.c
	BmpDecoder.c
	Draw.c
	Draw3D.c
	DrawFill.c
	TileMap.c
	Pointer.c
	Sprite.c
	Blit.c
	FrameBuffer.c
	Raycaster.c
	GPS.c
	I2C.c
	lfs.c
	picojpeg.c
	Turtle.c
	re.c
	hxcmod.c
	MATHS.c
	Editor.c
	aes.c
)

set(FULL_RP2040_VARIANTS
	PICO
	PICOUSB
	VGA
	VGAUSB
	WEB
	PICOMIN
)

set(RP2350_VARIANTS
	HDMI
	WEBRP2350
	HDMIUSB
	VGARP2350
	VGAUSBRP2350
	PICORP2350
	PICOUSBRP2350
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)

set(BT_VARIANTS
	PICOBTRP2350
)

# BT-host variants: pull in cyw43 BT firmware, link btstack, and provide
# bt_keyboard_init / bt_keyboard_poll (BTKeyboard.c). HDMIBTH reuses the
# same BT-host stack — pico_cyw43_arch_none alone has no async pump, so
# cyw43_arch_gpio_put hangs after the first call. With btstack in the
# picture the async_context is kept alive by btstack's workers, and
# bt_keyboard_poll() drives both the chip and the heartbeat.
set(BTH_VARIANTS
	PICOBTHRP2350
	HDMIBTH
)

set(WEB_VARIANTS
	WEB
	WEBRP2350
	HDMIWEB
)

set(GUI_VARIANTS
	PICO
	PICOUSB
	PICORP2350
	PICOUSBRP2350
	WEBRP2350
	PICOBTRP2350
	PICOBTHRP2350
)

set(USB_HOST_VARIANTS
	PICOUSB
	PICOUSBRP2350
	VGAUSB
	VGAUSBRP2350
	HDMIUSB
	HDMIBTH
	HDMIWEB
)

set(UPNG_VARIANTS
	VGARP2350
	WEBRP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)

set(RAYCASTER_VARIANTS
	VGARP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)

set(VGA_PIO_VARIANTS
	VGA
	VGAUSB
	VGARP2350
	VGAUSBRP2350
)

set(VGA_VARIANTS
	VGA
	VGAUSB
	VGARP2350
	VGAUSBRP2350
)

set(HDMI_VARIANTS
	HDMI
	HDMIUSB
	HDMIBTH
	HDMIWEB
)

set(PICOMITE_VARIANTS
	PICO
	PICOUSB
	PICORP2350
	PICOUSBRP2350
	PICOBTRP2350
	PICOBTHRP2350
)

set(PICO_MULTICORE_VARIANTS
	VGA
	PICO
	PICOMIN
	HDMI
	VGARP2350
	PICORP2350
	PICOBTRP2350
	PICOBTHRP2350
)

set(PICO_RAND_VARIANTS
	VGARP2350
	WEBRP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)
set(USES_CACHE_VARIANTS
	HDMIUSB
	PICO
	PICORP2350
	PICOUSBRP2350
	VGARP2350
	VGAUSBRP2350
	HDMI
	PICOBTRP2350
	PICOBTHRP2350
	HDMIBTH
	HDMIWEB
)
compile_is_in(IS_RP2350 ${RP2350_VARIANTS})
compile_is_in(IS_WEB ${WEB_VARIANTS})
compile_is_in(IS_WEBRP2350 WEBRP2350)
compile_is_in(IS_HDMIWEB HDMIWEB)
compile_is_in(HAS_GUI ${GUI_VARIANTS})
compile_is_in(IS_PICOMIN PICOMIN)
compile_is_in(IS_FULL_RP2040 ${FULL_RP2040_VARIANTS})
compile_is_in(USES_USB_HOST ${USB_HOST_VARIANTS})
compile_is_in(USES_UPNG ${UPNG_VARIANTS})
compile_is_in(USES_RAYCASTER ${RAYCASTER_VARIANTS})
compile_is_in(USES_VGA_PIO ${VGA_PIO_VARIANTS})
compile_is_in(IS_VGA ${VGA_VARIANTS})
compile_is_in(IS_HDMI ${HDMI_VARIANTS})
compile_is_in(IS_PICOMITE ${PICOMITE_VARIANTS})
compile_is_in(USES_PICO_MULTICORE ${PICO_MULTICORE_VARIANTS})
compile_is_in(USES_PICO_RAND ${PICO_RAND_VARIANTS})
compile_is_in(USES_CACHE ${USES_CACHE_VARIANTS})
compile_is_in(IS_BT ${BT_VARIANTS})
compile_is_in(IS_BTH ${BTH_VARIANTS})

option(MMBASIC_ENABLE_FM "Enable FM command and file manager runtime" ON)

if(IS_PICOMIN)
	list(REMOVE_ITEM COMMON_SOURCES VS1053.c)
	list(REMOVE_ITEM COMMON_SOURCES re.c)
	list(REMOVE_ITEM DISPLAY_SOURCES SSD1963.c)
	list(APPEND COMMON_SOURCES SSD1963min.c)
endif()

set(USES_FM ${MMBASIC_ENABLE_FM})
if(IS_PICOMIN)
	set(USES_FM FALSE)
endif()

# Turtle graphics is off for the SPI-LCD WebMite (no framebuffer/RAM) and
# PICOMITEMIN, but HDMIWEB is an HDMI graphics build (like HDMIBTH) and its
# Raycaster needs Turtle's fill_patterns[], so keep it on for HDMIWEB.
set(USES_TURTLE TRUE)
if((IS_WEB OR IS_PICOMIN) AND NOT IS_HDMIWEB)
	set(USES_TURTLE FALSE)
endif()

set(USES_STRUCTS TRUE)
if(IS_PICOMIN)
	set(USES_STRUCTS FALSE)
endif()

# Derive the board purely from COMPILE into a local var, then force it into
# the cache below. The RP2350 wireless variants use the RP2350B + CYW43 board
# (pimoroni_pico_plus2_w_rp2350) rather than pico2_w. pico2_w is the RP2350A
# package (PICO_RP2350A=1 -> NUM_BANK0_GPIOS=30), which compiles the SDK's
# gpio_put/gpio_get inline helpers with the 32-bit-mask path: any GPIO >=32
# wraps (a bit-banged SD card on GP32/GP33 silently drives GP0/GP1). The
# Plus 2 W board is RP2350B (NUM_BANK0_GPIOS=48) with identical CYW43 wiring
# (GP23/24/25/29), so WiFi/BT is unaffected while GP30-47 become usable. Safe
# on genuine RP2350A silicon too: CheckPin() caps pin numbers at 29 via the
# runtime-detected package. HDMIBTH lands in the BT/BTH branch (it is in
# BTH_VARIANTS, so IS_BTH is true). HDMIWEB lands here via IS_HDMIWEB — it
# uses the CYW43 chip for WiFi just like WEBRP2350, so it needs the same
# RP2350B + CYW43 board.
if(IS_RP2350)
	set(PICO_PLATFORM rp2350)
	if(IS_WEBRP2350 OR IS_HDMIWEB OR IS_BT OR IS_BTH)
		set(_PM_BOARD pimoroni_pico_plus2_w_rp2350)
	else()
		set(_PM_BOARD pimoroni_pga2350)
	endif()
else()
	set(PICO_PLATFORM rp2040)
	if(COMPILE STREQUAL "WEB")
		set(_PM_BOARD pico_w)
	else()
		set(_PM_BOARD pico)
	endif()
endif()

# Force PICO_BOARD on every configure so a build directory reused across
# variants (your VSCode workflow) never inherits a stale board from
# CMakeCache.txt. FORCE overwrites any previously cached value, including the
# Pico SDK's own default and whatever the last-built variant left behind.
# This is what makes a plain reconfigure (no dir deletion) correct.
set(PICO_BOARD ${_PM_BOARD} CACHE STRING "PICO target board (derived from COMPILE)" FORCE)
message(STATUS "PicoMite: COMPILE=${COMPILE} -> PICO_PLATFORM=${PICO_PLATFORM} PICO_BOARD=${PICO_BOARD}")

include(pico_sdk_import.cmake)
project(PicoMite C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# set(PICO_NO_COPRO_DIS 1)
pico_sdk_init()

add_executable(PicoMite ${COMMON_SOURCES})

# RP2040 SDK memmap_default.ld excludes libm from FLASH and pulls it into RAM
# via .data. Keep libm in FLASH to reduce RAM usage (e.g. complex math funcs).
file(READ ${PICO_LINKER_SCRIPT_PATH}/memmap_default.ld PICO_LINKER_SCRIPT_CONTENT)
string(REPLACE " *libm.a:" "" PICO_LINKER_SCRIPT_CONTENT "${PICO_LINKER_SCRIPT_CONTENT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/memmap_default_keep_libm_flash.ld "${PICO_LINKER_SCRIPT_CONTENT}")
pico_set_linker_script(PicoMite ${CMAKE_CURRENT_BINARY_DIR}/memmap_default_keep_libm_flash.ld)

if(IS_RP2350)
	target_sources(PicoMite PRIVATE
		psram.c
		stepper.c
	)
endif()

# DISPLAY_SOURCES = the SPI-LCD driver (SSD1963.c) + Touch.c. HDMIWEB is a
# WEB variant (IS_WEB) but its display is HDMI, not a SPI panel — exclude it
# so it mirrors HDMIBTH (GUI.c supplies the touch stubs). This also keeps
# SSD1963.h's `nop` macro out of every HDMIWEB TU.
if((IS_WEB OR HAS_GUI OR IS_PICOMIN) AND NOT IS_HDMIWEB)
	target_sources(PicoMite PRIVATE ${DISPLAY_SOURCES})
endif()

if(IS_WEB)
	target_sources(PicoMite PRIVATE ${WEB_SOURCES})
	set_source_files_properties(cJSON.c PROPERTIES COMPILE_FLAGS -Os)
	target_include_directories(PicoMite PRIVATE
		${CMAKE_CURRENT_LIST_DIR}
		${CMAKE_CURRENT_LIST_DIR}/..
	)
endif()

if(HAS_GUI OR IS_HDMI OR (IS_VGA AND IS_RP2350))
	target_sources(PicoMite PRIVATE GUI.c)
endif()

if(COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOBTRP2350" OR COMPILE STREQUAL "PICOBTHRP2350")
	target_sources(PicoMite PRIVATE VGA222.c)
endif()

if(IS_BT)
	target_sources(PicoMite PRIVATE BTConsole.c)
	# pico_btstack_make_gatt_header compiles nus_gatt.gatt to a C header
	# (nus_gatt.h in the build dir) listing the GATT attribute database.
	pico_btstack_make_gatt_header(PicoMite PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/nus_gatt.gatt)
endif()

if(IS_BTH)
	# PICOMITEBTH adds a BLE HID host so a Bluetooth keyboard can drive
	# the standard MMBasic console input. USB CDC console stays enabled
	# (BT is an input source, not a console replacement). BTKeyboard.c
	# pumps btstack and feeds incoming HID reports into the same
	# process_kbd_report path the USB HID host uses.
	target_sources(PicoMite PRIVATE BTKeyboard.c)
endif()

# KeyboardMap.c contains the shared HID keyboard keymap + report decoder
# (extracted from USBKeyboard.c). Compiled for either transport: USB
# HID host or BLE HID host (HOG). Skipped for headless / PS/2-only
# variants — they use Keyboard.c instead.
if(USES_USB_HOST OR IS_BTH)
	target_sources(PicoMite PRIVATE KeyboardMap.c)
endif()

if(USES_USB_HOST)
	target_sources(PicoMite PRIVATE USBKeyboard.c)
else()
	target_sources(PicoMite PRIVATE
		Keyboard.c
		mouse.c
	)
endif()

if(USES_UPNG)
	target_sources(PicoMite PRIVATE upng.c)
endif()

if(USES_TURTLE)
	target_sources(PicoMite PRIVATE Turtle.c)
endif()

if(USES_RAYCASTER)
	target_sources(PicoMite PRIVATE Raycaster.c)
endif()

if(USES_CACHE)
	target_sources(PicoMite PRIVATE ${CACHE_SOURCES})
	target_compile_definitions(PicoMite PRIVATE CACHE)
	set_source_files_properties(MMtrace.c PROPERTIES COMPILE_FLAGS -Os)
endif()

foreach(source IN LISTS OPTIMIZED_SOURCES)
	set_source_files_properties(${source} PROPERTIES COMPILE_FLAGS -Os)
endforeach()

pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteI2S.pio)

if(IS_FULL_RP2040)
	set_source_files_properties(Mouse.c PROPERTIES COMPILE_FLAGS -Os)
	set_source_files_properties(Keyboard.c PROPERTIES COMPILE_FLAGS -Os)
	set_source_files_properties(Audio.c PROPERTIES COMPILE_FLAGS -Os)
	set_source_files_properties(Serial.c PROPERTIES COMPILE_FLAGS -Os)
	set_source_files_properties(SSD1963.c PROPERTIES COMPILE_FLAGS -Os)
	set_source_files_properties(Custom.c PROPERTIES COMPILE_FLAGS -Os)

	pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
	target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
	pico_set_boot_stage2(PicoMite slower_boot2)
endif()

if(USES_VGA_PIO)
	pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteVGA.pio)
endif()

if(USES_USB_HOST)
	Pico_enable_stdio_usb(PicoMite 0)
	target_include_directories(PicoMite PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/usb_host_files
	)
elseif(IS_BT)
	# PICOMITEBT replaces the USB CDC console with an RFCOMM/SPP link over
	# the CYW43439's Bluetooth radio. Disable stdio_usb so TinyUSB device is
	# not linked.
	Pico_enable_stdio_usb(PicoMite 0)
else()
	Pico_enable_stdio_usb(PicoMite 1)
endif()

pico_enable_stdio_uart(PicoMite 0)
pico_add_extra_outputs(PicoMite)
pico_set_printf_implementation(PicoMite pico)

if(IS_RP2350)
	# double: 'pico' is the platform impl, which on RP2350 is DCP-backed
	#   (double_aeabi_dcp.S / double_sci_m33.S). There is NO 'pico_dcp' double
	#   variant - pico_set_double_implementation(... pico_dcp) links a
	#   non-existent -lpico_double_pico_dcp and the link fails. DCP is the only
	#   accelerated double path, so 'pico' IS the DCP one.
	# float: 'pico_vfp' -> single precision on the M33 FPU (pico_float_pico_vfp).
	pico_set_double_implementation(PicoMite pico)
	pico_set_float_implementation(PicoMite pico_vfp)
endif()

target_compile_definitions(PicoMite PRIVATE
	NDEBUG
	PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
	PICO_ADC_CLKDIV_ROUND_NEAREST
	PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
	PICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK
	PICO_FLASH_SIZE_BYTES=16777216
	PICO_CORE1_STACK_SIZE=0x00
	PICO_MALLOC_PANIC
	GCODE
)

target_compile_options(PicoMite PRIVATE
	-O2
	-Wall
)

if(USES_STRUCTS)
	target_compile_definitions(PicoMite PRIVATE STRUCTENABLED)
endif()

if(USES_FM)
	target_compile_definitions(PicoMite PRIVATE MMBASIC_FM)
endif()

if(USES_RAYCASTER)
	target_compile_definitions(PicoMite PRIVATE RAYCASTER)
endif()

if(IS_RP2350)
	target_compile_definitions(PicoMite PRIVATE
		rp2350
		PICO_USE_GPIO_COPROCESSOR=1
		PICO_FLASH_SPI_CLKDIV=4
		PICO_PIO_USE_GPIO_BASE
	)
endif()

if(IS_PICOMITE AND NOT IS_BT AND NOT IS_BTH)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITE
		PICO_HEAP_SIZE=0x800
		GUICONTROLS
		PICO_CORE0_STACK_SIZE=0x2000
	)
endif()

if((IS_BT OR IS_BTH) AND NOT COMPILE STREQUAL "HDMIBTH")
	# IS_BT / IS_BTH each supply their own PICO_HEAP_SIZE / PICO_CORE0_STACK_SIZE
	# / GUICONTROLS below; PICOMITE is still defined here so the existing
	# PicoMite RP2350 code paths fire. Excluded for HDMIBTH because that
	# variant is a PICOMITEVGA build (HDMI scanout, framebuffer modes,
	# GUI controls) — defining PICOMITE alongside PICOMITEVGA double-
	# declares globals (USBenabled, ScrollStart, …) and breaks the build.
	target_compile_definitions(PicoMite PRIVATE PICOMITE)
endif()

if(IS_PICOMIN)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITE
		PICO_HEAP_SIZE=0x800
		PICO_CORE0_STACK_SIZE=0x2000
		PICOMITEMIN
	)
endif()

if(IS_VGA)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEVGA
		PICO_HEAP_SIZE=0x800
		PICO_CORE0_STACK_SIZE=0x2000
	)
	# Mouse-driven GUI controls only on the RP2350 VGA builds —
	# RP2040 VGA is too tight on memory for the Ctrl[] heap reservation.
	if(IS_RP2350)
		target_compile_definitions(PicoMite PRIVATE GUICONTROLS)
	endif()
endif()

if(IS_HDMI)
	# HDMI is RP2350-only.
	# PICO_HEAP_SIZE / PICO_CORE0_STACK_SIZE are set per-variant below
	# — HDMIBTH and HDMIWEB need 0x4000 each like PICOMITEBT/BTH (cyw43
	# driver + async_context + libc/mbedtls allocations push past the
	# 0x800 / 0x2000 the bare HDMI builds use). HDMIWEB additionally runs
	# the USB host stack on core1, so it is excluded here and sets its own
	# budget in the IS_WEB block.
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEVGA
		HDMI
		GUICONTROLS
	)
	if(NOT COMPILE STREQUAL "HDMIBTH" AND NOT COMPILE STREQUAL "HDMIWEB")
		target_compile_definitions(PicoMite PRIVATE
			PICO_HEAP_SIZE=0x800
			PICO_CORE0_STACK_SIZE=0x2000
		)
	endif()
	# HDMICUTDOWN = the reduced HDMI scanout pipeline shared by HDMIBTH and
	# HDMIWEB (96 KB framebuffer pool, limited resolution set, RGB332
	# R640x480x8 special mode). Defined as a global -D — NOT in
	# configuration.h — because AllCommands.h gates on it / on HDMI and is
	# included before configuration.h. The live no-reboot RESOLUTION switch
	# is no longer cut-down-only: the full HDMI/HDMIUSB builds get it too
	# (combined with a runtime clk_sys change between the per-resolution CPU
	# speeds). This is purely the display concern; the BLE-HID-host concern
	# stays gated on PICOMITEHDMIBTH so HDMIWEB does not pull in btstack.
	if(COMPILE STREQUAL "HDMIBTH" OR COMPILE STREQUAL "HDMIWEB")
		target_compile_definitions(PicoMite PRIVATE HDMICUTDOWN)
	endif()
endif()

if(COMPILE STREQUAL "HDMIBTH")
	# HDMIBTH = HDMIUSB + BLE HID host (Bluetooth keyboard + heartbeat
	# LED on the CYW43 chip). HDMIBTH is in BTH_VARIANTS so IS_BTH
	# fires — that block pulls in BTKeyboard.c, links pico_btstack_ble /
	# pico_btstack_cyw43 / pico_cyw43_arch_none, and sets the shared
	# defines (CYW43_ENABLE_BLUETOOTH, CYW43_PIO_CLOCK_DIV_DYNAMIC,
	# heap=0x4000, stack=0x4000, GUICONTROLS). This block only adds
	# the HDMIBTH-specific identity.
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEHDMIBTH
		CYW43_HOST_NAME=\"PicoMiteHDMIBTH\"
	)
endif()

if(IS_WEB)
	# Common WiFi/lwIP defines for every WEB variant (WEB, WEBRP2350, HDMIWEB).
	# PICO_CYW43_ARCH_POLL = the lwIP poll backend (pumped from core0 in
	# ProcessWeb); CYW43_PIO_CLOCK_DIV_DYNAMIC lets the SPI link scale with
	# clk_sys. Per-variant heap/stack/host-name follow.
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEWEB
		PICO_CYW43_ARCH_POLL
		CYW43_PIO_CLOCK_DIV_DYNAMIC=1
		PICO_HEAP_SIZE=0x2800
		PICO_CORE0_STACK_SIZE=0x2000
	)
	if(IS_HDMIWEB)
		# HDMIWEB = HDMIUSB display + USB host stack + WebMite WiFi/lwIP/TLS,
		# no Bluetooth. PICOMITEHDMIWEB is the identity marker that disambiguates
		# the spots where PICOMITEVGA and PICOMITEWEB both apply (MES_SIGNON, the
		# configuration.h memory/CPU budget).
		#
		# Heap/stack = 0x4000/0x4000 (16 KB each). NOTE: in this linker script
		# these values mainly drive the GetHighestHexAddress fit check; the real
		# core0 stack grows from __StackTop down through SCRATCH into main RAM and
		# the C heap grows up from BSS, so the two meet in the gap above bss_end.
		# Keeping the budget at 0x4000/0x4000 (and HEAP_MEMORY_SIZE at 136 KB in
		# configuration.h) preserves that gap so a deep mbedtls TLS handshake
		# stack does not collide with the C heap. A trial at 0x2C00/0x3400 +
		# 144 KB MMBasic heap shrank the gap by 8 KB and hung mid-handshake.
		target_compile_definitions(PicoMite PRIVATE
			PICOMITEHDMIWEB
			CYW43_HOST_NAME=\"PicoMiteHDMIWEB\"
		)
	else()
		target_compile_definitions(PicoMite PRIVATE
			CYW43_HOST_NAME=\"WebMite\"
		)
	endif()
endif()

if(USES_USB_HOST)
	target_compile_definitions(PicoMite PRIVATE USBKEYBOARD)
elseif(IS_BT)
	# Bluetooth console build — no USB CDC, no TinyUSB CDC config.
else()
	target_compile_definitions(PicoMite PRIVATE
		PICO_STDIO_USB_TASK_INTERVAL_US=1000
		CFG_TUD_CDC_RX_BUFSIZE=256
		CFG_TUD_CDC_TX_BUFSIZE=256
		CFG_TUD_CDC_EP_BUFSIZE=256
	)
endif()

if(IS_WEBRP2350)
	target_compile_definitions(PicoMite PRIVATE GUICONTROLS)
endif()

if(IS_BT)
	# PICOMITEBT defines:
	# - PICOMITE: reuse the existing PicoMite RP2350 code paths
	# - PICOMITEBT: gate the BT-specific code blocks
	# - CYW43_ENABLE_BLUETOOTH=1: tells the cyw43 driver to load BT firmware
	# - CYW43_PIO_CLOCK_DIV_DYNAMIC=1: lets us call
	#       cyw43_set_pio_clkdiv_int_frac8() to scale the SPI link with
	#       clk_sys (same as WEB does).
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEBT
		CYW43_ENABLE_BLUETOOTH=1
		CYW43_PIO_CLOCK_DIV_DYNAMIC=1
		CYW43_HOST_NAME=\"PicoMiteBT\"
		# 16 KB each for heap and stack — matches WEB build. Adequate
		# given the MIN_CPU=200 MHz floor set in configuration.h
		# (below ~150 MHz the BLE stack call depth + IRQ stacking
		# can overflow these budgets under sustained bidirectional
		# AutoSave traffic).
		PICO_HEAP_SIZE=0x4000
		PICO_CORE0_STACK_SIZE=0x4000
		GUICONTROLS
	)
	# btstack_config.h lives at project root.
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})
endif()

if(IS_BTH)
	# BTH = PicoMite-style build + BLE HID host. Shared defines below
	# apply to all BTH variants (currently PICOBTHRP2350 and HDMIBTH).
	target_compile_definitions(PicoMite PRIVATE
		CYW43_ENABLE_BLUETOOTH=1
		CYW43_PIO_CLOCK_DIV_DYNAMIC=1
		PICO_HEAP_SIZE=0x4000
		PICO_CORE0_STACK_SIZE=0x4000
		GUICONTROLS
	)
	# btstack_config.h lives at project root.
	target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

	# PICOMITEBTH-specific build identity: keep this gated so HDMIBTH
	# does NOT get the PICOMITEBTH define (HDMIBTH has its own marker
	# PICOMITEHDMIBTH and signon banner, and the two MES_SIGNON
	# definitions would collide if both fired).
	if(COMPILE STREQUAL "PICOBTHRP2350")
		target_compile_definitions(PicoMite PRIVATE
			PICOMITEBTH
			CYW43_HOST_NAME=\"PicoMiteBTH\"
		)
	endif()
endif()

target_link_options(PicoMite PRIVATE
	"LINKER:--wrap=gpio_default_irq_handler"
)

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

if(USES_USB_HOST)
	target_link_libraries(PicoMite
		tinyusb_host
		tinyusb_board
		pico_multicore
	)
endif()

if(USES_PICO_MULTICORE)
	target_link_libraries(PicoMite pico_multicore)
endif()

if(USES_PICO_RAND)
	target_link_libraries(PicoMite pico_rand)
endif()

if(IS_WEB)
	target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)
endif()

if(IS_BT)
	# pico_cyw43_arch_none is the right backend for BT-only (no lwIP):
	# it sets CYW43_LWIP=0 and uses threadsafe_background internally via
	# async_context. pico_cyw43_arch_poll requires lwIP (only built when
	# pico_lwip target exists), so it can't be used here.
	# We supply our own hal_flash_bank (BTConsole.c) backed by Option
	# struct, so pico_btstack_flash_bank is not required.
	target_link_libraries(PicoMite
		pico_btstack_ble
		pico_btstack_cyw43
		pico_cyw43_arch_none
	)
endif()

if(IS_BTH)
	# Same backend as IS_BT: BLE-only + cyw43 + threadsafe-background.
	# BLE-central (HOG) reuses pico_btstack_ble; Bluetooth Classic HID
	# host would additionally need pico_btstack_classic, deferred until
	# basic HOG works.
	target_link_libraries(PicoMite
		pico_btstack_ble
		pico_btstack_cyw43
		pico_cyw43_arch_none
	)
endif()

# TLS support (mbedtls + lwIP altcp_tls glue) — RP2350 only.
# Enabled for the WEBRP2350 and HDMIWEB targets (both are RP2350 WiFi builds
# with the same lwIP MEM_SIZE headroom the mbedtls allocator needs).
if(IS_WEBRP2350 OR IS_HDMIWEB)
	target_link_libraries(PicoMite
		pico_lwip_mbedtls
		pico_mbedtls
	)
	# PICOMITEWEB_TLS gates the LWIP_ALTCP_TLS defines in lwipopts.h. lwIP
	# sources (including altcp_tls_mbedtls.c which pico_lwip_mbedtls pulls
	# into PicoMite via INTERFACE sources) are compiled with PicoMite's
	# PRIVATE flags, so set it here too — not only in configuration.h, which
	# isn't included by lwIP TUs.
	target_compile_definitions(PicoMite PRIVATE PICOMITEWEB_TLS)
	# pico-sdk sets MBEDTLS_CONFIG_FILE on pico_mbedtls_headers INTERFACE
	# (pointing at the sdk default). That definition reaches PicoMite via
	# both pico_mbedtls and pico_lwip_mbedtls -> pico_mbedtls_headers, and
	# our additional definition produces the "redefined" warning. Strip
	# pico-sdk's value from pico_mbedtls_headers, then install ours.
	foreach(_tls_target pico_mbedtls_headers pico_mbedtls pico_lwip_mbedtls)
		if(TARGET ${_tls_target})
			get_target_property(_defs ${_tls_target} INTERFACE_COMPILE_DEFINITIONS)
			if(_defs)
				list(FILTER _defs EXCLUDE REGEX "^MBEDTLS_CONFIG_FILE=")
				set_property(TARGET ${_tls_target} PROPERTY INTERFACE_COMPILE_DEFINITIONS "${_defs}")
			endif()
		endif()
	endforeach()
	target_compile_definitions(pico_mbedtls_headers INTERFACE
		MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_LIST_DIR}/mbedtls_config.h"
	)
endif()

# Emit build_limits.txt containing the resolved per-variant memory limits.
# Consumed by GetHighestHexAddress.py to verify the .uf2 fits within
# FLASH_TARGET_OFFSET and that BSS+heap+stack fits in RAM. Values are pulled
# directly from PicoMite's COMPILE_DEFINITIONS so they cannot drift from
# what was actually compiled in.
get_target_property(_fw_defs PicoMite COMPILE_DEFINITIONS)
set(FW_PICO_HEAP_SIZE "")
set(FW_PICO_CORE0_STACK_SIZE "")
foreach(_def ${_fw_defs})
	if(_def MATCHES "^PICO_HEAP_SIZE=(.+)$")
		set(FW_PICO_HEAP_SIZE "${CMAKE_MATCH_1}")
	elseif(_def MATCHES "^PICO_CORE0_STACK_SIZE=(.+)$")
		set(FW_PICO_CORE0_STACK_SIZE "${CMAKE_MATCH_1}")
	endif()
endforeach()
# Stack top = top of SCRATCH_Y in the pico-sdk default linker script
# (__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y)). Stack grows downward
# from there through SCRATCH_Y, SCRATCH_X (free because PICO_CORE1_STACK_SIZE=0),
# and into main RAM if it spills further. The collision-with-heap check is
# therefore against STACK_TOP, not against RAM_END.
if(IS_RP2350)
	set(FW_STACK_TOP 0x20082000)  # 512 KB RAM + 4 KB SCRATCH_X + 4 KB SCRATCH_Y
else()
	set(FW_STACK_TOP 0x20042000)  # 256 KB RAM + 4 KB SCRATCH_X + 4 KB SCRATCH_Y
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/build_limits.txt
"COMPILE=${COMPILE}
PICO_HEAP_SIZE=${FW_PICO_HEAP_SIZE}
PICO_CORE0_STACK_SIZE=${FW_PICO_CORE0_STACK_SIZE}
STACK_TOP=${FW_STACK_TOP}
FLASH_BASE=0x10000000
")
