# ports/dvi_wifi_rp2350/port_sources.cmake — single-board port for the
# pico_stretch RP2350B board: DVI/HDMI display + RM2 (CYW43) WiFi +
# QSPI PSRAM + I²S audio + USB-host keyboard. One PCB, one firmware
# variant — no COMPILE-name keyboard axis. Source list is the union
# of hdmi_rp2350 (HDMI + PSRAM scaffolding) and the WiFi stack from
# the WiFi ports.

target_include_directories(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}     # for our common lwipopts
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
    # SSD1963.c and Touch.c are intentionally omitted — they live in
    # the SPI-LCD path and Touch.c references PICOMITEVGA-incompatible
    # Option fields. AllCommands.h only dispatches fun_touch on
    # non-VGA ports so the linker is happy without Touch.c here.
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

    # rp2350 features. The pico_stretch RP2350B board has PSRAM on
    # board, so link the real psram_heap impl.
    ${CMAKE_SOURCE_DIR}/psram.c
    ${CMAKE_SOURCE_DIR}/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_pico.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite.c

    # Non-PICOMITE / non-WEB stubs.
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_fastgfx_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_oled_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_hdmi.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # PICOMITEVGA needs fun_3D / fun_map / fun_getscanline (gated on
    # #ifdef PICOMITEVGA in AllCommands.h). MMtcpserver.c's closeall3d
    # stub is gated out on PICOMITEVGA so the real closeall3d from
    # gfx_3d.c provides it.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c

    # No physical keyboard — USB controller runs in CDC device mode so
    # the host can drive the REPL via `screen /dev/cu.usbmodem*` and
    # see every printf trace come back the same way. console_cdc.c is
    # the shared CDC plumbing (also linked by every PS/2 port);
    # hal_keyboard_cdc_only.c stubs the keyboard / mouse / gamepad
    # surface and delegates the CDC HAL hooks to console_cdc.c.
    ${CMAKE_SOURCE_DIR}/drivers/console_cdc/console_cdc.c
    ${CMAKE_SOURCE_DIR}/drivers/console_cdc/hal_keyboard_cdc_only.c
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/cJSON.c PROPERTIES COMPILE_FLAGS -Os)

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
# Pick a divider so clk_sys/div stays under 45 MHz across the whole
# OPTION CPUSPEED range this port allows. With CPU range 250-378 MHz
# (HAL_PORT_MIN_CPU/MAX_CPU below) the worst case is 378/9 = 42 MHz.
target_compile_options(PicoMite PRIVATE -DCYW43_HOST_NAME="DVIWiFi"
                                        -DHAL_PORT_DEVICE_NAME="DVIWiFiMite"
                                        -DCYW43_PIO_CLOCK_DIV_INT=8
                                        -DCYW43_SPI_PROGRAM_NAME=spi_gap0_sample1
                                        )
# rp2350 chip flags.
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
# Diagnostic build: stdio over USB-CDC. printf (and CYW43_PRINTF, since
# CYW43_DEBUG is overridden to printf below) lands on the host as
# /dev/cu.usbmodem* — `screen /dev/cu.usbmodem* 115200` for live trace
# + REPL. No openocd / RTT plumbing required.
target_link_libraries(PicoMite pico_multicore pico_cyw43_arch_lwip_poll)
target_compile_options(PicoMite PRIVATE
    -DCYW43_DEBUG=printf
    -Wno-error=format
    -Wno-format
    -Wno-error=builtin-declaration-mismatch
    -Wno-error=implicit-function-declaration
    )
pico_set_float_implementation(PicoMite pico_dcp)

# Native USB peripheral in CDC device mode. HAL_PORT_KEYBOARD_USB_HOST=0
# (port_config.h) selects the CDC-only HAL keyboard backend; the Pico
# SDK's pico_stdio_usb pulls in TinyUSB device automatically.
Pico_enable_stdio_usb(PicoMite 1)
