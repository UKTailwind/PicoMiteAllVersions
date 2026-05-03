# ports/web_rp2350/port_sources.cmake — source-list contributions for
# COMPILE=WEBRP2350.

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}     # for our common lwipopts
)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # WiFi stack.
    ${CMAKE_SOURCE_DIR}/SSD1963.c
    ${CMAKE_SOURCE_DIR}/Touch.c
    ${CMAKE_SOURCE_DIR}/cJSON.c
    ${CMAKE_SOURCE_DIR}/mqtt.c
    ${CMAKE_SOURCE_DIR}/MMMqtt.c
    ${CMAKE_SOURCE_DIR}/MMTCPclient.c
    ${CMAKE_SOURCE_DIR}/MMtelnet.c
    ${CMAKE_SOURCE_DIR}/MMntp.c
    ${CMAKE_SOURCE_DIR}/MMtcpserver.c
    ${CMAKE_SOURCE_DIR}/tftp.c
    ${CMAKE_SOURCE_DIR}/MMtftp.c
    ${CMAKE_SOURCE_DIR}/MMudp.c
    ${CMAKE_SOURCE_DIR}/MMsetwifi.c

    # GUICONTROLS (only WEBRP2350 combines WiFi with the widget family).
    ${CMAKE_SOURCE_DIR}/GUI.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_real.c

    # rp2350 features. WEB doesn't link the PSRAM heap (the QSPI pins are
    # consumed by the CYW43 radio).
    ${CMAKE_SOURCE_DIR}/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # SPI-LCD framebuffer.
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_stub.c

    # Non-PICOMITE stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c

    # Non-VGA stub. WEB has gui_touch.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c

    # PS/2 keyboard (no USB variant for WEBRP2350).
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
    ${CMAKE_SOURCE_DIR}/mouse.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

# --- Per-port build config (Stage E2) -------------------------------------
target_compile_options(PicoMite PRIVATE -DPICO_HEAP_SIZE=0x3000
                                        -DCYW43_HOST_NAME="WebMite"
                                        -DPICO_CYW43_ARCH_POLL
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        -DHAL_PORT_DEVICE_NAME="WebMite"
                                        )
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)
pico_set_float_implementation(PicoMite pico_dcp)

Pico_enable_stdio_usb(PicoMite 1)

if (SDBOOT STREQUAL "true")
    pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/memmap_default_rp2350.ld)
endif()
