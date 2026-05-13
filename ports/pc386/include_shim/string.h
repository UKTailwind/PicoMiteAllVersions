/* ports/pc386/include_shim/string.h — freestanding shim. */
#ifndef _PC386_STRING_H
#define _PC386_STRING_H

#include <stddef.h>

void *memcpy (void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset (void *p, int c, size_t n);
int   memcmp (const void *a, const void *b, size_t n);
void *memchr (const void *p, int c, size_t n);

size_t strlen (const char *s);
size_t strnlen(const char *s, size_t n);
char  *strcpy (char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat (char *d, const char *s);
char  *strncat(char *d, const char *s, size_t n);
int    strcmp (const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcasecmp (const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);
char  *strchr (const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr (const char *h, const char *n);
char  *strdup (const char *s);
char  *strerror(int e);
char  *strtok  (char *s, const char *delim);
char  *strtok_r(char *s, const char *delim, char **saveptr);

#endif
