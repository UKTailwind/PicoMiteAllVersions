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

# Compiler-table sizes come from ports/mmbasic_ansi/port_config.h
# (which pulls in ports/bc_tables_rp2350.h).  No build-flag dispatch.

CFLAGS = -std=gnu11 -g -O2 -Wall -Wno-unused-variable -Wno-unused-function \
         -Wno-unused-but-set-variable -Wno-misleading-indentation \
         -Wno-missing-braces -funsigned-char \
         -DMMBASIC_HOST -DMMBASIC_ANSI -DFF_MAX_LFN_LARGE \
         -DMMBA_RELEASE_VERSION=\"$(MMBA_RELEASE_VERSION)\" \
         -include host_platform.h \
         $(THIRD_PARTY_CFLAGS) \
         -I$(PORT_DIR) -I$(HOST_DIR) -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT)/core/mmbasic -I$(REPO_ROOT)/runtime/vm -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT) \
         -MMD -MP

UNAME_S := $(shell uname -s 2>/dev/null)
ifneq (,$(filter MINGW% MSYS% UCRT% CYGWIN%,$(UNAME_S)))
  IS_WINDOWS := 1
else ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
endif

ifdef IS_WINDOWS
# Static-link libwinpthread + libgcc so the resulting .exe runs on
# any modern Windows without dropping mingw-w64 DLLs alongside it.
# UCRT (api-ms-win-crt-*.dll) ships with Win10+ and stays dynamic.
# -lwinmm provides timeBeginPeriod / timeEndPeriod used to bump
# Windows' timer resolution so --slowdown actually pauses.
LDFLAGS           := -lm -lpthread -lwinmm -static -static-libgcc
HOST_TERMINAL_SRC := host_terminal_win32.c
ANSI_RESIZE_SRC   := ansi_terminal_resize_win32.c
else
LDFLAGS           := -lm -lpthread
HOST_TERMINAL_SRC := host_terminal.c
ANSI_RESIZE_SRC   := ansi_terminal_resize_posix.c

ifeq ($(UNAME_S),Darwin)
  UNIVERSAL ?= 1
  ifeq ($(UNIVERSAL),1)
    CFLAGS  += -arch x86_64 -arch arm64
    LDFLAGS += -arch x86_64 -arch arm64
  endif
endif

endif

# Core MMBasic (same list as host_native — keeps core/mmbasic/Editor.c + REPL + Prompt
# so the interactive REPL and EDIT both work).
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c core/mmbasic/Memory.c core/mmbasic/Editor.c core/mmbasic/MMBasic_Prompt.c core/mmbasic/MMBasic_Print.c core/mmbasic/MMBasic_REPL.c
CORE_SRCS += shared/gfx/gfx_box_shared.c shared/gfx/gfx_circle_shared.c shared/gfx/gfx_line_shared.c shared/gfx/gfx_pixel_shared.c shared/gfx/gfx_cls_shared.c shared/gfx/gfx_text_shared.c shared/gfx/gfx_console_shared.c
CORE_SRCS += shared/mmbasic/mm_misc_shared.c core/mmbasic/OptionCommands.c
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += mm_net_interrupts.c
CORE_SRCS += mm_net_http.c
CORE_SRCS += mm_net_ntp.c
CORE_SRCS += mm_net_telnet_rx.c
CORE_SRCS += mm_net_state.c
CORE_SRCS += core/mmbasic/Draw.c shared/gfx/RGB121.c shared/gfx/Tilemap.c core/mmbasic/FileIO.c shared/audio/Audio.c drivers/bmp_decoder/BmpDecoder.c third_party/regex/re.c third_party/picojpeg/picojpeg.c
CORE_SRCS += audio_mp3_stub.c spi_lcd_mem332_stub.c display_merge_stub.c vga_ops_stub.c
CORE_SRCS += gfx_3d.c upng_sprite_stub.c gui_touch_stub.c gui_controls_stub.c psram_heap_stub.c hal_psram_stub.c i2c_keypad_stub.c heartbeat_stub.c
CORE_SRCS += editor_console_stub.c
CORE_SRCS += display_pixel_host.c

BC_SRCS = runtime/vm/bc_alloc.c runtime/vm/bc_bridge.c runtime/vm/vm_host_fat.c third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/fatfs/ffsystem.c \
          runtime/vm/vm_sys_input.c runtime/vm/vm_sys_graphics.c runtime/vm/vm_sys_time.c vm_sys_pin_sim.c vm_sys_file_sim.c \
          runtime/vm/bc_source.c runtime/vm/bc_compiler_core.c runtime/vm/bc_vm.c runtime/vm/bc_runtime.c runtime/vm/bc_debug.c

# host_native sources reused wholesale. Omit host_main.c (we supply our
# own entry); host_fastgfx.c ships the cmd_fastgfx / cmd_framebuffer /
# bc_fastgfx_* syscalls, so it has to link.
NATIVE_SRCS = host_runtime.c host_fastgfx.c host_fastgfx_resync.c host_fs_shims.c host_peripheral_stubs.c \
              $(HOST_TERMINAL_SRC) host_fs.c host_keyrepeat.c host_sim_audio.c host_sim_slowdown.c host_sim_emit_stub.c host_time.c \
              host_fb.c host_keys.c host_bc_runtime_noop.c \
              host_web_stubs.c \
              hal_flash_host.c hal_time_host.c hal_random_host.c hal_pin_host.c hal_storage_host.c \
              hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c

PORT_SRCS = ansi_main.c ansi_terminal.c ansi_display.c ansi_mode.c $(ANSI_RESIZE_SRC)
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_input_noop.c runtime_console_escdecode.c runtime_getline.c runtime_abort.c runtime_interrupt.c runtime_filesystem_defaults.c runtime_cmd_load_post_cleanup.c calendar_bare.c
HAL_GENERIC_SRCS = hal_watchdog_noop.c

SRCS = $(PORT_SRCS) $(NATIVE_SRCS) $(CORE_SRCS) $(BC_SRCS) $(RUNTIME_SRCS) $(HAL_GENERIC_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

TARGET = $(PORT_DIR)/mmbasic_ansi

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): $(VERSION_FILE)

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

$(BUILD_DIR)/third_party/%.o: $(REPO_ROOT)/third_party/%.c
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
$(BUILD_DIR)/%.o: $(REPO_ROOT)/drivers/calendar/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)

.PHONY: all clean
