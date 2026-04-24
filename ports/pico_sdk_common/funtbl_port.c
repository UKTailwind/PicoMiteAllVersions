/*
 * ports/pico_sdk_common/funtbl_port.c — device impl of MMBasic.c's
 * funtbl[] hash port hooks. Rp2350 is the only target that maintains
 * the funtbl[] hash alongside subfun[]; on rp2040 the hooks are
 * no-ops and the caller falls back to linear scans of subfun[] +
 * ProgMemory.
 *
 *   - port_try_find_subfun_hash(p, &out)
 *   - port_prepare_program_finalize_subfun(ErrAbort)
 *
 * hashlabels() is a local helper used only by the finalize hook on
 * rp2350, so it's kept file-static here instead of in MMBasic.c.
 *
 * Note: the rp2350 FindSubFun path intentionally does not filter by
 * SUB/FUN type.  That matches the pre-refactor behaviour — the
 * linear-scan version (rp2040 + host) does filter, so there's a
 * subtle semantic split, but it's not new.
 */

#include <string.h>

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

#ifdef rp2350

static void hashlabels(unsigned char *p, int ErrAbort) {
    int j, u, namelen;
    uint32_t originalhash, hash = FNV_offset_basis;
    char *lastp = (char *)p + 1;
    (void)ErrAbort;
    while (1) {
        if (p[0] == 0 && p[1] == 0) break;
        if (p[0] == T_NEWLINE) {
            lastp = (char *)p;
            p++;
            continue;
        }
        if (p[0] == T_LINENBR) { p += 3; continue; }
        if (p[0] == T_LABEL) {
            p++;
            hash = FNV_offset_basis;
            namelen = 0;
            for (j = 1; j <= p[0]; j++) {
                u = mytoupper(p[j]);
                hash ^= u;
                hash *= FNV_prime;
                namelen++;
            }
            hash %= MAXSUBHASH;
            originalhash = hash - 1;
            if (originalhash < 0) originalhash += MAXSUBFUN;
            while (funtbl[hash].name[0] != 0 && hash != originalhash) {
                hash++;
                hash %= MAXSUBFUN;
            }
            if (hash == originalhash) {
                MMPrintString("Error: Too many labels - erasing program\r\n");
                unsigned char dummy = 0;
                cmdline = &dummy;
                cmd_new();
            }
            funtbl[hash].index = (uint32_t)lastp;
            for (j = 0; j < p[0]; j++) funtbl[hash].name[j] = mytoupper(p[j + 1]);
            p += p[0] + 1;
            continue;
        }
        p++;
    }
}

void port_prepare_program_finalize_subfun(int ErrAbort) {
    int i, u, namelen;
    uint32_t hash;
    char printvar[MAXVARLEN + 1];
    unsigned char *p1, *p2;

    memset(funtbl, 0, sizeof(struct s_funtbl) * MAXSUBFUN);
    for (i = 0; i < MAXSUBFUN && subfun[i] != NULL; i++) {
        p1 = subfun[i];
        p1 += sizeof(CommandToken);
        skipspace(p1);
        p2 = (unsigned char *)printvar;
        namelen = 0;
        hash = FNV_offset_basis;
        do {
            u = mytoupper(*p1);
            hash ^= u;
            hash *= FNV_prime;
            *p2++ = u;
            p1++;
            if (++namelen > MAXVARLEN) {
                if (ErrAbort) error("Function name too long");
            }
        } while (isnamechar(*p1));
        if (namelen != MAXVARLEN) *p2 = 0;
        hash %= MAXSUBHASH;
        while (funtbl[hash].name[0] != 0) {
            hash++;
            if (hash == MAXSUBFUN) hash = 0;
        }
        funtbl[hash].index = i;
        memcpy(funtbl[hash].name, printvar,
               (namelen == MAXVARLEN ? (size_t)namelen : (size_t)namelen + 1));
    }
    if (Option.LIBRARY_FLASH_SIZE == MAX_PROG_SIZE) {
        hashlabels(LibMemory, ErrAbort);
    }
    hashlabels(ProgMemory, ErrAbort);
}

int port_try_check_var_subfun_collision(const unsigned char *name, int namelen) {
    uint32_t hash = FNV_offset_basis;
    uint32_t funhash;
    int j;
    const char *ip;
    char *tp;

    for (int i = 0; i < namelen; i++) {
        hash ^= name[i];
        hash *= FNV_prime;
    }
    funhash = hash % MAXSUBHASH;
    if (funtbl[funhash].name[0] == 0) return 1;
    while (funtbl[funhash].name[0] != 0) {
        ip = (const char *)name;
        tp = funtbl[funhash].name;
        if (*ip++ == *tp++) {
            j = namelen - 1;
            while (j > 0 && *ip == *tp) { j--; ip++; tp++; }
            if (j == 0 && (*(char *)tp == 0 || namelen == MAXVARLEN)) {
                if (funtbl[funhash].index < MAXSUBFUN)
                    error("A sub/fun has the same name: $", (char *)name);
            }
        }
        funhash++;
        if (funhash == MAXSUBFUN) funhash = 0;
    }
    return 1;
}

int port_try_find_label_hash(unsigned char *labelptr, unsigned char **out_ptr) {
    unsigned char *tp, *ip;
    int i;
    uint32_t hash = FNV_offset_basis;
    char label[MAXVARLEN + 1];

    if (labelptr == NULL) { *out_ptr = NULL; return 1; }

    label[1] = mytoupper(*labelptr++);
    hash ^= label[1];
    hash *= FNV_prime;
    for (i = 2; ; i++) {
        if (!isnamechar(*labelptr)) break;
        if (i > MAXVARLEN) error("Label too long");
        label[i] = mytoupper(*labelptr++);
        hash ^= label[i];
        hash *= FNV_prime;
    }
    label[0] = i - 1;
    hash %= MAXSUBHASH;
    if (funtbl[hash].name[0] == 0) error("Cannot find label");
    while (funtbl[hash].name[0] != 0) {
        tp = (unsigned char *)funtbl[hash].name;
        ip = (unsigned char *)&label[1];
        if (*ip++ == *tp++) {
            i = label[0] - 1;
            while (i > 0 && *ip == *tp) { i--; ip++; tp++; }
            if (i == 0 && (*(char *)tp == 0)) {
                *out_ptr = (unsigned char *)funtbl[hash].index;
                return 1;
            }
        }
        hash++;
        hash %= MAXSUBFUN;
    }
    if (funtbl[hash].name[0] == 0) error("Cannot find label");
    *out_ptr = NULL;
    return 1;
}

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

void port_prepare_program_finalize_subfun(int ErrAbort) {
    (void)ErrAbort;
}

int port_try_find_label_hash(unsigned char *labelptr, unsigned char **out_ptr) {
    (void)labelptr;
    (void)out_ptr;
    return 0;
}

int port_try_check_var_subfun_collision(const unsigned char *name, int namelen) {
    (void)name;
    (void)namelen;
    return 0;
}

#endif
