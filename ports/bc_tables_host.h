/*
 * Compiler-table sizes for unconstrained host builds.  Generous
 * tables so test programs that wouldn't fit on a device still compile.
 */
#ifndef BC_TABLES_HOST_H
#define BC_TABLES_HOST_H

#define BC_MAX_CODE       (64 * 1024)
#define BC_MAX_CONSTANTS  512
#define BC_MAX_SLOTS      512
#define BC_MAX_SUBFUNS    256
#define BC_MAX_FIXUPS     2048
#define BC_MAX_LINEMAP    4096
#define BC_MAX_LOCALS     64
#define BC_MAX_PARAMS     16
#define BC_MAX_LOCAL_META 4096
#define BC_MAX_NEST       64
#define BC_MAX_DATA_ITEMS 1024

#endif /* BC_TABLES_HOST_H */
