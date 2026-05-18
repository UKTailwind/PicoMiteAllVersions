# ports/host_wasm/Makefile — WASM/emscripten build for the PicoMite host port.
#
# Mirrors ports/host_native/Makefile's shared/VM source list, swapping
# three native modules for WASM-specific ones:
#
#   host_terminal.c  ->  host_wasm_console.c   (input-only key ring)
#   host_main.c      ->  host_wasm_main.c      (wasm_boot entry)
#   (new)                host_wasm_canvas.c    (JS-facing exports)
#
# host_sim_server.c / vendor/mongoose.c are NOT linked — JS calls the
# exported C functions directly via Module.ccall, no HTTP/WS layer.
#
# Objects land under ports/host_wasm/wasm_obj/. Final artefacts
# (picomite.{mjs,wasm,data}) land under ports/host_wasm/web/.

EMCC ?= emcc
PORT_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT  := $(abspath $(PORT_DIR)/../..)
NATIVE_DIR := $(REPO_ROOT)/ports/host_native
PICO_COMPAT_DIR := $(REPO_ROOT)/ports/pico_sdk_compat
SIM_VM_DIR := $(REPO_ROOT)/ports/vm_sys_sim
WASM_DIR   := $(PORT_DIR)
BUILD_DIR  := $(WASM_DIR)/wasm_obj
WEB_DIR    := $(REPO_ROOT)/ports/host_wasm/web
THIRD_PARTY_CFLAGS := -I$(REPO_ROOT)/third_party/aes \
                      -I$(REPO_ROOT)/third_party/cjson \
                      -I$(REPO_ROOT)/third_party/dr_libs \
                      -I$(REPO_ROOT)/third_party/fatfs \
                      -I$(REPO_ROOT)/third_party/hxcmod \
                      -I$(REPO_ROOT)/third_party/littlefs \
                      -I$(REPO_ROOT)/third_party/picojpeg \
                      -I$(REPO_ROOT)/third_party/regex \
                      -I$(REPO_ROOT)/third_party/upng

# Compiler-table sizing lives in bytecode.h keyed on MMBASIC_WASM.  No
# DEVICE_SIM knob here — the WASM build is deterministic regardless of
# environment.  The runtime heap profile (128 KB ... 8 MB) is still
# user-selectable at run time via wasm_set_heap_size().

CFLAGS = -O2 -std=gnu11 -funsigned-char \
         -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces \
         -DMMBASIC_HOST -DMMBASIC_WASM -DFF_MAX_LFN_LARGE \
         -include host_platform.h \
         $(THIRD_PARTY_CFLAGS) \
         -I$(WASM_DIR) -I$(NATIVE_DIR) -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT)/core/mmbasic -I$(REPO_ROOT)/runtime/vm -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT) \
         -MMD -MP \
         -pthread
# `-pthread` here makes the wasm module's linear memory a
# SharedArrayBuffer so main thread and the dedicated interpreter
# worker share a single HEAPU8 view (zero-copy framebuffer).
# MMBasic relies on gcc-style tentative-definition merging (e.g. `int
# gui_bcolour;` in core/mmbasic/Draw.c coexisting with `int gui_bcolour = 0;` in
# host_runtime.c). Under clang/wasm-ld we can't use -fcommon because
# wasm doesn't support common linkage; we also can't use plain
# -fno-common because that would promote every tentative def to a
# strong one, colliding with the initialised versions. -Wl,--allow-
# multiple-definition asks wasm-ld to take the first symbol it sees and
# ignore the rest — same effective behaviour as gcc's native merge.

LDFLAGS = -pthread -sPTHREAD_POOL_SIZE=1 \
          -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=268435456 \
          -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker \
          -sEXPORTED_FUNCTIONS='["_main","_wasm_boot","_wasm_push_key","_wasm_key_ring_ptr","_wasm_key_ring_head_ptr","_wasm_key_ring_tail_ptr","_wasm_key_ring_size","_wasm_break","_wasm_framebuffer_ptr","_wasm_framebuffer_width","_wasm_framebuffer_height","_wasm_framebuffer_generation","_wasm_framebuffer_generation_ptr","_wasm_framebuffer_config_generation_ptr","_wasm_vsync_counter_ptr","_wasm_set_framebuffer_size","_wasm_set_mode_resolution","_wasm_set_heap_size","_wasm_set_slowdown_us","_wasm_set_proxy_mode","_wasm_set_proxy_http_capability","_wasm_set_proxy_tcp_stream_capability","_wasm_set_proxy_tcp_server_capability","_wasm_set_proxy_udp_capability","_wasm_set_proxy_ntp_capability","_wasm_set_proxy_tftp_capability","_wasm_set_proxy_telnet_capability","_wasm_set_proxy_mqtt_plain_capability","_wasm_set_proxy_tftp_port","_wasm_set_proxy_telnet_port","_wasm_proxy_mode","_malloc","_free"]' \
          -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","HEAPU8","HEAPU32","FS","IDBFS"]' \
          -sFETCH=1 \
          -sFORCE_FILESYSTEM=1 \
          -lidbfs.js \
          --preload-file $(REPO_ROOT)/ports/host_wasm/demos@/bundle \
          -Wl,--allow-multiple-definition

