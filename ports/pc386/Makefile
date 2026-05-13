# ports/pc386/Makefile — bare-metal kernel for IBM-PC 386+.
#
# Stage 0 only: builds a freestanding kernel from multiboot2.S + a
# placeholder kmain.c (added by Stage 0 — see docs/pc386/stage-0-hello.md).
# Real interpreter sources are layered in by Stage 2 once the stdio HAL
# surface is mirrored onto bare-metal drivers.
#
# The cross compiler (i686-elf-gcc) is bootstrapped by
# toolchain/pc386/install_cross.sh. If it's not on PATH, this Makefile
# fails fast with a pointer to that script.

PORT_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PORT_DIR)/../..)
BUILD_DIR := $(PORT_DIR)/build
PC386_AUDIO ?= sb16

CROSS_PREFIX ?= i686-elf-
CC      := $(CROSS_PREFIX)gcc
LD      := $(CROSS_PREFIX)ld
OBJCOPY := $(CROSS_PREFIX)objcopy

# Sanity: bail with a useful message if the cross toolchain is missing.
ifeq ($(shell command -v $(CC) 2>/dev/null),)
$(error $(CC) not found on PATH. Run toolchain/pc386/install_cross.sh)
endif

# Freestanding flags. -ffreestanding tells gcc not to assume libc;
# -nostdlib at link strips the host C runtime; -fno-pic / -fno-pie
# match the flat-segment, no-relocation kernel layout; -mno-* strips
# x87/SSE/MMX which we don't init in early boot. Optimisation stays
# moderate (-O2) — -Os interacts badly with some inline asm idioms in
# driver code.
CFLAGS = -std=gnu11 -ffreestanding -O2 -g \
         -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
         -fno-pic -fno-pie -fno-stack-protector \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -funsigned-char \
         -DMMBASIC_HOST -DPORT_PC386 \
         -DFF_VOLUMES=3 -DFF_STR_VOLUME_ID=1 -DFF_VOLUME_STRS='"A","B","C"' \
         -include pc386_platform.h \
         -I$(PORT_DIR)/include_shim -I$(PORT_DIR) \
         -I$(REPO_ROOT)/ports/host_native -I$(REPO_ROOT) \
         -MMD -MP

# Assembly: drop -include (assembler can't parse C typedefs) and the
# include_shim path (no .S file needs it).
ASFLAGS = -ffreestanding -g -fno-pic -fno-pie \
          -DMMBASIC_HOST -DPORT_PC386 \
          -I$(PORT_DIR) -I$(REPO_ROOT)

LDFLAGS = -nostdlib -no-pie -T $(PORT_DIR)/linker.ld -Wl,--build-id=none

ifeq ($(PC386_AUDIO),pcspk)
PORT_AUDIO_SRC := hal_audio_pc386.c
else ifeq ($(PC386_AUDIO),sb16)
PORT_AUDIO_SRC := hal_audio_pc386_sb16.c
else
$(error PC386_AUDIO must be one of: pcspk sb16)
endif

# --- Sources -----------------------------------------------------------------
# Stage 0: entry asm + kmain that brings up serial COM1 and VGA text.
# Stage 2+ extends CORE_SRCS / NATIVE_SRCS / BC_SRCS in the same shape
# as ports/mmbasic_stdio/Makefile.

PORT_ASM  = boot.S setjmp.S idt_asm.S bios_video.S
PORT_SRCS = kmain.c kprint.c mmap.c heap_region.c kstring.c \
            pc386_panic.c pc386_libc.c \
            pc386_state.c pc386_runtime.c pc386_peripheral_stubs.c \
            pc386_flash.c vm_sys_pin_pc386.c idt.c \
            $(PORT_AUDIO_SRC) hal_filesystem_pc386.c hal_flash_pc386.c \
            hal_keyboard_pc386.c hal_pin_pc386.c hal_random_pc386.c \
            hal_storage_pc386.c hal_time_pc386.c \
            hal_vm_framebuffer_pc386.c

# MMBasic core sources brought in for stage 3c.1+. Mirrors mmbasic_stdio's
# CORE_SRCS list. Files in repo root, drivers/, and core/state/ each
# get their own pattern rule below so the build dir stays organised.
CORE_SRCS = MMBasic.c Commands.c Functions.c Operators.c MATHS.c Memory.c Editor.c
CORE_SRCS += MMBasic_Print.c MMBasic_REPL.c MMBasic_Prompt.c
CORE_SRCS += gfx_box_shared.c gfx_circle_shared.c gfx_line_shared.c gfx_pixel_shared.c
CORE_SRCS += gfx_cls_shared.c gfx_text_shared.c gfx_console_shared.c
CORE_SRCS += mm_misc_shared.c OptionCommands.c
CORE_SRCS += Draw.c RGB121.c Tilemap.c FileIO.c Audio.c BmpDecoder.c re.c picojpeg.c

