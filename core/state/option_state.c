/*
 * core/state/option_state.c — hoisted Option block storage.
 *
 * See docs/real-hal-plan.md § "Cross-cutting state — hoisted in Phase 0.5".
 *
 * Phase 0.5 hoist: consolidates the `Option` struct definition that was
 * previously duplicated across FileIO.c (device) and host/host_runtime.c
 * (host). Extern declaration lives in FileIO.h.
 *
 * The 256-byte alignment is load-bearing on device builds: cmd_option
 * writes the Option block to flash via `flash_range_program`, which
 * requires the source buffer to be aligned to a flash page boundary.
 * On host the attribute is harmless — the backing store just sits on a
 * 256-byte-aligned address in RAM.
 *
 * When Phase 1 lands hal_flash, the read/write path routes through
 * hal_flash_read_options / hal_flash_write_options; this storage is the
 * in-RAM working copy those functions populate.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

struct option_s __attribute__ ((aligned (256))) Option;
