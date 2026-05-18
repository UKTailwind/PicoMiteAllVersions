# ports/mmbasic_stdio/Makefile — pure-stdio MMBasic binary.
#
# This is the HAL litmus test.  The link line below deliberately omits
# core/mmbasic/Editor.c, core/mmbasic/MMBasic_REPL.c, core/mmbasic/MMBasic_Prompt.c, and every display /
# audio / pin driver.  If the core is HAL-clean, we get a working
# interpreter.  Missing symbols = HAL leak; fix in core, not here.

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
ifneq ($(filter v%,$(MMBA_RELEASE_VERSION)),)
$(error MMBA_RELEASE_VERSION must not start with v; use e.g. 1.0.1)
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

# Compiler-table sizes come from ports/host_native/port_config.h via
# the include path.  No build-flag dispatch.

CFLAGS = -std=gnu11 -g -Os -flto -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -ffunction-sections -fdata-sections \
         -DMMBASIC_HOST -DMMBASIC_STDIO -DFF_MAX_LFN_LARGE \
         -DMMBA_RELEASE_VERSION=\"$(MMBA_RELEASE_VERSION)\" \
         -include host_platform.h \
         $(THIRD_PARTY_CFLAGS) \
         -I$(PORT_DIR) -I$(REPO_ROOT)/ports/host_native -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT)/core/mmbasic -I$(REPO_ROOT)/runtime/vm -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT) \
         -MMD -MP

# Dead-code stripping: macOS ld uses -dead_strip; GNU ld uses --gc-sections.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LDFLAGS = -lm -flto -Wl,-dead_strip
else
  LDFLAGS = -lm -flto -Wl,--gc-sections
endif

# Core MMBasic — same list as host_native minus core/mmbasic/Editor.c,
# core/mmbasic/MMBasic_REPL.c, MMBasic_Prompt.c.  Keep shared gfx / state so core/mmbasic/Draw.c
# etc. link, but the display goes through hal_display stubs.
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c core/mmbasic/Memory.c core/mmbasic/MMBasic_Print.c
CORE_SRCS += shared/gfx/gfx_box_shared.c shared/gfx/gfx_circle_shared.c shared/gfx/gfx_line_shared.c shared/gfx/gfx_pixel_shared.c shared/gfx/gfx_cls_shared.c shared/gfx/gfx_text_shared.c shared/gfx/gfx_console_shared.c
CORE_SRCS += shared/mmbasic/mm_misc_shared.c core/mmbasic/OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_ntp.c
CORE_SRCS += mm_net_telnet_rx.c
CORE_SRCS += mm_net_state.c
CORE_SRCS += core/mmbasic/Draw.c shared/gfx/RGB121.c shared/gfx/Tilemap.c core/mmbasic/FileIO.c shared/audio/Audio.c drivers/bmp_decoder/BmpDecoder.c third_party/regex/re.c third_party/picojpeg/picojpeg.c
# Stubs inherited from host_native (dr_mp3, SPI-LCD, VGA, GUI, 3D, uPNG).
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c hal_psram_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

# Bytecode VM — the same set the native host links (so FRUN works).
BC_SRCS = runtime/vm/bc_alloc.c runtime/vm/bc_bridge.c runtime/vm/vm_host_fat.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/fatfs/ffsystem.c \
          runtime/vm/vm_sys_input.c runtime/vm/vm_sys_graphics.c runtime/vm/vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          runtime/vm/bc_source.c runtime/vm/bc_compiler_core.c runtime/vm/bc_vm.c runtime/vm/bc_runtime.c runtime/vm/bc_debug.c

# Port-native files reused wholesale — the HAL impls + runtime state
# container.  host_fb / host_fastgfx / host_terminal / host_main are
# deliberately omitted: the stdio port has no framebuffer, no raw tty,
# no argv-parsing loader (main.c here replaces it).
NATIVE_SRCS = host_runtime.c host_fs_shims.c host_peripheral_stubs.c host_fs.c \
              host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c host_keys.c \
              host_bc_runtime_noop.c host_web_stubs.c \
              hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c \
              hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c

PORT_SRCS = main.c stdio_runtime.c

HAL_GENERIC_SRCS = hal_watchdog_noop.c
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_input_noop.c runtime_console_escdecode.c runtime_getline.c runtime_terminal_hooks_noop.c runtime_abort.c runtime_interrupt.c runtime_filesystem_defaults.c runtime_cmd_load_post_cleanup.c calendar_bare.c

SRCS = $(PORT_SRCS) $(NATIVE_SRCS) $(CORE_SRCS) $(BC_SRCS) $(HAL_GENERIC_SRCS) $(RUNTIME_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(PORT_DIR)/mmbasic_stdio

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): $(VERSION_FILE)

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

$(BUILD_DIR)/third_party/%.o: $(REPO_ROOT)/third_party/%.c
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

$(BUILD_DIR)/%.o: $(REPO_ROOT)/runtime/%.c
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
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/calendar/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)

.PHONY: all clean
