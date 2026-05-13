#include "pc386_panic.h"
#include "kprint.h"

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
