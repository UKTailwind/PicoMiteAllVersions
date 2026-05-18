# ports/host_native/Makefile — native host build (mmbasic_test + mmbasic_sim).
#
# This is the canonical build for the native host port. Objects land
# under ports/host_native/build/, alongside the native test binary,
# simulator binary, and WASM network proxy helper.

CC = gcc
PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
SIM_VM_DIR := $(REPO_ROOT)/ports/vm_sys_sim
PICO_COMPAT_DIR := $(REPO_ROOT)/ports/pico_sdk_compat
BUILD_DIR := $(PORT_DIR)/build
VERSION_FILE := $(REPO_ROOT)/VERSION
MMBA_RELEASE_VERSION ?= $(shell sed -n '1p;q;' $(VERSION_FILE) 2>/dev/null)
MMBA_RELEASE_VERSION := $(strip $(MMBA_RELEASE_VERSION))
ifeq ($(MMBA_RELEASE_VERSION),)
$(error MMBA_RELEASE_VERSION is empty; set it in the environment or create $(VERSION_FILE))
endif
THIRD_PARTY_CFLAGS := -I$(REPO_ROOT)/third_party/aes \
                      -I$(REPO_ROOT)/third_party/cjson \
                      -I$(REPO_ROOT)/third_party/dr_libs \
                      -I$(REPO_ROOT)/third_party/fatfs \
                      -I$(REPO_ROOT)/third_party/hxcmod \
                      -I$(REPO_ROOT)/third_party/littlefs \
                      -I$(REPO_ROOT)/third_party/picojpeg \
                      -I$(REPO_ROOT)/third_party/regex \
                      -I$(REPO_ROOT)/third_party/upng

# Compiler-table sizes come from ports/host_native/port_config.h
# (which pulls in ports/bc_tables_host.h).  No build-flag dispatch.

CFLAGS = -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -DMMBASIC_HOST -DFF_MAX_LFN_LARGE \
         -DMMBA_RELEASE_VERSION=\"$(MMBA_RELEASE_VERSION)\" \
         -include host_platform.h \
         $(THIRD_PARTY_CFLAGS) \
         -I$(PORT_DIR) -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT)/core/mmbasic -I$(REPO_ROOT)/runtime/vm -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT) \
         -MMD -MP

LDFLAGS = -lm

# Core MMBasic sources (language runtime)
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c core/mmbasic/Memory.c core/mmbasic/Editor.c core/mmbasic/MMBasic_Prompt.c core/mmbasic/MMBasic_Print.c core/mmbasic/MMBasic_REPL.c
CORE_SRCS += shared/gfx/gfx_box_shared.c shared/gfx/gfx_circle_shared.c shared/gfx/gfx_line_shared.c shared/gfx/gfx_pixel_shared.c shared/gfx/gfx_cls_shared.c shared/gfx/gfx_text_shared.c shared/gfx/gfx_console_shared.c
CORE_SRCS += shared/mmbasic/mm_misc_shared.c core/mmbasic/OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_http_file.c
CORE_SRCS += mm_net_http_page.c
CORE_SRCS += mm_net_mqtt_cmd.c
CORE_SRCS += mm_net_mqtt_hal_cmd.c
CORE_SRCS += mm_net_mqtt_wire.c
CORE_SRCS += mm_net_ntp.c
CORE_SRCS += mm_net_telnet_rx.c
CORE_SRCS += mm_net_ntp_hal.c
CORE_SRCS += mm_net_lifecycle.c
CORE_SRCS += mm_net_options.c
CORE_SRCS += mm_net_service.c
CORE_SRCS += mm_net_state.c
CORE_SRCS += mm_net_tcp_client_cmd.c
CORE_SRCS += mm_net_tcp_server_cmd.c
CORE_SRCS += mm_net_tftp.c
CORE_SRCS += mm_net_transmit_cmd.c
CORE_SRCS += mm_net_udp_cmd.c
CORE_SRCS += mm_net_web_cmd.c
CORE_SRCS += mm_net_wifi_cmd.c
CORE_SRCS += core/mmbasic/Draw.c shared/gfx/RGB121.c shared/gfx/Tilemap.c core/mmbasic/FileIO.c shared/audio/Audio.c drivers/bmp_decoder/BmpDecoder.c third_party/regex/re.c third_party/picojpeg/picojpeg.c
# Stubs + host-only subsystems
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c hal_psram_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

