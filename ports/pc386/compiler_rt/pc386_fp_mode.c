// pc386 no-FPU builds do not expose a mutable IEEE-754 environment.
// The compiler-rt soft-float helpers only need the default rounding mode.

#include "fp_mode.h"

CRT_FE_ROUND_MODE __fe_getround(void) {
    return CRT_FE_TONEAREST;
}

int __fe_raise_inexact(void) {
    return 0;
}
