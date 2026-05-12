/*
 * ports/pc386/boot.S — boot header(s) + 32-bit kernel entry.
 *
 * Currently emits a multiboot1 header. QEMU's `-kernel` flag supports
 * multiboot1 + Linux bzImage + PVH, but NOT multiboot2 — multiboot2
 * requires an actual bootloader (Limine, GRUB). For the development
 * inner loop (qemu -kernel mmbasic.elf) we therefore use multiboot1.
 *
 * Stage 7 (real-hardware / ISO path) adds a multiboot2 header
 * alongside this one. Both headers can coexist: bootloaders scan for
 * their preferred magic and match to whichever they recognise.
 *
 * Multiboot1 header (spec 3.1.1):
 *   .long 0x1BADB002              magic
 *   .long flags                   feature requests
 *   .long -(magic + flags)        checksum
 *
 * Header must land within first 8 KB of the file, 4-byte aligned. The
 * linker script places .multiboot first to satisfy that.
 *
 * Entry contract (multiboot1 spec, section 3.2):
 *   EAX = 0x2BADB002      multiboot1 boot signature
 *   EBX = physical addr of multiboot1 info structure
 *   CS  = 32-bit flat code segment
 *   DS/ES/FS/GS/SS = 32-bit flat data segment
 *   A20 enabled, paging disabled, interrupts disabled
 */

.set MB1_MAGIC,    0x1BADB002

/* Header flags ask the bootloader to satisfy these features. Only
 * three are defined for multiboot1:
 *   bit 0 (0x01) — page-align modules (we don't load any)
 *   bit 1 (0x02) — MEMINFO: pass us mem_lower/mem_upper, and the
 *                  bootloader is also encouraged to fill the mmap
 *   bit 2 (0x04) — request a video mode. We intentionally do not set
 *                  this for the direct QEMU -kernel path: some loaders
 *                  switch video modes without passing framebuffer fields.
 *                  The floppy stage2 handles BIOS VBE setup itself. */
.set MB1_FLAGS,    0x00000002
.set MB1_CHECKSUM, -(MB1_MAGIC + MB1_FLAGS)

.section .multiboot, "a"
.align 4
multiboot1_header:
    .long   MB1_MAGIC
    .long   MB1_FLAGS
    .long   MB1_CHECKSUM

/* Minimal multiboot2 header. Limine is still configured to load this
 * kernel through multiboot1 until kmain grows a multiboot2 info parser,
 * but having both headers in the ELF keeps that boot path open. */
.set MB2_MAGIC,    0xE85250D6
.set MB2_ARCH,     0
.set MB2_LEN,      multiboot2_header_end - multiboot2_header
.set MB2_CHECKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

.align 8
multiboot2_header:
    .long   MB2_MAGIC
    .long   MB2_ARCH
    .long   MB2_LEN
    .long   MB2_CHECKSUM
    .short  0                       /* end tag */
    .short  0
    .long   8
multiboot2_header_end:

.section .bss
.align 16
stack_bottom:
    .skip   16384                   /* 16 KB boot stack */
stack_top:

/* GDT — own it ourselves rather than relying on the bootloader's
 * (which may unmap when stage 4's IDT goes live, and whose selector
 * values are bootloader-specific). Three entries:
 *   0x00  null
 *   0x08  32-bit code,  base 0, limit 4G, ring 0   (kernel CS)
 *   0x10  32-bit data,  base 0, limit 4G, ring 0   (kernel DS/ES/FS/GS/SS)
 *
 * Access byte 0x9A = present, ring 0, code, executable, readable.
 * Access byte 0x92 = present, ring 0, data, writable.
 * Granularity 0xCF = 4K granularity, 32-bit, limit-high nibble F. */
.section .rodata
.align 8
gdt:
    .quad   0x0000000000000000      /* null  (selector 0x00) */
    .quad   0x00CF9A000000FFFF      /* code  (selector 0x08) */
    .quad   0x00CF92000000FFFF      /* data  (selector 0x10) */
gdt_end:

.align 8
gdtr:
    .word   gdt_end - gdt - 1       /* limit */
    .long   gdt                     /* base  */

.section .text
.global _start
.type _start, @function
_start:
    cli                             /* belt + suspenders */
    movl    $stack_top, %esp
    xorl    %ebp, %ebp              /* terminate frame pointer chain */

    /* Save the multiboot1 magic (EAX) + info ptr (EBX) before we
     * clobber EAX in the GDT load + CR0 dance below. */
    pushl   %ebx                    /* kmain arg 2: info pointer  */
    pushl   %eax                    /* kmain arg 1: multiboot magic */

    /* Load our own GDT, then reload all segment registers so they use
     * our descriptors. Far jmp resets CS via long return through the
     * new code selector (0x08); the rest take a mov from a constant. */
    lgdt    gdtr
    ljmp    $0x08, $.gdt_done
.gdt_done:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* Bring up the x87 FPU. CR0:
     *   bit 1 (MP)  set   — Monitor coProcessor; #NM on WAIT/FWAIT
     *                       when TS is set. Required when an FPU is present.
     *   bit 2 (EM)  clear — no soft-FP emulation; real FPU instructions
     *                       run natively.
     *   bit 3 (TS)  clear — Task Switched flag; no lazy FPU context yet.
     *   bit 5 (NE)  set   — Native FPU error reporting via #16 exception
     *                       (vs. legacy IRQ13 PIC routing).
     * After CR0 is set, FNINIT puts the x87 in a known state. We're
     * targeting 486DX+ in practice; the original 386 needed an external
     * 387, but every machine that runs MMBasic-as-OS in 2026 has one
     * integrated. */
    movl    %cr0, %eax
    andl    $~(1 << 2), %eax        /* clear EM */
    andl    $~(1 << 3), %eax        /* clear TS */
    orl     $(1 << 1), %eax         /* set   MP */
    orl     $(1 << 5), %eax         /* set   NE */
    movl    %eax, %cr0
    fninit                          /* reset FPU state — exceptions masked */

    call    kmain

.halt:
    cli
    hlt
    jmp     .halt
.size _start, . - _start