# Bytecode VM sources
BC_SRCS = runtime/vm/bc_alloc.c runtime/vm/bc_bridge.c runtime/vm/vm_host_fat.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/fatfs/ffsystem.c \
          runtime/vm/vm_sys_input.c runtime/vm/vm_sys_graphics.c runtime/vm/vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          runtime/vm/bc_source.c runtime/vm/bc_compiler_core.c runtime/vm/bc_vm.c runtime/vm/bc_runtime.c runtime/vm/bc_debug.c

# Port-native sources (this directory).
HOST_SRCS = host_runtime.c host_fastgfx.c host_fastgfx_resync.c host_fs_shims.c host_peripheral_stubs.c host_web.c \
            host_main.c host_terminal.c host_fs.c host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c host_fb.c host_keys.c \
            host_bc_runtime_noop.c \
            hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c \
            hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c \
            hal_net_posix.c
WEB_CONSOLE_SRCS = web_console_protocol.c web_console_input.c

HAL_GENERIC_SRCS = hal_watchdog_noop.c
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_input_noop.c runtime_console_escdecode.c runtime_getline.c runtime_terminal_hooks_noop.c runtime_abort.c runtime_interrupt.c runtime_filesystem_defaults.c runtime_cmd_load_post_cleanup.c calendar_bare.c

SRCS = $(HOST_SRCS) $(WEB_CONSOLE_SRCS) $(CORE_SRCS) $(BC_SRCS) $(HAL_GENERIC_SRCS) $(RUNTIME_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(REPO_ROOT)/ports/host_native/build/mmbasic_test
NET_HAL_TEST = $(BUILD_DIR)/host_net_hal_test
WEB_CONSOLE_PROTOCOL_TEST = $(BUILD_DIR)/web_console_protocol_test
WEB_CONSOLE_DISPLAY_TEST = $(BUILD_DIR)/web_console_display_test
WEB_CONSOLE_INPUT_TEST = $(BUILD_DIR)/web_console_input_test
MM_NET_WEBSOCKET_TEST = $(BUILD_DIR)/mm_net_websocket_test
MM_NET_WEBSOCKET_LOOPBACK_TEST = $(BUILD_DIR)/mm_net_websocket_loopback_test
WASM_PROXY_TARGET = $(REPO_ROOT)/ports/host_native/build/wasm_network_proxy

# --- Simulator target ---------------------------------------------------
SIM_TARGET  = $(REPO_ROOT)/ports/host_native/build/mmbasic_sim
SIM_EXTRA_SRCS = host_sim_server.c vendor/mongoose.c
SIM_OBJS = $(OBJS) $(addprefix $(BUILD_DIR)/sim/,$(SIM_EXTRA_SRCS:.c=.o)) \
           $(addprefix $(BUILD_DIR)/sim/,$(SRCS:.c=.o))
SIM_CFLAGS  = $(CFLAGS) -DMMBASIC_SIM -pthread
SIM_LDFLAGS = $(LDFLAGS) -pthread

MONGOOSE_DIR = $(PORT_DIR)/vendor

# --- WASM companion network proxy ---------------------------------------
WASM_PROXY_SRCS = wasm_proxy_main.c wasm_proxy_protocol.c wasm_proxy_http.c wasm_proxy_net.c vendor/mongoose.c
WASM_PROXY_OBJS = $(addprefix $(BUILD_DIR)/wasm_proxy/,$(WASM_PROXY_SRCS:.c=.o))
WASM_PROXY_DEPS = $(WASM_PROXY_OBJS:.o=.d)
WASM_PROXY_CFLAGS = $(CFLAGS) -pthread
WASM_PROXY_LDFLAGS = $(LDFLAGS) -pthread

$(OBJS) $(SIM_OBJS) $(WASM_PROXY_OBJS): $(VERSION_FILE)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

net-hal-test: $(NET_HAL_TEST)
	$(NET_HAL_TEST)

web-console-protocol-test: $(WEB_CONSOLE_PROTOCOL_TEST) $(WEB_CONSOLE_DISPLAY_TEST) $(WEB_CONSOLE_INPUT_TEST)
	$(WEB_CONSOLE_PROTOCOL_TEST)
	$(WEB_CONSOLE_DISPLAY_TEST)
	$(WEB_CONSOLE_INPUT_TEST)

websocket-test: $(MM_NET_WEBSOCKET_TEST) $(MM_NET_WEBSOCKET_LOOPBACK_TEST)
	$(MM_NET_WEBSOCKET_TEST)
	$(MM_NET_WEBSOCKET_LOOPBACK_TEST)

$(NET_HAL_TEST): $(PORT_DIR)/hal_net_posix_test.c $(PORT_DIR)/hal_net_posix.c \
                 $(REPO_ROOT)/shared/net/mm_net_http.c \
                 $(REPO_ROOT)/shared/net/mm_net_http_file.c \
                 $(REPO_ROOT)/shared/net/mm_net_mqtt_wire.c \
                 $(REPO_ROOT)/shared/net/mm_net_ntp.c \
                 $(REPO_ROOT)/shared/net/mm_net_ntp_hal.c \
                 $(REPO_ROOT)/shared/net/mm_net_tftp.c \
                 $(REPO_ROOT)/hal/hal_net.h $(REPO_ROOT)/shared/net/mm_net_http.h \
                 $(REPO_ROOT)/shared/net/mm_net_http_file.h \
                 $(REPO_ROOT)/shared/net/mm_net_ntp.h $(REPO_ROOT)/shared/net/mm_net_ntp_hal.h \
                 $(REPO_ROOT)/shared/net/mm_net_tftp.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -DMMBASIC_HOST -I$(PORT_DIR) -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT) \
	      -o $@ $(PORT_DIR)/hal_net_posix_test.c $(PORT_DIR)/hal_net_posix.c \
	      $(REPO_ROOT)/shared/net/mm_net_http.c \
	      $(REPO_ROOT)/shared/net/mm_net_http_file.c \
	      $(REPO_ROOT)/shared/net/mm_net_mqtt_wire.c \
	      $(REPO_ROOT)/shared/net/mm_net_ntp.c \
	      $(REPO_ROOT)/shared/net/mm_net_ntp_hal.c \
	      $(REPO_ROOT)/shared/net/mm_net_tftp.c \
	      -pthread $(LDFLAGS)

