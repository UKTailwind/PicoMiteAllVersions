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

int  printf  (const char *fmt, ...) { (void)fmt; pc386_panic("printf — vendor mpaland"); }
int  sprintf (char *s, const char *fmt, ...) { (void)s; (void)fmt; pc386_panic("sprintf — vendor mpaland"); }
int  snprintf(char *s, size_t n, const char *fmt, ...) { (void)s; (void)n; (void)fmt; pc386_panic("snprintf — vendor mpaland"); }
int  vsnprintf(char *s, size_t n, const char *fmt, va_list ap) { (void)s; (void)n; (void)fmt; (void)ap; pc386_panic("vsnprintf — vendor mpaland"); }
int  vsprintf (char *s, const char *fmt, va_list ap) { (void)s; (void)fmt; (void)ap; pc386_panic("vsprintf — vendor mpaland"); }
int  fprintf (FILE *fp, const char *fmt, ...) { (void)fp; (void)fmt; pc386_panic("fprintf — vendor mpaland"); }
int  vfprintf(FILE *fp, const char *fmt, va_list ap) { (void)fp; (void)fmt; (void)ap; pc386_panic("vfprintf — vendor mpaland"); }
int  sscanf  (const char *s, const char *fmt, ...) { (void)s; (void)fmt; pc386_panic("sscanf — vendor mpaland"); }
int  vsscanf (const char *s, const char *fmt, va_list ap) { (void)s; (void)fmt; (void)ap; pc386_panic("vsscanf — vendor mpaland"); }

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
long long llabs(long long x){ return x < 0 ? -x : x; }

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

long long atoll(const char *s) {
    int neg;
    long long v = 0;
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

long long strtoll(const char *s, char **end, int base) {
    /* MMBasic uses strtoll for HEX$/BIN$/OCT$ parsing; integer-only.
     * Re-do strtol's body in long-long for correct overflow on values
     * like 0xFFFFFFFF that don't fit in 32-bit long. */
    int neg;
    long long v = 0;
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

/* atof / strtod / strtof — defer to pc386_math.c when math vendored. */
double atof(const char *s) {
    (void)s;
    pc386_panic("atof not yet implemented (3c.1: vendor strtod/openlibm)");
}

double strtod(const char *s, char **end) {
    (void)s; (void)end;
    pc386_panic("strtod not yet implemented (3c.1: vendor strtod/openlibm)");
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

/* rand/srand — same xorshift32 hal_random uses. */
extern uint32_t hal_random_u32(void);
int rand(void) { return (int)(hal_random_u32() & 0x7FFFFFFFu); }
void srand(unsigned int seed) { (void)seed; /* xorshift seeds itself */ }

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

time_t time(time_t *t) {
    time_t v = (time_t)(hal_time_us_64() / 1000000ull);
    if (t) *t = v;
    return v;
}

clock_t clock(void) {
    return (clock_t)hal_time_us_64();
}

/* localtime/gmtime/mktime/strftime — defer real impl to pc386_time.c. */
struct tm *localtime(const time_t *t) {
    (void)t;
    pc386_panic("localtime not yet implemented (3c.x: pc386_time.c)");
}

struct tm *gmtime(const time_t *t) {
    return localtime(t);
}

struct tm *localtime_r(const time_t *t, struct tm *out) {
    (void)t; (void)out;
    pc386_panic("localtime_r not yet implemented (3c.x: pc386_time.c)");
}

struct tm *gmtime_r(const time_t *t, struct tm *out) {
    return localtime_r(t, out);
}

time_t mktime(struct tm *tm) {
    (void)tm;
    pc386_panic("mktime not yet implemented (3c.x: pc386_time.c)");
}

time_t timegm(const struct tm *tm) {
    /* GPS.h declares timegm with const; honour that signature.
     * mktime takes non-const so we copy through a scratch struct. */
    struct tm scratch = *tm;
    return mktime(&scratch);
}

size_t strftime(char *s, size_t n, const char *fmt, const struct tm *tm) {
    (void)s; (void)n; (void)fmt; (void)tm;
    pc386_panic("strftime not yet implemented (3c.x: pc386_time.c)");
}

double difftime(time_t a, time_t b) { return (double)(a - b); }
char *asctime(const struct tm *tm) { (void)tm; pc386_panic("asctime not impl"); }
char *ctime  (const time_t *t)     { (void)t;  pc386_panic("ctime not impl"); }

/* ===== math stubs ======================================================= */

#include <math.h>

#define MATH_STUB(name) \
    double name(double x) { (void)x; pc386_panic(#name " not yet implemented (3c.x: vendor openlibm)"); }
#define MATH_STUB2(name) \
    double name(double x, double y) { (void)x; (void)y; pc386_panic(#name " not yet implemented (3c.x: vendor openlibm)"); }

MATH_STUB(sin)   MATH_STUB(cos)   MATH_STUB(tan)
MATH_STUB(asin)  MATH_STUB(acos)  MATH_STUB(atan)
MATH_STUB2(atan2)
MATH_STUB(sinh)  MATH_STUB(cosh)  MATH_STUB(tanh)
MATH_STUB(asinh) MATH_STUB(acosh) MATH_STUB(atanh)
MATH_STUB(exp)   MATH_STUB(exp2)  MATH_STUB(expm1)
MATH_STUB(log)   MATH_STUB(log2)  MATH_STUB(log10) MATH_STUB(log1p)
MATH_STUB2(pow)
MATH_STUB(sqrt)  MATH_STUB(cbrt)
MATH_STUB2(hypot)
MATH_STUB(round) MATH_STUB(trunc)
MATH_STUB2(fmod)
MATH_STUB2(copysign) MATH_STUB2(nextafter)

/* These three are simple enough to write inline. */
double fabs(double x)  { return x < 0 ? -x : x; }
double floor(double x) {
    /* Convert to long long; for negatives that aren't exact, subtract 1. */
    long long i = (long long)x;
    if (x < 0 && (double)i != x) i--;
    return (double)i;
}
double ceil(double x) {
    long long i = (long long)x;
    if (x > 0 && (double)i != x) i++;
    return (double)i;
}

double modf(double x, double *iptr) {
    double i = (x < 0) ? ceil(x) : floor(x);
    *iptr = i;
    return x - i;
}

double frexp(double x, int *e) { (void)x; (void)e; pc386_panic("frexp not impl"); }
double ldexp(double x, int e)  { (void)x; (void)e; pc386_panic("ldexp not impl"); }
double scalbn(double x, int e) { return ldexp(x, e); }

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
