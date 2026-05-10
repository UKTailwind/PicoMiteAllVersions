/*
 * ports/pc386/kmain.c — Stage 2a kernel entry.
 *
 * Called from boot.S after the bootloader / QEMU -kernel hands us
 * control in 32-bit protected mode with flat segments and interrupts
 * disabled.
 *
 * Stage 0: serial + VGA text, banner.
 * Stage 1: multiboot mmap parse, MMBasic heap region reserved.
 * Stage 2a (this stage): probe ATA drives, IDENTIFY each, read sector 0
 *          from every drive present and print the first 16 bytes —
 *          proves PIO works end to end before FatFs lands on top.
 *
 * Deliberately NOT yet:
 *   - filesystem (Stage 2b)
 *   - allocator on top of heap region (Stage 3)
 *   - IDT / PIC / IRQs (Stage 4)
 *   - any MMBasic core (Stage 3)
 */

#include <stdbool.h>
#include <stdint.h>

#include "../../drivers/ata_pio/ata_pio.h"
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

static const char *drive_label(unsigned i) {
    switch (i) {
        case ATA_PRIMARY_MASTER:   return "primary master  ";
        case ATA_PRIMARY_SLAVE:    return "primary slave   ";
        case ATA_SECONDARY_MASTER: return "secondary master";
        case ATA_SECONDARY_SLAVE:  return "secondary slave ";
        default:                   return "drive ?         ";
    }
}

static void probe_and_dump_drives(void) {
    static uint8_t sector_buf[ATA_SECTOR_SIZE]
        __attribute__((aligned(2)));

    ata_init();
    kputs("ATA-PIO probe:\n");
    int present = 0;
    for (unsigned i = 0; i < ATA_DRIVE_COUNT; i++) {
        const ata_drive_info_t *d = ata_drive(i);
        kputs("  ");
        kputs(drive_label(i));
        kputs(": ");
        if (!d->present) {
            kputs("absent\n");
            continue;
        }
        present++;
        kputs("present, ");
        kputu32(d->sector_count);
        kputs(" sectors (");
        /* sectors * 512 = bytes; print KB. */
        kputu32(d->sector_count / 2);
        kputs(" KB), model=\"");
        kputs(d->model);
        kputs("\"\n");

        if (ata_read_sectors(i, 0, 1, sector_buf) != 0) {
            kputs("    sector 0 read FAILED\n");
            continue;
        }
        kputs("    sector 0 [0..15]:");
        for (int k = 0; k < 16; k++) {
            kputc(' ');
            uint8_t b = sector_buf[k];
            const char *hex = "0123456789abcdef";
            char pair[3] = { hex[(b >> 4) & 0xF], hex[b & 0xF], '\0' };
            kputs(pair);
        }
        kputc('\n');
    }
    if (present == 0) {
        kputs("  (no drives detected)\n");
    }
}

void kmain(uint32_t magic, uint32_t info_addr) {
    bool serial_ok = serial_init();
    vga_text_init();

    vga_text_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kputs("PicoMite PC386 - Stage 2a\n");
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

    kputc('\n');
    probe_and_dump_drives();

    kputs("\nStage 2a complete. Halting.\n");
    halt();
}
