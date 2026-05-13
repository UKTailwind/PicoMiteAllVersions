# ports/mmbasic_stdio/Makefile — pure-stdio MMBasic binary.
#
# This is the HAL litmus test.  The link line below deliberately omits
# Editor.c, MMBasic_REPL.c, MMBasic_Prompt.c, and every display /
# audio / pin driver.  If the core is HAL-clean, we get a working
# interpreter.  Missing symbols = HAL leak; fix in core, not here.

CC = gcc
PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
SIM_VM_DIR := $(REPO_ROOT)/ports/vm_sys_sim
BUILD_DIR := $(PORT_DIR)/build

# Mirror host_native's DEVICE_SIM knob so array sizes / heap budget
# stay consistent.
DEVICE_SIM ?= rp2040
ifeq ($(DEVICE_SIM),rp2040)
  SIM_FLAGS = -DBC_SIM_RP2040
else ifeq ($(DEVICE_SIM),rp2350)
  SIM_FLAGS = -DBC_SIM_RP2350
else
  SIM_FLAGS =
endif

CFLAGS = -std=gnu11 -g -Os -flto -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -ffunction-sections -fdata-sections \
         -DMMBASIC_HOST -DMMBASIC_STDIO -DFF_MAX_LFN_LARGE $(SIM_FLAGS) \
         -include host_platform.h \
         -I$(PORT_DIR) -I$(REPO_ROOT)/ports/host_native -I$(REPO_ROOT) \
         -MMD -MP

# Dead-code stripping: macOS ld uses -dead_strip; GNU ld uses --gc-sections.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDFLAGS = -lm -flto -Wl,-dead_strip
else
  LDFLAGS = -lm -flto -Wl,--gc-sections
endif

# Core MMBasic — same list as host_native minus Editor.c,
# MMBasic_REPL.c, MMBasic_Prompt.c.  Keep shared gfx / state so Draw.c
# etc. link, but the display goes through hal_display stubs.
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c MMBasic_Print.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_ntp.c
CORE_SRCS += mm_net_state.c
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c picojpeg.c
# Stubs inherited from host_native (dr_mp3, SPI-LCD, VGA, GUI, 3D, uPNG).
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

# Bytecode VM — the same set the native host links (so FRUN works).
BC_SRCS = bc_alloc.c bc_bridge.c vm_host_fat.c ff.c ffunicode.c ffsystem.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          bc_source.c bc_compiler_core.c bc_vm.c bc_runtime.c bc_debug.c

# Port-native files reused wholesale — the HAL impls + runtime state
# container.  host_fb / host_fastgfx / host_terminal / host_main are
# deliberately omitted: the stdio port has no framebuffer, no raw tty,
# no argv-parsing loader (main.c here replaces it).
NATIVE_SRCS = host_runtime.c host_fs_shims.c host_peripheral_stubs.c host_fs.c \
              host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c host_keys.c \
              host_bc_runtime_noop.c host_terminal_hooks_noop.c host_web_stubs.c \
              hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c \
              hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c

PORT_SRCS = main.c stdio_runtime.c

HAL_GENERIC_SRCS = hal_watchdog_noop.c

SRCS = $(PORT_SRCS) $(NATIVE_SRCS) $(CORE_SRCS) $(BC_SRCS) $(HAL_GENERIC_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(PORT_DIR)/mmbasic_stdio

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/ports/host_native/%.c
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

$(BUILD_DIR)/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(REPO_ROOT)/shared/net/%.c
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
