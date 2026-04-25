# ports/vga/port_sources.cmake — source-list contributions for
# COMPILE=VGA and COMPILE=VGAUSB (rp2040 PICOMITEVGA, QVGA scanout).

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # VGA-PIO scanout family (HAS_VGA_PIO=1) + QVGA-specific mode tables.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_qvga_modes.c

    # Non-PICOMITE stubs (no SPI-LCD merge, no VM framebuffer, no fastgfx,
    # no SSD1963/Touch).
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_nextgen_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c

    # rp2040 stubs.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/MMweb_stubs.c
)

if (COMPILE STREQUAL "VGAUSB")
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
    )
else()
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/mouse.c
    )
endif()

# VGA scanout PIO (PIOmite I2S is generated globally for every device build).
pico_generate_pio_header(PicoMite ${CMAKE_SOURCE_DIR}/PicoMiteVGA.pio)
