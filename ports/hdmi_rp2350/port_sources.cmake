# ports/hdmi_rp2350/port_sources.cmake — source-list contributions for
# COMPILE=HDMI and COMPILE=HDMIUSB (rp2350-only DVI scanout).

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

picomite_enable_bytecode_vm(PicoMite)
picomite_enable_bytecode_vm_pico_hooks(PicoMite)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c
    # HDMI provides its own MMBasic port-hook body (prompt-font selection
    # has extra cases for FullColour / SCREENMODE3). Shared between
    # hdmi_rp2350 and dvi_wifi_rp2350.
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_prompt_font.c

    # VGA-PIO scanout scaffolding (HDMI is a sibling of VGA-PIO inside the
    # VGA family) + HDMI-specific sink + DVI mode tables.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_modes.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_load.c
    ${CMAKE_SOURCE_DIR}/drivers/hdmi/hdmi_scanout.c

    # rp2350 features.
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram.c
    ${CMAKE_SOURCE_DIR}/third_party/upng/upng.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_real.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_real.c
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

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMweb_stubs.c
)

if (COMPILE STREQUAL "HDMIUSB")
    usb_role(KEYBOARD)
else()
    usb_role(CDC)
endif()

# Non-PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_stub.c
)

# --- Per-port build config (Stage E2) -------------------------------------
# HDMI is a sibling of VGA inside the PICOMITEVGA family, with extra heap.
target_compile_options(PicoMite PRIVATE                                         -DPICO_HEAP_SIZE=0x2000
                                        -DPICO_CORE0_STACK_SIZE=0x2000
                                        )
target_compile_options(PicoMite PRIVATE -Drp2350
                                        -DPICO_FLASH_SPI_CLKDIV=4
                                        -DPICO_PIO_USE_GPIO_BASE
                                        )
target_link_libraries(PicoMite pico_multicore)
pico_set_float_implementation(PicoMite pico_dcp)

if (COMPILE STREQUAL "HDMIUSB")
    target_compile_definitions(PicoMite PRIVATE HAL_PORT_DEVICE_NAME="PicoMiteHDMIUSB")
else()
    target_compile_definitions(PicoMite PRIVATE HAL_PORT_DEVICE_NAME="PicoMiteHDMI")
endif()
