/*
 * ports/pc386/idt.c — IDT setup + dispatch.
 */

#include <stdint.h>
#include <string.h>

#include "idt.h"
#include "kprint.h"
#include "pc386_panic.h"
#include "io.h"

/* 8-byte gate descriptor (Intel SDM Vol 3 §6.11). */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} idt_entry_t;

/* 6-byte register image used by lidt. */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t idt[IDT_NUM_VECTORS];
static idt_ptr_t idtr;
static idt_handler_t handlers[IDT_NUM_VECTORS];

#define BSOD_COLS 80
#define BSOD_ROWS 25
#define BSOD_ATTR 0x1F /* white on blue */
#define BSOD_BUFFER ((volatile uint16_t *)0xB8000)
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5

static uint8_t bsod_row;
static uint8_t bsod_col;

static void bsod_cursor(void) {
    uint16_t pos = (uint16_t)bsod_row * BSOD_COLS + bsod_col;
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
}

static void bsod_clear(void) {
    for (uint32_t i = 0; i < BSOD_COLS * BSOD_ROWS; i++) {
        BSOD_BUFFER[i] = ((uint16_t)BSOD_ATTR << 8) | ' ';
    }
    bsod_row = 0;
    bsod_col = 0;
    bsod_cursor();
}

static void bsod_putc(char c) {
    if (c == '\n') {
        bsod_col = 0;
        if (bsod_row < BSOD_ROWS - 1) bsod_row++;
        bsod_cursor();
        return;
    }
    if (c == '\r') {
        bsod_col = 0;
        bsod_cursor();
        return;
    }
    BSOD_BUFFER[(uint32_t)bsod_row * BSOD_COLS + bsod_col] =
        ((uint16_t)BSOD_ATTR << 8) | (uint8_t)c;
    if (++bsod_col >= BSOD_COLS) {
        bsod_col = 0;
        if (bsod_row < BSOD_ROWS - 1) bsod_row++;
    }
    bsod_cursor();
}

static void bsod_puts(const char * s) {
    if (!s) s = "(null)";
    while (*s) bsod_putc(*s++);
}

static void bsod_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    bsod_puts("0x");
    for (int i = 7; i >= 0; i--) {
        bsod_putc(hex[(v >> (i * 4)) & 0xF]);
    }
}

static void bsod_u32(uint32_t v) {
    char buf[10];
    int n = 0;
    if (v == 0) {
        bsod_putc('0');
        return;
    }
    while (v && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) bsod_putc(buf[--n]);
}

static const char * exception_name(uint32_t vec) {
    switch (vec) {
    case 0:
        return "divide error";
    case 1:
        return "debug";
    case 2:
        return "non-maskable interrupt";
    case 3:
        return "breakpoint";
    case 4:
        return "overflow";
    case 5:
        return "bound range";
    case 6:
        return "invalid opcode";
    case 7:
        return "device not available";
    case 8:
        return "double fault";
    case 10:
        return "invalid TSS";
    case 11:
        return "segment not present";
    case 12:
        return "stack fault";
    case 13:
        return "general protection";
    case 14:
        return "page fault";
    case 16:
        return "x87 floating point";
    case 17:
        return "alignment check";
    case 18:
        return "machine check";
    case 19:
        return "SIMD floating point";
    default:
        return "CPU exception";
    }
}

static uint32_t read_cr2(void) {
    uint32_t v;
    __asm__ volatile("movl %%cr2,%0" : "=r"(v));
    return v;
}

static void bsod_exception(idt_regs_t * r) {
    __asm__ volatile("cli");
    bsod_clear();
    bsod_puts("PC386 MMBasic crash\n\n");
    bsod_puts("Exception ");
    bsod_u32(r->int_no);
    bsod_puts(": ");
    bsod_puts(exception_name(r->int_no));
    bsod_putc('\n');

    bsod_puts("Context: ");
    bsod_puts((const char *)pc386_fault_context);
    bsod_putc('\n');

    bsod_puts("CS:EIP  ");
    bsod_hex32(r->cs);
    bsod_putc(':');
    bsod_hex32(r->eip);
    bsod_putc('\n');

    bsod_puts("ERR     ");
    bsod_hex32(r->err_code);
    bsod_puts("  EFLAGS ");
    bsod_hex32(r->eflags);
    bsod_putc('\n');

    if (r->int_no == 14) {
        bsod_puts("CR2     ");
        bsod_hex32(read_cr2());
        bsod_putc('\n');
    }

    bsod_puts("\nEAX ");
    bsod_hex32(r->eax);
    bsod_puts("  EBX ");
    bsod_hex32(r->ebx);
    bsod_putc('\n');
    bsod_puts("ECX ");
    bsod_hex32(r->ecx);
    bsod_puts("  EDX ");
    bsod_hex32(r->edx);
    bsod_putc('\n');
    bsod_puts("ESI ");
    bsod_hex32(r->esi);
    bsod_puts("  EDI ");
    bsod_hex32(r->edi);
    bsod_putc('\n');
    bsod_puts("EBP ");
    bsod_hex32(r->ebp);
    bsod_puts("  ESP ");
    bsod_hex32(r->esp_dummy);
    bsod_puts("\n\nSystem halted.");
    pc386_halt();
}

/* Asm stubs in idt_asm.S — one per vector. Their address goes into
 * idt[vec].offset_*; the CPU jumps to them on the matching trap/IRQ. */
extern void * isr_stub_table[IDT_NUM_VECTORS];

/* Boot.S installed the GDT with the 32-bit code segment at selector 0x08. */
#define KERNEL_CS_SELECTOR 0x08

static void idt_set(uint8_t vec, void * handler, uint8_t type) {
    uintptr_t addr = (uintptr_t)handler;
    idt[vec].offset_low = addr & 0xFFFF;
    idt[vec].selector = KERNEL_CS_SELECTOR;
    idt[vec].zero = 0;
    idt[vec].type_attr = type;
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
    idtr.base = (uint32_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

void idt_register_handler(uint8_t vector, idt_handler_t fn) {
    handlers[vector] = fn;
}

/* Common dispatch — called from the asm common stub after register save.
 * Looks up the registered C handler; runs it (or panics if NULL — that
 * means an unexpected vector fired). */
void idt_dispatch(idt_regs_t * r) {
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

void exc_unhandled(idt_regs_t * r) {
    bsod_exception(r);
}
