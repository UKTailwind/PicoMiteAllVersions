/*
 * core/state/option_state.c — persistent Option block storage.
 *
 * The Option struct is the canonical in-RAM copy of the persistent user
 * configuration; LoadOptions() reads the backing flash into it and
 * SaveOptions() writes it back. Keeping its storage in a single TU avoids
 * duplication between the device and host builds.
 *
 * Extern declaration lives in FileIO.h.
 *
 * The 256-byte alignment is load-bearing on device: cmd_option writes the
 * Option block to flash via flash_range_program, which requires the source
 * buffer to be aligned to a flash page boundary. Harmless on host.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

struct option_s __attribute__ ((aligned (256))) Option;
