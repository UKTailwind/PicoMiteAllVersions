# ports/picocalc_wifi_rp2040/port_sources.cmake — source-list
# contributions for the ClockworkPi PicoCalc Pico W port.

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common
)

# This port deliberately does not opt in to the bytecode VM helper.
# RP2040 + CYW43 + PicoCalc display leaves too little RAM for the bytecode
# compiler/runtime to run realistic FRUN workloads reliably.
target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # WiFi stack (CYW43 + lwIP + MQTT/UDP/TFTP/Telnet/NTP/HTTPD).
    ${CMAKE_SOURCE_DIR}/drivers/ssd1963/SSD1963.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/Touch.c
    ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c
    ${CMAKE_SOURCE_DIR}/shared/net/mqtt.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_http_file.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_http_page.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_mqtt_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_mqtt_hal_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_ntp_hal.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_lifecycle.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_options.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_service.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_state.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_tcp_client_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_tcp_server_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_tftp.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_transmit_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_udp_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_web_cmd.c
    ${CMAKE_SOURCE_DIR}/shared/net/mm_net_wifi_cmd.c
    ${CMAKE_SOURCE_DIR}/drivers/net_lwip_raw/hal_net_lwip.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMMqtt.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMTCPclient.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMtelnet.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMntp.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMtcpserver.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMtftp.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMudp.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMsetwifi.c

    # SPI-LCD framebuffer + nextgen stub (WEB has SPI-LCD support but no
    # rp2350 nextgen displays).
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_options.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/main_init/main_init_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c

    # Non-PICOMITE stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/fastgfx_minimal/fastgfx_minimal.c

    # rp2040 stubs.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-VGA / non-HDMI stub.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c

    # WEB has gui_touch (needed for SSD1963 + Touch flows). gfx_3d is
    # excluded — closeall3d stub already in shared/net/MMtcpserver.c.
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c
    # WEB rp2040 has no GUICONTROLS (no headroom for the widget family).
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # PS/2 keyboard (no USB variant for WEB rp2040).
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
        ${CMAKE_SOURCE_DIR}/drivers/console_cdc/console_cdc.c
    ${CMAKE_SOURCE_DIR}/drivers/ps2_mouse/mouse.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

# PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2ckbd.c
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_picocalc.c
)

# --- Per-port build config (Stage E2) -------------------------------------
# WEB rp2040 — CYW43 polled stack, larger heap, no PICOMITE/PICOMITEVGA flag.
# CYW43_PIO_CLOCK_DIV_INT=3 (default 2 = 50 MHz gSPI at clk_sys 200 MHz,
# right at chip spec). /3 → 33 MHz gSPI, comfortable margin.
target_compile_options(PicoMite PRIVATE -DPICO_HEAP_SIZE=0x3000
                                        -DCYW43_HOST_NAME="PicoCalcWiFi"
                                        -DPICO_CYW43_ARCH_POLL
                                        -DCYW43_PIO_CLOCK_DIV_INT=3
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        -DHAL_PORT_DEVICE_NAME="PicoCalcWiFi"
                                        )
target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(PicoMite slower_boot2)

Pico_enable_stdio_usb(PicoMite 1)