$(WEB_CONSOLE_PROTOCOL_TEST): $(REPO_ROOT)/drivers/web_console/web_console_protocol_test.c \
                             $(REPO_ROOT)/drivers/web_console/web_console_protocol.c \
                             $(REPO_ROOT)/drivers/web_console/web_console_protocol.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -I$(REPO_ROOT) -I$(REPO_ROOT)/drivers/web_console \
	      -o $@ $(REPO_ROOT)/drivers/web_console/web_console_protocol_test.c \
	      $(REPO_ROOT)/drivers/web_console/web_console_protocol.c $(LDFLAGS)

$(WEB_CONSOLE_INPUT_TEST): $(REPO_ROOT)/drivers/web_console/web_console_input_test.c \
                          $(REPO_ROOT)/drivers/web_console/web_console_input.c \
                          $(REPO_ROOT)/drivers/web_console/web_console_input.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -I$(REPO_ROOT) -I$(REPO_ROOT)/drivers/web_console \
	      -o $@ $(REPO_ROOT)/drivers/web_console/web_console_input_test.c \
	      $(REPO_ROOT)/drivers/web_console/web_console_input.c $(LDFLAGS)

$(WEB_CONSOLE_DISPLAY_TEST): $(REPO_ROOT)/drivers/web_console/web_console_display_test.c \
                            $(REPO_ROOT)/drivers/web_console/web_console_display.c \
                            $(REPO_ROOT)/drivers/web_console/web_console_display.h \
                            $(REPO_ROOT)/drivers/web_console/web_console_protocol.c \
                            $(REPO_ROOT)/drivers/web_console/web_console_protocol.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -I$(REPO_ROOT) -I$(REPO_ROOT)/drivers/web_console \
	      -o $@ $(REPO_ROOT)/drivers/web_console/web_console_display_test.c \
	      $(REPO_ROOT)/drivers/web_console/web_console_display.c \
	      $(REPO_ROOT)/drivers/web_console/web_console_protocol.c $(LDFLAGS)

