# ports/vga_wifi_rp2350/port_sources.cmake — F2 validation port:
# VGA-PIO scanout + WiFi (CYW43 polled) on RP2350. Combines the
# source list from vga_rp2350 with the WiFi stack from web_rp2350.

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

    # VGA-PIO scanout family + QVGA modes.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_qvga_modes.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_stub.c

    # WiFi stack (CYW43 + lwIP + MQTT/UDP/TFTP/Telnet/NTP/HTTPD). NOTE:
    # drivers/ssd1963/SSD1963.c and drivers/gui_touch/Touch.c are intentionally omitted — they live in the
    # SPI-LCD path and drivers/gui_touch/Touch.c references TOUCH_XSCALE/YSCALE fields
    # that don't exist on PICOMITEVGA's `struct option_s`. AllCommands.h
    # only dispatches fun_touch on non-VGA ports so the linker is happy
    # without drivers/gui_touch/Touch.c on this F2 combo.
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

    # rp2350 features. This board has no PSRAM, so link the
    # psram_heap stub.
    ${CMAKE_SOURCE_DIR}/third_party/upng/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-PICOMITE / non-HDMI stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/fastgfx_minimal/fastgfx_minimal.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_oled_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_vga.c

    # gfx_3d.c included — F2 has PICOMITEVGA so the dispatch table
    # references fun_3D / fun_map / fun_getscanline (gated on
    # #ifdef PICOMITEVGA in AllCommands.h). shared/net/MMtcpserver.c's closeall3d
    # stub is gated out on PICOMITEVGA so the real closeall3d from
    # gfx_3d.c provides it.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    # gui_touch_stub for VGA-family (no SPI-LCD touch panel).
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c
    # No GUICONTROLS in VGA+WiFi composition.
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # PS/2 keyboard (no USB variant for this validation port).
    ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/Keyboard.c
        ${CMAKE_SOURCE_DIR}/drivers/ps2_matrix/hal_keyboard_ps2.c
        ${CMAKE_SOURCE_DIR}/drivers/console_cdc/console_cdc.c
    ${CMAKE_SOURCE_DIR}/drivers/ps2_mouse/mouse.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

pico_generate_pio_header(PicoMite ${CMAKE_SOURCE_DIR}/drivers/pio/PicoMiteVGA.pio)

# Non-PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_stub.c
)

# --- Per-port build config ------------------------------------------------
# PICOMITEVGA — VGA-family core branches.
target_compile_options(PicoMite PRIVATE                                         -DPICO_HEAP_SIZE=0x1000
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        )
# WiFi stack settings. CYW43_PIO_CLOCK_DIV_INT=4 keeps gSPI under the
# chip's ~50 MHz spec at this port's 252 MHz clk_sys (default divider 2
# would put gSPI at 63 MHz and stall the join handshake at
# WIFI_JOIN_STATE_ACTIVE — same trap dvi_wifi_rp2350 hit).
target_compile_options(PicoMite PRIVATE -DCYW43_HOST_NAME="VGAMite"
                                        -DPICO_CYW43_ARCH_POLL
                                        -DCYW43_PIO_CLOCK_DIV_INT=4
                                        -DHAL_PORT_DEVICE_NAME="VGAMiteWiFi"
                                        )
# rp2350 chip flags.
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_multicore pico_cyw43_arch_lwip_poll)
pico_set_float_implementation(PicoMite pico_dcp)

Pico_enable_stdio_usb(PicoMite 1)
