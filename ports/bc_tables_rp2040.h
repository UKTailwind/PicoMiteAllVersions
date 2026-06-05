/*
 * Compiler-table sizes for RP2040-class targets (~128 KB heap budget).
 *
 * Included by a port's port_config.h.  Never include directly from core
 * sources.  bytecode.h consumes BC_MAX_* and errors if a port forgets
 * to define them.
 */
#ifndef BC_TABLES_RP2040_H
#define BC_TABLES_RP2040_H

#define BC_MAX_CODE (16 * 1024)
#define BC_MAX_CONSTANTS 64
#define BC_MAX_SLOTS 128
#define BC_MAX_SUBFUNS 32
#define BC_MAX_FIXUPS 256
#define BC_MAX_LINEMAP 512
#define BC_MAX_LOCALS 64
#define BC_MAX_PARAMS 16
#define BC_MAX_LOCAL_META 256
#define BC_MAX_NEST 16
#define BC_MAX_DATA_ITEMS 512

#endif /* BC_TABLES_RP2040_H */
