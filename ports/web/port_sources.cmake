# ports/web/port_sources.cmake — source-list contributions for
# COMPILE=WEB (rp2040 PICOMITEWEB).

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}     # for our common lwipopts
)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # WiFi stack (CYW43 + lwIP + MQTT/UDP/TFTP/Telnet/NTP/HTTPD).
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

    # SPI-LCD framebuffer + nextgen stub (WEB has SPI-LCD support but no
    # rp2350 nextgen displays).
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_nextgen_stub.c

    # Non-PICOMITE stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c

    # rp2040 stubs.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-VGA / non-HDMI stub.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c

    # WEB has gui_touch (needed for SSD1963 + Touch flows). gfx_3d is
    # excluded — closeall3d stub already in MMtcpserver.c.
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    # WEB rp2040 has no GUICONTROLS (no headroom for the widget family).
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # PS/2 keyboard (no USB variant for WEB rp2040).
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
    ${CMAKE_SOURCE_DIR}/mouse.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

# --- Per-port build config (Stage E2) -------------------------------------
# WEB rp2040 — CYW43 polled stack, larger heap, no PICOMITE/PICOMITEVGA flag.
target_compile_options(PicoMite PRIVATE -DPICO_HEAP_SIZE=0x3000
                                        -DCYW43_HOST_NAME="WebMite"
                                        -DPICO_CYW43_ARCH_POLL
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        -DHAL_PORT_DEVICE_NAME="WebMite"
                                        )
target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(PicoMite slower_boot2)

Pico_enable_stdio_usb(PicoMite 1)
