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
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_real.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-PICOMITE / non-HDMI stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_oled_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/MMweb_stubs.c
)

if (COMPILE STREQUAL "VGAUSBRP2350")
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

pico_generate_pio_header(PicoMite ${CMAKE_SOURCE_DIR}/PicoMiteVGA.pio)

# --- Per-port build config (Stage E2) -------------------------------------
target_compile_options(PicoMite PRIVATE                                         -DPICO_HEAP_SIZE=0x1000
                                        -DPICO_CORE0_STACK_SIZE=0x2000
                                        )
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_multicore)
pico_set_float_implementation(PicoMite pico_dcp)

if (COMPILE STREQUAL "VGAUSBRP2350")
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_HAS_USB_KEYBOARD=1
                                            -DHAL_PORT_DEVICE_NAME="PicoMiteVGAUSB"
                                            )
    target_link_libraries(PicoMite tinyusb_host tinyusb_board)
    target_include_directories(PicoMite PRIVATE
        ${CMAKE_SOURCE_DIR}/usb_host_files
    )
    Pico_enable_stdio_usb(PicoMite 0)
else()
    target_compile_options(PicoMite PRIVATE -DHAL_PORT_HAS_USB_KEYBOARD=0
                                            -DHAL_PORT_DEVICE_NAME="PicoMiteVGA")
    Pico_enable_stdio_usb(PicoMite 1)
endif()