$(MM_NET_WEBSOCKET_TEST): $(REPO_ROOT)/shared/net/mm_net_websocket_test.c \
                         $(REPO_ROOT)/shared/net/mm_net_websocket.c \
                         $(REPO_ROOT)/shared/net/mm_net_websocket.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -I$(REPO_ROOT) \
	      -o $@ $(REPO_ROOT)/shared/net/mm_net_websocket_test.c \
	      $(REPO_ROOT)/shared/net/mm_net_websocket.c $(LDFLAGS)

$(MM_NET_WEBSOCKET_LOOPBACK_TEST): $(PORT_DIR)/mm_net_websocket_loopback_test.c \
                                  $(REPO_ROOT)/shared/net/mm_net_websocket.c \
                                  $(REPO_ROOT)/shared/net/mm_net_websocket.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
	      -Wno-unused-but-set-variable -Wno-misleading-indentation \
	      -funsigned-char -I$(REPO_ROOT) \
	      -o $@ $(PORT_DIR)/mm_net_websocket_loopback_test.c \
	      $(REPO_ROOT)/shared/net/mm_net_websocket.c -pthread $(LDFLAGS)

sim: $(SIM_TARGET)

wasm-proxy: $(WASM_PROXY_TARGET)

$(WASM_PROXY_TARGET): $(WASM_PROXY_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(WASM_PROXY_CFLAGS) -o $@ $^ $(WASM_PROXY_LDFLAGS)

# The sim build recompiles EVERY source with -DMMBASIC_SIM, not just a
# few — gfx_console_shared, host_main, etc. all check `#ifdef
# MMBASIC_SIM` to swap in the WebSocket-backed display path. Objects
# land under build/sim/ so the test-harness build's objects aren't
# clobbered.
$(SIM_TARGET): $(addprefix $(BUILD_DIR)/sim/,$(SRCS:.c=.o)) \
               $(addprefix $(BUILD_DIR)/sim/,$(SIM_EXTRA_SRCS:.c=.o))
	$(CC) $(SIM_CFLAGS) -o $@ $^ $(SIM_LDFLAGS)

# --- Compile rules ------------------------------------------------------
# Port-native sources in this directory.
$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/wasm_proxy/%.o: $(PORT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(WASM_PROXY_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SIM_VM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Core MMBasic sources.
$(BUILD_DIR)/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/third_party/%.o: $(REPO_ROOT)/third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Shared HAL impls (no-op watchdog, etc).
$(BUILD_DIR)/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Stubs / drivers that host links.
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/audio_mp3/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/web_console/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/spi_lcd/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/display_merge/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/vga_pio/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gfx_3d/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/upng_sprite/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gui_touch/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/gui_controls/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/i2c_picocalc_kbd/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/heartbeat/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/psram_heap/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/editor_console/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/display_pixel_host/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/net_stub/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/calendar/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Sim build: all sources rebuilt with -DMMBASIC_SIM ------------------
$(BUILD_DIR)/sim/%.o: $(PORT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(SIM_VM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/third_party/%.o: $(REPO_ROOT)/third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/audio_mp3/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/web_console/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/spi_lcd/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/display_merge/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/vga_pio/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/gfx_3d/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/upng_sprite/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/gui_touch/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/gui_controls/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/i2c_picocalc_kbd/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/heartbeat/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/psram_heap/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/editor_console/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/display_pixel_host/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/net_stub/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<
$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/calendar/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

# Mongoose (sim-only).
$(BUILD_DIR)/sim/vendor/mongoose.o: $(MONGOOSE_DIR)/mongoose.c $(MONGOOSE_DIR)/mongoose.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -I$(MONGOOSE_DIR) -Wno-unused-parameter -Wno-sign-compare -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(SIM_TARGET) $(WASM_PROXY_TARGET) \
	       $(WEB_CONSOLE_PROTOCOL_TEST) $(MM_NET_WEBSOCKET_TEST) \
	       $(MM_NET_WEBSOCKET_LOOPBACK_TEST)

-include $(DEPS) $(WASM_PROXY_DEPS)

.PHONY: all clean sim net-hal-test web-console-protocol-test websocket-test wasm-proxy
