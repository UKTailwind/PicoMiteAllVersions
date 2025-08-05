cmake_minimum_required(VERSION 3.13)

# Supported COMPILE values:
#   PICO:          Standard Pico build (RP2040, no USB or VGA)
#   PICOUSB:       Pico with USB support (RP2040)
#   VGA:           VGA output support (RP2040)
#   VGAUSB:        VGA with USB support (RP2040)
#   HDMI:          HDMI output (RP2350 only)
#   HDMIUSB:       HDMI with USB support (RP2350 only)
#   WEB:           Web-enabled build (Wi-Fi, RP2040 pico_w)
#   WEBRP2350:     Web-enabled build (Wi-Fi, RP2350 pico2_w)
#   PICORP2350:    Standard Pico build (RP2350)
#   PICOUSBRP2350: Pico with USB support (RP2350)
#   VGARP2350:     VGA output support (RP2350)
#   VGAUSBRP2350:  VGA with USB support (RP2350)

set(VALID_BUILDS "PICO;PICOUSB;VGA;VGAUSB;HDMI;HDMIUSB;WEB;WEBRP2350;PICORP2350;PICOUSBRP2350;VGARP2350;VGAUSBRP2350")
if(NOT DEFINED COMPILE OR NOT COMPILE IN_LIST VALID_BUILDS)
    message(FATAL_ERROR "COMPILE must be one of: ${VALID_BUILDS}")
endif()

if(COMPILE MATCHES "HDMI|WEBRP2350|HDMIUSB|VGARP2350|VGAUSBRP2350|PICORP2350|PICOUSBRP2350")
    set(PICO_PLATFORM rp2350)
    set(PICO_FLASH_SIZE_BYTES 4194304) # 4mb for pico 2
    if(COMPILE STREQUAL "WEBRP2350")
        set(PICO_BOARD pico2_w)
    else()
        set(PICO_BOARD pico2)
    endif()
else()
    set(PICO_PLATFORM rp2040)
    set(PICO_FLASH_SIZE_BYTES 2097152) # 2mb for pico
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
#set(PICO_NO_COPRO_DIS 1)

pico_sdk_init()

add_executable(PicoMite
    PicoMite.c
    regex.c
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
)

if(COMPILE MATCHES "HDMI|WEBRP2350|HDMIUSB|VGARP2350|VGAUSBRP2350|PICORP2350|PICOUSBRP2350")
    target_sources(PicoMite PRIVATE psram.c)
endif()

if(COMPILE MATCHES "WEB|WEBRP2350")
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
    )
    set_source_files_properties(cJSON.c PROPERTIES COMPILE_FLAGS -Os)
    target_include_directories(PicoMite PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
    )
    pico_enable_stdio_usb(PicoMite 1)
endif()

if(COMPILE MATCHES "PICO|PICOUSB|PICORP2350|PICOUSBRP2350|WEBRP2350")
    target_sources(PicoMite PRIVATE
        SSD1963.c
        Touch.c
        GUI.c
    )
endif()

if(COMPILE MATCHES "VGAUSB|PICOUSBRP2350|VGAUSBRP2350|PICOUSB|HDMIUSB")
    target_sources(PicoMite PRIVATE USBKeyboard.c)
else()
    target_sources(PicoMite PRIVATE
        Keyboard.c
        mouse.c
    )
endif()

if(COMPILE MATCHES "VGARP2350|WEBRP2350|PICORP2350|HDMI|HDMIUSB|VGAUSBRP2350|PICOUSBRP2350")
    target_sources(PicoMite PRIVATE upng.c)
endif()

set_source_files_properties(mmc_stm32.c PROPERTIES COMPILE_FLAGS -O2)
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

if(COMPILE MATCHES "VGAUSB|VGA|PICO|PICOUSB|WEB")
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    pico_set_boot_stage2(PicoMite slower_boot2)
endif()

if(COMPILE MATCHES "VGAUSB|VGA|VGAUSBRP2350|VGARP2350")
    pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteVGA.pio)
endif()

if(COMPILE MATCHES "VGAUSB|PICOUSB|HDMIUSB|PICOUSBRP2350|VGAUSBRP2350")
    pico_enable_stdio_usb(PicoMite 0)
    target_include_directories(PicoMite PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/usb_host_files
    )
else()
    pico_enable_stdio_usb(PicoMite 1)
endif()

pico_enable_stdio_uart(PicoMite 0)
pico_add_extra_outputs(PicoMite)
pico_set_printf_implementation(PicoMite compiler)

if(COMPILE MATCHES "VGARP2350|WEBRP2350|PICORP2350|HDMI|HDMIUSB|VGAUSBRP2350|PICOUSBRP2350")
    pico_set_float_implementation(PicoMite pico_dcp)
endif()

target_compile_options(PicoMite PRIVATE
    -DNDEBUG
    -DPICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
    -DPICO_ADC_CLKDIV_ROUND_NEAREST
    -DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    -DPICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK
    -DPICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES}
    -DPICO_CORE1_STACK_SIZE=0x00
    -DPICO_MALLOC_PANIC
    -O2
    -Wall
)

# all RP2350 variants
if(COMPILE MATCHES "HDMI|WEBRP2350|HDMIUSB|PICORP2350|PICOUSBRP2350|VGARP2350|VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -Drp2350
        -DPICO_FLASH_SPI_CLKDIV=4
        -DPICO_PIO_USE_GPIO_BASE
    )
endif()

# all PicoMite variants - enable the gui
if(COMPILE MATCHES "PICO|PICOUSB|PICORP2350|PICOUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITE
        -DPICO_HEAP_SIZE=0x2000
        -DGUICONTROLS
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# all VGA variants
if(COMPILE MATCHES "VGA|VGAUSB|VGARP2350|VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEVGA
        -DPICO_HEAP_SIZE=0x1000
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# HDMI variants
if(COMPILE MATCHES "HDMI|HDMIUSB")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEVGA
        -DHDMI
        -DPICO_HEAP_SIZE=0x2000
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# Web variants need more heap
if(COMPILE MATCHES "WEB|WEBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEWEB
        -DPICO_HEAP_SIZE=0x3000
        -DCYW43_HOST_NAME="WebMite"
        -DPICO_CYW43_ARCH_POLL
        -DPICO_CORE0_STACK_SIZE=0x4000
    )
endif()

# USB variants
if(COMPILE MATCHES "PICOUSB|PICOUSBRP2350|VGAUSB|VGAUSBRP2350|HDMIUSB")
    target_compile_options(PicoMite PRIVATE -DUSBKEYBOARD)
endif()

# special case WEB RP2350 gets the GUI controls
if(COMPILE MATCHES "WEBRP2350")
    target_compile_options(PicoMite PRIVATE -DGUICONTROLS)
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

if(COMPILE MATCHES "VGAUSB|PICOUSB|HDMIUSB|PICOUSBRP2350|VGAUSBRP2350")
    target_link_libraries(PicoMite
        tinyusb_host
        tinyusb_board
        pico_multicore
    )
endif()

if(COMPILE MATCHES "VGA|PICO|HDMI|VGARP2350|PICORP2350")
    target_link_libraries(PicoMite pico_multicore)
endif()

if(COMPILE MATCHES "VGARP2350|WEBRP2350|PICORP2350|HDMI|HDMIUSB|VGAUSBRP2350|PICOUSBRP2350")
    target_link_libraries(PicoMite pico_rand)
endif()

if(COMPILE MATCHES "WEB|WEBRP2350")
    target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)
endif()
