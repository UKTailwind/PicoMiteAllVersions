# Host build for MMBasic interpreter + bytecode VM testing
#
# Builds a native macOS/Linux executable that can run BASIC programs
# through both the old interpreter and the new bytecode VM for comparison.

CC = gcc

# Device memory simulation target.  Controls compiler/VM array sizes AND
# the heap budget in bc_alloc.c.  Values: rp2040 (default), rp2350, host.
# Usage:  make DEVICE_SIM=rp2350
#         BC_HEAP_LIMIT=307200 ./run_tests.sh   (runtime override)
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
         -DPICOMITE -DMMBASIC_HOST $(SIM_FLAGS) \
         -include host_platform.h \
         -I. -I.. -I../ports/host_native \
         -MMD -MP

LDFLAGS = -lm

# Source directory
VPATH = ..:../core/state:../drivers/audio_mp3:../drivers/spi_lcd:../drivers/display_merge:../drivers/vga_pio:../drivers/gfx_3d:../drivers/upng_sprite:../drivers/gui_touch:../drivers/psram_heap:../ports/host_native

# Core MMBasic sources (language runtime)
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c Editor.c MMBasic_Prompt.c MMBasic_Print.c MMBasic_REPL.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c
# Cross-cutting state (display, pin, Option, audio globals) shared across
# Draw.c / External.c / FileIO.c / Audio.c.
CORE_SRCS += display_state.c pin_state.c option_state.c audio_state.c
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c picojpeg.c
# dr_mp3 stub: host doesn't link the 100 KB amalgamated decoder. Audio.c
# still includes dr_mp3.h for types — the stub resolves the drmp3_*
# symbols to no-ops.
CORE_SRCS += audio_mp3_stub.c
# SPI-LCD NEXTGEN stubs: host doesn't compile spi_lcd.c at all, but
# Draw.c unconditionally references the MEM332 function pointers in
# restorepanel(). Link the stub file for the symbols.
CORE_SRCS += spi_lcd_nextgen_stub.c
# Merge-pipeline HAL stubs: cmd_refresh / the FASTGFX path / etc. call
# the hal_display_merge_* / hal_display_nextgen_* hooks unconditionally;
# host has no core1 and no DMA, so link the no-op stubs.
CORE_SRCS += display_merge_stub.c
# VGA helper stubs: Draw.c's gfx_cls / ClearScreen call hal_vga_ops_*
# unconditionally.  Host has no QVGA framebuffer so link the no-ops.
CORE_SRCS += vga_ops_stub.c
# 3D polygon / DRAW3D subsystem: Draw.c references closeall3d, cmd_3D,
# fun_3D. Host gets the real impl.
CORE_SRCS += gfx_3d.c
# upng sprite LOADPNG: host doesn't link upng, so ship the stub.
CORE_SRCS += upng_sprite_stub.c
# GUI touch-panel sub-commands: host has no touch panel, stub them.
CORE_SRCS += gui_touch_stub.c
# PSRAM heap driver: host has no QSPI PSRAM, link the no-op stub so
# Memory.c's GetPSMemory / SBitsGet / SBitsSet references resolve.
CORE_SRCS += psram_heap_stub.c

# Bytecode VM sources
BC_SRCS = bc_alloc.c bc_bridge.c vm_host_fat.c ff.c ffunicode.c ffsystem.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c vm_sys_pin_host.c vm_sys_file_host.c bc_source.c bc_compiler_core.c \
          bc_vm.c bc_runtime.c bc_debug.c

# Host-specific sources
HOST_SRCS = host_runtime.c host_fastgfx.c host_fs_shims.c host_peripheral_stubs.c host_main.c host_terminal.c host_fs.c host_sim_audio.c host_time.c host_fb.c host_keys.c hal_flash_host.c hal_time_host.c hal_pin_host.c hal_storage_host.c hal_filesystem_host.c hal_keyboard_host.c hal_audio_host.c hal_vm_framebuffer_host.c

SRCS = $(HOST_SRCS) $(CORE_SRCS) $(BC_SRCS)
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

TARGET = mmbasic_test

