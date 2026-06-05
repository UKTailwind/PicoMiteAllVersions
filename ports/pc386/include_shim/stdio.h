/* ports/pc386/include_shim/stdio.h — freestanding shim.
 *
 * No real FILE* I/O — there's no kernel underneath us. printf and the
 * sprintf family are provided by pc386_printf.c (vendored
 * mpaland/printf, single-translation-unit reentrant impl). FILE-based
 * I/O calls panic; MMBasic core's own file path routes through
 * hal_filesystem instead. fopen/fread/fwrite stubs exist so files
 * including <stdio.h> for prototypes still compile.
 */
#ifndef _PC386_STDIO_H
#define _PC386_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct _FILE FILE;

#define EOF (-1)
#ifndef NULL
#define NULL ((void *)0)
#endif
#define BUFSIZ 512
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE * stdin;
extern FILE * stdout;
extern FILE * stderr;

int printf(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
int sprintf(char * s, const char * fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char * s, size_t n, const char * fmt, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char * s, size_t n, const char * fmt, va_list ap);
int vsprintf(char * s, const char * fmt, va_list ap);
int fprintf(FILE * fp, const char * fmt, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE * fp, const char * fmt, va_list ap);

int fputs(const char * s, FILE * fp);
int fputc(int c, FILE * fp);
int putc(int c, FILE * fp);
int putchar(int c);
int puts(const char * s);
int fgetc(FILE * fp);
int getc(FILE * fp);
char * fgets(char * s, int n, FILE * fp);
size_t fread(void * p, size_t sz, size_t n, FILE * fp);
size_t fwrite(const void * p, size_t sz, size_t n, FILE * fp);

FILE * fopen(const char * path, const char * mode);
int fclose(FILE * fp);
int fflush(FILE * fp);
int feof(FILE * fp);
int ferror(FILE * fp);
int fseek(FILE * fp, long off, int whence);
long ftell(FILE * fp);
void rewind(FILE * fp);

void perror(const char * s);
int remove(const char * path);
int rename(const char * from, const char * to);

int sscanf(const char * s, const char * fmt, ...) __attribute__((format(scanf, 2, 3)));
int vsscanf(const char * s, const char * fmt, va_list ap);

#endif
