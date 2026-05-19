# ports/picocalc_rp2040/port_sources.cmake — source-list contributions
# for the ClockworkPi PicoCalc RP2040 port.
#
# Included by the top-level CMakeLists.txt for the selected port. Stage C
# of the HAL decascade plan: source-list inclusion is per-port composition,
# not a central COMPILE-STREQUAL ladder.

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

picomite_enable_bytecode_vm(PicoMite)
picomite_enable_bytecode_vm_pico_hooks(PicoMite)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    # PICOMITE feature backends (SPI-LCD, merge pipeline, VM framebuffer).
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_options.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/ssd1963/SSD1963.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/Touch.c

    # rp2040 stubs for rp2350-only features.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_real.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-VGA stub.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMweb_stubs.c
)

# Console and legacy keyboard/mouse backends.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
    ${CMAKE_SOURCE_DIR}/drivers/console_cdc/console_cdc.c
    ${CMAKE_SOURCE_DIR}/drivers/ps2_mouse/mouse.c
)

# PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2ckbd.c
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_picocalc.c
)

# --- Per-port build config (Stage E2) -------------------------------------
# PICOMITE base defines + heap/stack budget. PICOMITE is consulted by
# drivers/spi_lcd/spi_lcd.c (NEXTGEN gates) and a handful of port files.
target_compile_options(PicoMite PRIVATE
                                        -DPICO_HEAP_SIZE=0x800
                                        -DPICO_CORE0_STACK_SIZE=0x1000
                                        )
target_link_libraries(PicoMite pico_multicore)

# rp2040 ports use a slower flash boot stage so external SPI flash fans
# the clock down 4×.
pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(PicoMite slower_boot2)

# USB CDC console.
target_compile_options(PicoMite PRIVATE -DHAL_PORT_KEYBOARD_USB_HOST=0
                                        -DHAL_PORT_DEVICE_NAME="PicoCalc")
Pico_enable_stdio_usb(PicoMite 1)

# Optional SDBOOT linker script — relocates firmware so a 256 KB
# bootloader can sit in the first part of flash.
if (SDBOOT STREQUAL "true")
    pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/cmake/linker/memmap_default_rp2040.ld)
endif()
