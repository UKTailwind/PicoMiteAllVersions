/*
 * ports/pc386/idt.h — Interrupt Descriptor Table.
 *
 * 256 vectors:
 *   0..31    CPU exceptions (Divide Error, Page Fault, GP, etc.)
 *   32..47   PIC IRQs (after 4b's PIC remap to 0x20..0x2F)
 *   48..255  reserved / unused for stage 4
 *
 * idt_init() lays out the table and lidt's it. CPU exceptions wire to
 * exc_unhandled (in idt.c) which prints the vector + EIP and halts —
 * better than a triple-fault when something goes wrong. PIC IRQ slots
 * start as unhandled too; 4b registers real handlers via
 * idt_register_handler().
 */
#ifndef PORTS_PC386_IDT_H
#define PORTS_PC386_IDT_H

#include <stdint.h>

/* Total IDT vector count. */
#define IDT_NUM_VECTORS 256

/* Gate type byte: present=1, DPL=0, 32-bit interrupt gate. */
#define IDT_GATE_INTR32  0x8E

/* CPU register snapshot the asm stubs push before calling into C.
 * Order matches the actual stack layout (top to bottom of struct):
 *
 *   pushed by stub:    edi esi ebp esp ebx edx ecx eax  (pusha)
 *   pushed by stub:    int_no  err_code
 *   pushed by CPU:     eip cs eflags  (and ss/esp if cpl change)
 *
 * The common-handler saves pusha first then reads back via this layout. */
typedef struct {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} idt_regs_t;

/* Install the table + lidt. Safe to call once at boot. */
void idt_init(void);

/* Register a C handler for a specific vector. The handler is called
 * from the common asm dispatcher with a pointer to the saved register
 * frame. NULL clears the slot back to "unhandled". */
typedef void (*idt_handler_t)(idt_regs_t *r);
void idt_register_handler(uint8_t vector, idt_handler_t fn);

/* Convenience for stage 4b — print "EXCEPTION %d at %08x" and halt. */
void exc_unhandled(idt_regs_t *r);

#endif
