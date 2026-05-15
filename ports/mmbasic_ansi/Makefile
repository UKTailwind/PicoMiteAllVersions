# ports/mmbasic_ansi/Makefile — terminal half-block MMBasic runner.
#
# Reuses host_native's HAL implementations and core sources. The port
# adds three files: ansi_main.c (entry + REPL), ansi_terminal.c (alt
# screen + raw-mode glue), ansi_display.c (framebuffer → 24-bit
# truecolor half-block renderer). No text ever goes to stdout —
# OptionConsole is forced to SCREEN so all BASIC output rasterises
# into host_framebuffer, which the render thread paints to the
# terminal as Unicode ▀ glyphs.

CC = gcc
PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
HOST_DIR  := $(REPO_ROOT)/ports/host_native
PICO_COMPAT_DIR := $(REPO_ROOT)/ports/pico_sdk_compat
SIM_VM_DIR := $(REPO_ROOT)/ports/vm_sys_sim
BUILD_DIR := $(PORT_DIR)/build

DEVICE_SIM ?= rp2350
ifeq ($(DEVICE_SIM),rp2040)
  SIM_FLAGS = -DBC_SIM_RP2040
else ifeq ($(DEVICE_SIM),rp2350)
  SIM_FLAGS = -DBC_SIM_RP2350
else
  SIM_FLAGS =
endif

CFLAGS = -std=gnu11 -g -O2 -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -DMMBASIC_HOST -DMMBASIC_ANSI -DFF_MAX_LFN_LARGE $(SIM_FLAGS) \
         -include host_platform.h \
         -I$(PORT_DIR) -I$(HOST_DIR) -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT) \
         -MMD -MP

LDFLAGS = -lm -lpthread

# Core MMBasic (same list as host_native — keeps Editor.c + REPL + Prompt
# so the interactive REPL and EDIT both work).
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c Editor.c MMBasic_Prompt.c MMBasic_Print.c MMBasic_REPL.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_ntp.c
CORE_SRCS += mm_net_telnet_rx.c
CORE_SRCS += mm_net_state.c
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c picojpeg.c
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c hal_psram_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

BC_SRCS = bc_alloc.c bc_bridge.c vm_host_fat.c ff.c ffunicode.c ffsystem.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          bc_source.c bc_compiler_core.c bc_vm.c bc_runtime.c bc_debug.c

# host_native sources reused wholesale. Omit host_main.c (we supply our
# own entry); host_fastgfx.c ships the cmd_fastgfx / cmd_framebuffer /
# bc_fastgfx_* syscalls, so it has to link.
NATIVE_SRCS = host_runtime.c host_fastgfx.c host_fastgfx_resync.c host_fs_shims.c host_peripheral_stubs.c \
              host_terminal.c host_fs.c host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c \
              host_fb.c host_keys.c host_bc_runtime_noop.c \
              host_web_stubs.c \
              hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c \
              hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c

PORT_SRCS = ansi_main.c ansi_terminal.c ansi_display.c
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_input_noop.c runtime_console_escdecode.c runtime_getline.c runtime_terminal_hooks_noop.c runtime_abort.c runtime_interrupt.c runtime_filesystem_defaults.c
HAL_GENERIC_SRCS = hal_watchdog_noop.c

SRCS = $(PORT_SRCS) $(NATIVE_SRCS) $(CORE_SRCS) $(BC_SRCS) $(RUNTIME_SRCS) $(HAL_GENERIC_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(PORT_DIR)/mmbasic_ansi

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(HOST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(SIM_VM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/shared/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

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

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)

.PHONY: all clean
