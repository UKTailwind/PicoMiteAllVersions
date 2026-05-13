/*
 * ports/pc386/mmap.h — multiboot1 memory-map parsing.
 *
 * Walk the bootloader-supplied mmap and extract usable RAM regions
 * for Stage 1's heap reservation, Stage 2's disk DMA buffers (if we
 * ever DMA), and Stage 3+'s arbitrary kernel allocations.
 */

#ifndef PORTS_PC386_MMAP_H
#define PORTS_PC386_MMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "multiboot1.h"

typedef struct {
    uint64_t total_available_bytes;
    uint64_t largest_region_base;
    uint64_t largest_region_bytes;
    uint32_t entry_count;
    uint32_t available_count;
} mmap_summary_t;

/* Print the mmap to the kernel console, one line per entry. Safe to
 * call even when the bootloader did not provide a map (prints a one-
 * line note in that case). */
void mmap_print(const mb1_info_t *info);

/* Compute aggregate stats. Returns true if the mmap was usable. */
bool mmap_summarize(const mb1_info_t *info, mmap_summary_t *out);

#endif
