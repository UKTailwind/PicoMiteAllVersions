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
         -I. -I.. \
         -MMD -MP

LDFLAGS = -lm

# Source directory
VPATH = ..:../core/state

# Core MMBasic sources (language runtime)
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c Editor.c MMBasic_Prompt.c MMBasic_Print.c MMBasic_REPL.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c
# Cross-cutting state hoisted from Draw.c / External.c / Audio.c in Phase 0.5 of
# the Real HAL refactor (see docs/real-hal-plan.md § "Cross-cutting state").
CORE_SRCS += display_state.c pin_state.c
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c

# Bytecode VM sources
BC_SRCS = bc_alloc.c bc_bridge.c vm_host_fat.c ff.c ffunicode.c ffsystem.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c vm_sys_pin.c vm_sys_file.c bc_source.c bc_compiler_core.c \
          bc_vm.c bc_runtime.c bc_debug.c

# Host-specific sources
HOST_SRCS = host_runtime.c host_fastgfx.c host_fs_shims.c host_peripheral_stubs.c host_main.c host_terminal.c host_fs.c host_sim_audio.c host_time.c host_fb.c host_keys.c

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

host_time.o: host_time.c host_time.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_time.o: host_time.c host_time.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

host_fb.o: host_fb.c host_fb.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_fb.o: host_fb.c host_fb.h
	@mkdir -p $(dir $@)
	$(CC) $(SIM_CFLAGS) -c -o $@ $<

host_keys.o: host_keys.c host_keys.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_obj/host_keys.o: host_keys.c host_keys.h
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