CORE_STATE_SRCS = display_state.c pin_state.c option_state.c audio_state.c
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_abort.c runtime_interrupt.c

# BC (bytecode) compiler + VM. ff/ffunicode/ffsystem are already in
# FATFS_SRCS so don't re-list them here.
BC_SRCS = bc_alloc.c bc_bridge.c \
          vm_sys_input.c vm_sys_graphics.c vm_sys_time.c \
          bc_source.c bc_compiler_core.c bc_vm.c bc_runtime.c bc_debug.c
# vm_host_fat.c (in-RAM FAT for sim B: drive) is omitted: pc386 has
# real FatFs over ATA-PIO from Stage 2 and provides its own
# get_fattime() in drivers/fatfs/ff_glue.c.

# vm_sys_file_sim.c lives in ports/vm_sys_sim/ rather than the repo root.
# Pc386 has real LPT-backed pins, so vm_sys_pin is port-local.
SIM_VM_SRCS = vm_sys_file_sim.c

# Stubs inherited from host_native — same set mmbasic_stdio links.
CORE_DRV_SRCS = audio_mp3/audio_mp3_stub.c \
                spi_lcd/spi_lcd_mem332_stub.c \
                display_merge/display_merge_stub.c \
                vga_pio/vga_ops_stub.c \
                gfx_3d/gfx_3d.c \
                upng_sprite/upng_sprite_stub.c \
                gui_touch/gui_touch_stub.c \
                gui_controls/gui_controls_stub.c \
                psram_heap/psram_heap_stub.c \
                i2c_picocalc_kbd/i2c_keypad_stub.c \
                heartbeat/heartbeat_stub.c \
                editor_console/editor_console_stub.c \
                display_pixel_readbuffer/display_pixel_readbuffer.c

CORE_C_OBJS    := $(addprefix $(BUILD_DIR)/core/,$(CORE_SRCS:.c=.o))
BC_C_OBJS      := $(addprefix $(BUILD_DIR)/bc/,$(BC_SRCS:.c=.o))
SIMVM_C_OBJS   := $(addprefix $(BUILD_DIR)/simvm/,$(SIM_VM_SRCS:.c=.o))
STATE_C_OBJS   := $(addprefix $(BUILD_DIR)/state/,$(CORE_STATE_SRCS:.c=.o))
RUNTIME_C_OBJS := $(addprefix $(BUILD_DIR)/runtime/,$(RUNTIME_SRCS:.c=.o))
COREDRV_C_OBJS := $(addprefix $(BUILD_DIR)/coredrv/,$(CORE_DRV_SRCS:.c=.o))

# Shared no-op HAL impls (linked across host_native, mmbasic_stdio,
# esp32, pc386 — anything without real hardware for that surface).
HAL_GENERIC_SRCS = hal_watchdog_noop.c

# Drivers live under $(REPO_ROOT)/drivers/<name>/<file>.c per project
# convention. List them as <name>/<file> pairs so the rule below can
# resolve the source path.
DRIVER_SRCS = \
    vga_text/vga_text.c \
    serial_16550/serial_16550.c \
    ata_pio/ata_pio.c \
    fdc_82077/fdc_82077.c \
    fatfs/ff_glue.c \
    i8259_pic/i8259_pic.c \
    i8042_kbd/i8042_kbd.c \
    i8042_kbd/scan_to_ascii.c \
    lpt_centronics/lpt_centronics.c \
    vga_mode13h/vga_mode13h.c

# FatFs is vendored at the repo root (R0.14, shared with every other
# port). Compile in-place; our drivers/fatfs/ff_glue.c provides the
# disk_io adapter onto ATA-PIO.
FATFS_SRCS = ff.c ffunicode.c ffsystem.c

ASM_OBJS    := $(addprefix $(BUILD_DIR)/,$(PORT_ASM:.S=.o))
PORT_C_OBJS := $(addprefix $(BUILD_DIR)/,$(PORT_SRCS:.c=.o))
DRV_C_OBJS  := $(addprefix $(BUILD_DIR)/drivers/,$(DRIVER_SRCS:.c=.o))
ROOT_C_OBJS := $(addprefix $(BUILD_DIR)/root/,$(FATFS_SRCS:.c=.o))
HAL_C_OBJS  := $(addprefix $(BUILD_DIR)/hal/generic/,$(HAL_GENERIC_SRCS:.c=.o))
OBJS        := $(ASM_OBJS) $(PORT_C_OBJS) $(DRV_C_OBJS) $(ROOT_C_OBJS) $(HAL_C_OBJS) \
               $(CORE_C_OBJS) $(BC_C_OBJS) $(SIMVM_C_OBJS) $(STATE_C_OBJS) $(RUNTIME_C_OBJS) $(COREDRV_C_OBJS)
