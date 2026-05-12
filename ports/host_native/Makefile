# ports/host_native/Makefile — native host build (mmbasic_test + mmbasic_sim).
#
# This is the canonical build for the native host port. Objects land
# under ports/host_native/build/. Binaries still land in the legacy
# host/ location (mmbasic_test, mmbasic_sim) so run_tests.sh,
# buildall.sh, and every existing doc-referenced workflow keeps
# working unchanged during retirement. host/Makefile is now a thin
# delegator that forwards here.

CC = gcc
PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
SIM_VM_DIR := $(REPO_ROOT)/ports/vm_sys_sim
BUILD_DIR := $(PORT_DIR)/build

# Device memory simulation target. Controls compiler/VM array sizes
# AND the heap budget in bc_alloc.c.
DEVICE_SIM ?= rp2040

ifeq ($(DEVICE_SIM),rp2040)
  SIM_FLAGS = -DBC_SIM_RP2040
else ifeq ($(DEVICE_SIM),rp2350)
  SIM_FLAGS = -DBC_SIM_RP2350
else
  SIM_FLAGS =
endif

CFLAGS = -std=gnu11 -g -O0 -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -DMMBASIC_HOST -DFF_MAX_LFN_LARGE $(SIM_FLAGS) \
         -include host_platform.h \
         -I$(PORT_DIR) -I$(REPO_ROOT) \
         -MMD -MP

LDFLAGS = -lm

# Core MMBasic sources (language runtime)
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c Editor.c MMBasic_Prompt.c MMBasic_Print.c MMBasic_REPL.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_http_file.c
CORE_SRCS += mm_net_http_page.c
CORE_SRCS += mm_net_mqtt_cmd.c
CORE_SRCS += mm_net_mqtt_hal_cmd.c
CORE_SRCS += mm_net_mqtt_wire.c
CORE_SRCS += mm_net_ntp.c
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
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c picojpeg.c
# Stubs + host-only subsystems
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

# Bytecode VM sources
BC_SRCS = bc_alloc.c bc_bridge.c vm_host_fat.c ff.c ffunicode.c ffsystem.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          bc_source.c bc_compiler_core.c bc_vm.c bc_runtime.c bc_debug.c

# Port-native sources (this directory).
HOST_SRCS = host_runtime.c host_fastgfx.c host_fastgfx_resync.c host_fs_shims.c host_peripheral_stubs.c host_web.c \
            host_main.c host_terminal.c host_fs.c host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c host_fb.c host_keys.c \
            host_bc_runtime_noop.c host_terminal_hooks_noop.c \
            hal_flash_host.c hal_time_host.c hal_random_host.c hal_watchdog_host.c hal_pin_host.c hal_storage_host.c \
            hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c \
            hal_net_posix.c

SRCS = $(HOST_SRCS) $(CORE_SRCS) $(BC_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(REPO_ROOT)/host/mmbasic_test
NET_HAL_TEST = $(BUILD_DIR)/host_net_hal_test
WASM_PROXY_TARGET = $(REPO_ROOT)/host/wasm_network_proxy

# --- Simulator target ---------------------------------------------------
SIM_TARGET  = $(REPO_ROOT)/host/mmbasic_sim
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

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

net-hal-test: $(NET_HAL_TEST)
	$(NET_HAL_TEST)

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
	      -funsigned-char -DMMBASIC_HOST -I$(PORT_DIR) -I$(REPO_ROOT) \
	      -o $@ $(PORT_DIR)/hal_net_posix_test.c $(PORT_DIR)/hal_net_posix.c \
	      $(REPO_ROOT)/shared/net/mm_net_http.c \
	      $(REPO_ROOT)/shared/net/mm_net_http_file.c \
	      $(REPO_ROOT)/shared/net/mm_net_mqtt_wire.c \
	      $(REPO_ROOT)/shared/net/mm_net_ntp.c \
	      $(REPO_ROOT)/shared/net/mm_net_ntp_hal.c \
	      $(REPO_ROOT)/shared/net/mm_net_tftp.c \
	      -pthread $(LDFLAGS)

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

# Core MMBasic sources in the repo root.
$(BUILD_DIR)/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Stubs / drivers that host links.
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/audio_mp3/%.c
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

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sim/%.o: $(REPO_ROOT)/drivers/audio_mp3/%.c
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

# Mongoose (sim-only). Still in host/vendor/ until that dir retires.
$(BUILD_DIR)/sim/vendor/mongoose.o: $(MONGOOSE_DIR)/mongoose.c $(MONGOOSE_DIR)/mongoose.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -I$(MONGOOSE_DIR) -Wno-unused-parameter -Wno-sign-compare -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(SIM_TARGET) $(WASM_PROXY_TARGET)

-include $(DEPS) $(WASM_PROXY_DEPS)

.PHONY: all clean sim net-hal-test wasm-proxy
