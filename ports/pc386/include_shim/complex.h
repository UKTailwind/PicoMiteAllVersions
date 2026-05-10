/* ports/pc386/include_shim/complex.h — freestanding shim.
 *
 * MATHS.c uses `float complex` and `double complex` for FFT and the
 * MATH() complex-arithmetic family. C99 says `complex` is a macro
 * for `_Complex` defined by this header; gcc supports `_Complex`
 * natively in freestanding mode.
 *
 * The complex math functions (cpowf/cacosf/conjf/etc.) are stubbed
 * to panic in pc386_libc.c — they're only reachable from MATH(...)
 * which BASIC programs rarely use, and adding real impls means
 * vendoring a substantial chunk of openlibm-complex.
 */
#ifndef _PC386_COMPLEX_H
#define _PC386_COMPLEX_H

#define complex  _Complex
#define I        _Complex_I
#define _Complex_I  (__extension__ (0.0f + 1.0fi))

float       crealf(float complex);
float       cimagf(float complex);
float       cargf (float complex);
float       cabsf (float complex);
float complex conjf (float complex);
float complex cpowf (float complex, float complex);
float complex cacosf(float complex);
float complex casinf(float complex);
float complex catanf(float complex);
float complex csinf (float complex);
float complex ccosf (float complex);
float complex ctanf (float complex);
float complex csinhf(float complex);
float complex ccoshf(float complex);
float complex ctanhf(float complex);
float complex casinhf(float complex);
float complex cacoshf(float complex);
float complex catanhf(float complex);
float complex cprojf (float complex);
float complex csqrtf(float complex);
float complex clogf (float complex);
float complex cexpf (float complex);

double       creal(double complex);
double       cimag(double complex);
double       carg (double complex);
double       cabs (double complex);
double complex conj (double complex);
double complex cpow (double complex, double complex);
double complex cacos(double complex);
double complex casin(double complex);
double complex catan(double complex);
double complex csin (double complex);
double complex ccos (double complex);
double complex ctan (double complex);
double complex csqrt(double complex);
double complex clog (double complex);
double complex cexp (double complex);

#endif
