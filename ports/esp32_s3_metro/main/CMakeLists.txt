# Phase B (in progress): pull in the MMBasic core + bytecode VM source list.
# Mirrors ports/mmbasic_stdio/Makefile's CORE_SRCS / BC_SRCS / CORE_STATE_SRCS,
# adapted to ESP-IDF's idf_component_register layout.
#
# All MMBasic source files compile with esp32_platform.h force-included
# (-include) so MMBASIC_HOST + MMBASIC_ESP32 are set everywhere uniformly.
#
# Pico SDK header shims (pico/stdlib.h, hardware/pwm.h etc.) are reused
# from ports/host_native/{pico,hardware}/ via -I. Same pattern as the
# WASM port.

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
)

set(STATE_SRCS
    ${REPO_ROOT}/core/state/display_state.c
    ${REPO_ROOT}/core/state/pin_state.c
    ${REPO_ROOT}/core/state/option_state.c
    ${REPO_ROOT}/core/state/audio_state.c
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
    ${REPO_ROOT}/drivers/psram_heap/psram_heap_stub.c
    ${REPO_ROOT}/drivers/heartbeat/heartbeat_stub.c
    ${REPO_ROOT}/drivers/editor_console/editor_console_stub.c
    ${REPO_ROOT}/drivers/display_pixel_host/display_pixel_host.c
    ${REPO_ROOT}/drivers/fastgfx_minimal/fastgfx_minimal.c
)

set(BC_SRCS
    ${REPO_ROOT}/lfs.c                      # vendored LittleFS used by A: drive
    ${REPO_ROOT}/lfs_util.c
    ${REPO_ROOT}/bc_alloc.c
    ${REPO_ROOT}/bc_bridge.c
    ${REPO_ROOT}/bc_source.c
    ${REPO_ROOT}/bc_compiler_core.c
    ${REPO_ROOT}/bc_vm.c
    ${REPO_ROOT}/bc_runtime.c
    ${REPO_ROOT}/bc_debug.c
    ${REPO_ROOT}/vm_host_fat.c
    ${REPO_ROOT}/ff.c
    ${REPO_ROOT}/ffunicode.c
    ${REPO_ROOT}/ffsystem.c
    ${REPO_ROOT}/vm_sys_input.c
    ${REPO_ROOT}/vm_sys_graphics.c
    ${REPO_ROOT}/vm_sys_time.c
    # vm_sys_pin / vm_sys_file have host variants that route through
    # hal_pin / hal_filesystem instead of pulling Pico SDK headers
    # directly. ESP32 uses the host variants like mmbasic_stdio /
    # mmbasic_ansi do.
    ${REPO_ROOT}/ports/host_native/vm_sys_pin_host.c
    ${REPO_ROOT}/ports/host_native/vm_sys_file_host.c
)

# ESP32 port-local sources. app_main.c keeps the blink for now; will be
# replaced by an MMBasic_Init() driver once the link goes green.
set(PORT_LOCAL_SRCS
    app_main.c                              # IDF entry, MMBasic boot, REPL
    esp32_console.c                         # USB Serial/JTAG read/write (raw bytes)
    esp32_mmbasic_console_glue.c            # MMputchar / MMPrintString / MMInkey + ring state
    esp32_system.c                          # CPU RESTART / SLEEP
    esp32_terminal.c                        # VT100 CLS + COLOUR via Draw.c port hooks
    esp32_lfs.c                             # LittleFS over esp_partition_* (A: drive)
    esp32_flash_storage.c                   # flash_target_* / flash_option_* / flash_range_* / host_options_snapshot
    esp32_compat.c                          # small porting bits
    hal_pin_esp32.c                         # real GPIO/ADC over driver/gpio.h + esp_adc
    hal_audio_esp32_stub.c
    hal_vm_framebuffer_esp32_stub.c
    hal_time_esp32.c
    hal_flash_esp32_stub.c
    hal_storage_esp32_stub.c
    hal_filesystem_esp32.c                  # path / dir ops via f_*
    hal_keyboard_esp32_stub.c
    # D-decouple Step B: minimal ESP32 files to take over symbols
    # currently sole-provided by host_native. Coexist with
    # HOST_NATIVE_REUSED via --allow-multiple-definition until Step C
    # drops host_native from the link entirely.
    esp32_default_hooks.c                   # 17 port_* no-ops
    esp32_globals.c                         # 14 tentative-def globals
    esp32_runtime.c                         # 9 VM trampoline / abort hooks + state
    esp32_cmd_files_hooks.c                 # 3 A:-only filesystem hooks
    esp32_peripheral_stubs.c                # 117 cmd_/fun_/AES_/GPS_/etc decls
)

# host_native sources are no longer in the link. The 251-symbol
# contract surface they used to sole-provide is now owned by the
# esp32_*.c files in PORT_LOCAL_SRCS above (D-decouple Step B).
# vm_sys_pin_host.c / vm_sys_file_host.c (the remaining 26 syms) sit
# below — they're effectively generic shims behind hal_pin / hal_filesystem,
# usable by any host-shape port; future cleanup can relocate them
# but they don't carry host-specific semantics.

idf_component_register(
    SRCS
        ${PORT_LOCAL_SRCS}
        ${CORE_SRCS}
        ${STATE_SRCS}
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
    INCLUDE_DIRS
        "."
        ${PORT_DIR}
        ${REPO_ROOT}
        ${REPO_ROOT}/ports/host_native      # Pico SDK header shims
    REQUIRES
        driver
        esp_adc
        esp_timer
        esp_psram
        nvs_flash
        spi_flash
        esp_partition
        vfs
)

# Force-include the platform header for every C TU in this component.
# Generator-expression-restricted to C so the auto-generated .S assembly
# wrappers IDF emits for EMBED_TXTFILES don't try to assemble it.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include esp32_platform.h>
    -funsigned-char
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
    MMBASIC_HOST
    MMBASIC_ESP32
    FF_MAX_LFN_LARGE
    BC_SIM_RP2040
)

# MMBasic uses tentative-definition merging (gui_bcolour, FSerror, etc.):
# the same global is declared without an initialiser in multiple TUs, and
# the linker is expected to coalesce them into one symbol. ESP-IDF defaults
# to -Wl,--warn-common which rejects that. Allow multiple definitions for
# this component link, same as the mmbasic_stdio Makefile does on host.
#
# --wrap=port_drive_check dropped — esp32_cmd_files_hooks.c provides
# the strong def directly (D-decouple Step C). --allow-multiple-definition
# stays for tentative-def globals; Step E plans -fcommon as the fix.
target_link_options(${COMPONENT_LIB} INTERFACE
    -Wl,--allow-multiple-definition
)
