/*
 * ports/pc386/pc386_libc.c — freestanding libc.
 *
 * Hand-rolled implementations of the standard C library entries
 * MMBasic core uses but i686-elf-gcc doesn't ship. Companions:
 *   kstring.c           — memcpy/memset/memmove/memcmp/strlen/
 *                         strcmp/strncmp/strchr (already there
 *                         from the early-boot kernel; kept as-is)
 *   pc386_printf.c      — vendored mpaland printf/sprintf/snprintf
 *   ports/pc386/setjmp.S — i686 setjmp/longjmp
 *   pc386_math.c        — vendored openlibm trig/log/exp (TODO)
 *
 * malloc/free route through the kernel heap region until 3c.2 wires
 * MMBasic's own bc_alloc on top of it.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "pc386_panic.h"

/* ===== errno ============================================================ */
int errno = 0;

/* ===== stdio: stdin/stdout/stderr + printf family ======================= */
/* The struct _FILE typedef in <stdio.h> is opaque; we need an actual
 * incomplete-type definition for the externs to resolve. The pointers
 * are never dereferenced — fopen/fread/fwrite all panic. */
struct _FILE { int placeholder; };
static struct _FILE pc386_stdin_obj  = {0};
static struct _FILE pc386_stdout_obj = {0};
static struct _FILE pc386_stderr_obj = {0};
FILE *stdin  = &pc386_stdin_obj;
FILE *stdout = &pc386_stdout_obj;
FILE *stderr = &pc386_stderr_obj;

/* ----- Minimal vsnprintf -----
 *
 * Handles the format specifiers MMBasic core actually uses (grepped):
 *   %% %c %s %d %u %g  (with width / 0-padding / precision modifiers
 *                       like %02d, %04d, %.100s)
 *
 * %g uses MMBasic's own FloatToStr (already linked in) — which is what
 * the rest of the interpreter uses for PRINT, so we get the same
 * formatting for free. Other format letters that aren't on this list
 * panic so the caller surfaces visibly.
 *
 * Compact (~150 LOC). Replace with vendored mpaland printf if a
 * BASIC program ever exercises a richer specifier set.
 */

extern char *IntToStr(char *strr, int64_t nbr, unsigned int base);
extern void  FloatToStr(char *p, double f, int m1, int m2, unsigned char ch);

static void pc386_emit(char **out, char *end, char c) {
    if (*out < end - 1) *(*out)++ = c;
}

static void pc386_emit_str(char **out, char *end, const char *s,
                            int width, int left, int max) {
    int n = 0;
    if (s == NULL) s = "(null)";
    const char *p = s;
    while (*p && (max < 0 || n < max)) { p++; n++; }
    int pad = width > n ? width - n : 0;
    if (!left) while (pad--) pc386_emit(out, end, ' ');
    int k = n;
    while (k--) pc386_emit(out, end, *s++);
    if (left) while (pad--) pc386_emit(out, end, ' ');
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
    if (n == 0) return 0;
    char *p   = out;
    char *end = out + n;
    char numbuf[32];

    while (*fmt) {
        if (*fmt != '%') { pc386_emit(&p, end, *fmt++); continue; }
        fmt++;

        int left = 0, zero = 0;
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zero = 1;
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        /* Length modifier — MMBasic uses none of l, ll, h, etc. with
         * any of the formats grepped, but accept and ignore for safety. */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j') fmt++;

        char fc = *fmt++;
        switch (fc) {
            case '%': pc386_emit(&p, end, '%'); break;
            case 'c': pc386_emit(&p, end, (char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                pc386_emit_str(&p, end, s, width, left, prec);
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = va_arg(ap, int);
                IntToStr(numbuf, v, 10);
                int len = (int)strlen(numbuf);
                int pad = width > len ? width - len : 0;
                if (!left && !zero) while (pad--) pc386_emit(&p, end, ' ');
                if (zero && pad > 0 && (numbuf[0] == '-')) {
                    pc386_emit(&p, end, '-');
                    while (pad--) pc386_emit(&p, end, '0');
                    for (char *q = numbuf + 1; *q; q++) pc386_emit(&p, end, *q);
                } else {
                    if (zero && !left) while (pad--) pc386_emit(&p, end, '0');
                    for (char *q = numbuf; *q; q++) pc386_emit(&p, end, *q);
                    if (left) while (pad--) pc386_emit(&p, end, ' ');
                }
                break;
            }
            case 'u': {
                unsigned u = va_arg(ap, unsigned);
                IntToStr(numbuf, (int64_t)u, 10);
                pc386_emit_str(&p, end, numbuf, width, left, -1);
                break;
            }
            case 'x':
            case 'X': {
                unsigned u = va_arg(ap, unsigned);
                IntToStr(numbuf, (int64_t)u, 16);
                if (fc == 'x') {
                    for (char *q = numbuf; *q; q++) {
                        if (*q >= 'A' && *q <= 'Z') *q = (char)(*q - 'A' + 'a');
                    }
                }
                pc386_emit_str(&p, end, numbuf, width, left, -1);
                break;
            }
            case 'g':
            case 'f':
            case 'e': {
                double v = va_arg(ap, double);
                FloatToStr(numbuf, v, 0, prec >= 0 ? prec : 6, ' ');
                pc386_emit_str(&p, end, numbuf, width, left, -1);
                break;
            }
            default:
                /* Unknown specifier — drop it visibly so we notice. */
                pc386_emit(&p, end, '?');
                pc386_emit(&p, end, fc);
                break;
        }
    }

    if (p < end) *p = '\0';
    else end[-1] = '\0';
    return (int)(p - out);
}

int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *s, const char *fmt, va_list ap) {
    return vsnprintf(s, (size_t)0x7FFFFFFF, fmt, ap);
}

int printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    extern void MMPrintString(char *);
    MMPrintString(buf);
    return r;
}

