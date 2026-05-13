/*
 * PC386 boot floppy stage 2.
 *
 * Loaded by stage1 at 0800:0000. Reads A:/BOOT/MMBASIC.ELF from the
 * known contiguous FAT12 allocation, loads the ELF PT_LOAD segments to
 * their physical addresses, zeros BSS, creates a minimal multiboot1 info
 * block, and enters the existing kernel _start contract.
 */

.code16
.section .text
.global _start

.set STAGE2_SEG,       0x0800
.set STAGE2_PHYS,      0x00008000
.set SECTORS_TRACK,    18
.set HEADS,            2
.set BOUNCE_SEG,       0x0000
.set BOUNCE_OFF,       0x7000
.set MB_INFO_ADDR,     0x5000
.set MB_MMAP_ADDR,     0x5100
.set VBE_MODE_INFO,    0x5400
.set MB_MAGIC,         0x2BADB002
.set MB_FLAGS,         0x00000043
.set MB_FLAG_FB,       0x00001000
.set KERNEL_FILE_LBA,  161

_start:
    cli
    movw %cs, %ax
    movw %ax, %ds
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x7000, %sp
    sti

    movb %dl, boot_drive
    call serial_init
    movw $msg_boot, %si
    call print

    call enable_a20
    call setup_vga13h
    call setup_unreal_gs
    call build_multiboot_info
    call load_kernel_segments
    call enter_kernel

halt:
    cli
    hlt
    jmp halt

enable_a20:
    inb $0x92, %al
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92
    ret

setup_vga13h:
    movw $0x0013, %ax
    int $0x10
    pushw %cs
    popw %ds
    ret

setup_vesa:
    xorw %dx, %dx
    movw %dx, %es
    movw $vesa_modes, %si
1:
    lodsw
    testw %ax, %ax
    jz 4f
    movw %ax, vesa_mode
    movw %ax, %cx
    movw $VBE_MODE_INFO, %di
    movw $0x4F01, %ax
    int $0x10
    pushw %cs
    popw %ds
    xorw %dx, %dx
    movw %dx, %es
    cmpw $0x004F, %ax
    jne 1b
    movw %es:VBE_MODE_INFO, %ax
    testw $0x0001, %ax
    jz 1b
    testw $0x0080, %ax
    jz 1b
    movb %es:VBE_MODE_INFO+25, %al
    cmpb $24, %al
    jb 1b
    movw vesa_mode, %bx
    movw $0x4F02, %ax
    int $0x10
    pushw %cs
    popw %ds
    xorw %dx, %dx
    movw %dx, %es
    cmpw $0x004F, %ax
    jne 1b
    movb $1, vesa_ok
4:
    ret

setup_unreal_gs:
    cli
    lgdt gdt_desc
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $(STAGE2_PHYS + pm16_for_unreal)

pm16_for_unreal:
    movw $0x10, %ax
    movw %ax, %gs
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax
    movl %eax, %cr0
    ljmp $STAGE2_SEG, $real16_after_unreal

real16_after_unreal:
    movw %cs, %ax
    movw %ax, %ds
    xorw %ax, %ax
    movw %ax, %es
    sti
    ret

build_multiboot_info:
    xorw %ax, %ax
    movw %ax, %es
    movw $MB_INFO_ADDR, %di
    movw $128, %cx
    xorw %ax, %ax
    rep stosb

    movw $MB_INFO_ADDR, %di
    movl $MB_FLAGS, %es:0(%di)
    movl $639, %es:4(%di)
    movl $15360, %es:8(%di)
    movl $0x00000000, %es:12(%di)
    movl $72, %es:44(%di)
    movl $MB_MMAP_ADDR, %es:48(%di)
    cmpb $0, vesa_ok
    je 1f
    movl $(MB_FLAGS | MB_FLAG_FB), %es:0(%di)
    movl $VBE_MODE_INFO, %es:76(%di)
    movw vesa_mode, %ax
    movw %ax, %es:80(%di)
    movl %es:VBE_MODE_INFO+40, %eax
    movl %eax, %es:88(%di)
    movl $0, %es:92(%di)
    xorl %eax, %eax
    movw %es:VBE_MODE_INFO+16, %ax
    movl %eax, %es:96(%di)
    xorl %eax, %eax
    movw %es:VBE_MODE_INFO+18, %ax
    movl %eax, %es:100(%di)
    xorl %eax, %eax
    movw %es:VBE_MODE_INFO+20, %ax
    movl %eax, %es:104(%di)
    movb %es:VBE_MODE_INFO+25, %al
    movb %al, %es:108(%di)
    movb $1, %es:109(%di)
    movb %es:VBE_MODE_INFO+32, %al
    movb %al, %es:110(%di)
    movb %es:VBE_MODE_INFO+31, %al
    movb %al, %es:111(%di)
    movb %es:VBE_MODE_INFO+34, %al
    movb %al, %es:112(%di)
    movb %es:VBE_MODE_INFO+33, %al
    movb %al, %es:113(%di)
    movb %es:VBE_MODE_INFO+36, %al
    movb %al, %es:114(%di)
    movb %es:VBE_MODE_INFO+35, %al
    movb %al, %es:115(%di)