# --- Simulator target ---------------------------------------------------
# `make sim` builds mmbasic_sim, a superset of mmbasic_test that embeds
# Mongoose and a background HTTP+WS server for --sim mode. The test
# harness build is unaffected.
SIM_TARGET  = mmbasic_sim
SIM_EXTRA_SRCS = host_sim_server.c vendor/mongoose.c
SIM_EXTRA_OBJS = $(SIM_EXTRA_SRCS:.c=.o)
SIM_OBJS = $(OBJS) $(SIM_EXTRA_OBJS)
SIM_CFLAGS  = $(CFLAGS) -DMMBASIC_SIM -pthread
SIM_LDFLAGS = $(LDFLAGS) -pthread

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

sim: $(SIM_TARGET)

# Simulator build: recompile host_main.o with -DMMBASIC_SIM.
# Put --sim-only objects in a separate directory tree so we don't stomp
# on the test-harness build.
$(SIM_TARGET): $(SIM_OBJS:%.o=sim_obj/%.o)
	$(CC) $(SIM_CFLAGS) -o $@ $^ $(SIM_LDFLAGS)

sim_obj/host_stubs_legacy.o: host_stubs_legacy.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

sim_obj/host_main.o: host_main.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

sim_obj/host_terminal.o: host_terminal.c host_terminal.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

sim_obj/host_fs.o: host_fs.c host_fs.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

sim_obj/host_sim_server.o: host_sim_server.c host_sim_server.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

sim_obj/vendor/mongoose.o: vendor/mongoose.c vendor/mongoose.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -Wno-unused-parameter -Wno-sign-compare -c -o $@ $<

sim_obj/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

# core/state/*.c lives under ../core/state/, not ../ — add a matching rule.
sim_obj/%.o: ../core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

# Host sources compile from current directory
host_stubs_legacy.o: host_stubs_legacy.c
	$(CC) $(CFLAGS) -c -o $@ $<

host_main.o: host_main.c
	$(CC) $(CFLAGS) -c -o $@ $<

host_terminal.o: host_terminal.c host_terminal.h
	$(CC) $(CFLAGS) -c -o $@ $<

host_fs.o: host_fs.c host_fs.h
	$(CC) $(CFLAGS) -c -o $@ $<

host_sim_audio.o: host_sim_audio.c host_sim_audio.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_sim_audio.o: host_sim_audio.c host_sim_audio.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

host_time.o: ../ports/host_native/host_time.c ../ports/host_native/host_time.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_time.o: ../ports/host_native/host_time.c ../ports/host_native/host_time.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

host_fb.o: ../ports/host_native/host_fb.c ../ports/host_native/host_fb.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_fb.o: ../ports/host_native/host_fb.c ../ports/host_native/host_fb.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

host_keys.o: ../ports/host_native/host_keys.c ../ports/host_native/host_keys.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_keys.o: ../ports/host_native/host_keys.c ../ports/host_native/host_keys.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

# ---------------------------------------------------------------------------
# CRITICAL: every host/ source (HOST_SRCS) must compile with SIM_CFLAGS when
# it lands in sim_obj/. Without this, the default VPATH-backed pattern rule
# (`sim_obj/%.o: ../%.c`) won't match files that live in host/ (not ..), and
# make falls back to `%.o: %.c` which uses plain CFLAGS — silently dropping
# every `#ifdef MMBASIC_SIM` branch.
#
# The bug is invisible until runtime: the sim binary links fine, the server
# boots, the WebSocket accepts keys, but MMInkey's sim-queue pop and
# host_fb's emit_rect / emit_blit calls disappear. Symptoms: banner never
# reaches the canvas, keystrokes from the browser never reach MMBasic.
#
# This pattern rule catches *any* file in HOST_SRCS automatically, so new
# host/ files that don't have an explicit rule above still compile with the
# right flags. Keep HOST_SRCS as the single source of truth.
# ---------------------------------------------------------------------------
HOST_SIM_OBJS = $(HOST_SRCS:%.c=sim_obj/%.o)
$(HOST_SIM_OBJS): sim_obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

# Core sources from parent directory
%.o: ../%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(SIM_TARGET)
	rm -rf sim_obj

-include $(DEPS)

.PHONY: all clean sim
