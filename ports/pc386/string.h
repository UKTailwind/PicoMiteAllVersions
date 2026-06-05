/*
 * ports/pc386/string.h — minimal freestanding string.h for the pc386 port.
 *
 * We're building with `-ffreestanding` against i686-elf-gcc, which
 * does not ship a libc string.h. FatFs (#include "string.h") and any
 * future code path that uses memcpy/memset/etc. picks this up
 * via -I$(PORT_DIR) being first on the include search path.
 *
 * Only the handful of functions actually used in our build are
 * declared. Add more here (with implementations in kstring.c) as
 * later stages need them.
 */

#ifndef PORTS_PC386_STRING_H
#define PORTS_PC386_STRING_H

#include <stddef.h>

void * memcpy(void * dst, const void * src, size_t n);
void * memset(void * s, int c, size_t n);
void * memmove(void * dst, const void * src, size_t n);
int memcmp(const void * a, const void * b, size_t n);

size_t strlen(const char * s);
int strcmp(const char * a, const char * b);
int strncmp(const char * a, const char * b, size_t n);
int strcasecmp(const char * a, const char * b);
int strncasecmp(const char * a, const char * b, size_t n);
char * strchr(const char * s, int c);
char * strrchr(const char * s, int c);
char * strstr(const char * h, const char * n);
char * strcpy(char * d, const char * s);
char * strncpy(char * d, const char * s, size_t n);
char * strcat(char * d, const char * s);
char * strncat(char * d, const char * s, size_t n);
void * memchr(const void * p, int c, size_t n);
size_t strnlen(const char * s, size_t n);

#endif
