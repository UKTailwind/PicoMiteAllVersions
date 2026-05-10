/*
 * ports/pc386/kmain.c — kernel entry.
 *
 * Called from boot.S after the bootloader / QEMU -kernel hands us
 * control in 32-bit protected mode with flat segments and interrupts
 * disabled, the FPU brought up, and the multiboot magic + info
 * pointer pushed as cdecl args.
 *
 * Boot sequence (working state):
 *   1. Serial COM1 + VGA text consoles, banner.
 *   2. Multiboot1 magic check + mmap walk, heap region reserved.
 *   3. ATA-PIO probe + FAT mount of A: and C: from the IDE volumes.
 *   4. pc386_flash_init() — RAM-backed program/option buffers.
 *   5. MMBasic runtime instantiation: LoadOptions, InitBasic,
 *      InitHeap, host_runtime_begin. Reaches the point where a
 *      tokenised BASIC program could be ExecuteProgram'd.
 */

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>

#include "ff.h"

#include "../../drivers/ata_pio/ata_pio.h"
#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#include "heap_region.h"
#include "kprint.h"
#include "mmap.h"
#include "multiboot1.h"
#include "pc386_panic.h"

extern void pc386_flash_init(void);
extern void host_runtime_begin(void);
extern jmp_buf mark;

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

static const char *fr_str(FRESULT r) {
    switch (r) {
        case FR_OK:               return "OK";
        case FR_DISK_ERR:         return "DISK_ERR";
        case FR_INT_ERR:          return "INT_ERR";
        case FR_NOT_READY:        return "NOT_READY";
        case FR_NO_FILE:          return "NO_FILE";
        case FR_NO_PATH:          return "NO_PATH";
        case FR_INVALID_NAME:     return "INVALID_NAME";
        case FR_DENIED:           return "DENIED";
        case FR_EXIST:            return "EXIST";
        case FR_INVALID_OBJECT:   return "INVALID_OBJECT";
        case FR_WRITE_PROTECTED:  return "WRITE_PROTECTED";
        case FR_INVALID_DRIVE:    return "INVALID_DRIVE";
        case FR_NOT_ENABLED:      return "NOT_ENABLED";
        case FR_NO_FILESYSTEM:    return "NO_FILESYSTEM";
        case FR_MKFS_ABORTED:     return "MKFS_ABORTED";
        case FR_TIMEOUT:          return "TIMEOUT";
        case FR_LOCKED:           return "LOCKED";
        case FR_NOT_ENOUGH_CORE:  return "NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES: return "TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER:   return "INVALID_PARAMETER";
        default:                  return "?";
    }
}

/* One FATFS work area per mount, kept in BSS so Stage 1's heap region
 * stays untouched. */
static FATFS fs0, fs1;

static void mount_and_list(const char *vol, const char *label, FATFS *fs) {
    kputs(label);
    kputs(" (");
    kputs(vol);
    kputs("): ");

    FRESULT r = f_mount(fs, vol, /* opt = */ 1);  /* mount now */
    if (r != FR_OK) {
        kputs("mount failed: ");
        kputs(fr_str(r));
        kputc('\n');
        return;
    }

    DIR     dir;
    FILINFO fi;
    r = f_opendir(&dir, vol);
    if (r != FR_OK) {
        kputs("opendir failed: ");
        kputs(fr_str(r));
        kputc('\n');
        return;
    }

    /* Print FAT type + free space on the same line as the mount line. */
    DWORD free_clusters = 0;
    FATFS *fs_for_free = NULL;
    if (f_getfree(vol, &free_clusters, &fs_for_free) == FR_OK && fs_for_free) {
        DWORD free_sectors = free_clusters * fs_for_free->csize;
        const char *ftype =
              fs_for_free->fs_type == FS_FAT12 ? "FAT12"
            : fs_for_free->fs_type == FS_FAT16 ? "FAT16"
            : fs_for_free->fs_type == FS_FAT32 ? "FAT32"
            : fs_for_free->fs_type == FS_EXFAT ? "exFAT"
            : "?";
        kputs(ftype);
        kputs(", ");
        kputu32((uint32_t) free_sectors / 2);
        kputs(" KB free\n");
    } else {
        kputs("OK\n");
    }

    int n = 0;
    for (;;) {
        r = f_readdir(&dir, &fi);
        if (r != FR_OK || fi.fname[0] == 0) break;
        kputs("    ");
        kputs(fi.fname);
        if (fi.fattrib & AM_DIR) {
            kputs("/\n");
        } else {
            kputs("  ");
            kputu32((uint32_t) fi.fsize);
            kputs(" bytes\n");
        }
        n++;
    }
    if (n == 0) {
        kputs("    (empty)\n");
    }
    f_closedir(&dir);
}

void kmain(uint32_t magic, uint32_t info_addr) {
    bool serial_ok = serial_init();
    vga_text_init();

    vga_text_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kputs("PicoMite PC386 - Stage 3c\n");
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

    kputc('\n');
    kputs("FAT volumes:\n");
    mount_and_list("A:", "  drive A", &fs0);
    mount_and_list("C:", "  drive C", &fs1);

    /* ---------- MMBasic runtime instantiation (stage 3c.4) ---------- */

    kputc('\n');
    kputs("MMBasic runtime: ");
    pc386_flash_init();

    /* Mirror host_main.c's bring-up. setjmp catches error()'s longjmp
     * out of LoadOptions / InitBasic / etc. so a fault during init
     * surfaces as a kernel panic rather than a wild jump. */
    if (setjmp(mark) != 0) {
        kputs("MMBasic init faulted\n");
        kputs("MMErrMsg: ");
        kputs(MMErrMsg);
        kputc('\n');
        halt();
    }

    LoadOptions();
    kputs("LoadOptions ok, ");

    InitBasic();
    kputs("InitBasic ok, ");

    InitHeap(true);
    kputs("InitHeap ok, ");

    MMerrno = 0;
    MMErrMsg[0] = '\0';
    host_runtime_begin();
    kputs("host_runtime_begin ok\n");

    kputs("\nMMBasic standing — REPL/program execution lands in stage 3d/3e.\n");
    halt();
}
