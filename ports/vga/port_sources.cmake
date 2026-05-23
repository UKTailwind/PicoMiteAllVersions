# ports/vga/port_sources.cmake — source-list contributions for
# COMPILE=VGA and COMPILE=VGAUSB (rp2040 PICOMITEVGA, QVGA scanout).

target_include_directories(PicoMite PRIVATE ${CMAKE_CURRENT_LIST_DIR})

picomite_enable_bytecode_vm(PicoMite)
picomite_enable_bytecode_vm_pico_hooks(PicoMite)

target_sources(PicoMite PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pin_tables.c
    ${CMAKE_CURRENT_LIST_DIR}/port_defaults.c

    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/mmbasic_port_pico.c

    # VGA-PIO scanout family (HAS_VGA_PIO=1) + QVGA-specific mode tables.
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_mode_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_blit_ops.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_memory.c
    ${CMAKE_SOURCE_DIR}/drivers/vga_pio/vga_qvga_modes.c
    ${CMAKE_SOURCE_DIR}/drivers/audio_i2s_pio/audio_i2s_pio_stub.c

    # Non-PICOMITE stubs (no SPI-LCD merge, no VM framebuffer, no fastgfx,
    # no SSD1963/Touch).
    ${CMAKE_SOURCE_DIR}/drivers/display_merge/display_merge_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/vm_framebuffer_unsupported/vm_framebuffer_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/fastgfx_minimal/fastgfx_minimal.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_oled_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/spi_lcd/spi_lcd_periph_io_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/editor_console/editor_console_vga.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_touch/gui_touch_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/gui_controls/gui_controls_stub.c

    # rp2040 stubs.
    ${CMAKE_SOURCE_DIR}/drivers/audio_mp3/audio_mp3_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/heartbeat/heartbeat_real.c
    ${CMAKE_SOURCE_DIR}/drivers/psram_heap/psram_heap_stub.c
    ${CMAKE_SOURCE_DIR}/drivers/upng_sprite/upng_sprite_stub.c

    # Non-WEB feature backends.
    ${CMAKE_SOURCE_DIR}/drivers/gfx_3d/gfx_3d.c
    ${CMAKE_SOURCE_DIR}/shared/net/MMweb_stubs.c
)

if (COMPILE STREQUAL "VGAUSB")
    usb_role(KEYBOARD)
else()
    usb_role(CDC)
endif()

# VGA scanout PIO (PIOmite I2S is generated globally for every device build).
pico_generate_pio_header(PicoMite ${CMAKE_SOURCE_DIR}/drivers/pio/PicoMiteVGA.pio)

# Non-PicoCalc board hooks.
target_sources(PicoMite PRIVATE
    ${CMAKE_SOURCE_DIR}/drivers/i2c_picocalc_kbd/i2c_keypad_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/picocalc_features_stub.c
    ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/port_load_overrides_stub.c
)

# --- Per-port build config (Stage E2) -------------------------------------
# PICOMITEVGA still consulted by Hardware_Includes.h (multicore include)
# and configuration.h's mode-table block. Decascade follow-on item.
target_compile_options(PicoMite PRIVATE                                         -DPICO_HEAP_SIZE=0x1000
                                        -DPICO_CORE0_STACK_SIZE=0x2000
                                        )
target_link_libraries(PicoMite pico_multicore)

pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(PicoMite slower_boot2)

if (COMPILE STREQUAL "VGAUSB")
    target_compile_definitions(PicoMite PRIVATE HAL_PORT_DEVICE_NAME="PicoMiteVGAUSB")
else()
    target_compile_definitions(PicoMite PRIVATE HAL_PORT_DEVICE_NAME="PicoMiteVGA")
endif()
