# ports/dvi_wifi_rp2350/port_sources.cmake — single-board port for the
# pico_stretch RP2350B board: DVI/HDMI display + RM2 (CYW43) WiFi +
# QSPI PSRAM + I²S audio + USB-host keyboard. One PCB, one firmware
# variant — no COMPILE-name keyboard axis. Source list is the union
# of hdmi_rp2350 (HDMI + PSRAM scaffolding) and the WiFi stack from
# the WiFi ports.
#
# Uses pico_cyw43_arch_lwip_poll (not threadsafe_background) so all
# lwIP work happens synchronously on the main thread inside
# cyw43_arch_poll() — pumped from CheckAbort/ProcessWeb. CYW43 SPI
# clock is divided down to ~42 MHz max (CYW43_PIO_CLOCK_DIV_INT=8 +
# spi_gap0_sample1) so the gSPI link stays under spec across the
# 250-378 MHz CPU range.

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common
)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c
    # HDMI provides its own MMBasic port-hook body (prompt-font selection
    # has extra cases for FullColour / SCREENMODE3). Shared between
    # hdmi_rp2350 and dvi_wifi_rp2350.
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_prompt_font.c

    # VGA-PIO scanout scaffolding (HDMI rides on the VGA-PIO family) +
    # HDMI sink + DVI mode tables.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_modes.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_scanout.c

    # WiFi stack (CYW43 + lwIP + MQTT/UDP/TFTP/Telnet/NTP). NOTE:
    # drivers/ssd1963/SSD1963.c and drivers/gui_touch/Touch.c are intentionally omitted — they live in
    # the SPI-LCD path and drivers/gui_touch/Touch.c references PICOMITEVGA-incompatible
    # Option fields. AllCommands.h only dispatches fun_touch on
    # non-VGA ports so the linker is happy without drivers/gui_touch/Touch.c here.
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

    # rp2350 features. The pico_stretch RP2350B board has PSRAM on
    # board, so link the real psram_heap impl.
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram.c
    ${CMAKE_SOURCE_DIR}/third_party/upng/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_real.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-PICOMITE / non-WEB stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/fastgfx_minimal/fastgfx_minimal.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_oled_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_hdmi.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # PICOMITEVGA needs fun_3D / fun_map / fun_getscanline (gated on
    # #ifdef PICOMITEVGA in AllCommands.h). shared/net/MMtcpserver.c's closeall3d
    # stub is gated out on PICOMITEVGA so the real closeall3d from
    # gfx_3d.c provides it.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c

    # USB-host keyboard. The board's USB-A port is owned by TinyUSB
    # host stack so users can plug in a USB keyboard. Connect to the
    # BASIC REPL via that keyboard + HDMI display, or via UART
    # (Option.SerialConsole on GP0/GP1 per the pico_stretch board
    # file). USB-CDC stdio is unavailable on this build because the
    # USB controller is mode-exclusive (host vs device).
    ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/USBKeyboard.c
    ${CMAKE_SOURCE_DIR}/drivers/usb_host_kbd/hal_keyboard_usb.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

# --- Per-port build config -------------------------------------------------
# PICOMITEVGA — VGA-family core branches.
target_compile_options(PicoMite PRIVATE                                         -DPICO_HEAP_SIZE=0x2000
                                        -DPICO_CORE0_STACK_SIZE=0x4000
                                        )
# WiFi stack settings. Device name is what shows in the boot banner
# and MM.DEVICE$.
# CYW43 gSPI tops out around 50 MHz. The pico-sdk default of
# CYW43_PIO_CLOCK_DIV_INT=2 is calibrated for clk_sys ≈ 150 MHz; it
# overdrives the gSPI on rp2350 at any clock above that and the WiFi
# link lands at CYW43_LINK_JOIN (1) instead of CYW43_LINK_UP (3) —
# packets flow during association, then drop once the link is steady.
# Use the SDK's runtime-tunable divider mode (CYW43_PIO_CLOCK_DIV_DYNAMIC)
# and pick the smallest int divider that keeps gSPI ≤ 45 MHz at boot
# based on the actual clock_get_hz(clk_sys) value (see
# shared/net/MMsetwifi.c::cyw43_pio_divider_for_clk_sys).  This adapts to whatever
# OPTION CPUSPEED the user has set without leaving headroom on the
# table at low CPU speeds — at 252 MHz we now run gSPI at 42 MHz
# instead of the previous fixed 31 MHz.
target_compile_options(PicoMite PRIVATE -DCYW43_HOST_NAME="DVIWiFi"
                                        -DHAL_PORT_DEVICE_NAME="DVIWiFiMite"
                                        -DCYW43_PIO_CLOCK_DIV_DYNAMIC=1
                                        -DCYW43_SPI_PROGRAM_NAME=spi_gap0_sample1
                                        )
# rp2350 chip flags.
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_multicore pico_cyw43_arch_lwip_poll
                               tinyusb_host tinyusb_board)
pico_set_float_implementation(PicoMite pico_dcp)

# USB-host keyboard config. HAL_PORT_KEYBOARD_USB_HOST=1 (set in
# port_config.h) selects the USB-host backend in port-impl files;
# usb_host_files dir holds the TinyUSB device-config headers
# (tusb_config.h, etc.); stdio_usb=0 because the USB-A port is in
# host mode for the keyboard.
target_include_directories(PicoMite PRIVATE ${CMAKE_SOURCE_DIR}/usb_host_files)
Pico_enable_stdio_usb(PicoMite 0)
