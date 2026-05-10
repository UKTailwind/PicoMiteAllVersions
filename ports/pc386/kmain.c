/*
 * ports/pc386/kmain.c — Stage 1 kernel entry.
 *
 * Called from boot.S after the bootloader / QEMU -kernel hands us
 * control in 32-bit protected mode with flat segments and interrupts
 * disabled.
 *
 * Stage 0 brought up serial COM1 and VGA text and proved the boot
 * chain works. Stage 1 adds:
 *   - validating the multiboot1 info pointer
 *   - parsing the bootloader-supplied memory map
 *   - reporting the reserved MMBasic heap region (BSS-backed)
 *
 * Deliberately NOT in this stage:
 *   - any allocator on top of the heap region (Stage 3)
 *   - disk / filesystem (Stage 2)
 *   - IDT / PIC / IRQs (Stage 4)
 *   - any MMBasic core (Stage 3)
 */

#include <stdbool.h>
#include <stdint.h>

#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"

#include "heap_region.h"
#include "kprint.h"
#include "mmap.h"
#include "multiboot1.h"

static __attribute__((noreturn)) void halt(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kmain(uint32_t magic, uint32_t info_addr) {
    bool serial_ok = serial_init();
    vga_text_init();

    vga_text_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kputs("PicoMite PC386 - Stage 1\n");
    vga_text_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    kputs("multiboot1 magic: ");
    kputhex32(magic);
    if (magic == MULTIBOOT1_BOOTLOADER_MAGIC) {
        kputs("  [ok]\n");
    } else {
        kputs("  [BAD - expected 0x2BADB002]\n");
        kputs("\nCannot continue without a valid multiboot1 environment. Halting.\n");
        halt();
    }

    kputs("serial COM1: ");
    kputs(serial_ok ? "ok\n" : "FAILED loopback\n");

    const mb1_info_t *info = (const mb1_info_t *) (uintptr_t) info_addr;
    kputs("multiboot info: ");
    kputhex32(info_addr);
    kputs("  flags=");
    kputhex32(info->flags);
    kputc('\n');

    mmap_print(info);

    mmap_summary_t summary;
    if (mmap_summarize(info, &summary)) {
        kputs("Total available RAM: ");
        kputu64(summary.total_available_bytes);
        kputs(" bytes (");
        kputu64(summary.total_available_bytes / 1024);
        kputs(" KB)\n");
        kputs("Largest free region: ");
        kputu64(summary.largest_region_bytes);
        kputs(" bytes at ");
        kputhex64(summary.largest_region_base);
        kputc('\n');
    }

    kputs("MMBasic heap reserved: ");
    kputu32((uint32_t) heap_region_size());
    kputs(" bytes at ");
    kputhex32((uint32_t) (uintptr_t) heap_region_base());
    kputc('\n');

    kputs("\nStage 1 complete. Halting.\n");
    halt();
}
