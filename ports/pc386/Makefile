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
PICO_COMPAT_DIR := $(REPO_ROOT)/ports/pico_sdk_compat
BUILD_DIR := $(PORT_DIR)/build
PC386_AUDIO ?= opl3
PC386_NO_FPU ?= 0
PC386_NO_FDC ?= 0
PC386_BOOT_TRACE ?= 0
PC386_NO_BIOS_VIDEO ?= 0
PC386_VIDEO ?= auto
PC386_KBD_SCANCODE_SET ?= 0
PC386_BOOT_MEM_UPPER_KB ?= 7168
PC386_BOOT_HIGH_MEM_BYTES ?= 0x00700000
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
         -fno-delete-null-pointer-checks \
         -fno-isolate-erroneous-paths-dereference \
         -fno-isolate-erroneous-paths-attribute \
         -march=i386 -mtune=i386 \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -funsigned-char \
         -DMMBASIC_HOST -DPORT_PC386 \
         -DPC386_KBD_SCANCODE_SET=$(PC386_KBD_SCANCODE_SET) \
         -DMMBA_RELEASE_VERSION=\"$(MMBA_RELEASE_VERSION)\" \
         -DFF_VOLUMES=3 -DFF_STR_VOLUME_ID=1 -DFF_VOLUME_STRS='"A","B","C"' \
         -include pc386_platform.h \
         $(THIRD_PARTY_CFLAGS) \
         -I$(PORT_DIR)/include_shim -I$(PORT_DIR) \
         -I$(REPO_ROOT)/ports/host_native -I$(PICO_COMPAT_DIR) -I$(REPO_ROOT)/core/mmbasic -I$(REPO_ROOT)/runtime/vm -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT) \
         -MMD -MP

ifeq ($(PC386_NO_FPU),1)
CFLAGS += -DPC386_NO_FPU=1 -msoft-float -mlong-double-64
endif
ifeq ($(PC386_NO_FDC),1)
CFLAGS += -DPC386_NO_FDC=1 -DFATFS_NO_FDC=1
endif
ifeq ($(PC386_BOOT_TRACE),1)
CFLAGS += -DPC386_BOOT_TRACE=1
endif
ifeq ($(PC386_NO_BIOS_VIDEO),1)
CFLAGS += -DPC386_NO_BIOS_VIDEO=1
endif
ifeq ($(PC386_VIDEO),auto)
CFLAGS += -DPC386_VIDEO_AUTO=1
else ifeq ($(PC386_VIDEO),vesa)
CFLAGS += -DPC386_VIDEO_VESA=1
else ifeq ($(PC386_VIDEO),cirrus)
CFLAGS += -DPC386_VIDEO_CIRRUS=1
else
$(error PC386_VIDEO must be one of: auto vesa cirrus)
endif

# Assembly: drop -include (assembler can't parse C typedefs) and the
# include_shim path (no .S file needs it).
ASFLAGS = -ffreestanding -g -fno-pic -fno-pie \
          -march=i386 -mtune=i386 \
          -DMMBASIC_HOST -DPORT_PC386 \
          -I$(PORT_DIR) -I$(REPO_ROOT)/shared/gfx -I$(REPO_ROOT)/shared/audio -I$(REPO_ROOT)/shared/mmbasic -I$(REPO_ROOT)/drivers/bmp_decoder -I$(REPO_ROOT)/drivers/gps -I$(REPO_ROOT)/drivers/gui_controls -I$(REPO_ROOT)/drivers/gui_touch -I$(REPO_ROOT)/drivers/i2c_bus -I$(REPO_ROOT)/drivers/onewire -I$(REPO_ROOT)/drivers/psram_heap -I$(REPO_ROOT)/drivers/serial -I$(REPO_ROOT)/drivers/spi_bus -I$(REPO_ROOT)/drivers/spi_lcd -I$(REPO_ROOT)/drivers/ssd1963 -I$(REPO_ROOT)

ASFLAGS += -DPC386_BOOT_MEM_UPPER_KB=$(PC386_BOOT_MEM_UPPER_KB) \
           -DPC386_BOOT_HIGH_MEM_BYTES=$(PC386_BOOT_HIGH_MEM_BYTES)
ifeq ($(PC386_NO_FPU),1)
ASFLAGS += -DPC386_NO_FPU=1
endif
ifeq ($(PC386_NO_FDC),1)
ASFLAGS += -DPC386_NO_FDC=1
endif
ifeq ($(PC386_BOOT_TRACE),1)
ASFLAGS += -DPC386_BOOT_TRACE=1
endif
ifeq ($(PC386_NO_BIOS_VIDEO),1)
ASFLAGS += -DPC386_NO_BIOS_VIDEO=1
endif

