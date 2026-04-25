# ports/pico/port_sources.cmake — source-list contributions for
# COMPILE=PICO and COMPILE=PICOUSB (rp2040 PICOMITE / SPI-LCD board).
#
# Included by the top-level CMakeLists.txt for the selected port. Stage C
# of the HAL decascade plan: source-list inclusion is per-port composition,
# not a central COMPILE-STREQUAL ladder.

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    # PICOMITE feature backends (SPI-LCD, merge pipeline, VM framebuffer).
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_nextgen_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    ${CMAKE_SOURCE_DIR}/SSD1963.c
    ${CMAKE_SOURCE_DIR}/Touch.c

    # rp2040 stubs for rp2350-only features.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-VGA stub.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/MMweb_stubs.c
)

# Keyboard backend axis (USB host stack vs PS/2 matrix).
if (COMPILE STREQUAL "PICOUSB")
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
    )
else()
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/mouse.c
    )
endif()
