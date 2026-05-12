/*
 * PC386 boot floppy stage 1.
 *
 * BIOS loads this sector at 0000:7C00. It reads the fixed reserved-sector
 * stage2 payload into 0800:0000 and jumps there, preserving DL as the BIOS
 * boot drive number.
 */

.code16
.section .text
.global _start

.set STAGE2_SEG,     0x0800
.set STAGE2_SECTORS, 127
.set SECTORS_TRACK,  18
.set HEADS,          2

_start:
    jmp boot_start
    nop

    .ascii "PC386   "          /* OEM */
    .word  512                 /* bytes/sector */
    .byte  1                   /* sectors/cluster */
    .word  128                 /* reserved sectors, including stage2 */
    .byte  2                   /* FAT copies */
    .word  224                 /* root entries */
    .word  2880                /* total sectors */
    .byte  0xF0                /* media */
    .word  9                   /* sectors/FAT */
    .word  18                  /* sectors/track */
    .word  2                   /* heads */
    .long  0                   /* hidden sectors */
    .long  0                   /* large sector count */
    .byte  0                   /* drive */
    .byte  0                   /* reserved */
    .byte  0x29                /* extended boot signature */
    .long  0x50433338          /* serial */
    .ascii "PC386BOOT  "       /* volume label */
    .ascii "FAT12   "          /* filesystem */

boot_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7C00, %sp
    sti

    movb %dl, boot_drive
    call serial_init
    movw $boot_msg, %si
    call print
    call reset_drive

    movw $STAGE2_SEG, %ax
    movw %ax, %es
    movw $0, dest_off
    movw $1, %si
    movw $STAGE2_SECTORS, %di

read_stage2:
    movb $3, retry_count
read_retry:
    movw %si, %ax
    call lba_to_chs
    movw dest_off, %bx
    movb boot_drive, %dl
    movw $0x0201, %ax
    int $0x13
    jnc read_ok
    call reset_drive
    decb retry_count
    jnz read_retry
    jmp disk_error
read_ok:
    addw $512, dest_off
    incw %si
    decw %di
    jnz read_stage2

    movb boot_drive, %dl
    ljmp $STAGE2_SEG, $0x0000

disk_error:
    movw $disk_msg, %si
    call print
halt:
    cli
    hlt
    jmp halt

reset_drive:
    xorb %ah, %ah
    movb boot_drive, %dl
    int $0x13
    ret

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

print:
    lodsb
    testb %al, %al
    jz 1f
    call serial_putc
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp print
1:
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
dest_off:
    .word 0
retry_count:
    .byte 0
boot_msg:
    .asciz "PC386 floppy stage1\r\n"
disk_msg:
    .asciz "PC386 floppy stage1 disk error\r\n"

    .org 510
    .word 0xAA55