int fprintf(FILE *fp, const char *fmt, ...) {
    /* All FILE* paths route to the console on pc386 (no real FILE I/O). */
    (void)fp;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    extern void MMPrintString(char *);
    MMPrintString(buf);
    return r;
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    (void)fp;
    char buf[512];
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    extern void MMPrintString(char *);
    MMPrintString(buf);
    return r;
}

int sscanf(const char *s, const char *fmt, ...) {
    (void)s; (void)fmt;
    pc386_panic("sscanf not yet implemented");
}

int vsscanf(const char *s, const char *fmt, va_list ap) {
    (void)s; (void)fmt; (void)ap;
    pc386_panic("vsscanf not yet implemented");
}

int  fputs (const char *s, FILE *fp) { (void)s; (void)fp; return 0; }
int  fputc (int c, FILE *fp)         { (void)c; (void)fp; return c; }
int  putc  (int c, FILE *fp)         { return fputc(c, fp); }
int  putchar(int c)                  { extern char SerialConsolePutC(char, int); return SerialConsolePutC((char)c, 0); }
int  puts  (const char *s)           { extern void SSPrintString(char *); SSPrintString((char *)s); putchar('\n'); return 0; }
int  fgetc (FILE *fp)                { (void)fp; return -1; }
int  getc  (FILE *fp)                { return fgetc(fp); }
char *fgets(char *s, int n, FILE *fp){ (void)s; (void)n; (void)fp; return NULL; }
size_t fread (void *p, size_t sz, size_t n, FILE *fp)       { (void)p; (void)sz; (void)n; (void)fp; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *fp) { (void)p; (void)sz; (void)n; (void)fp; return 0; }
FILE *fopen (const char *path, const char *mode)            { (void)path; (void)mode; return NULL; }
int   fclose(FILE *fp)              { (void)fp; return 0; }
int   fflush(FILE *fp)              { (void)fp; return 0; }
int   feof  (FILE *fp)              { (void)fp; return 1; }
int   ferror(FILE *fp)              { (void)fp; return 0; }
int   fseek (FILE *fp, long off, int whence) { (void)fp; (void)off; (void)whence; return -1; }
long  ftell (FILE *fp)              { (void)fp; return -1; }
void  rewind(FILE *fp)              { (void)fp; }
void  perror(const char *s)         { (void)s; }
int   remove(const char *path)      { (void)path; return -1; }
int   rename(const char *from, const char *to) { (void)from; (void)to; return -1; }

/* ===== string (additions beyond kstring.c) ============================== */

size_t strnlen(const char *s, size_t n) {
    const char *p = s;
    while (n-- && *p) p++;
    return (size_t)(p - s);
}

char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}

char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && (*d++ = *s++)) n--;
    while (n--) *d++ = '\0';
    return r;
}

char *strcat(char *d, const char *s) {
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) { }
    return r;
}

char *strncat(char *d, const char *s, size_t n) {
    char *r = d;
    while (*d) d++;
    while (n-- && (*d++ = *s++)) { }
    if (n == (size_t)-1) *d = '\0';  /* loop wrote terminator already */
    else                  *d = '\0';
    return r;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    char target = (char)c;
    do {
        if (*s == target) last = s;
    } while (*s++);
    return (char *)last;
}

char *strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return NULL;
}

