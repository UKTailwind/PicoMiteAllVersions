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
    app_main.c
    esp32_glue.c
    hal_pin_esp32_stub.c
    hal_audio_esp32_stub.c
    hal_vm_framebuffer_esp32_stub.c
    hal_time_esp32.c
    hal_flash_esp32_stub.c
    hal_storage_esp32_stub.c
    hal_filesystem_esp32_stub.c
    hal_keyboard_esp32_stub.c
)

# Reused from ports/host_native — runtime glue, peripheral globals, and
# FAT/POSIX shims. Same set mmbasic_stdio's NATIVE_SRCS uses (minus the
# hal_*_host.c files, which ESP32 replaces with its own stubs above,
# minus host_sim_audio.c and host_time.c which depend on POSIX/pthread
# semantics that need an esp32-specific replacement).
set(HOST_NATIVE_REUSED
    ${REPO_ROOT}/ports/host_native/host_runtime.c
    ${REPO_ROOT}/ports/host_native/host_peripheral_stubs.c
    ${REPO_ROOT}/ports/host_native/host_fs_shims.c
    ${REPO_ROOT}/ports/host_native/host_fs.c
    ${REPO_ROOT}/ports/host_native/host_keys.c
    ${REPO_ROOT}/ports/host_native/host_sim_slowdown.c
    ${REPO_ROOT}/ports/host_native/host_sim_emit_stub.c
)

idf_component_register(
    SRCS
        ${PORT_LOCAL_SRCS}
        ${HOST_NATIVE_REUSED}
        ${CORE_SRCS}
        ${STATE_SRCS}
        ${DEVICE_FACING_SRCS}
        ${DRIVER_STUBS}
        ${BC_SRCS}
    INCLUDE_DIRS
        "."
        ${PORT_DIR}
        ${REPO_ROOT}
        ${REPO_ROOT}/ports/host_native      # Pico SDK header shims
    REQUIRES
        driver
        esp_timer
        esp_psram
        nvs_flash
        spi_flash
        esp_partition
        vfs
)

# Force-include the platform header for every TU in this component.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -include esp32_platform.h
    -funsigned-char
    # Match the warning posture host_native uses. IDF defaults
    # -Werror on, so silence the new warnings (format-truncation,
    # maybe-uninitialized) that gcc 14 / xtensa-gcc surface.
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
target_link_options(${COMPONENT_LIB} INTERFACE
    -Wl,--allow-multiple-definition
)
