/*
 * ports/pc386/kstring.c — minimal freestanding string functions.
 *
 * GCC with -ffreestanding still expects memcpy/memset/memmove/memcmp
 * to be available at link time — it can synthesise calls to them for
 * struct assignment, large literal initialisers, etc. — so we must
 * provide real (non-inline) implementations here.
 *
 * These are the textbook simple loops. Performance doesn't matter
 * for early boot; once the BASIC interpreter runs, hot paths inside
 * MMBasic have their own optimised string handling (in core/state and
 * elsewhere) and don't go through this layer.
 */

#include "string.h"

void * memcpy(void * dst, const void * src, size_t n) {
    unsigned char * d = dst;
    const unsigned char * s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void * memset(void * s, int c, size_t n) {
    unsigned char * p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void * memmove(void * dst, const void * src, size_t n) {
    unsigned char * d = dst;
    const unsigned char * s = src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void * a, const void * b, size_t n) {
    const unsigned char * p = a;
    const unsigned char * q = b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++;
        q++;
    }
    return 0;
}

size_t strlen(const char * s) {
    const char * p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char * a, const char * b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char * a, const char * b, size_t n) {
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char * strchr(const char * s, int c) {
    char target = (char)c;
    while (*s) {
        if (*s == target) return (char *)s;
        s++;
    }
    return target == '\0' ? (char *)s : NULL;
}
