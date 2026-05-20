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
 *   3. ATA-PIO + FDC probe, then FAT mount of A:/B:/C:.
 *   4. pc386_flash_init() — RAM-backed program/option buffers.
 *   5. MMBasic runtime instantiation: LoadOptions, InitBasic,
 *      InitHeap, mmbasic_runtime_port_begin. Reaches the point where a
 *      tokenised BASIC program could be ExecuteProgram'd.
 */

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>

#include "ff.h"

#include "../../drivers/ata_pio/ata_pio.h"
#include "../../drivers/fdc_82077/fdc_82077.h"
#include "../../drivers/i8042_kbd/i8042_kbd.h"
#include "../../drivers/i8259_pic/i8259_pic.h"
#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "runtime/runtime.h"

#include "heap_region.h"
#include "idt.h"
#include "kprint.h"
#include "mmap.h"
#include "multiboot1.h"
#include "pc386_panic.h"

extern void pc386_flash_init(void);
extern void vm_host_fat_reset(void);
extern void vm_sys_file_reset(void);
extern void vm_sys_pin_reset(void);
extern void MMBasic_PrintBanner(void);
extern jmp_buf mark;

const mb1_info_t *pc386_multiboot_info = NULL;

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
static FATFS fs0, fs1, fs2;

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
    kputs("PicoMite PC386 - Stage 5\n");
    vga_text_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    /* IDT comes up first thing — once we lidt, every CPU exception
     * routes through exc_unhandled with a useful message instead of
     * triple-faulting. */
    idt_init();
    kputs("IDT loaded (256 vectors, exception handlers wired)\n");

    /* PIC remap: master IRQs to vectors 0x20..0x27, slave to
     * 0x28..0x2F. All lines start masked except the cascade (IRQ2);
     * individual drivers (PS/2 in 4c) call pic_unmask(N) when ready.
     * sti() lets external IRQs fire, but with everything masked nothing
     * actually does until 4c. */
    pic_init();
    __asm__ volatile("sti");
    kputs("PIC remapped to 0x20-0x2F, IRQs enabled\n");

    /* PS/2 keyboard: register IRQ1 handler + unmask. Raw scancodes
     * accumulate in the kbd ring; 4d/4e drain them and translate to
     * ASCII. */
    kbd_init();
    kputs("PS/2 keyboard online (IRQ1)\n");

    /* COM1 RX: switch from poll to IRQ4-driven so the test harness's
     * piped input doesn't drop the first char on the boot race, and
     * MMgetchar can hlt when idle (no need to spin a core). */
    serial_irq_init();
    kputs("Serial COM1 RX online (IRQ4)\n");

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
    pc386_multiboot_info = info;
    kputs("multiboot info: ");
    kputhex32(info_addr);
    kputs("  flags=");
    kputhex32(info->flags);
    kputc('\n');
    if (info->flags & MB1_INFO_FRAMEBUFFER) {
        kputs("framebuffer: addr=");
        kputhex64(info->framebuffer_addr);
        kputs(" pitch=");
        kputu32(info->framebuffer_pitch);
        kputs(" size=");
        kputu32(info->framebuffer_width);
        kputc('x');
        kputu32(info->framebuffer_height);
        kputs(" bpp=");
        kputu32(info->framebuffer_bpp);
        kputs(" type=");
        kputu32(info->framebuffer_type);
        kputs(" rgb=");
        kputu32(info->color_info[1]);
        kputc('@');
        kputu32(info->color_info[0]);
        kputc('/');
        kputu32(info->color_info[3]);
        kputc('@');
        kputu32(info->color_info[2]);
        kputc('/');
        kputu32(info->color_info[5]);
        kputc('@');
        kputu32(info->color_info[4]);
        kputc('\n');
    }

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
#ifdef PC386_NO_FDC
    kputs("FDC probe: disabled\n");
#else
    kputs("FDC probe:\n");
    kputs("  drive A: ");
    kputs(fdc_present(0) ? "present\n" : "absent\n");
    kputs("  drive B: ");
    kputs(fdc_present(1) ? "present\n" : "absent\n");
#endif

    kputc('\n');
    kputs("FAT volumes:\n");
#ifndef PC386_NO_FDC
    mount_and_list("A:", "  drive A", &fs0);
    mount_and_list("B:", "  drive B", &fs1);
#else
    (void)fs0;
    (void)fs1;
#endif
    mount_and_list("C:", "  drive C", &fs2);

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

#ifdef PC386_BOOT_TRACE
    kputs("InitBasic start, ");
#endif
    InitBasic();
    kputs("InitBasic ok, ");

#ifdef PC386_BOOT_TRACE
    kputs("InitHeap start, ");
#endif
    InitHeap(true);
    kputs("InitHeap ok, ");

    MMerrno = 0;
    MMErrMsg[0] = '\0';
#ifdef PC386_BOOT_TRACE
    kputs("runtime begin start, ");
#endif
    mmbasic_runtime_port_begin();
    kputs("mmbasic_runtime_port_begin ok\n");

    /* ---------- Banner + REPL --------------------------------------- */

    MMBasic_PrintBanner();
    mmbasic_runtime_enter_repl(NULL, 0);

    /* MMBasic_RunPromptLoop never returns. */
    halt();
}
