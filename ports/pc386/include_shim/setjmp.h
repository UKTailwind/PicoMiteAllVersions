/* ports/pc386/include_shim/setjmp.h — freestanding shim.
 *
 * x86-32 callee-saved + return-address layout: ebx, esi, edi, ebp,
 * esp, eip, eflags. Seven 32-bit slots = 28 bytes; round to 32
 * (8 slots) for any future-proofing.
 *
 * Actual setjmp/longjmp bodies are in setjmp.S.
 */
#ifndef _PC386_SETJMP_H
#define _PC386_SETJMP_H

typedef long jmp_buf[8];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
