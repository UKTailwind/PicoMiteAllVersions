/*
 * ports/pc386/bios_video.S - BIOS INT 10h thunk for video mode changes.
 *
 * Paging is off in the pc386 kernel, so low physical addresses are
 * directly addressable. We copy a small thunk to conventional memory,
 * temporarily return to real mode, call the BIOS, then restore protected
 * mode and the kernel IDT.
 */

.set RM_THUNK_ADDR, 0x7000
.set RM_STACK,      0x6FF0
.set RM_AX,         0x7F00
.set RM_BX,         0x7F02
.set RM_CX,         0x7F04
.set RM_DX,         0x7F06
.set RM_ES,         0x7F08
.set RM_DI,         0x7F0A
.set RM_STATUS,     0x7F0C
.set RM_SAVED_ESP,  0x7F10
.set RM_SAVED_IDTR, 0x7F18
.set RM_REAL_IDTR,  0x7F20
.set RM_GDTR,       0x7F28
.set RM_SAVED_GDTR, 0x7F30
.set RM_GDT,        0x7F40

.section .text
.code32
.global pc386_bios_video_int10
.type pc386_bios_video_int10, @function
pc386_bios_video_int10:
    pushfl
    cli
    pushal

    movl %esp, RM_SAVED_ESP
    sidt RM_SAVED_IDTR
    sgdt RM_SAVED_GDTR

    movw 40(%esp), %ax
    movw %ax, RM_AX
    movw 44(%esp), %ax
    movw %ax, RM_BX
    movw 48(%esp), %ax
    movw %ax, RM_CX
    movw 52(%esp), %ax
    movw %ax, RM_DX
    movw 56(%esp), %ax
    movw %ax, RM_ES
    movw 60(%esp), %ax
    movw %ax, RM_DI
    movw $0, RM_STATUS

    cld
    movl $bios_thunk_start, %esi
    movl $RM_THUNK_ADDR, %edi
    movl $(bios_thunk_end - bios_thunk_start), %ecx
    rep movsb

    movw $0x03FF, RM_REAL_IDTR
    movl $0x00000000, RM_REAL_IDTR + 2

    movw $31, RM_GDTR
    movl $RM_GDT, RM_GDTR + 2
    movl $0x00000000, RM_GDT
    movl $0x00000000, RM_GDT + 4
    movl $0x0000FFFF, RM_GDT + 8
    movl $0x00CF9A00, RM_GDT + 12
    movl $0x0000FFFF, RM_GDT + 16
    movl $0x00CF9200, RM_GDT + 20
    movl $0x0000FFFF, RM_GDT + 24
    movl $0x00009A00, RM_GDT + 28

    lidt RM_REAL_IDTR
    lgdt RM_GDTR
    ljmp $0x0018, $RM_THUNK_ADDR

bios_pm_return:
    lgdt RM_SAVED_GDTR
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    lidt RM_SAVED_IDTR
    movl RM_SAVED_ESP, %esp
    popal
    popfl
    movzwl RM_STATUS, %eax
    ret
.size pc386_bios_video_int10, . - pc386_bios_video_int10

.code16
bios_thunk_start:
    cli
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax
    movl %eax, %cr0
    ljmp $0x0000, $(RM_THUNK_ADDR + (bios_real_entry - bios_thunk_start))

bios_real_entry:
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movw $RM_STACK, %sp

    movw RM_ES, %ax
    movw %ax, %es
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %edi, %edi
    movw RM_AX, %ax
    movw RM_BX, %bx
    movw RM_CX, %cx
    movw RM_DX, %dx
    movw RM_DI, %di
    sti
    int $0x10
    cli
    movw %ax, RM_STATUS

    lgdt RM_GDTR
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x0008, $(RM_THUNK_ADDR + (bios_thunk_pm - bios_thunk_start))

.code32
bios_thunk_pm:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $0x0008, $bios_pm_return

bios_thunk_end:

.code32
