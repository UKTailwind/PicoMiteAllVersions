cmake_minimum_required(VERSION 3.13)

# Valid builds are PICO, PICOUSB, PICOMIN, VGA, VGAUSB, WEB,
# PICORP2350, PICOUSBRP2350, VGARP2350, VGAUSBRP2350, WEBRP2350, HDMI, HDMIUSB.
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
)

# VS Code uses this default when COMPILE is not supplied explicitly.
if(NOT DEFINED COMPILE)
set(COMPILE HDMIUSB
)
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
	MMTrace.c
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
)

set(WEB_VARIANTS
	WEB
	WEBRP2350
)

set(GUI_VARIANTS
	PICO
	PICOUSB
	PICORP2350
	PICOUSBRP2350
	WEBRP2350
)

set(USB_HOST_VARIANTS
	PICOUSB
	PICOUSBRP2350
	VGAUSB
	VGAUSBRP2350
	HDMIUSB
)

set(UPNG_VARIANTS
	VGARP2350
	WEBRP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
)

set(RAYCASTER_VARIANTS
	VGARP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
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
)

set(PICOMITE_VARIANTS
	PICO
	PICOUSB
	PICORP2350
	PICOUSBRP2350
)

set(PICO_MULTICORE_VARIANTS
	VGA
	PICO
	PICOMIN
	HDMI
	VGARP2350
	PICORP2350
)

set(PICO_RAND_VARIANTS
	VGARP2350
	WEBRP2350
	PICORP2350
	HDMI
	HDMIUSB
	VGAUSBRP2350
	PICOUSBRP2350
)
set(USES_CACHE_VARIANTS
	HDMIUSB
	PICO
	PICORP2350
	PICOUSBRP2350
	VGARP2350
	VGAUSBRP2350
	HDMI
	WEBRP2350
)
compile_is_in(IS_RP2350 ${RP2350_VARIANTS})
compile_is_in(IS_WEB ${WEB_VARIANTS})
compile_is_in(IS_WEBRP2350 WEBRP2350)
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

set(USES_TURTLE TRUE)
if(IS_WEB OR IS_PICOMIN)
	set(USES_TURTLE FALSE)
endif()

set(USES_STRUCTS TRUE)
if(IS_PICOMIN)
	set(USES_STRUCTS FALSE)
endif()

if(IS_RP2350)
	set(PICO_PLATFORM rp2350)
	if(IS_WEBRP2350)
		set(WEBRP2350_BOARD pico2_w CACHE STRING "Board definition used for WEBRP2350 build")
		set(PICO_BOARD ${WEBRP2350_BOARD})
	else()
		set(PICO_BOARD pimoroni_pga2350)
	endif()
else()
	set(PICO_PLATFORM rp2040)
	if(COMPILE STREQUAL "WEB")
		set(PICO_BOARD pico_w)
	else()
		set(PICO_BOARD pico)
	endif()
endif()

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

if(IS_WEB OR HAS_GUI OR IS_PICOMIN)
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

if(HAS_GUI)
	target_sources(PicoMite PRIVATE GUI.c)
endif()

if(COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "PICORP2350")
	target_sources(PicoMite PRIVATE VGA222.c)
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
else()
	Pico_enable_stdio_usb(PicoMite 1)
endif()

pico_enable_stdio_uart(PicoMite 0)
pico_add_extra_outputs(PicoMite)
pico_set_printf_implementation(PicoMite compiler)

if(USES_PICO_RAND)
	pico_set_float_implementation(PicoMite pico_dcp)
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

if(IS_PICOMITE)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITE
		PICO_HEAP_SIZE=0x800
		GUICONTROLS
		PICO_CORE0_STACK_SIZE=0x2000
	)
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
endif()

if(IS_HDMI)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEVGA
		HDMI
		PICO_HEAP_SIZE=0x800
		PICO_CORE0_STACK_SIZE=0x2000
	)
endif()

if(IS_WEB)
	target_compile_definitions(PicoMite PRIVATE
		PICOMITEWEB
		PICO_HEAP_SIZE=0x2800
		CYW43_HOST_NAME=\"WebMite\"
		PICO_CYW43_ARCH_POLL
		PICO_CORE0_STACK_SIZE=0x4000
	)
endif()

if(USES_USB_HOST)
	target_compile_definitions(PicoMite PRIVATE USBKEYBOARD)
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