LDFLAGS = -nostdlib -no-pie -T $(PORT_DIR)/linker.ld -Wl,--build-id=none

ifeq ($(PC386_AUDIO),pcspk)
PORT_AUDIO_SRC := hal_audio_pc386.c
else ifeq ($(PC386_AUDIO),opl3)
PORT_AUDIO_SRC := hal_audio_pc386_opl3.c
else ifeq ($(PC386_AUDIO),sb16)
PORT_AUDIO_SRC := hal_audio_pc386_sb16.c
else
$(error PC386_AUDIO must be one of: pcspk opl3 sb16)
endif

ifeq ($(PC386_NO_FPU),1)
PC386_SOFTFLOAT_SRCS := compiler_rt/adddf3.c \
                        compiler_rt/subdf3.c \
                        compiler_rt/muldf3.c \
                        compiler_rt/divdf3.c \
                        compiler_rt/comparedf2.c \
                        compiler_rt/addsf3.c \
                        compiler_rt/subsf3.c \
                        compiler_rt/mulsf3.c \
                        compiler_rt/divsf3.c \
                        compiler_rt/comparesf2.c \
                        compiler_rt/fixdfsi.c \
                        compiler_rt/fixsfsi.c \
                        compiler_rt/fixunsdfsi.c \
                        compiler_rt/fixsfdi.c \
                        compiler_rt/fixdfdi.c \
                        compiler_rt/fixunssfdi.c \
                        compiler_rt/fixunsdfdi.c \
                        compiler_rt/floatsidf.c \
                        compiler_rt/floatsisf.c \
                        compiler_rt/floatunsidf.c \
                        compiler_rt/floatdisf.c \
                        compiler_rt/floatdidf.c \
                        compiler_rt/floatundidf.c \
                        compiler_rt/extendsfdf2.c \
                        compiler_rt/truncdfsf2.c \
                        compiler_rt/pc386_fp_mode.c
else
PC386_SOFTFLOAT_SRCS :=
endif

# --- Sources -----------------------------------------------------------------
# Stage 0: entry asm + kmain that brings up serial COM1 and VGA text.
# Stage 2+ extends CORE_SRCS / NATIVE_SRCS / BC_SRCS in the same shape
# as ports/mmbasic_stdio/Makefile.

PORT_ASM  = boot.S setjmp.S idt_asm.S bios_video.S
PORT_SRCS = kmain.c kprint.c mmap.c heap_region.c kstring.c \
            pc386_panic.c pc386_libc.c \
            pc386_state.c pc386_runtime.c pc386_peripheral_stubs.c \
            pc386_cpu.c pc386_rtc.c pc386_memio.c pc386_lpt.c pc386_interp.c \
            pc386_flash.c vm_sys_pin_pc386.c idt.c \
            $(PORT_AUDIO_SRC) hal_filesystem_pc386.c hal_flash_pc386.c \
            hal_keyboard_pc386.c hal_pin_pc386.c hal_random_pc386.c \
            hal_storage_pc386.c hal_time_pc386.c \
            hal_vm_framebuffer_pc386.c \
            $(PC386_SOFTFLOAT_SRCS)

# MMBasic core sources brought in for stage 3c.1+. Mirrors mmbasic_stdio's
# CORE_SRCS list. Files in repo root, drivers/, and core/state/ each
# get their own pattern rule below so the build dir stays organised.
CORE_SRCS = core/mmbasic/MMBasic.c core/mmbasic/Commands.c core/mmbasic/Functions.c core/mmbasic/Operators.c core/mmbasic/MATHS.c core/mmbasic/Memory.c core/mmbasic/Editor.c
CORE_SRCS += core/mmbasic/MMBasic_Print.c core/mmbasic/MMBasic_REPL.c core/mmbasic/MMBasic_Prompt.c
CORE_SRCS += shared/gfx/gfx_box_shared.c shared/gfx/gfx_circle_shared.c shared/gfx/gfx_line_shared.c shared/gfx/gfx_pixel_shared.c
CORE_SRCS += shared/gfx/gfx_cls_shared.c shared/gfx/gfx_text_shared.c shared/gfx/gfx_console_shared.c
CORE_SRCS += shared/mmbasic/mm_misc_shared.c core/mmbasic/OptionCommands.c
CORE_SRCS += core/mmbasic/Draw.c shared/gfx/RGB121.c shared/gfx/Tilemap.c core/mmbasic/FileIO.c shared/audio/Audio.c drivers/bmp_decoder/BmpDecoder.c third_party/regex/re.c third_party/picojpeg/picojpeg.c
CORE_SRCS += core/mmbasic/XModem.c

