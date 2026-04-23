/*
 * ports/pico_sdk_common/bc_bridge_pico.c — device impl of the
 * bc_bridge.c port hooks.
 *
 *   - port_bc_bridge_clear_subfun_hash()
 *   - port_bc_bridge_rehash_subfun(subfun_arr)
 *
 * Mirrors the two rp2350-only paths that used to live inline in
 * bc_bridge.c. rp2350 keeps a funtbl[] hash alongside subfun[] so
 * FindSubFun can resolve bridged SUB / FUNCTION names in O(1) instead
 * of the rp2040 linear scan. The bridge's subfun[] rebuild must
 * rebuild the hash too; both entry points route through the hook
 * pair. On rp2040 the hash table doesn't exist — both hooks no-op.
 */

#include <string.h>
#include <stdint.h>

#include "MMBasic_Includes.h"

#ifdef rp2350

void port_bc_bridge_clear_subfun_hash(void) {
    memset(funtbl, 0, sizeof(struct s_funtbl) * MAXSUBFUN);
}

void port_bc_bridge_rehash_subfun(unsigned char **subfun_arr) {
    memset(funtbl, 0, sizeof(struct s_funtbl) * MAXSUBFUN);
    for (int i = 0; i < MAXSUBFUN && subfun_arr[i] != NULL; i++) {
        unsigned char *np = subfun_arr[i];
        np += sizeof(CommandToken);
        while (*np == ' ') np++;
        char name[MAXVARLEN + 1];
        int namelen = 0;
        uint32_t hash = FNV_offset_basis;
        do {
            unsigned u = mytoupper(*np);
            hash ^= u;
            hash *= FNV_prime;
            if (namelen < MAXVARLEN) name[namelen] = (char)u;
            np++;
            namelen++;
        } while (isnamechar(*np));
        if (namelen < MAXVARLEN) name[namelen] = 0;
        else namelen = MAXVARLEN;
        hash %= MAXSUBHASH;
        while (funtbl[hash].name[0] != 0) {
            hash++;
            if (hash == MAXSUBFUN) hash = 0;
        }
        funtbl[hash].index = i;
        memcpy(funtbl[hash].name, name, (size_t)namelen);
    }
}

#else

void port_bc_bridge_clear_subfun_hash(void) {}
void port_bc_bridge_rehash_subfun(unsigned char **subfun_arr) { (void)subfun_arr; }

#endif
