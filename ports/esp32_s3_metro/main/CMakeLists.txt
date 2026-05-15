# ESP32-S3 Metro main component.
#
# All MMBasic source files compile with esp32_platform.h force-included
# (-include) so MMBASIC_ESP32 and port feature macros are set uniformly.
#
# Remaining Pico SDK compatibility headers are neutral hardware/* shims.
# Core/shared code no longer needs pico/stdlib.h.

set(REPO_ROOT  ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(PORT_DIR   ${CMAKE_CURRENT_LIST_DIR}/..)

set(CORE_SRCS
    ${REPO_ROOT}/MMBasic.c
    ${REPO_ROOT}/Commands.c
    ${REPO_ROOT}/Functions.c
    ${REPO_ROOT}/Operators.c
    ${REPO_ROOT}/MATHS.c
    ${REPO_ROOT}/Memory.c
    ${REPO_ROOT}/MMBasic_Print.c
    ${REPO_ROOT}/MMBasic_REPL.c
    ${REPO_ROOT}/MMBasic_Prompt.c
    ${REPO_ROOT}/Editor.c
    ${REPO_ROOT}/gfx_box_shared.c
    ${REPO_ROOT}/gfx_circle_shared.c
    ${REPO_ROOT}/gfx_line_shared.c
    ${REPO_ROOT}/gfx_pixel_shared.c
    ${REPO_ROOT}/gfx_cls_shared.c
    ${REPO_ROOT}/gfx_text_shared.c
    ${REPO_ROOT}/gfx_console_shared.c
    ${REPO_ROOT}/mm_misc_shared.c
    ${REPO_ROOT}/drivers/fs_lfs_fatfs/fs_lfs_fatfs_helpers.c
    ${REPO_ROOT}/OptionCommands.c
    ${REPO_ROOT}/cmd_ws2812_shared.c
    ${REPO_ROOT}/shared/cmd_psram.c
)

set(STATE_SRCS
    ${REPO_ROOT}/core/state/display_state.c
    ${REPO_ROOT}/core/state/pin_state.c
    ${REPO_ROOT}/core/state/option_state.c
    ${REPO_ROOT}/core/state/audio_state.c
    ${REPO_ROOT}/shared/net/mm_net_interrupts.c
    ${REPO_ROOT}/shared/net/mm_net_http.c
    ${REPO_ROOT}/shared/net/mm_net_http_file.c
    ${REPO_ROOT}/shared/net/mm_net_http_page.c
    ${REPO_ROOT}/shared/net/mm_net_mqtt_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_mqtt_hal_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_ntp.c
    ${REPO_ROOT}/shared/net/mm_net_ntp_hal.c
    ${REPO_ROOT}/shared/net/mm_net_lifecycle.c
    ${REPO_ROOT}/shared/net/mm_net_options.c
    ${REPO_ROOT}/shared/net/mm_net_service.c
    ${REPO_ROOT}/shared/net/mm_net_state.c
    ${REPO_ROOT}/shared/net/mm_net_tcp_client_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_tcp_server_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_tftp.c
    ${REPO_ROOT}/shared/net/mm_net_transmit_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_udp_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_web_cmd.c
    ${REPO_ROOT}/shared/net/mm_net_websocket.c
    ${REPO_ROOT}/shared/net/mm_net_wifi_cmd.c
    ${REPO_ROOT}/drivers/web_console/web_console_assets.c
    ${REPO_ROOT}/drivers/web_console/web_console_display.c
    ${REPO_ROOT}/drivers/web_console/web_console_input.c
    ${REPO_ROOT}/drivers/web_console/web_console_protocol.c
)

set(RUNTIME_SRCS
    ${REPO_ROOT}/runtime/runtime_boot.c
    ${REPO_ROOT}/runtime/runtime_program.c
    ${REPO_ROOT}/runtime/runtime_abort.c
    ${REPO_ROOT}/runtime/runtime_interrupt.c
)

set(DEVICE_FACING_SRCS
    ${REPO_ROOT}/Draw.c
    ${REPO_ROOT}/RGB121.c
    ${REPO_ROOT}/Tilemap.c
    ${REPO_ROOT}/FileIO.c
    ${REPO_ROOT}/Audio.c
    ${REPO_ROOT}/BmpDecoder.c
    ${REPO_ROOT}/re.c
    ${REPO_ROOT}/picojpeg.c
)

set(DRIVER_STUBS
    ${REPO_ROOT}/drivers/audio_mp3/audio_mp3_stub.c
    ${REPO_ROOT}/drivers/spi_lcd/spi_lcd_mem332_stub.c
    ${REPO_ROOT}/drivers/display_merge/display_merge_stub.c
    ${REPO_ROOT}/drivers/vga_pio/vga_ops_stub.c
    ${REPO_ROOT}/drivers/gfx_3d/gfx_3d.c
    ${REPO_ROOT}/drivers/upng_sprite/upng_sprite_stub.c
    ${REPO_ROOT}/drivers/gui_touch/gui_touch_stub.c
    ${REPO_ROOT}/drivers/gui_controls/gui_controls_stub.c
    ${REPO_ROOT}/drivers/psram_heap/psram_heap_real.c
    ${REPO_ROOT}/drivers/heartbeat/heartbeat_stub.c
    ${REPO_ROOT}/drivers/editor_console/editor_console_stub.c
    ${REPO_ROOT}/drivers/display_pixel_readbuffer/display_pixel_readbuffer.c
    ${REPO_ROOT}/drivers/fastgfx_minimal/fastgfx_minimal.c
)

set(BC_SRCS
    ${REPO_ROOT}/lfs.c                      # vendored LittleFS used by A: drive
    ${REPO_ROOT}/lfs_util.c
    ${REPO_ROOT}/bc_bridge.c
    ${REPO_ROOT}/bc_source.c
    ${REPO_ROOT}/bc_compiler_core.c
    ${REPO_ROOT}/bc_vm.c
    ${REPO_ROOT}/bc_runtime.c
    ${REPO_ROOT}/bc_debug.c
    ${REPO_ROOT}/ff.c
    ${REPO_ROOT}/ffunicode.c
    ${REPO_ROOT}/ffsystem.c
    ${REPO_ROOT}/vm_sys_input.c
    ${REPO_ROOT}/vm_sys_graphics.c
    ${REPO_ROOT}/vm_sys_time.c
    esp32_bc_alloc.c                        # VM runtime on MMBasic heap; compiler scratch on ESP-IDF internal heap
    vm_sys_pin_esp32.c
    ${REPO_ROOT}/vm_sys_file.c
)

# ESP32 port-local sources. Unsupported peripherals are represented by
# explicit ESP32 stubs here instead of being inherited from host_native.
set(PORT_LOCAL_SRCS
    app_main.c                              # IDF entry, MMBasic boot, REPL
    esp32_console.c                         # USB Serial/JTAG read/write (raw bytes)
    esp32_mmbasic_console_glue.c            # MMPrintString / MMInkey + ring state (MMputchar / MMgetline are shared)
    ${REPO_ROOT}/runtime/runtime_getline.c        # shared console-line reader (see runtime/runtime_getline.c)
    ${REPO_ROOT}/runtime/runtime_console_putchar.c # shared MMputchar (Finding 1 in docs/port-duplication-audit.md)
    ${REPO_ROOT}/runtime/runtime_console_escdecode.c # shared escape-sequence decoder (Finding 2)
    esp32_system.c                          # CPU RESTART / SLEEP
    esp32_terminal.c                        # VT100 CLS + COLOUR via Draw.c port hooks
    esp32_lfs.c                             # LittleFS over esp_partition_* (A: drive)
    esp32_flash_storage.c                   # flash_target_* / flash_option_* / flash_range_* / esp32_options_snapshot
    esp32_compat.c                          # small porting bits
    hal_psram_esp32.c                       # PSRAM HAL: slab acquisition, cache sync, PSRAMbase/PSRAMsize publication
    hal_pin_esp32.c                         # real GPIO/ADC over driver/gpio.h + esp_adc
    esp32_pin_tables.c                      # Metro GPIO map + GPn codemap
    hal_ws2812_esp32.c                      # WS2812/SK6812 over ESP32 RMT
    hal_audio_esp32_stub.c
    hal_vm_framebuffer_esp32_stub.c
    hal_time_esp32.c
    hal_random_esp32.c
    hal_net_esp32.c                         # ESP-IDF socket transport HAL
    ../../../hal/generic/hal_watchdog_noop.c
    hal_flash_esp32.c                         # NVS-backed Options plus mmslots-backed program/slot persistence
    hal_storage_esp32_stub.c
    ${REPO_ROOT}/ports/pico_sdk_common/hal_filesystem_pico.c  # shared A:/B: LFS/FatFS dispatcher
    esp32_sd_diskio.c                       # Metro microSD SDSPI diskio backend for B:
    hal_keyboard_esp32_stub.c
    # D-decouple: ESP32 owns the small port surface that was originally
    # borrowed from host_native during bring-up.
    esp32_default_hooks.c                   # 17 port_* no-ops
    esp32_globals.c                         # 14 tentative-def globals
    esp32_runtime.c                         # 9 VM trampoline / abort hooks + state
    esp32_cmd_files_hooks.c                 # 3 A:-only filesystem hooks
    esp32_peripheral_stubs.c                # 117 cmd_/fun_/AES_/GPS_/etc decls
    esp32_wifi.c                            # ESP-IDF native WiFi OPTION / WEB / MM.INFO surface
    esp32_mqtt.c                            # ESP32 MQTT command/poll adapter over HAL MQTT
    esp32_ntp.c                             # ESP32 WEB NTP adapter over shared HAL transport
    esp32_tcp_client.c                      # ESP32 TCP client/stream command adapter
    esp32_tcp_server.c                      # ESP32 TCP server service and WEB TRANSMIT adapter
    esp32_tftp.c                            # ESP32 TFTP service adapter over shared core
    esp32_telnet.c                          # ESP32 Telnet console service over HAL TCP
    esp32_udp.c                             # ESP32 UDP service and WEB UDP adapter
)

# host_native runtime/peripheral sources are no longer in the link, and ESP32
# uses neutral legacy Pico SDK header shims.

idf_component_register(
    SRCS
        ${PORT_LOCAL_SRCS}
        ${CORE_SRCS}
        ${STATE_SRCS}
        ${RUNTIME_SRCS}
        ${DEVICE_FACING_SRCS}
        ${DRIVER_STUBS}
        ${BC_SRCS}
    EMBED_TXTFILES
        # Bundled demo programs. esp32_lfs writes these into the LFS
        # partition on first-boot format, so the user lands in a
        # working REPL with example code already in A:.
        demos/hello.bas
        demos/fizzbuzz.bas
        demos/sieve.bas
        demos/mand.bas
        demos/web_hello.bas
        demos/site.bas
        demos/site_index.htm
        demos/site_status.htm
        demos/site_about.htm
        demos/site_gpio.htm
        demos/site_files.htm
        demos/site_style.css
    INCLUDE_DIRS
        "."
        ${PORT_DIR}
        ${REPO_ROOT}
        ${REPO_ROOT}/ports/pico_sdk_compat # legacy hardware/* Pico SDK header shims
    REQUIRES
        driver
        esp_adc
        esp_driver_rmt
        esp_driver_sdspi
        esp_timer
        esp_psram
        esp_mm
        esp_event
        esp_netif
        esp_wifi
        mqtt
        nvs_flash
        spi_flash
        esp_partition
        sdmmc
        vfs
)

target_include_directories(${COMPONENT_LIB} BEFORE PRIVATE
    ${IDF_PATH}/components/lwip/port/include
)

# Force-include the platform header for every C TU in this component.
# Generator-expression-restricted to C so the auto-generated .S assembly
# wrappers IDF emits for EMBED_TXTFILES don't try to assemble it.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include esp32_platform.h>
    -funsigned-char
    # Preserve legacy tentative-definition merging without allowing
    # duplicate strong functions through the linker.
    -fcommon
    -Wno-error
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-misleading-indentation
    -Wno-missing-braces
    -Wno-format-truncation
    -Wno-maybe-uninitialized
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MMBASIC_ESP32
    FF_MAX_LFN_LARGE
    BC_SIM_RP2040
)

# No --wrap and no --allow-multiple-definition here. Per-port hooks must
# have exactly one strong definition in the ESP32 source list; duplicate
# functions should fail at link time.
