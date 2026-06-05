/*
 * ports/pc386/kprint.h — minimal kernel print helpers.
 *
 * Bare-metal C has no <stdio.h>. These helpers cover the ~10% of
 * format-printing the early-boot stages need (literal strings, hex,
 * unsigned decimal). Both VGA text and serial COM1 receive every
 * write — neither console is selected away from until later stages.
 *
 * No printf-style varargs. Use the named helpers; they're cheap and
 * the compiler can inline trivially.
 */

#ifndef PORTS_PC386_KPRINT_H
#define PORTS_PC386_KPRINT_H

#include <stdint.h>

void kputc(char c);
void kputs(const char * s);
void kputhex32(uint32_t v);
void kputhex64(uint64_t v);
void kputu32(uint32_t v); /* unsigned decimal, no leading zeroes */
void kputu64(uint64_t v);

#endif
