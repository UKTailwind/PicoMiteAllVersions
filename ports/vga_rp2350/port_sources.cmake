# ports/vga_rp2350/port_sources.cmake — source-list contributions for
# COMPILE=VGARP2350 and COMPILE=VGAUSBRP2350.

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # VGA-PIO scanout family + QVGA modes.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_qvga_modes.c

    # rp2350 features (PSRAM, MP3, upng).
    ${CMAKE_SOURCE_DIR}/psram.c
    ${CMAKE_SOURCE_DIR}/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-PICOMITE / non-HDMI stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_nextgen_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/MMweb_stubs.c
)

if (COMPILE STREQUAL "VGAUSBRP2350")
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
    )
else()
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/mouse.c
    )
endif()

pico_generate_pio_header(PicoMite ${CMAKE_SOURCE_DIR}/PicoMiteVGA.pio)
