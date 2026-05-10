/* ports/pc386/include_shim/math.h — freestanding shim.
 *
 * MMBasic uses sin, cos, tan, asin, acos, atan, atan2, log, log10,
 * exp, sqrt, pow, floor, ceil, fabs, fmod, modf, frexp, ldexp.
 * Implementations land in pc386_math.c (vendored from openlibm).
 */
#ifndef _PC386_MATH_H
#define _PC386_MATH_H

#ifndef M_PI
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_E        2.7182818284590452354
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_SQRT2    1.41421356237309504880
#endif

#define HUGE_VAL  (__builtin_huge_val())
#define NAN       (__builtin_nanf(""))
#define INFINITY  (__builtin_inff())

double sin   (double);
double cos   (double);
double tan   (double);
double asin  (double);
double acos  (double);
double atan  (double);
double atan2 (double, double);
double sinh  (double);
double cosh  (double);
double tanh  (double);
double asinh (double);
double acosh (double);
double atanh (double);
double exp   (double);
double exp2  (double);
double expm1 (double);
double log   (double);
double log2  (double);
double log10 (double);
double log1p (double);
double pow   (double, double);
double sqrt  (double);
double cbrt  (double);
double hypot (double, double);
double floor (double);
double ceil  (double);
double trunc (double);
double round (double);
double fabs  (double);
double fmod  (double, double);
double modf  (double, double *);
double frexp (double, int *);
double ldexp (double, int);
double scalbn(double, int);
double copysign(double, double);
double nextafter(double, double);

float  sinf  (float);
float  cosf  (float);
float  sqrtf (float);
float  fabsf (float);
float  floorf(float);
float  ceilf (float);
float  expf  (float);
float  logf  (float);
float  powf  (float, float);
float  fmodf (float, float);

int isnan   (double);
int isinf   (double);
int isfinite(double);
int signbit (double);

#endif