DEPS        := $(PORT_C_OBJS:.o=.d) $(DRV_C_OBJS:.o=.d) $(ROOT_C_OBJS:.o=.d) $(HAL_C_OBJS:.o=.d) \
               $(CORE_C_OBJS:.o=.d) $(BC_C_OBJS:.o=.d) $(SIMVM_C_OBJS:.o=.d) \
               $(STATE_C_OBJS:.o=.d) $(RUNTIME_C_OBJS:.o=.d) $(COREDRV_C_OBJS:.o=.d)

KERNEL          := $(BUILD_DIR)/mmbasic.elf
KERNEL_STRIPPED := $(BUILD_DIR)/mmbasic-stripped.elf
KERNEL_BIN      := $(BUILD_DIR)/mmbasic.bin
CONFIG_STAMP    := $(BUILD_DIR)/.pc386_audio_config
FLOPPY_BOOT_DIR := $(BUILD_DIR)/bootloader
FLOPPY_LOAD_PLAN := $(FLOPPY_BOOT_DIR)/floppy_load_plan.inc
FLOPPY_STAGE1_OBJ := $(FLOPPY_BOOT_DIR)/floppy_stage1.o
FLOPPY_STAGE2_OBJ := $(FLOPPY_BOOT_DIR)/floppy_stage2.o
FLOPPY_STAGE1_BIN := $(FLOPPY_BOOT_DIR)/floppy_stage1.bin
FLOPPY_STAGE2_BIN := $(FLOPPY_BOOT_DIR)/floppy_stage2.bin

# --- Targets -----------------------------------------------------------------

.PHONY: all clean iso release floppy_bootloader FORCE

all: $(KERNEL) $(KERNEL_STRIPPED) floppy_bootloader

$(KERNEL): $(OBJS) $(PORT_DIR)/linker.ld $(CONFIG_STAMP) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(CONFIG_STAMP): FORCE | $(BUILD_DIR)
	@cfg='PC386_AUDIO=$(PC386_AUDIO)'; \
	if [ ! -f $@ ] || [ "$$(cat $@)" != "$$cfg" ]; then \
		echo "$$cfg" > $@; \
	fi

release: $(KERNEL_STRIPPED) $(KERNEL_BIN)

$(KERNEL_STRIPPED): $(KERNEL)
	$(OBJCOPY) --strip-all $< $@

$(KERNEL_BIN): $(KERNEL)
	$(OBJCOPY) -O binary $< $@

floppy_bootloader: $(FLOPPY_STAGE1_BIN) $(FLOPPY_STAGE2_BIN)

$(FLOPPY_BOOT_DIR):
	mkdir -p $@

$(FLOPPY_LOAD_PLAN): $(KERNEL_STRIPPED) $(PORT_DIR)/tools/gen_floppy_load_plan.py | $(FLOPPY_BOOT_DIR)
	python3 $(PORT_DIR)/tools/gen_floppy_load_plan.py --kernel-lba 161 $(KERNEL_STRIPPED) $@

$(FLOPPY_STAGE1_OBJ): $(PORT_DIR)/bootloader/floppy_stage1.S | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(FLOPPY_STAGE1_BIN): $(FLOPPY_STAGE1_OBJ)
	$(LD) -Ttext 0x7C00 --oformat binary -o $@ $<

$(FLOPPY_STAGE2_OBJ): $(PORT_DIR)/bootloader/floppy_stage2.S $(FLOPPY_LOAD_PLAN) | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -I$(FLOPPY_BOOT_DIR) -c -o $@ $<

$(FLOPPY_STAGE2_BIN): $(FLOPPY_STAGE2_OBJ)
	$(LD) -Ttext 0x0000 --oformat binary -o $@ $<

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Drivers under $(REPO_ROOT)/drivers/<name>/<file>.c.
$(BUILD_DIR)/drivers/%.o: $(REPO_ROOT)/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Repo-root sources (FatFs lives here, alongside every other port's copy).
$(BUILD_DIR)/root/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Shared HAL impls.
$(BUILD_DIR)/hal/generic/%.o: $(REPO_ROOT)/hal/generic/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# MMBasic core sources from the repo root.
$(BUILD_DIR)/core/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/state/%.o: $(REPO_ROOT)/core/state/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: $(REPO_ROOT)/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/coredrv/%.o: $(REPO_ROOT)/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/bc/%.o: $(REPO_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/simvm/%.o: $(REPO_ROOT)/ports/vm_sys_sim/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	@mkdir -p $@

# Bootable ISO via Limine. Stage 0 doesn't need this — `qemu -kernel`
# is enough — but Stage 7 (real hardware) does. Implementation lands
# alongside Stage 7.
iso: $(KERNEL)
	@echo "ISO build: not yet implemented (Stage 7 deliverable)"
	@false

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
