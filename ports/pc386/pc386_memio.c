/*
 * ports/pc386/pc386_memio.c — direct memory and I/O port access.
 *
 *   PEEK(BYTE|SHORT|WORD|INTEGER|FLOAT addr)   — memory read
 *   POKE BYTE|SHORT|WORD|INTEGER|FLOAT addr, v — memory write
 *   PEEK(PORT addr)                            — x86 inb
 *   POKE PORT addr, v                          — x86 outb / outw (auto-width)
 *
 * Real driver — issues raw load/store and x86 in/out instructions.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void cmd_poke(void) {
    unsigned char * p;
    getargs(&cmdline, 5, (unsigned char *)",");
    if ((p = checkstring(argv[0], (unsigned char *)"BYTE"))) {
        if (argc != 3) error("Argument count");
        *(uint8_t *)(uintptr_t)getinteger(p) = (uint8_t)getinteger(argv[2]);
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"SHORT"))) {
        if (argc != 3) error("Argument count");
        uint32_t a = (uint32_t)getinteger(p);
        if (a & 1) error("Address not divisible by 2");
        *(uint16_t *)(uintptr_t)a = (uint16_t)getinteger(argv[2]);
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"WORD"))) {
        if (argc != 3) error("Argument count");
        uint32_t a = (uint32_t)getinteger(p);
        if (a & 3) error("Address not divisible by 4");
        *(uint32_t *)(uintptr_t)a = (uint32_t)getinteger(argv[2]);
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"INTEGER"))) {
        if (argc != 3) error("Argument count");
        uint32_t a = (uint32_t)getinteger(p);
        if (a & 7) error("Address not divisible by 8");
        *(uint64_t *)(uintptr_t)a = (uint64_t)getinteger(argv[2]);
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"FLOAT"))) {
        if (argc != 3) error("Argument count");
        uint32_t a = (uint32_t)getinteger(p);
        if (a & 7) error("Address not divisible by 8");
        *(MMFLOAT *)(uintptr_t)a = getnumber(argv[2]);
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"PORT"))) {
        if (argc != 3) error("Argument count");
        uint16_t port = (uint16_t)getinteger(p);
        uint32_t v = (uint32_t)getinteger(argv[2]);
        if (v > 0xFF) {
            __asm__ volatile("outw %0, %1" ::"a"((uint16_t)v), "Nd"(port));
        } else {
            __asm__ volatile("outb %0, %1" ::"a"((uint8_t)v), "Nd"(port));
        }
        return;
    }
    error("Syntax");
}

void fun_peek(void) {
    unsigned char * p;
    getargs(&ep, 3, (unsigned char *)",");
    if ((p = checkstring(argv[0], (unsigned char *)"INT8")) ||
        (p = checkstring(argv[0], (unsigned char *)"BYTE"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint8_t *)(uintptr_t)getinteger(p);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"SHORT"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint16_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)1);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"WORD"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint32_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)3);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"INTEGER"))) {
        if (argc != 1) error("Syntax");
        iret = *(uint64_t *)((uintptr_t)getinteger(p) & ~(uintptr_t)7);
        targ = T_INT;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"FLOAT"))) {
        if (argc != 1) error("Syntax");
        fret = *(MMFLOAT *)((uintptr_t)getinteger(p) & ~(uintptr_t)7);
        targ = T_NBR;
        return;
    }
    if ((p = checkstring(argv[0], (unsigned char *)"PORT"))) {
        if (argc != 1) error("Syntax");
        uint16_t port = (uint16_t)getinteger(p);
        uint8_t v;
        __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
        iret = v;
        targ = T_INT;
        return;
    }
    if (argc == 1) {
        iret = *(uint8_t *)(uintptr_t)getinteger(argv[0]);
        targ = T_INT;
        return;
    }
    error("Syntax");
}
