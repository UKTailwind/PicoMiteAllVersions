/*
 * ports/pc386/multiboot2.S — multiboot2 header + 32-bit kernel entry.
 *
 * Layout: the .multiboot2 section is placed first by linker.ld so the
 * header lands within the first 32 KB of the ELF file (multiboot2
 * spec, section 3.1.1). QEMU -kernel and Limine both scan that window.
 *
 * Entry contract (multiboot2 spec, section 3.3):
 *   EAX = multiboot2 magic (0x36d76289)
 *   EBX = physical address of multiboot2 info structure
 *   CS  = 32-bit flat code segment
 *   DS/ES/FS/GS/SS = 32-bit flat data segment
 *   A20 enabled, paging disabled, interrupts disabled
 *
 * We preserve EAX and EBX, set up a stack, and call kmain(magic, info).
 * If kmain returns we halt; if it triple-faults QEMU exits cleanly
 * under -no-reboot.
 */

.set MB2_MAGIC,        0xe85250d6
.set MB2_ARCH_I386,    0
.set MB2_HEADER_LEN,   (multiboot2_end - multiboot2_start)
.set MB2_CHECKSUM,     -(MB2_MAGIC + MB2_ARCH_I386 + MB2_HEADER_LEN)

.section .multiboot2, "a"
.align 8
multiboot2_start:
    .long   MB2_MAGIC
    .long   MB2_ARCH_I386
    .long   MB2_HEADER_LEN
    .long   MB2_CHECKSUM

    /* End tag. Type=0, flags=0, size=8. */
    .align 8
    .short  0
    .short  0
    .long   8
multiboot2_end:

.section .bss
.align 16
stack_bottom:
    .skip   16384                   /* 16 KB boot stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    cli                             /* belt + suspenders; bootloader should already have done this */
    movl    $stack_top, %esp
    xorl    %ebp, %ebp              /* terminate frame pointer chain */

    /* Push EBX (info pointer) then EAX (magic) — cdecl, so they end up
     * as kmain's first and second arguments. */
    pushl   %ebx
    pushl   %eax

    call    kmain

    /* kmain returns only as a defensive last resort. Halt forever. */
.halt:
    cli
    hlt
    jmp     .halt
.size _start, . - _start
