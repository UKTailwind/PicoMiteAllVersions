/*
 * ports/pc386/linker.ld — kernel link script for bare-metal i386.
 *
 * Loads the kernel at 1 MB physical (0x100000), the lowest address
 * above conventional memory + the BIOS data area + low-memory BIOS
 * extensions. Matches the standard multiboot kernel placement and
 * what Limine + QEMU -kernel both expect.
 *
 * .multiboot2 is placed first so the header falls within the first
 * 32 KB of the file (multiboot2 spec scan window). The other sections
 * follow in the conventional order with 4 KB alignment so they're
 * easy to map page-wise if we ever enable paging (we won't, for now).
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

KERNEL_PHYS_BASE = 0x00100000;       /* 1 MB */

SECTIONS
{
    . = KERNEL_PHYS_BASE;

    .multiboot2 BLOCK(8) : ALIGN(8)
    {
        KEEP(*(.multiboot2))
    }

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.text)
        *(.text.*)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
        *(.data.*)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Discard host-toolchain noise that may sneak in via cross-libgcc
     * or default link rules. We don't use exception unwinding, debug
     * frames, or notes in a freestanding kernel. */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
        *(.comment)
    }
}
