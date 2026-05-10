/*
 * ports/pc386/kmain.c — Stage 0 kernel entry.
 *
 * Called from boot.S after the bootloader / QEMU -kernel hands us
 * control in 32-bit protected mode with flat segments and interrupts
 * disabled.
 *
 * Stage 0 responsibilities:
 *   - verify the multiboot2 magic the loader passed in EAX
 *   - bring up serial COM1 (test-output channel)
 *   - bring up VGA text-mode console (interactive channel)
 *   - print a banner over both
 *   - halt
 *
 * Deliberately NOT in this stage:
 *   - parsing the multiboot2 info structure (Stage 1)
 *   - heap allocator (Stage 1)
 *   - IDT / PIC / IRQs (Stage 3)
 *   - any MMBasic core (Stage 2)
 */

#include <stdint.h>

#include "../../drivers/serial_16550/serial_16550.h"
#include "../../drivers/vga_text/vga_text.h"

/* Multiboot1 boot signature. Stage 7 dual-headers and accepts both;
 * for now we expect the multiboot1 magic since QEMU -kernel only
 * speaks multiboot1. */
#define MULTIBOOT1_BOOTLOADER_MAGIC 0x2BADB002u

static void kputs(const char *s) {
    serial_puts(s);
    vga_text_puts(s);
}

static void kputhex32(uint32_t v) {
    char buf[11] = "0x00000000";
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 0xF];
    }
    kputs(buf);
}

/* Halt the CPU. Called from kmain on completion; multiboot2.S also
 * has a halt loop for kmain returning. */
static __attribute__((noreturn)) void halt(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kmain(uint32_t magic, uint32_t info_addr) {
    (void) info_addr;  /* Stage 1 will parse this. */

    /* Bring up serial first — if VGA init wedges for any reason, we
     * still get a banner over COM1. */
    bool serial_ok = serial_init();
    vga_text_init();

    vga_text_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kputs("PicoMite PC386 - Stage 0\n");
    vga_text_set_color(VGA_LIGHT_GRAY, VGA_BLACK);

    kputs("multiboot1 magic: ");
    kputhex32(magic);
    if (magic == MULTIBOOT1_BOOTLOADER_MAGIC) {
        kputs("  [ok]\n");
    } else {
        kputs("  [BAD - expected 0x2BADB002]\n");
    }

    kputs("serial COM1: ");
    kputs(serial_ok ? "ok\n" : "FAILED loopback\n");

    kputs("VGA text 80x25: ok (you're reading this)\n");

    kputs("\nStage 0 complete. Halting.\n");
    halt();
}
