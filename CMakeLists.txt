cmake_minimum_required(VERSION 3.13)

# Supported build types: PICO, PICOUSB, VGA, VGAUSB, HDMI (RP2350 only), HDMIUSB (RP2350 only), WEB
#set(COMPILE PICORP2350)

if(COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    set(PICO_PLATFORM rp2350)
    if(COMPILE STREQUAL "WEBRP2350")
        set(PICO_BOARD pico2_w)
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

if(COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE psram.c)
endif()

if(COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
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

if(COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "WEBRP2350")
    target_sources(PicoMite PRIVATE
        SSD1963.c
        Touch.c
        GUI.c
    )
endif()

if(COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "HDMIUSB")
    target_sources(PicoMite PRIVATE
        USBKeyboard.c
    )
else()
    target_sources(PicoMite PRIVATE
        Keyboard.c
        mouse.c
    )
endif()

if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE
        upng.c
    )
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

if(COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "WEB")
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
    pico_set_boot_stage2(PicoMite slower_boot2)
endif()

if(COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "VGARP2350")
    pico_generate_pio_header(PicoMite ${CMAKE_CURRENT_LIST_DIR}/PicoMiteVGA.pio)
endif()

if(COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
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

if(COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    pico_set_float_implementation(PicoMite pico_dcp)
endif()

target_compile_options(PicoMite PRIVATE
    -DNDEBUG
    -DPICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=0
    -DPICO_ADC_CLKDIV_ROUND_NEAREST
    -DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    -DPICO_CLOCK_AJDUST_PERI_CLOCK_WITH_SYS_CLOCK
    -DPICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    -DPICO_FLASH_SIZE_BYTES=16777216
    -DPICO_CORE1_STACK_SIZE=0x00
    -DPICO_MALLOC_PANIC
    -O2
    -Wall
)

# RP2350 variants
if(COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "WEBRP2350" OR COMPILE STREQUAL "HDMIUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -Drp2350
        -DPICO_FLASH_SPI_CLKDIV=4
        -DPICO_PIO_USE_GPIO_BASE
    )
endif()

# PicoMite variants - enable GUI
if(COMPILE STREQUAL "PICO" OR COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICORP2350" OR COMPILE STREQUAL "PICOUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITE
        -DPICO_HEAP_SIZE=0x2000
        -DGUICONTROLS
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# VGA variants
if(COMPILE STREQUAL "VGA" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGARP2350" OR COMPILE STREQUAL "VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEVGA
        -DPICO_HEAP_SIZE=0x1000
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# HDMI variants
if(COMPILE STREQUAL "HDMI" OR COMPILE STREQUAL "HDMIUSB")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEVGA
        -DHDMI
        -DPICO_HEAP_SIZE=0x2000
        -DPICO_CORE0_STACK_SIZE=0x2000
    )
endif()

# Web variants - increased heap
if(COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DPICOMITEWEB
        -DPICO_HEAP_SIZE=0x3000
        -DCYW43_HOST_NAME="WebMite"
        -DPICO_CYW43_ARCH_POLL
        -DPICO_CORE0_STACK_SIZE=0x4000
    )
endif()

# USB variants
if(COMPILE STREQUAL "PICOUSB" OR COMPILE STREQUAL "PICOUSBRP2350" OR COMPILE STREQUAL "VGAUSB" OR COMPILE STREQUAL "VGAUSBRP2350" OR COMPILE STREQUAL "HDMIUSB")
    target_compile_options(PicoMite PRIVATE
        -DUSBKEYBOARD
    )
endif()

# WEBRP2350 - enable GUI controls
if(COMPILE STREQUAL "WEBRP2350")
    target_compile_options(PicoMite PRIVATE
        -DGUICONTROLS
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

if(COMPILE STREQUAL "WEB" OR COMPILE STREQUAL "WEBRP2350")
    target_link_libraries(PicoMite
        pico_cyw43_arch_lwip_poll
    )
endif()
