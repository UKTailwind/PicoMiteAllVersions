# ports/pico_rp2350/port_sources.cmake — source-list contributions for
# COMPILE=PICORP2350 and COMPILE=PICOUSBRP2350.

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/picocalc_keypad.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    # PICOMITE feature backends.
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    ${CMAKE_SOURCE_DIR}/SSD1963.c
    ${CMAKE_SOURCE_DIR}/Touch.c
    ${CMAKE_SOURCE_DIR}/GUI.c

    # rp2350 features.
    ${CMAKE_SOURCE_DIR}/psram.c
    ${CMAKE_SOURCE_DIR}/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-VGA stub.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/MMweb_stubs.c
)

# Keyboard backend axis.
if (COMPILE STREQUAL "PICOUSBRP2350")
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
    )
else()
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/mouse.c
    )
endif()
