/*
 * drivers/psram_heap/psram_heap_pico.c — rp2350 QSPI PSRAM heap.
 *
 * Linked on rp2350 non-WEB targets (PICORP2350, PICOUSBRP2350,
 * VGARP2350, VGAUSBRP2350, HDMI, HDMIUSB). Provides:
 *
 *   - psmap[] — page-bitmap for the 6 MB PSRAM heap (used by
 *     Commands.c's SaveContext / RestoreContext fast-path too).
 *   - SBitsGet / SBitsSet — per-page used / used+last-of-run flag
 *     helpers, identical to Memory.c's MBitsGet / MBitsSet pattern
 *     but indexed against PSRAMbase instead of MMHeap.
 *   - GetPSMemory — top-down page allocator, bitmap-backed.
 *
 * Symbols are declared in Memory.h (psmap / GetPSMemory) and
 * referenced from Memory.c (FreeMemory / GetMemory / TryGetMemory /
 * MemSize / UsedHeap / FreeHeap / FreeMemorySafe) and Commands.c
 * (SaveContext / RestoreContext). All callers runtime-guard with
 * `if(PSRAMsize)` or an address-range check, so on rp2350 WEB
 * (where PSRAMsize stays 0 because CYW43 consumes the QSPI pins)
 * this code compiles but never executes.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

unsigned int psmap[6*1024*1024/ PAGESIZE / PAGESPERWORD] = {0};
const unsigned int psmap_size_bytes = sizeof(psmap);

unsigned int __not_in_flash_func(SBitsGet)(unsigned char *addr) {
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr / PAGESIZE) / PAGESPERWORD];
    i = ((((unsigned int)addr / PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS;
    return (*p >> i) & ((1 << PAGEBITS) - 1);
}

void __not_in_flash_func(SBitsSet)(unsigned char *addr, int bits) {
    unsigned int i, *p;
    addr -= (unsigned int)PSRAMbase;
    p = &psmap[((unsigned int)addr / PAGESIZE) / PAGESPERWORD];
    i = ((((unsigned int)addr / PAGESIZE)) & (PAGESPERWORD - 1)) * PAGEBITS;
    *p = (bits << i) | (*p & (~(((1 << PAGEBITS) - 1) << i)));
}

void __not_in_flash_func(*GetPSMemory)(int size) {
    unsigned int j, n;
    unsigned char *addr;
    j = n = (size + PAGESIZE - 1) / PAGESIZE;
    for (addr = (unsigned char *)(PSRAMbase + PSRAMsize - PAGESIZE);
         addr >= (unsigned char *)PSRAMbase;
         addr -= PAGESIZE) {
        if (!(SBitsGet(addr) & PUSED)) {
            if (--n == 0) {
                j--;
                SBitsSet(addr + (j * PAGESIZE), PUSED | PLAST);
                while (j--) SBitsSet(addr + (j * PAGESIZE), PUSED);
                memset(addr, 0, size);
                return (void *)addr;
            }
        } else {
            n = j;
        }
    }
    TempStringClearStart = 0;
    ClearTempMemory();
    error("Not enough PSRAM memory");
    return NULL;
}