void *memchr(const void *p, int c, size_t n) {
    const unsigned char *s = p;
    unsigned char target = (unsigned char)c;
    while (n--) {
        if (*s == target) return (void *)s;
        s++;
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (!r) return NULL;
    memcpy(r, s, n);
    return r;
}

char *strerror(int e) {
    /* MMBasic core only uses strerror() in diagnostic prints. The
     * exact text doesn't matter; "errno=N" is unambiguous. */
    static char buf[16];
    char *p = buf;
    *p++ = 'e'; *p++ = 'r'; *p++ = 'r'; *p++ = 'n'; *p++ = 'o'; *p++ = '=';
    if (e < 0) { *p++ = '-'; e = -e; }
    if (e >= 100) { *p++ = '0' + (e / 100) % 10; }
    if (e >= 10)  { *p++ = '0' + (e / 10)  % 10; }
    *p++ = '0' + e % 10;
    *p = '\0';
    return buf;
}

char *strtok_r(char *s, const char *delim, char **saveptr) {
    if (s) *saveptr = s;
    if (!*saveptr) return NULL;
    char *p = *saveptr;
    while (*p && strchr(delim, *p)) p++;
    if (!*p) { *saveptr = NULL; return NULL; }
    char *start = p;
    while (*p && !strchr(delim, *p)) p++;
    if (*p) { *p = '\0'; *saveptr = p + 1; }
    else    { *saveptr = NULL; }
    return start;
}

static char *strtok_state = NULL;
char *strtok(char *s, const char *delim) {
    return strtok_r(s, delim, &strtok_state);
}

/* ===== ctype ============================================================ */

int isalpha (int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isdigit (int c) { return c >= '0' && c <= '9'; }
int isalnum (int c) { return isalpha(c) || isdigit(c); }
int isspace (int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isupper (int c) { return c >= 'A' && c <= 'Z'; }
int islower (int c) { return c >= 'a' && c <= 'z'; }
int isprint (int c) { return c >= 0x20 && c <= 0x7e; }
int isgraph (int c) { return c >  0x20 && c <= 0x7e; }
int ispunct (int c) { return isprint(c) && !isspace(c) && !isalnum(c); }
int iscntrl (int c) { return (c >= 0 && c < 0x20) || c == 0x7f; }
int isascii (int c) { return (unsigned)c < 128; }
int toupper (int c) { return islower(c) ? c - ('a' - 'A') : c; }
int tolower (int c) { return isupper(c) ? c + ('a' - 'A') : c; }

/* ===== stdlib: atoi family ============================================== */

int abs (int x)             { return x < 0 ? -x : x; }
long labs(long x)           { return x < 0 ? -x : x; }
int64_t llabs(int64_t x){ return x < 0 ? -x : x; }

static const char *skip_ws_sign(const char *s, int *neg) {
    *neg = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { *neg = 1; s++; }
    else if (*s == '+') s++;
    return s;
}

int atoi(const char *s) {
    int neg, v = 0;
    s = skip_ws_sign(s, &neg);
    while (isdigit((unsigned char)*s)) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

long atol(const char *s) {
    int neg;
    long v = 0;
    s = skip_ws_sign(s, &neg);
    while (isdigit((unsigned char)*s)) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

int64_t atoll(const char *s) {
    int neg;
    int64_t v = 0;
    s = skip_ws_sign(s, &neg);
    while (isdigit((unsigned char)*s)) { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base) {
    int neg;
    long v = 0;
    s = skip_ws_sign(s, &neg);
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && s[0] == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    while (*s) {
        int d;
        if (isdigit((unsigned char)*s))      d = *s - '0';
        else if (*s >= 'a' && *s <= 'z')     d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')     d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **end, int base) {
    return (unsigned long)strtol(s, end, base);
}

int64_t strtoll(const char *s, char **end, int base) {
    /* MMBasic uses strtoll for HEX$/BIN$/OCT$ parsing; integer-only.
     * Re-do strtol's body in long-long for correct overflow on values
     * like 0xFFFFFFFF that don't fit in 32-bit long. */
    int neg;
    int64_t v = 0;
    s = skip_ws_sign(s, &neg);
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    } else if (base == 0 && s[0] == '0') {
        s++; base = 8;
    } else if (base == 0) {
        base = 10;
    }
    while (*s) {
        int d;
        if (isdigit((unsigned char)*s))      d = *s - '0';
        else if (*s >= 'a' && *s <= 'z')     d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')     d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    return (unsigned long long)strtoll(s, end, base);
}

static double pc386_pow10_int(int exp) {
    double scale = 1.0;
    double base = 10.0;
    unsigned int n;

    if (exp < 0) n = (unsigned int)-exp;
    else n = (unsigned int)exp;

    while (n) {
        if (n & 1U) scale *= base;
        base *= base;
        n >>= 1;
    }
    return exp < 0 ? 1.0 / scale : scale;
}

/* atof / strtod / strtof — minimal decimal parser for MMBasic numeric input. */
double atof(const char *s) {
    return strtod(s, NULL);
}

double strtod(const char *s, char **end) {
    const char *p = s;
    int neg = 0;
    int any = 0;
    double v = 0.0;

    while (isspace((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }

    while (isdigit((unsigned char)*p)) {
        v = v * 10.0 + (double)(*p - '0');
        p++;
        any = 1;
    }

    if (*p == '.') {
        double place = 0.1;
        p++;
        while (isdigit((unsigned char)*p)) {
            v += (double)(*p - '0') * place;
            place *= 0.1;
            p++;
            any = 1;
        }
    }

    if (any && (*p == 'e' || *p == 'E')) {
        const char *exp_start = p;
        int exp_neg = 0;
        int exp_any = 0;
        int exp_val = 0;
        p++;
        if (*p == '+' || *p == '-') {
            exp_neg = (*p == '-');
            p++;
        }
        while (isdigit((unsigned char)*p)) {
            if (exp_val < 308) exp_val = exp_val * 10 + (*p - '0');
            p++;
            exp_any = 1;
        }
        if (exp_any) v *= pc386_pow10_int(exp_neg ? -exp_val : exp_val);
        else p = exp_start;
    }

    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

float strtof(const char *s, char **end) {
    return (float)strtod(s, end);
}

/* qsort — naive in-place quicksort. MMBasic uses it for FILES sorting
 * and a few other small arrays; performance not critical. */
static void qsort_swap(char *a, char *b, size_t sz) {
    while (sz--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t n, size_t sz,
           int (*cmp)(const void *, const void *)) {
    if (n < 2) return;
    char *a = (char *)base;
    char *pivot = a + (n / 2) * sz;
    /* Move pivot to front. */
    qsort_swap(a, pivot, sz);
    pivot = a;
    size_t lo = sz, hi = n * sz;
    while (lo < hi) {
        if (cmp(a + lo, pivot) < 0) lo += sz;
        else {
            hi -= sz;
            qsort_swap(a + lo, a + hi, sz);
        }
    }
    lo -= sz;
    qsort_swap(pivot, a + lo, sz);
    qsort(a, lo / sz, sz, cmp);
    qsort(a + lo + sz, n - lo / sz - 1, sz, cmp);
}

void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *)) {
    const char *a = base;
    while (n) {
        size_t mid = n / 2;
        const char *p = a + mid * sz;
        int r = cmp(key, p);
        if (r == 0) return (void *)p;
        if (r < 0) n = mid;
        else { a = p + sz; n -= mid + 1; }
    }
    return NULL;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;  /* No process environment on bare metal. */
}

/* rand/srand — share the xorshift32 state in hal_random_pc386.c so
 * MMBasic's RANDOMIZE n (which goes through srand) deterministically
 * seeds the same stream RND() reads via hal_random_u32. Previously
 * srand was a no-op, so RANDOMIZE didn't actually do anything. */
extern uint32_t hal_random_u32(void);
extern uint32_t pc386_rand_state;
int rand(void) { return (int)(hal_random_u32() & 0x7FFFFFFFu); }
void srand(unsigned int seed) { pc386_rand_state = seed ? seed : 1; }

void exit(int code) {
    (void)code;
    pc386_panic("exit() called from BASIC core (3c.x: route to clean shutdown)");
}

void abort(void) {
    pc386_panic("abort() called");
}

/* ===== malloc/free wrapper — temporary ================================== */

/* For 3c.1 we use the kernel heap region directly with a trivial bump
 * allocator. 3c.2 wires bc_alloc on top of this and the bump allocator
 * goes away. Track the high-water mark so we can at least see if MMBasic
 * core would have run out at startup. */
extern unsigned char *heap_region_base(void);
extern size_t         heap_region_size(void);

static unsigned char *bump_ptr  = NULL;
static unsigned char *bump_end  = NULL;

static void bump_init_lazy(void) {
    if (bump_ptr) return;
    bump_ptr = heap_region_base();
    bump_end = bump_ptr + heap_region_size();
}

void *malloc(size_t n) {
    bump_init_lazy();
    /* 8-byte alignment */
    n = (n + 7u) & ~(size_t)7u;
    if (bump_ptr + n > bump_end) return NULL;
    void *r = bump_ptr;
    bump_ptr += n;
    return r;
}

void *calloc(size_t nmemb, size_t sz) {
    size_t total = nmemb * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n) {
    /* Bump allocator can't free; realloc gets a new block + copy. The
     * old block becomes leaked. 3c.2 fixes this with bc_alloc. */
    void *r = malloc(n);
    if (r && p) memcpy(r, p, n);
    return r;
}

void free(void *p) {
    (void)p;  /* Bump allocator: leak. 3c.2 fixes. */
}

/* ===== time stubs ======================================================= */

extern uint64_t hal_time_us_64(void);

#include <time.h>

/* CMOS RTC at I/O 0x70 (index) / 0x71 (data). Standard since the PC/AT.
 * Registers: 00 sec, 02 min, 04 hour, 07 day, 08 month, 09 year-of-cent,
 * 32 century, 0A status A (bit 7 = update-in-progress), 0B status B
 * (bit 1 = 24-hour mode, bit 2 = binary mode). */
static uint8_t pc386_cmos_read(uint8_t reg) {
    __asm__ volatile("outb %0, $0x70" :: "a"(reg));
    uint8_t v;
    __asm__ volatile("inb $0x71, %0" : "=a"(v));
    return v;
}

static uint8_t pc386_bcd_to_bin(uint8_t v) {
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

extern time_t hal_calendar_tm_to_epoch(const struct tm *tm);
extern void   hal_calendar_epoch_to_tm(time_t epoch, struct tm *out);

static void pc386_read_rtc_tm(struct tm *out) {
    /* Spin while update-in-progress to avoid reading mid-tick. Bounded
     * loop in case the RTC is hosed. */
    for (int i = 0; i < 1000000; i++) {
        if ((pc386_cmos_read(0x0A) & 0x80) == 0) break;
    }
    uint8_t sec  = pc386_cmos_read(0x00);
    uint8_t min  = pc386_cmos_read(0x02);
    uint8_t hour = pc386_cmos_read(0x04);
    uint8_t day  = pc386_cmos_read(0x07);
    uint8_t mon  = pc386_cmos_read(0x08);
    uint8_t year = pc386_cmos_read(0x09);
    uint8_t cent = pc386_cmos_read(0x32);
    uint8_t statusB = pc386_cmos_read(0x0B);

    if (!(statusB & 0x04)) {
        /* BCD mode — convert. PM bit (0x80) of the hour byte must be
         * preserved across the BCD conversion. */
        uint8_t hour_pm = hour & 0x80;
        sec  = pc386_bcd_to_bin(sec);
        min  = pc386_bcd_to_bin(min);
        hour = (uint8_t)(pc386_bcd_to_bin(hour & 0x7F) | hour_pm);
        day  = pc386_bcd_to_bin(day);
        mon  = pc386_bcd_to_bin(mon);
        year = pc386_bcd_to_bin(year);
        cent = pc386_bcd_to_bin(cent);
    }
    if (!(statusB & 0x02) && (hour & 0x80)) {
        /* 12-hour mode with PM set — translate to 24h. */
        hour = (uint8_t)(((hour & 0x7F) % 12) + 12);
    }
    hour &= 0x7F;

    int full_year;
    if (cent >= 19 && cent <= 99) {
        full_year = cent * 100 + year;
    } else {
        /* CMOS century register not implemented on some chipsets;
         * assume 21st century. */
        full_year = 2000 + year;
    }

    out->tm_sec  = sec;
    out->tm_min  = min;
    out->tm_hour = hour;
    out->tm_mday = day;
    out->tm_mon  = (mon >= 1) ? (mon - 1) : 0;  /* RTC is 1-12; struct tm is 0-11 */
    out->tm_year = full_year - 1900;
    out->tm_wday = 0;
    out->tm_yday = 0;
    out->tm_isdst = 0;
}

/* Boot-time wall-clock offset: epoch - uptime. Lazy-init on first call.
 * Non-static so cmd_rtc (peripheral_stubs.c) can force a re-init after
 * writing the CMOS RTC. */
time_t pc386_boot_epoch = 0;
int    pc386_boot_epoch_inited = 0;

static void pc386_init_boot_epoch(void) {
    struct tm now;
    pc386_read_rtc_tm(&now);
    time_t now_epoch = hal_calendar_tm_to_epoch(&now);
    time_t uptime = (time_t)(hal_time_us_64() / 1000000ull);
    pc386_boot_epoch = now_epoch - uptime;
    pc386_boot_epoch_inited = 1;
}

time_t time(time_t *t) {
    if (!pc386_boot_epoch_inited) pc386_init_boot_epoch();
    time_t v = pc386_boot_epoch + (time_t)(hal_time_us_64() / 1000000ull);
    if (t) *t = v;
    return v;
}

clock_t clock(void) {
    return (clock_t)hal_time_us_64();
}

/* Single static struct for the non-_r variants (POSIX allows this). */
static struct tm pc386_static_tm;

struct tm *localtime_r(const time_t *t, struct tm *out) {
    if (!t || !out) return out;
    hal_calendar_epoch_to_tm(*t, out);
    return out;
}

struct tm *localtime(const time_t *t) {
    return localtime_r(t, &pc386_static_tm);
}

struct tm *gmtime_r(const time_t *t, struct tm *out) {
    return localtime_r(t, out);  /* no timezone offset on bare metal */
}

struct tm *gmtime(const time_t *t) {
    return gmtime_r(t, &pc386_static_tm);
}

time_t mktime(struct tm *tm) {
    return hal_calendar_tm_to_epoch(tm);
}

time_t timegm(const struct tm *tm) {
    /* GPS.h declares timegm with const; honour that signature.
     * mktime takes non-const so we copy through a scratch struct. */
    struct tm scratch = *tm;
    return mktime(&scratch);
}

/* strftime — useful subset. Supports the common date/time specifiers
 * plus weekday/month names. Unrecognised %X codes are copied verbatim.
 * Output is truncated to n-1 chars + NUL. */
static const char *const pc386_wday_short[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const pc386_wday_long[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const pc386_mon_short[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *const pc386_mon_long[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static size_t pc386_strftime_append(char *out, size_t outsz, size_t pos, const char *s) {
    while (*s) {
        if (pos + 1 >= outsz) return pos;
        out[pos++] = *s++;
    }
    return pos;
}

static size_t pc386_strftime_append_num(char *out, size_t outsz, size_t pos,
                                        int value, int width) {
    char buf[8];
    int neg = (value < 0);
    unsigned int v = (unsigned int)(neg ? -value : value);
    int len = 0;
    do { buf[len++] = (char)('0' + (v % 10)); v /= 10; } while (v && len < (int)sizeof(buf));
    if (neg && pos + 1 < outsz) out[pos++] = '-';
    while (len < width) {
        if (pos + 1 >= outsz) return pos;
        out[pos++] = '0';
        width--;
    }
    while (len > 0) {
        if (pos + 1 >= outsz) return pos;
        out[pos++] = buf[--len];
    }
    return pos;
}

size_t strftime(char *s, size_t n, const char *fmt, const struct tm *tm) {
    if (!s || n == 0 || !fmt || !tm) return 0;
    size_t pos = 0;
    while (*fmt && pos + 1 < n) {
        if (*fmt != '%') { s[pos++] = *fmt++; continue; }
        fmt++;
        switch (*fmt) {
            case 'Y': pos = pc386_strftime_append_num(s, n, pos, tm->tm_year + 1900, 4); break;
            case 'y': pos = pc386_strftime_append_num(s, n, pos, (tm->tm_year + 1900) % 100, 2); break;
            case 'm': pos = pc386_strftime_append_num(s, n, pos, tm->tm_mon + 1, 2); break;
            case 'd': pos = pc386_strftime_append_num(s, n, pos, tm->tm_mday, 2); break;
            case 'H': pos = pc386_strftime_append_num(s, n, pos, tm->tm_hour, 2); break;
            case 'M': pos = pc386_strftime_append_num(s, n, pos, tm->tm_min, 2); break;
            case 'S': pos = pc386_strftime_append_num(s, n, pos, tm->tm_sec, 2); break;
            case 'j': pos = pc386_strftime_append_num(s, n, pos, tm->tm_yday + 1, 3); break;
            case 'I': {
                int h = tm->tm_hour % 12; if (h == 0) h = 12;
                pos = pc386_strftime_append_num(s, n, pos, h, 2);
                break;
            }
            case 'p': pos = pc386_strftime_append(s, n, pos, tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'A':
                if (tm->tm_wday >= 0 && tm->tm_wday < 7)
                    pos = pc386_strftime_append(s, n, pos, pc386_wday_long[tm->tm_wday]);
                break;
            case 'a':
                if (tm->tm_wday >= 0 && tm->tm_wday < 7)
                    pos = pc386_strftime_append(s, n, pos, pc386_wday_short[tm->tm_wday]);
                break;
            case 'B':
                if (tm->tm_mon >= 0 && tm->tm_mon < 12)
                    pos = pc386_strftime_append(s, n, pos, pc386_mon_long[tm->tm_mon]);
                break;
            case 'b':
                if (tm->tm_mon >= 0 && tm->tm_mon < 12)
                    pos = pc386_strftime_append(s, n, pos, pc386_mon_short[tm->tm_mon]);
                break;
            case '%': if (pos + 1 < n) s[pos++] = '%'; break;
            case '\0': goto done;
            default:
                if (pos + 2 < n) { s[pos++] = '%'; s[pos++] = *fmt; }
                break;
        }
        fmt++;
    }
done:
    s[pos] = '\0';
    return pos;
}

double difftime(time_t a, time_t b) { return (double)(a - b); }
char *asctime(const struct tm *tm) { (void)tm; pc386_panic("asctime not impl"); }
char *ctime  (const time_t *t)     { (void)t;  pc386_panic("ctime not impl"); }

/* ===== math: x87-backed implementations =================================
 *
 * We have the x87 FPU online (boot.S brings it up before kmain). All
 * the standard math functions wrap one or two FPU instructions; no
 * vendoring needed.
 *
 * The asm constraints use "+t"(x) to bind the input/output to st(0).
 * Multi-result instructions (fsincos, fxtract) clear higher st()
 * slots manually with fstp/fucomp where needed.
 */

#include <math.h>

#ifndef PC386_NO_FPU

double sin(double x)   { __asm__("fsin"  : "+t"(x)); return x; }
double cos(double x)   { __asm__("fcos"  : "+t"(x)); return x; }
double sqrt(double x)  { __asm__("fsqrt" : "+t"(x)); return x; }
double fabs(double x)  { __asm__("fabs"  : "+t"(x)); return x; }

double tan(double x) {
    /* fptan pushes 1.0 onto the stack after computing tan; pop it. */
    double junk;
    __asm__("fptan; fstp %1" : "+t"(x), "=m"(junk));
    return x;
}

double atan(double x) {
    /* fpatan computes atan2(st(1), st(0)). Push 1.0 as st(1). */
    __asm__("fld1; fxch; fpatan" : "+t"(x));
    return x;
}

double atan2(double y, double x) {
    /* fpatan: st(0) = atan2(st(1), st(0)); we need atan2(y, x). */
    double r;
    __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

double asin(double x) {
    /* asin(x) = atan2(x, sqrt(1 - x*x)) */
    double s = sqrt(1.0 - x * x);
    return atan2(x, s);
}

double acos(double x) {
    /* acos(x) = atan2(sqrt(1 - x*x), x) */
    double s = sqrt(1.0 - x * x);
    return atan2(s, x);
}

/* ln(x) = log2(x) * ln(2). x87's fyl2x computes y * log2(x) for st(1)*log2(st(0)). */
double log(double x) {
    double r;
    __asm__("fldln2; fxch; fyl2x" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}

double log2(double x) {
    double r;
    __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}

double log10(double x) {
    double r;
    __asm__("fldlg2; fxch; fyl2x" : "=t"(r) : "0"(x) : "st(1)");
    return r;
}

/* exp(x) = 2^(x * log2(e)). x87's f2xm1 needs |arg| < 1, so split. */
double exp2(double x) {
    /* x = i + f, where i is int part and f in [-0.5, 0.5]. Then
     * 2^x = (2^f) * 2^i. f2xm1 returns 2^f - 1; add 1 then fscale. */
    double i, f, r;
    __asm__("fld   %%st(0)\n\t"     /* st: x x */
            "frndint\n\t"           /* st: round(x) x */
            "fxch  %%st(1)\n\t"     /* st: x round(x) */
            "fsub  %%st(1), %%st\n\t" /* st: x-round x */
            "f2xm1\n\t"             /* st: 2^f-1 round */
            "fld1\n\t"              /* st: 1 2^f-1 round */
            "faddp\n\t"             /* st: 2^f round */
            "fscale\n\t"            /* st: 2^f * 2^round = 2^x */
            "fstp  %%st(1)\n\t"     /* drop the round */
            : "=t"(r) : "0"(x) : "st(1)");
    /* avoid unused-var warnings */
    (void)i; (void)f;
    return r;
}

double exp(double x) {
    /* exp(x) = exp2(x * log2(e)); log2(e) ≈ 1.4426950408889634 */
    return exp2(x * 1.4426950408889634);
}

double pow(double x, double y) {
    /* pow(x, y) = exp2(y * log2(x)). Special-case x = 0 to avoid log(0). */
    if (x == 0.0) return y == 0.0 ? 1.0 : 0.0;
    if (x < 0.0) {
        /* For negative bases, only integer exponents make sense. */
        double yi = (double)(int64_t)y;
        if (yi != y) return 0.0;
        double r = exp2(y * log2(-x));
        return ((int64_t)y & 1) ? -r : r;
    }
    return exp2(y * log2(x));
}

double floor(double x) {
    int64_t i = (int64_t)x;
    if (x < 0 && (double)i != x) i--;
    return (double)i;
}

double ceil(double x) {
    int64_t i = (int64_t)x;
    if (x > 0 && (double)i != x) i++;
    return (double)i;
}

double trunc(double x) {
    return (double)(int64_t)x;
}

double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double fmod(double x, double y) {
    /* x87 fprem repeats partial remainders; loop until C2 in the
     * status word clears (= reduction complete). */
    if (y == 0.0) return 0.0;
    double r;
    __asm__ volatile("1: fprem\n\t"
                     "fnstsw %%ax\n\t"
                     "test  $0x0400, %%ax\n\t"
                     "jnz   1b\n\t"
                     : "=t"(r) : "0"(x), "u"(y) : "ax", "st(1)");
    return r;
}

double modf(double x, double *iptr) {
    double i = trunc(x);
    *iptr = i;
    return x - i;
}

double sinh(double x) { return (exp(x) - exp(-x)) * 0.5; }
double cosh(double x) { return (exp(x) + exp(-x)) * 0.5; }
double tanh(double x) {
    double e2 = exp(2.0 * x);
    return (e2 - 1.0) / (e2 + 1.0);
}
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double acosh(double x) { return log(x + sqrt(x * x - 1.0)); }
double atanh(double x) { return 0.5 * log((1.0 + x) / (1.0 - x)); }

double expm1(double x) { return exp(x) - 1.0; }
double log1p(double x) { return log(1.0 + x); }
double cbrt(double x) {
    /* x^(1/3) via pow; sign-preserving. */
    return (x < 0) ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0);
}
double hypot(double x, double y) { return sqrt(x * x + y * y); }

double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { x }, uy = { y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFull) | (uy.u & 0x8000000000000000ull);
    return ux.d;
}

double nextafter(double x, double y) {
    /* Just enough for callers that test direction. */
    if (x == y) return y;
    return x + (y > x ? 1.0 : -1.0) * 1e-308;
}

double frexp(double x, int *e) {
    if (x == 0.0) { *e = 0; return 0.0; }
    /* x = m * 2^*e where 0.5 <= |m| < 1. fxtract returns mantissa
     * (in [1, 2)) and exponent. We adjust by 1 to get m in [0.5, 1). */
    double m, exp_d;
    __asm__("fxtract" : "=t"(m), "=u"(exp_d) : "0"(x));
    *e = (int)exp_d + 1;
    return m * 0.5;
}

double ldexp(double x, int e) {
    double ed = (double)e;
    double r;
    __asm__("fscale" : "=t"(r) : "0"(x), "u"(ed) : "st(1)");
    return r;
}

double scalbn(double x, int e) { return ldexp(x, e); }

#else

static double pc386_wrap_pi(double x) {
    const double pi = 3.14159265358979323846;
    const double two_pi = 6.28318530717958647692;
    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    return x;
}

double fabs(double x) { return x < 0.0 ? -x : x; }

double floor(double x) {
    int64_t i = (int64_t)x;
    if (x < 0.0 && (double)i != x) i--;
    return (double)i;
}

double ceil(double x) {
    int64_t i = (int64_t)x;
    if (x > 0.0 && (double)i != x) i++;
    return (double)i;
}

double trunc(double x) { return (double)(int64_t)x; }
double round(double x) { return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5); }

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    return x - trunc(x / y) * y;
}

double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 24; i++) g = 0.5 * (g + x / g);
    return g;
}

double sin(double x) {
    x = pc386_wrap_pi(x);
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 - (x2 * x2 * x2) / 5040.0);
}

double cos(double x) {
    x = pc386_wrap_pi(x);
    double x2 = x * x;
    return 1.0 - x2 / 2.0 + (x2 * x2) / 24.0 - (x2 * x2 * x2) / 720.0;
}

double tan(double x) {
    double c = cos(x);
    return c == 0.0 ? 0.0 : sin(x) / c;
}

double atan(double x) {
    /* arctan(x) by argument reduction + Taylor series.
     *
     * The naive Maclaurin x - x^3/3 + x^5/5 - ... converges painfully
     * slowly near |x| = 1 (it's the Leibniz formula for pi/4 there).
     * Four terms gave ~7% error at x=1 which broke math.bas. We use
     * two reductions:
     *
     *   1. For |x| > 1, the identity  atan(x) = pi/2 - atan(1/x)
     *      moves the argument into [-1, 1].
     *   2. For 0 < |x| <= 1, repeatedly apply
     *          atan(x) = 2 * atan( x / (1 + sqrt(1 + x*x)) )
     *      until |x| < 0.18, then sum a 6-term Taylor. Each halving
     *      gives an extra factor of ~2 in convergence speed; with the
     *      threshold of 0.18, six terms give ~1e-10 precision.
     */
    const double half_pi = 1.57079632679489661923;
    if (x > 1.0) return half_pi - atan(1.0 / x);
    if (x < -1.0) return -half_pi - atan(1.0 / x);
    int doublings = 0;
    while (x > 0.18 || x < -0.18) {
        x = x / (1.0 + sqrt(1.0 + x * x));
        doublings++;
    }
    double x2 = x * x;
    double r = x * (1.0
                    - x2 / 3.0
                    + (x2 * x2) / 5.0
                    - (x2 * x2 * x2) / 7.0
                    + (x2 * x2 * x2 * x2) / 9.0
                    - (x2 * x2 * x2 * x2 * x2) / 11.0
                    + (x2 * x2 * x2 * x2 * x2 * x2) / 13.0);
    while (doublings-- > 0) r *= 2.0;
    return r;
}

double atan2(double y, double x) {
    const double pi = 3.14159265358979323846;
    if (x > 0.0) return atan(y / x);
    if (x < 0.0 && y >= 0.0) return atan(y / x) + pi;
    if (x < 0.0 && y < 0.0) return atan(y / x) - pi;
    if (y > 0.0) return pi / 2.0;
    if (y < 0.0) return -pi / 2.0;
    return 0.0;
}

double asin(double x) { return atan2(x, sqrt(1.0 - x * x)); }
double acos(double x) { return atan2(sqrt(1.0 - x * x), x); }

double exp(double x) {
    if (x < -40.0) return 0.0;
    if (x > 40.0) x = 40.0;
    int n = (int)x;
    double r = x - (double)n;
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 18; i++) {
        term *= r / (double)i;
        sum += term;
    }
    const double e = 2.71828182845904523536;
    while (n > 0) { sum *= e; n--; }
    while (n < 0) { sum /= e; n++; }
    return sum;
}

double log(double x) {
    if (x <= 0.0) return -1.0 / 0.0;
    int k = 0;
    while (x > 1.5) { x *= 0.5; k++; }
    while (x < 0.75) { x *= 2.0; k--; }
    double z = (x - 1.0) / (x + 1.0);
    double z2 = z * z;
    double term = z;
    double sum = 0.0;
    for (int n = 1; n <= 19; n += 2) {
        sum += term / (double)n;
        term *= z2;
    }
    return 2.0 * sum + (double)k * 0.69314718055994530942;
}

double log2(double x) { return log(x) / 0.69314718055994530942; }
double log10(double x) { return log(x) / 2.30258509299404568402; }
double exp2(double x) { return exp(x * 0.69314718055994530942); }

double pow(double x, double y) {
    if (x == 0.0) return y == 0.0 ? 1.0 : 0.0;
    if (x < 0.0) {
        int64_t yi = (int64_t)y;
        if ((double)yi != y) return 0.0;
        double r = exp(y * log(-x));
        return (yi & 1) ? -r : r;
    }
    return exp(y * log(x));
}

double modf(double x, double *iptr) {
    *iptr = trunc(x);
    return x - *iptr;
}

double sinh(double x) { return (exp(x) - exp(-x)) * 0.5; }
double cosh(double x) { return (exp(x) + exp(-x)) * 0.5; }
double tanh(double x) {
    double e2 = exp(2.0 * x);
    return (e2 - 1.0) / (e2 + 1.0);
}
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double acosh(double x) { return log(x + sqrt(x * x - 1.0)); }
double atanh(double x) { return 0.5 * log((1.0 + x) / (1.0 - x)); }
double expm1(double x) { return exp(x) - 1.0; }
double log1p(double x) { return log(1.0 + x); }
double cbrt(double x) { return x < 0.0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0); }
double hypot(double x, double y) { return sqrt(x * x + y * y); }

double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { x }, uy = { y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFull) | (uy.u & 0x8000000000000000ull);
    return ux.d;
}

double nextafter(double x, double y) {
    if (x == y) return y;
    return x + (y > x ? 1.0 : -1.0) * 1e-308;
}

double frexp(double x, int *e) {
    if (x == 0.0) { *e = 0; return 0.0; }
    int exp = 0;
    double ax = fabs(x);
    while (ax >= 1.0) { ax *= 0.5; exp++; }
    while (ax < 0.5) { ax *= 2.0; exp--; }
    *e = exp;
    return x < 0.0 ? -ax : ax;
}

double ldexp(double x, int e) {
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x *= 0.5; e++; }
    return x;
}

double scalbn(double x, int e) { return ldexp(x, e); }

#endif

float _Complex __mulsc3(float a, float b, float c, float d) {
    float _Complex z;
    __real__ z = a * c - b * d;
    __imag__ z = a * d + b * c;
    return z;
}

float _Complex __divsc3(float a, float b, float c, float d) {
    float denom = c * c + d * d;
    float _Complex z;
    if (denom == 0.0f) {
        __real__ z = 0.0f;
        __imag__ z = 0.0f;
        return z;
    }
    __real__ z = (a * c + b * d) / denom;
    __imag__ z = (b * c - a * d) / denom;
    return z;
}

double _Complex __muldc3(double a, double b, double c, double d) {
    double _Complex z;
    __real__ z = a * c - b * d;
    __imag__ z = a * d + b * c;
    return z;
}

double _Complex __divdc3(double a, double b, double c, double d) {
    double denom = c * c + d * d;
    double _Complex z;
    if (denom == 0.0) {
        __real__ z = 0.0;
        __imag__ z = 0.0;
        return z;
    }
    __real__ z = (a * c + b * d) / denom;
    __imag__ z = (b * c - a * d) / denom;
    return z;
}

float sinf(float x)            { return (float)sin((double)x); }
float cosf(float x)            { return (float)cos((double)x); }
float sqrtf(float x)           { return (float)sqrt((double)x); }
float fabsf(float x)           { return x < 0 ? -x : x; }
float floorf(float x)          { return (float)floor((double)x); }
float ceilf(float x)           { return (float)ceil((double)x); }
float expf(float x)            { return (float)exp((double)x); }
float logf(float x)            { return (float)log((double)x); }
float powf(float x, float y)   { return (float)pow((double)x, (double)y); }
float fmodf(float x, float y)  { return (float)fmod((double)x, (double)y); }

int isnan   (double x) { return x != x; }
int isinf   (double x) { return x == INFINITY || x == -INFINITY; }
int isfinite(double x) { return !isnan(x) && !isinf(x); }
int signbit (double x) {
    union { double d; uint64_t u; } u = { x };
    return (int)(u.u >> 63);
}

float roundf(float x) { return (float)round((double)x); }
float truncf(float x) { return (float)trunc((double)x); }

/* ===== complex math stubs ===============================================
 *
 * MATHS.c uses these only from MATH("ADD", ...) / MATH("FFT", ...) and
 * friends — rare paths, fine to panic until openlibm-complex is vendored.
 */

#include <complex.h>

#define CPLX_STUB1(rt, name) \
    rt name(float complex z) { (void)z; pc386_panic(#name " not yet implemented (vendor openlibm)"); }
#define CPLX_STUB1D(rt, name) \
    rt name(double complex z) { (void)z; pc386_panic(#name " not yet implemented (vendor openlibm)"); }
#define CPLX_STUB2(rt, name) \
    rt name(float complex z, float complex w) { (void)z; (void)w; pc386_panic(#name " not yet implemented (vendor openlibm)"); }
#define CPLX_STUB2D(rt, name) \
    rt name(double complex z, double complex w) { (void)z; (void)w; pc386_panic(#name " not yet implemented (vendor openlibm)"); }

CPLX_STUB1(float, crealf)
CPLX_STUB1(float, cimagf)
CPLX_STUB1(float, cargf)
CPLX_STUB1(float, cabsf)
CPLX_STUB1(float complex, conjf)
CPLX_STUB2(float complex, cpowf)
CPLX_STUB1(float complex, cacosf)
CPLX_STUB1(float complex, casinf)
CPLX_STUB1(float complex, catanf)
CPLX_STUB1(float complex, csinf)
CPLX_STUB1(float complex, ccosf)
CPLX_STUB1(float complex, ctanf)
CPLX_STUB1(float complex, csinhf)
CPLX_STUB1(float complex, ccoshf)
CPLX_STUB1(float complex, ctanhf)
CPLX_STUB1(float complex, casinhf)
CPLX_STUB1(float complex, cacoshf)
CPLX_STUB1(float complex, catanhf)
CPLX_STUB1(float complex, cprojf)
CPLX_STUB1(float complex, csqrtf)
CPLX_STUB1(float complex, clogf)
CPLX_STUB1(float complex, cexpf)

CPLX_STUB1D(double, creal)
CPLX_STUB1D(double, cimag)
CPLX_STUB1D(double, carg)
CPLX_STUB1D(double, cabs)
CPLX_STUB1D(double complex, conj)
CPLX_STUB2D(double complex, cpow)
CPLX_STUB1D(double complex, cacos)
CPLX_STUB1D(double complex, casin)
CPLX_STUB1D(double complex, catan)
CPLX_STUB1D(double complex, csin)
CPLX_STUB1D(double complex, ccos)
CPLX_STUB1D(double complex, ctan)
CPLX_STUB1D(double complex, csqrt)
CPLX_STUB1D(double complex, clog)
CPLX_STUB1D(double complex, cexp)
