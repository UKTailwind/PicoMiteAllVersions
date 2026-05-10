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
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mno-80387 \
         -DMMBASIC_HOST -DPORT_PC386 \
         -I$(PORT_DIR) -I$(REPO_ROOT)/ports/host_native -I$(REPO_ROOT) \
         -MMD -MP

ASFLAGS = $(CFLAGS)

LDFLAGS = -nostdlib -no-pie -T $(PORT_DIR)/linker.ld -Wl,--build-id=none

# --- Sources -----------------------------------------------------------------
# Stage 0: entry asm + kmain that brings up serial COM1 and VGA text.
# Stage 2+ extends CORE_SRCS / NATIVE_SRCS / BC_SRCS in the same shape
# as ports/mmbasic_stdio/Makefile.

PORT_ASM  = boot.S
PORT_SRCS = kmain.c

# Drivers live under $(REPO_ROOT)/drivers/<name>/<file>.c per project
# convention. List them as <name>/<file> pairs so the rule below can
# resolve the source path.
DRIVER_SRCS = \
    vga_text/vga_text.c \
    serial_16550/serial_16550.c

ASM_OBJS    := $(addprefix $(BUILD_DIR)/,$(PORT_ASM:.S=.o))
PORT_C_OBJS := $(addprefix $(BUILD_DIR)/,$(PORT_SRCS:.c=.o))
DRV_C_OBJS  := $(addprefix $(BUILD_DIR)/drivers/,$(DRIVER_SRCS:.c=.o))
OBJS        := $(ASM_OBJS) $(PORT_C_OBJS) $(DRV_C_OBJS)
DEPS        := $(PORT_C_OBJS:.o=.d) $(DRV_C_OBJS:.o=.d)

KERNEL := $(BUILD_DIR)/mmbasic.elf

# --- Targets -----------------------------------------------------------------

.PHONY: all clean iso

all: $(KERNEL)

$(KERNEL): $(OBJS) $(PORT_DIR)/linker.ld | $(BUILD_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: $(PORT_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Drivers under $(REPO_ROOT)/drivers/<name>/<file>.c.
# Pattern with two stems requires secondary expansion.
$(BUILD_DIR)/drivers/%.o: $(REPO_ROOT)/drivers/%.c
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