# Core MMBasic sources — mirror ports/host_native/Makefile CORE_SRCS.
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c \
            core/mmbasic/Memory.c core/mmbasic/Editor.c \
            core/mmbasic/MMBasic_Prompt.c core/mmbasic/MMBasic_Print.c core/mmbasic/MMBasic_REPL.c \
            shared/gfx/gfx_box_shared.c shared/gfx/gfx_circle_shared.c shared/gfx/gfx_line_shared.c \
            shared/gfx/gfx_pixel_shared.c shared/gfx/gfx_cls_shared.c shared/gfx/gfx_text_shared.c \
            shared/gfx/gfx_console_shared.c \
            shared/mmbasic/mm_misc_shared.c core/mmbasic/OptionCommands.c \
            display_state.c pin_state.c option_state.c audio_state.c \
            mm_net_interrupts.c \
            mm_net_http.c \
            mm_net_http_file.c \
            mm_net_http_page.c \
            mm_net_mqtt_cmd.c \
            mm_net_mqtt_hal_cmd.c \
            mm_net_mqtt_wire.c \
            mm_net_ntp.c \
            mm_net_telnet_rx.c \
            mm_net_ntp_hal.c \
            mm_net_lifecycle.c \
            mm_net_options.c \
            mm_net_service.c \
            mm_net_state.c \
            mm_net_tcp_client_cmd.c \
            mm_net_tcp_server_cmd.c \
            mm_net_tftp.c \
            mm_net_transmit_cmd.c \
            mm_net_udp_cmd.c \
            mm_net_web_cmd.c \
            mm_net_wifi_cmd.c \
            core/mmbasic/Draw.c shared/gfx/RGB121.c shared/gfx/Tilemap.c core/mmbasic/FileIO.c shared/audio/Audio.c drivers/bmp_decoder/BmpDecoder.c third_party/regex/re.c third_party/picojpeg/picojpeg.c
# Stubs + host-only subsystems — match ports/host_native/Makefile.
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c hal_psram_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

# Bytecode VM sources.
BC_SRCS = runtime/vm/bc_alloc.c runtime/vm/bc_bridge.c runtime/vm/vm_host_fat.c \
          third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/fatfs/ffsystem.c \
          runtime/vm/vm_sys_input.c runtime/vm/vm_sys_graphics.c runtime/vm/vm_sys_time.c \
          vm_sys_pin_sim.c vm_sys_file_sim.c \
          runtime/vm/bc_source.c runtime/vm/bc_compiler_core.c \
          runtime/vm/bc_vm.c runtime/vm/bc_runtime.c runtime/vm/bc_debug.c

# Host HAL — the portable subset that works under emcc unchanged, plus
# the WASM-specific trio that replaces host_terminal.c / host_main.c
# and adds the JS-facing canvas bridge.
HOST_PORTABLE_SRCS = host_runtime.c host_fastgfx.c host_fs_shims.c \
                     host_peripheral_stubs.c host_fs.c \
                     host_sim_emit_stub.c \
                     host_time.c host_fb.c host_keys.c host_bc_runtime_noop.c \
                     hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c hal_filesystem_host.c \
                     hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c
HOST_WASM_SRCS = host_wasm_main.c host_wasm_console.c host_wasm_canvas.c \
                 host_wasm_audio.c host_wasm_web.c host_sim_slowdown.c \
                 host_fastgfx_resync.c host_wasm_mode.c

HAL_GENERIC_SRCS = hal_watchdog_noop.c
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_input_noop.c runtime_console_escdecode.c runtime_getline.c runtime_terminal_hooks_noop.c runtime_abort.c runtime_interrupt.c runtime_filesystem_defaults.c runtime_cmd_load_post_cleanup.c calendar_bare.c

SRCS = $(HOST_PORTABLE_SRCS) $(HOST_WASM_SRCS) $(CORE_SRCS) $(BC_SRCS) $(HAL_GENERIC_SRCS) $(RUNTIME_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

# --preload-file demos@/bundle pulls every .bas from ports/host_wasm/demos/ into
# picomite.data at link time. Listing them as explicit prerequisites
# means `make` relinks when you edit or add a demo.
DEMO_FILES := $(wildcard $(REPO_ROOT)/ports/host_wasm/demos/*.bas)

TARGET = $(WEB_DIR)/picomite.mjs

all: $(TARGET)

$(TARGET): $(OBJS) $(DEMO_FILES)
	@mkdir -p $(WEB_DIR)
	$(EMCC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)
	@$(REPO_ROOT)/tools/check_wasm_symbols.sh

# Per-source-family compile rules (no VPATH on the object side so
# make doesn't rediscover stale .o files if another port's build
# landed objects in an overlapping tree).
#
# WASM_DIR rule comes before NATIVE_DIR so port-specific overrides
# (e.g. host_sim_slowdown.c lives in both — WASM has its own
# accumulator impl) resolve to the WASM copy.
$(BUILD_DIR)/%.o: $(WASM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(NATIVE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SIM_VM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/third_party/%.o: $(REPO_ROOT)/third_party/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/runtime/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/audio_mp3/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/spi_lcd/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/display_merge/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/vga_pio/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gfx_3d/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/upng_sprite/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gui_touch/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gui_controls/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/i2c_picocalc_kbd/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/heartbeat/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/psram_heap/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/editor_console/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/display_pixel_host/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/calendar/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(WEB_DIR)/picomite.mjs $(WEB_DIR)/picomite.wasm $(WEB_DIR)/picomite.data
	@# Transitional multi-profile artefacts from an earlier attempt:
	rm -f $(WEB_DIR)/picomite-*.mjs $(WEB_DIR)/picomite-*.wasm $(WEB_DIR)/picomite-*.data

-include $(DEPS)

.PHONY: all clean
