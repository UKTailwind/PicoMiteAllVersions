# ports/picocalc_wifi_rp2350/port_sources.cmake — source-list
# contributions for the ClockworkPi PicoCalc Pico 2 W port.

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common
)

picomite_enable_bytecode_vm(PicoMite)
picomite_enable_bytecode_vm_pico_hooks(PicoMite)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # WiFi stack.
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

    # GUICONTROLS: this WiFi RP2350 variant has room for the widget family.
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/GUI.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_real.c

    # rp2350 features. PicoCalc RP2350B boards keep QSPI PSRAM
    # available while CYW43 uses regular GPIOs for its PIO-SPI bus.
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram.c
    ${CMAKE_SOURCE_DIR}/third_party/upng/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_real.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # SPI-LCD framebuffer.
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_framebuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_options.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c

    # SPI-LCD display pipeline.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx.c

    # Non-VGA stub. WEB has gui_touch.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch.c

)

# USB-A in device mode for the CDC console (PicoCalc keyboard is I²C).
usb_role(CDC)

set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

# PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2ckbd.c
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_real.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_picocalc.c
)

# --- Per-port build config (Stage E2) -------------------------------------
# CYW43_PIO_CLOCK_DIV_INT=3 (default 2 = 50 MHz gSPI at clk_sys 200 MHz,
# right at chip spec). /3 → 33 MHz gSPI, comfortable margin.
target_compile_options(PicoMite PRIVATE -DPICO_HEAP_SIZE=0x3000
                                        -DCYW43_HOST_NAME="PicoCalcWiFi"
                                        -DPICO_CYW43_ARCH_POLL
                                        -DCYW43_PIO_CLOCK_DIV_INT=3
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        -DHAL_PORT_DEVICE_NAME="PicoCalcWiFi"
                                        )
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_cyw43_arch_lwip_poll)
target_link_libraries(PicoMite pico_multicore)
pico_set_float_implementation(PicoMite pico_dcp)


if (SDBOOT STREQUAL "true")
    pico_set_linker_script(PicoMite ${CMAKE_SOURCE_DIR}/cmake/linker/memmap_default_rp2350.ld)
endif()
