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
 *   .long flags                   feature requests (we ask for nothing)
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
 *   bit 2 (0x04) — request a specific video mode (we don't)
 * Setting bit 1 alone is enough; QEMU and Limine both fill the mmap
 * fields regardless once MEMINFO is set. */
.set MB1_FLAGS,    0x00000002
.set MB1_CHECKSUM, -(MB1_MAGIC + MB1_FLAGS)

.section .multiboot, "a"
.align 4
multiboot1_header:
    .long   MB1_MAGIC
    .long   MB1_FLAGS
    .long   MB1_CHECKSUM

.section .bss
.align 16
stack_bottom:
    .skip   16384                   /* 16 KB boot stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli                             /* belt + suspenders */
    movl    $stack_top, %esp
    xorl    %ebp, %ebp              /* terminate frame pointer chain */

    /* Push EBX (info pointer) then EAX (magic) — cdecl, so they end
     * up as kmain's first and second arguments. */
    pushl   %ebx
    pushl   %eax

    call    kmain

.halt:
    cli
    hlt
    jmp     .halt
.size _start, . - _start