1:

    movw $MB_MMAP_ADDR, %di
    movl $20, %es:0(%di)
    movl $0x00000000, %es:4(%di)
    movl $0x00000000, %es:8(%di)
    movl $0x0009FC00, %es:12(%di)
    movl $0x00000000, %es:16(%di)
    movl $1, %es:20(%di)

    addw $24, %di
    movl $20, %es:0(%di)
    movl $0x0009FC00, %es:4(%di)
    movl $0x00000000, %es:8(%di)
    movl $0x00060400, %es:12(%di)
    movl $0x00000000, %es:16(%di)
    movl $2, %es:20(%di)

    addw $24, %di
    movl $20, %es:0(%di)
    movl $0x00100000, %es:4(%di)
    movl $0x00000000, %es:8(%di)
    movl $0x00F00000, %es:12(%di)
    movl $0x00000000, %es:16(%di)
    movl $1, %es:20(%di)
    ret

load_kernel_segments:
    movw $msg_load, %si
    call print
    movw $load_plan, %si
    movw $LOAD_PLAN_COUNT, %bp

segment_loop:
    movl 4(%si), %edi
    movl 8(%si), %ecx
    movw 0(%si), %ax
    movw %ax, cur_lba
    movw $0, progress_count

sector_loop:
    pushw %si
    pushw %bp
    pushl %ecx
    pushl %edi
    movw cur_lba, %ax
    call read_sector
    popl %edi
    call copy_bounce_to_high
    popl %ecx
    popw %bp
    popw %si
    incw cur_lba
    incw progress_count
    testw $0x007F, progress_count
    jnz 1f
    movb $'.', %al
    call print_char
1:
    decl %ecx
    jnz sector_loop

    movl 4(%si), %edi
    addl 12(%si), %edi
    movl 16(%si), %ecx
    subl 12(%si), %ecx
    call zero_high

    addw $20, %si
    decw %bp
    jnz segment_loop
    ret

/* AX=LBA, reads one 512-byte sector to 0000:7000. */
read_sector:
    pushw %ax
    call lba_to_chs
    xorw %ax, %ax
    movw %ax, %es
    movw $BOUNCE_OFF, %bx
    movb boot_drive, %dl
    movw $0x0201, %ax
    int $0x13
    jc disk_error
    popw %ax
    ret

disk_error:
    movw $msg_disk_error, %si
    call print
    jmp halt

/* AX=LBA, returns CH=cylinder, CL=sector, DH=head. */
lba_to_chs:
    xorw %dx, %dx
    movw $(SECTORS_TRACK * HEADS), %bx
    divw %bx
    movb %al, %ch
    movw %dx, %ax
    xorw %dx, %dx
    movw $SECTORS_TRACK, %bx
    divw %bx
    movb %al, %dh
    movb %dl, %cl
    incb %cl
    ret

/* Copies 512 bytes from 0000:7000 to GS:EDI. */
copy_bounce_to_high:
    pushw %fs
    xorw %ax, %ax
    movw %ax, %fs
    movl $BOUNCE_OFF, %esi
    movw $128, %cx
1:
    movl %fs:(%esi), %eax
    movl %eax, %gs:(%edi)
    addl $4, %esi
    addl $4, %edi
    loop 1b
    popw %fs
    ret

zero_high:
    testl %ecx, %ecx
    jz 3f
    movb $'z', %al
    call print_char
    xorl %eax, %eax
    movl %ecx, %edx
    shrl $2, %ecx
    jz 2f
1:
    movl %eax, %gs:(%edi)
    addl $4, %edi
    decl %ecx
    jnz 1b
2:
    movl %edx, %ecx
    andl $3, %ecx
    jz 3f
    xorb %al, %al
2:
    movb %al, %gs:(%edi)
    incl %edi
    decl %ecx
    jnz 2b
3:
    ret

enter_kernel:
    cli
    movw $msg_jump, %si
    call print
    lgdt gdt_desc
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x18, $(STAGE2_PHYS + pm32_entry)

print:
    lodsb
    testb %al, %al
    jz 1f
    call print_char
    jmp print
1:
    ret

print_char:
    call serial_putc
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    ret

serial_init:
    movw $0x3F9, %dx
    xorb %al, %al
    outb %al, %dx
    movw $0x3FB, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $0x3F8, %dx
    movb $0x01, %al
    outb %al, %dx
    movw $0x3F9, %dx
    xorb %al, %al
    outb %al, %dx
    movw $0x3FB, %dx
    movb $0x03, %al
    outb %al, %dx
    movw $0x3FA, %dx
    movb $0xC7, %al
    outb %al, %dx
    movw $0x3FC, %dx
    movb $0x0B, %al
    outb %al, %dx
    ret

serial_putc:
    pushw %ax
    pushw %dx
    movb %al, %ah
1:
    movw $0x3FD, %dx
    inb %dx, %al
    testb $0x20, %al
    jz 1b
    movw $0x3F8, %dx
    movb %ah, %al
    outb %al, %dx
    popw %dx
    popw %ax
    ret

boot_drive:
    .byte 0
cur_lba:
    .word 0
progress_count:
    .word 0
vesa_ok:
    .byte 0
vesa_mode:
    .word 0
vesa_modes:
    .word 0x4118, 0x4115, 0x4112, 0

msg_boot:
    .asciz "PC386 floppy loader\r\n"
msg_load:
    .asciz "Loading MMBASIC.ELF\r\n"
msg_jump:
    .asciz "Starting kernel\r\n"
msg_disk_error:
    .asciz "PC386 floppy stage2 disk error\r\n"

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x008F9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00CF9A000000FFFF
gdt_end:
gdt_desc:
    .word gdt_end - gdt - 1
    .long STAGE2_PHYS + gdt

load_plan:
#include "floppy_load_plan.inc"

.code32
pm32_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $MB_MAGIC, %eax
    movl $MB_INFO_ADDR, %ebx
    movl $KERNEL_ENTRY, %edi
    jmp *%edi
