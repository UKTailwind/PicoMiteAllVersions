/*
 * ports/pico_sdk_common/funtbl_port.c — device impl of MMBasic.c's
 * FindSubFun hash-lookup port hook.
 *
 *   - port_try_find_subfun_hash(p, &out) : on rp2350 (the only port
 *     that maintains the funtbl[] hash), walk the probe chain for
 *     the name at `p`, store the matching subfun[] index in `*out`,
 *     and return 1 (even on miss — the hash table is authoritative
 *     when it exists).  On rp2040 the hash table doesn't exist, so
 *     return 0 and let the caller do a linear scan.
 *
 * Note: the rp2350 path intentionally does not filter by SUB/FUN
 * type.  That matches the pre-refactor behaviour — the linear-scan
 * version (rp2040 + host) does filter, so there's a subtle semantic
 * split, but it's not new.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#ifdef rp2350

int __not_in_flash_func(port_try_find_subfun_hash)(unsigned char *p, int *out_index) {
    unsigned char *s;
    unsigned char name[MAXVARLEN + 1];
    int j, u, namelen;
    unsigned int hash = FNV_offset_basis;
    unsigned char *tp, *ip;

    s = name; namelen = 0;
    do {
        u = mytoupper(*p);
        hash ^= u;
        hash *= FNV_prime;
        *s++ = u;
        p++;
        if (++namelen > MAXVARLEN) error("Variable name too long");
    } while (isnamechar(*p));
    *s = 0;
    hash %= MAXSUBHASH;

    while (funtbl[hash].name[0] != 0) {
        ip = name;
        tp = (unsigned char *)funtbl[hash].name;
        if (*ip++ == *tp++) {
            j = namelen - 1;
            while (j > 0 && *ip == *tp) { j--; ip++; tp++; }
            if (j == 0 && (*(char *)tp == 0 || namelen == MAXVARLEN) && funtbl[hash].index < MAXSUBFUN) {
                *out_index = funtbl[hash].index;
                return 1;
            }
        }
        hash++;
        if (hash == MAXSUBFUN) hash = 0;
    }
    *out_index = -1;
    return 1;
}

#else

int port_try_find_subfun_hash(unsigned char *p, int *out_index) {
    (void)p;
    (void)out_index;
    return 0;
}

#endif