CORE_STATE_SRCS = display_state.c pin_state.c option_state.c audio_state.c
# runtime_interrupt.c is omitted: pc386's check_interrupt/cmd_ireturn
# are local stubs that don't invoke any mmbasic_runtime_* symbol, and
# the shared TU references MQTT/UDP network externs pc386 doesn't
# provide (pc386 has no networking).
RUNTIME_SRCS = runtime_boot.c runtime_program.c runtime_console_putchar.c runtime_console_printstring.c runtime_console_adapter.c runtime_console_escdecode.c runtime_getline.c runtime_terminal_hooks_noop.c runtime_abort.c runtime_cmd_load_post_cleanup.c
# Note: runtime_console_input_noop.c is intentionally NOT linked; pc386
# provides real getConsole/kbhitConsole in pc386_runtime.c so XMODEM
# RECEIVE (and any other path that calls getConsole()) actually drains
# serial bytes instead of seeing -1.

# BC (bytecode) compiler + VM. ff/ffunicode/ffsystem are already in
# FATFS_SRCS so don't re-list them here.
BC_SRCS = runtime/vm/bc_alloc.c runtime/vm/bc_bridge.c \
          runtime/vm/vm_sys_input.c runtime/vm/vm_sys_graphics.c runtime/vm/vm_sys_time.c \
          runtime/vm/bc_source.c runtime/vm/bc_compiler_core.c runtime/vm/bc_vm.c runtime/vm/bc_runtime.c runtime/vm/bc_debug.c
# runtime/vm/vm_host_fat.c (in-RAM FAT for sim B: drive) is omitted: pc386 has
# real FatFs over ATA-PIO from Stage 2 and provides its own
# get_fattime() in drivers/fatfs/ff_glue.c.

# vm_sys_file_sim.c lives in ports/vm_sys_sim/ rather than the repo root.
# Pc386 has real LPT-backed pins, so vm_sys_pin is port-local.
SIM_VM_SRCS = vm_sys_file_sim.c

# Stubs inherited from host_native — same set mmbasic_stdio links.
ifeq ($(PC386_NO_FPU),1)
PC386_GFX_3D_SRC := gfx_3d/gfx_3d_stub.c
else
PC386_GFX_3D_SRC := gfx_3d/gfx_3d.c
endif

CORE_DRV_SRCS = audio_mp3/audio_mp3_stub.c \
                spi_lcd/spi_lcd_mem332_stub.c \
                display_merge/display_merge_stub.c \
                vga_pio/vga_ops_stub.c \
                $(PC386_GFX_3D_SRC) \
                upng_sprite/upng_sprite_stub.c \
                gui_touch/gui_touch_stub.c \
                gui_controls/gui_controls_stub.c \
                psram_heap/psram_heap_stub.c \
                i2c_picocalc_kbd/i2c_keypad_stub.c \
                heartbeat/heartbeat_stub.c \
                editor_console/editor_console_stub.c \
                display_pixel_readbuffer/display_pixel_readbuffer.c \
                calendar/calendar_bare.c

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
    vga_mode13h/cirrus_gd542x.c \
    vga_mode13h/vga_mode13h.c

# FatFs is vendored under third_party/ (R0.14, shared with every other
# port). Compile in-place; our drivers/fatfs/ff_glue.c provides the
# disk_io adapter onto ATA-PIO.
FATFS_SRCS = third_party/fatfs/ff.c third_party/fatfs/ffunicode.c third_party/fatfs/ffsystem.c

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
HDD_LOAD_PLAN := $(FLOPPY_BOOT_DIR)/hdd_load_plan.inc
HDD_STAGE1_OBJ := $(FLOPPY_BOOT_DIR)/hdd_stage1.o
HDD_STAGE2_OBJ := $(FLOPPY_BOOT_DIR)/hdd_stage2.o
HDD_STAGE1_BIN := $(FLOPPY_BOOT_DIR)/hdd_stage1.bin
HDD_STAGE2_BIN := $(FLOPPY_BOOT_DIR)/hdd_stage2.bin

# --- Targets -----------------------------------------------------------------

