/*
 * ports/pc386/mmap.c — multiboot1 memory-map walking + display.
 *
 * Each mmap entry is a variable-length struct: the `size` field gives
 * the entry's byte length minus the size of `size` itself, so to
 * advance to the next entry we step `size + 4` bytes forward. This is
 * the classic ACPI E820-derived layout passed through unchanged.
 */

#include "mmap.h"

#include <stddef.h>

#include "kprint.h"

static const char *type_name(uint32_t type) {
    switch (type) {
        case MB1_MEM_AVAILABLE:        return "available";
        case MB1_MEM_RESERVED:         return "reserved";
        case MB1_MEM_ACPI_RECLAIMABLE: return "ACPI reclaim";
        case MB1_MEM_NVS:              return "ACPI NVS";
        case MB1_MEM_BADRAM:           return "bad RAM";
        default:                       return "unknown";
    }
}

static void for_each_entry(
    const mb1_info_t *info,
    void (*visit)(const mb1_mmap_entry_t *e, void *ctx),
    void *ctx)
{
    if ((info->flags & MB1_INFO_MMAP) == 0) {
        return;
    }
    const uint8_t *cursor = (const uint8_t *) (uintptr_t) info->mmap_addr;
    const uint8_t *end    = cursor + info->mmap_length;
    while (cursor + sizeof(uint32_t) <= end) {
        const mb1_mmap_entry_t *e = (const mb1_mmap_entry_t *) cursor;
        visit(e, ctx);
        cursor += e->size + sizeof(e->size);
    }
}

static void print_entry(const mb1_mmap_entry_t *e, void *ctx) {
    (void) ctx;
    kputs("  ");
    kputhex64(e->addr);
    kputs("  ");
    kputu64(e->length);
    kputs(" bytes  ");
    kputs(type_name(e->type));
    kputc('\n');
}

void mmap_print(const mb1_info_t *info) {
    if ((info->flags & MB1_INFO_MMAP) == 0) {
        kputs("Memory map: not provided by bootloader\n");
        if (info->flags & MB1_INFO_MEMORY) {
            kputs("  basic info: lower=");
            kputu32(info->mem_lower);
            kputs(" KB, upper=");
            kputu32(info->mem_upper);
            kputs(" KB\n");
        }
        return;
    }
    kputs("Memory map:\n");
    for_each_entry(info, print_entry, NULL);
}

static void summarize_entry(const mb1_mmap_entry_t *e, void *ctx) {
    mmap_summary_t *s = (mmap_summary_t *) ctx;
    s->entry_count++;
    if (e->type == MB1_MEM_AVAILABLE) {
        s->available_count++;
        s->total_available_bytes += e->length;
        if (e->length > s->largest_region_bytes) {
            s->largest_region_bytes = e->length;
            s->largest_region_base  = e->addr;
        }
    }
}

bool mmap_summarize(const mb1_info_t *info, mmap_summary_t *out) {
    *out = (mmap_summary_t){0};
    if ((info->flags & MB1_INFO_MMAP) == 0) {
        return false;
    }
    for_each_entry(info, summarize_entry, out);
    return true;
}
