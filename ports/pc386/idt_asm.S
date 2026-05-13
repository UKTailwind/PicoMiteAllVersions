/*
 * ports/pc386/idt_asm.S — per-vector ISR stubs + common dispatch.
 *
 * 256 stubs, one per IDT vector. Each:
 *   - pushes a fake error code (0) for vectors that don't get one from
 *     the CPU; vectors 8/10/11/12/13/14/17 do, so they skip this push
 *   - pushes the vector number
 *   - jumps to the common entry, which saves all GP regs + segments,
 *     calls C dispatch, restores, iretd
 *
 * The handler addresses live in isr_stub_table[] so idt_init() can
 * loop and write them into the IDT entries.
 *
 * AT&T syntax. Dual labels for the macro (.macro args + label local)
 * because gas's altmacro is finicky.
 */

    .text

/* ---------- Common entry shared by every stub ---------- */
    .globl  isr_common
    .type   isr_common, @function
isr_common:
    pushal                          /* edi esi ebp esp ebx edx ecx eax */
    pushl   %ds
    pushl   %es
    pushl   %fs
    pushl   %gs

    /* Reload segs to kernel-data — required for the C handler to
     * touch any global. (CPU keeps CS/SS, but DS/ES/FS/GS persist
     * from whatever was loaded; in our pure-ring-0 setup they should
     * already be 0x10 but be defensive.) */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    /* Pass pointer to the saved frame as the first C arg. The frame
     * starts above the four pushed segs — the C struct definition
     * (idt_regs_t) matches the post-pusha layout, so step over the
     * 16 bytes of pushed segs. */
    leal    16(%esp), %eax
    pushl   %eax
    call    idt_dispatch
    addl    $4, %esp

    popl    %gs
    popl    %fs
    popl    %es
    popl    %ds
    popal
    addl    $8, %esp                /* drop int_no + err_code */
    iretl
    .size   isr_common, . - isr_common

/* ---------- Per-vector stub macros ---------- */
.macro ISR_NOERR vec
    .globl  isr_\vec
isr_\vec :
    pushl   $0                      /* dummy error code */
    pushl   $\vec
    jmp     isr_common
.endm

.macro ISR_ERR vec
    .globl  isr_\vec
isr_\vec :
    /* CPU already pushed the error code. */
    pushl   $\vec
    jmp     isr_common
.endm

/* CPU exception vectors 0..31. The ones that push their own error
 * code are 8, 10, 11, 12, 13, 14, 17 (per Intel SDM Vol 3 §6.13). */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* PIC IRQ slots after 4b's remap to vectors 0x20..0x2F. */
.altmacro
.set vec, 32
.rept (256 - 32)
    ISR_NOERR %vec
    .set vec, vec + 1
.endr

/* ---------- Stub address table for idt_init() ----------
 * .altmacro lets %vec expand inside macro args; wrap the .long in a
 * trivial macro so the substitution happens before the assembler tries
 * to parse `isr_<n>` as a single token. */
.macro EMIT_STUB_ADDR n
    .long isr_\n
.endm

    .data
    .align 4
    .globl  isr_stub_table
isr_stub_table:
    .set vec, 0
    .rept 256
        EMIT_STUB_ADDR %vec
        .set vec, vec + 1
    .endr

    .section .note.GNU-stack,"",@progbits
