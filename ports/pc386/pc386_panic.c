#include "pc386_panic.h"
#include "kprint.h"

volatile const char *pc386_fault_context = "";

void pc386_fault_set_context(const char *ctx) {
    pc386_fault_context = ctx ? ctx : "";
}

void pc386_fault_clear_context(void) {
    pc386_fault_context = "";
}

void pc386_halt(void) {
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void pc386_panic(const char *msg) {
    kputs("\n*** PANIC: ");
    kputs(msg ? msg : "(null)");
    kputs(" ***\n");
    pc386_halt();
}