.PHONY: all clean iso release floppy_bootloader hdd_bootloader FORCE

all: $(KERNEL) $(KERNEL_STRIPPED) floppy_bootloader

$(KERNEL): $(OBJS) $(PORT_DIR)/linker.ld $(CONFIG_STAMP) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(OBJS): $(VERSION_FILE) $(CONFIG_STAMP)

$(CONFIG_STAMP): FORCE | $(BUILD_DIR)
	@cfg='PC386_AUDIO=$(PC386_AUDIO) PC386_NO_FPU=$(PC386_NO_FPU) PC386_NO_FDC=$(PC386_NO_FDC) PC386_BOOT_TRACE=$(PC386_BOOT_TRACE) PC386_NO_BIOS_VIDEO=$(PC386_NO_BIOS_VIDEO) PC386_VIDEO=$(PC386_VIDEO) PC386_KBD_SCANCODE_SET=$(PC386_KBD_SCANCODE_SET) PC386_BOOT_MEM_UPPER_KB=$(PC386_BOOT_MEM_UPPER_KB) PC386_BOOT_HIGH_MEM_BYTES=$(PC386_BOOT_HIGH_MEM_BYTES)'; \
	if [ ! -f $@ ] || [ "$$(cat $@)" != "$$cfg" ]; then \
		echo "$$cfg" > $@; \
	fi

release: $(KERNEL_STRIPPED) $(KERNEL_BIN)

$(KERNEL_STRIPPED): $(KERNEL)
	$(OBJCOPY) --strip-all $< $@

$(KERNEL_BIN): $(KERNEL)
	$(OBJCOPY) -O binary $< $@

floppy_bootloader: $(FLOPPY_STAGE1_BIN) $(FLOPPY_STAGE2_BIN)

hdd_bootloader: $(HDD_STAGE1_BIN) $(HDD_STAGE2_BIN)

$(FLOPPY_BOOT_DIR):
	mkdir -p $@

$(FLOPPY_LOAD_PLAN): $(KERNEL_STRIPPED) $(PORT_DIR)/tools/gen_floppy_load_plan.py | $(FLOPPY_BOOT_DIR)
	python3 $(PORT_DIR)/tools/gen_floppy_load_plan.py --kernel-lba 161 $(KERNEL_STRIPPED) $@

$(FLOPPY_STAGE1_OBJ): $(PORT_DIR)/bootloader/floppy_stage1.S | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(FLOPPY_STAGE1_BIN): $(FLOPPY_STAGE1_OBJ)
	$(LD) -Ttext 0x7C00 --oformat binary -o $@ $<

$(FLOPPY_STAGE2_OBJ): $(PORT_DIR)/bootloader/floppy_stage2.S $(FLOPPY_LOAD_PLAN) $(CONFIG_STAMP) | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -I$(FLOPPY_BOOT_DIR) -c -o $@ $<

$(FLOPPY_STAGE2_BIN): $(FLOPPY_STAGE2_OBJ)
	$(LD) -Ttext 0x0000 --oformat binary -o $@ $<

$(HDD_STAGE1_OBJ): $(PORT_DIR)/bootloader/hdd_stage1.S | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(HDD_STAGE1_BIN): $(HDD_STAGE1_OBJ)
	$(LD) -Ttext 0x7C00 --oformat binary -o $@ $<

$(HDD_STAGE2_OBJ): $(PORT_DIR)/bootloader/floppy_stage2.S $(HDD_LOAD_PLAN) $(CONFIG_STAMP) | $(FLOPPY_BOOT_DIR)
	$(CC) $(ASFLAGS) -I$(FLOPPY_BOOT_DIR) -DPC386_BOOT_SECTORS_TRACK=63 -DPC386_BOOT_HEADS=16 -DPC386_BOOT_LOAD_PLAN='"hdd_load_plan.inc"' -DPC386_BOOT_STAGE2_MSG='"PC386 HDD stage2\r\n"' -DPC386_BOOT_DISK_ERROR_MSG='"PC386 HDD stage2 disk error\r\n"' -DPC386_BOOT_MEM_UPPER_KB=$(PC386_BOOT_MEM_UPPER_KB) -DPC386_BOOT_HIGH_MEM_BYTES=$(PC386_BOOT_HIGH_MEM_BYTES) -c -o $@ $<

$(HDD_STAGE2_BIN): $(HDD_STAGE2_OBJ)
	$(LD) -Ttext 0x0000 --oformat binary -o $@ $<

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.S | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
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

# MMBasic core sources.
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
