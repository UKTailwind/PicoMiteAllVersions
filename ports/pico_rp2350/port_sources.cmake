# ports/pico_rp2350/port_sources.cmake — source-list contributions for
# COMPILE=PICORP2350 and COMPILE=PICOUSBRP2350.

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    # PICOMITE feature backends.
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_picomite/vm_framebuffer_picomite.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    # rp2350 PicoMite is the only port with the RAM budget for the
    # MEM332 RGB332 shadow framebuffer family (ILI9488WBUFF,
    # ST7796SPBUFF, etc.). Other SPI-LCD ports link _mem332_stub.c.
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_options.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_real.c
    ${CMAKE_SOURCE_DIR}/SSD1963.c
    ${CMAKE_SOURCE_DIR}/Touch.c
    ${CMAKE_SOURCE_DIR}/GUI.c

    # rp2350 features.
    ${CMAKE_SOURCE_DIR}/psram.c
    ${CMAKE_SOURCE_DIR}/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_real.c
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
        ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/hal_keyboard_usb.c
    )
else()
    target_sources(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
        ${CMAKE_SOURCE_DIR}/mouse.c
    )
endif()

# --- Per-port build config (Stage E2) -------------------------------------
target_compile_options(PicoMite PRIVATE
                                        -DPICO_HEAP_SIZE=0x800
                                        -DPICO_CORE0_STACK_SIZE=0x1000
                                        )
# rp2350 chip flags (also applied to vga_rp2350, hdmi_rp2350, web_rp2350).
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_multicore)
pico_set_float_implementation(PicoMite pico_dcp)

# USB axis.
if (COMPILE STREQUAL "PICOUSBRP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_KEYBOARD_USB_HOST=1
                                            -DHAL_PORT_DEVICE_NAME="PicoMiteUSB"
                                            )
    target_link_libraries(PicoMite tinyusb_host tinyusb_board)
    target_include_directories(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/usb_host_files
    )
    Pico_enable_stdio_usb(PicoMite 0)
else()
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_KEYBOARD_USB_HOST=0
                                            -DHAL_PORT_DEVICE_NAME="PicoMite")
    Pico_enable_stdio_usb(PicoMite 1)
endif()

if (SDBOOT STREQUAL "true" AND COMPILE STREQUAL "PICORP2350")
    pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/memmap_default_rp2350.ld)
endif()
