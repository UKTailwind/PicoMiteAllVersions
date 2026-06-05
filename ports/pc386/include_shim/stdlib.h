/* ports/pc386/include_shim/stdlib.h — freestanding shim. */
#ifndef _PC386_STDLIB_H
#define _PC386_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7FFFFFFF

void * malloc(size_t n);
void * calloc(size_t nmemb, size_t sz);
void * realloc(void * p, size_t n);
void free(void * p);

int atoi(const char * s);
long atol(const char * s);
long long atoll(const char * s);
double atof(const char * s);

long strtol(const char * s, char ** e, int base);
unsigned long strtoul(const char * s, char ** e, int base);
long long strtoll(const char * s, char ** e, int base);
unsigned long long strtoull(const char * s, char ** e, int base);
double strtod(const char * s, char ** e);
float strtof(const char * s, char ** e);

void exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

int abs(int);
long labs(long);
long long llabs(long long);

void qsort(void * base, size_t nmemb, size_t sz,
           int (*cmp)(const void *, const void *));
void * bsearch(const void * key, const void * base, size_t nmemb, size_t sz,
               int (*cmp)(const void *, const void *));

char * getenv(const char * name);

int rand(void);
void srand(unsigned int seed);

#endif
