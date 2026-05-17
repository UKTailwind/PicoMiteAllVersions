/*
 * Compiler-table sizes for RP2350-class targets (~300 KB heap budget).
 *
 * Also a good match for browser/host builds that want a realistic
 * profile rather than the indulgent host-generous set.
 */
#ifndef BC_TABLES_RP2350_H
#define BC_TABLES_RP2350_H

#define BC_MAX_CODE       (32 * 1024)
#define BC_MAX_CONSTANTS  96
#define BC_MAX_SLOTS      192
#define BC_MAX_SUBFUNS    96
#define BC_MAX_FIXUPS     512
#define BC_MAX_LINEMAP    1024
#define BC_MAX_LOCALS     64
#define BC_MAX_PARAMS     16
#define BC_MAX_LOCAL_META 384
#define BC_MAX_NEST       32
#define BC_MAX_DATA_ITEMS 1024

#endif /* BC_TABLES_RP2350_H */
