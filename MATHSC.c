/***********************************************************************************************************************
PicoMite MMBasic

MATHS.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/
/**
* @file MATHS.c
* @author Geoff Graham, Peter Mather
* @brief Source for MATHS MMBasic commands and functions
*/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include <complex.h>
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include <math.h>
#ifdef rp2350
#include "pico/rand.h"
#endif
#define CBC 1
#define CTR 1
#define ECB 1
#include "aes.h"
typedef MMFLOAT complex _cplx;
typedef float complex fcplx;

static_assert(sizeof(fcplx) == 8);

typedef union {
    uint64_t i;
    fcplx z;
} u_t;

typedef union {
    cplx td;
    _cplx z;
} u2_t;

_Static_assert(sizeof(cplx) == sizeof(_cplx), "cplx and _cplx must match");

float _crealf(uint64_t x) {
    u_t _x;
    _x.i = x;
    return crealf(_x.z);
}
float _cimagf(uint64_t x) {
    u_t _x;
    _x.i = x;
    return cimagf(_x.z);
}
float _cargf(uint64_t x) {
    u_t _x;
    _x.i = x;
    return cargf(_x.z);
}
uint64_t _conjf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = conjf(_x.z);
    return _x.i;
}
uint64_t _cacosf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = cacosf(_x.z);
    return _x.i;
}
uint64_t _casinf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = casinf(_x.z);
    return _x.i;
}
uint64_t _catanf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = catanf(_x.z);
    return _x.i;
}
uint64_t _csinf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = csinf(_x.z);
    return _x.i;
}
uint64_t _ccosf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = ccosf(_x.z);
    return _x.i;
}
uint64_t _ctanf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = ctanf(_x.z);
    return _x.i;
}
uint64_t _csinhf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = csinhf(_x.z);
    return _x.i;
}
uint64_t _ccoshf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = ccoshf(_x.z);
    return _x.i;
}
uint64_t _ctanhf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = ctanhf(_x.z);
    return _x.i;
}
uint64_t _casinhf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = casinhf(_x.z);
    return _x.i;
}
uint64_t _cacoshf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = cacoshf(_x.z);
    return _x.i;
}
uint64_t _catanhf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = catanhf(_x.z);
    return _x.i;
}
uint64_t _cexpf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = cexpf(_x.z);
    return _x.i;
}
uint64_t _clogf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = clogf(_x.z);
    return _x.i;
}
float _cabsf(uint64_t x) {
    u_t _x;
    _x.i = x;
    return cabsf(_x.z);
}
uint64_t _csqrtf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = csqrtf(_x.z);
    return _x.i;
}
uint64_t _cprojf(uint64_t x) {
    u_t _x;
    _x.i = x;
    _x.z = cprojf(_x.z);
    return _x.i;
}

uint64_t add_complexI(float x, float y) {
    u_t res;
    res.z = x + y*I;
    return res.i;
}

uint64_t _cpowf(uint64_t x1, uint64_t x2) {
    u_t _x1, _x2;
    _x1.i = x1;
    _x2.i = x2;
    _x1.z = cpowf(_x1.z, _x2.z);
	return _x1.i;
}
uint64_t add_complex(uint64_t x1, uint64_t x2) {
    u_t _x1, _x2;
    _x1.i = x1;
    _x2.i = x2;
    _x1.z += _x2.z;
	return _x1.i;
}
uint64_t sub_complex(uint64_t x1, uint64_t x2) {
    u_t _x1, _x2;
    _x1.i = x1;
    _x2.i = x2;
    _x1.z -= _x2.z;
	return _x1.i;
}
uint64_t mul_complex(uint64_t x1, uint64_t x2) {
    u_t _x1, _x2;
    _x1.i = x1;
    _x2.i = x2;
    _x1.z *= _x2.z;
	return _x1.i;
}
uint64_t div_complex(uint64_t x1, uint64_t x2) {
    u_t _x1, _x2;
    _x1.i = x1;
    _x2.i = x2;
    _x1.z /= _x2.z;
	return _x1.i;
}
/*  @endcond */

static size_t reverse_bits(size_t val, int width) {
	size_t result = 0;
	for (int i = 0; i < width; i++, val >>= 1)
		result = (result << 1) | (val & 1U);
	return result;
}
/* 
 * @cond
 * The following section will be excluded from the documentation.
 */
double _carg(cplx x) {
    u2_t t = { x };
    return carg(t.z);
}

double _cabs(cplx x) {
    u2_t t = { x };
    return cabs(t.z);
}

bool Fft_transformRadix2(cplx vec[], size_t n, bool inverse) {
	// Length variables
	int levels = 0;  // Compute levels = floor(log2(n))
	for (size_t temp = n; temp > 1U; temp >>= 1)
		levels++;
	if ((size_t)1U << levels != n)
		return false;  // n is not a power of 2

	// Trigonometric tables
	if (SIZE_MAX / sizeof(double complex) < n / 2)
		return false;
	double complex *exptable = GetMemory((n / 2) * sizeof(double complex));
	if (exptable == NULL)
		return false;
	for (size_t i = 0; i < n / 2; i++)
		exptable[i] = cexp((inverse ? 2 : -2) * M_PI * i / n * I);

	// Bit-reversed addressing permutation
	for (size_t i = 0; i < n; i++) {
		size_t j = reverse_bits(i, levels);
		if (j > i) {
			cplx temp = vec[i];
			vec[i] = vec[j];
			vec[j] = temp;
		}
	}

	// Cooley-Tukey decimation-in-time radix-2 FFT
	for (size_t size = 2; size <= n; size *= 2) {
		size_t halfsize = size / 2;
		size_t tablestep = n / size;
		for (size_t i = 0; i < n; i += size) {
			for (size_t j = i, k = 0; j < i + halfsize; j++, k += tablestep) {
				size_t l = j + halfsize;
                u2_t tt = { vec[l] };
                u2_t t2 = { vec[j] };
				double complex temp = tt.z * exptable[k];
                tt.z -= temp;
                t2.z += temp;
				vec[l] = tt.td;
				vec[j] = t2.td;
			}
		}
		if (size == n)  // Prevent overflow in 'size *= 2'
			break;
	}

	FreeMemory((void *)exptable);
	return true;
}
/*  @endcond */

