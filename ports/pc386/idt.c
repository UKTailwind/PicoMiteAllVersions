/*
 * ports/pc386/idt.c — IDT setup + dispatch.
 */

#include <stdint.h>
#include <string.h>

#include "idt.h"
#include "kprint.h"
#include "pc386_panic.h"

/* 8-byte gate descriptor (Intel SDM Vol 3 §6.11). */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} idt_entry_t;

/* 6-byte register image used by lidt. */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t  idt[IDT_NUM_VECTORS];
static idt_ptr_t    idtr;
static idt_handler_t handlers[IDT_NUM_VECTORS];

/* Asm stubs in idt_asm.S — one per vector. Their address goes into
 * idt[vec].offset_*; the CPU jumps to them on the matching trap/IRQ. */
extern void *isr_stub_table[IDT_NUM_VECTORS];

/* Boot.S installed the GDT with the 32-bit code segment at selector 0x08. */
#define KERNEL_CS_SELECTOR  0x08

static void idt_set(uint8_t vec, void *handler, uint8_t type) {
    uintptr_t addr = (uintptr_t)handler;
    idt[vec].offset_low  = addr & 0xFFFF;
    idt[vec].selector    = KERNEL_CS_SELECTOR;
    idt[vec].zero        = 0;
    idt[vec].type_attr   = type;
    idt[vec].offset_high = (addr >> 16) & 0xFFFF;
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    memset(handlers, 0, sizeof(handlers));

    for (int v = 0; v < IDT_NUM_VECTORS; v++) {
        idt_set((uint8_t)v, isr_stub_table[v], IDT_GATE_INTR32);
    }

    /* All exception vectors default to exc_unhandled until something
     * registers a real handler. PIC IRQ slots also default here; 4b
     * registers real ones. */
    for (int v = 0; v < 32; v++) handlers[v] = exc_unhandled;

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void idt_register_handler(uint8_t vector, idt_handler_t fn) {
    handlers[vector] = fn;
}

/* Common dispatch — called from the asm common stub after register save.
 * Looks up the registered C handler; runs it (or panics if NULL — that
 * means an unexpected vector fired). */
void idt_dispatch(idt_regs_t *r) {
    idt_handler_t fn = handlers[r->int_no & 0xFF];
    if (fn) {
        fn(r);
        return;
    }
    kputs("\n*** UNHANDLED INTERRUPT ");
    kputu32(r->int_no);
    kputs(" at EIP=");
    kputhex32(r->eip);
    kputs(" ***\n");
    pc386_panic("unhandled interrupt");
}

void exc_unhandled(idt_regs_t *r) {
    kputs("\n*** EXCEPTION #");
    kputu32(r->int_no);
    kputs(" at CS:EIP=");
    kputhex32(r->cs);
    kputs(":");
    kputhex32(r->eip);
    kputs(" err=");
    kputhex32(r->err_code);
    kputs(" eflags=");
    kputhex32(r->eflags);
    kputs(" ***\n");
    pc386_panic("CPU exception");
}
