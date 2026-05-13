/*
 * ports/pc386/multiboot1.h — multiboot1 information-structure layout.
 *
 * Spec: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 * Section 3.3, "Boot information format".
 *
 * The bootloader (Limine, GRUB) or QEMU `-kernel` populates a struct
 * at the address EBX points to on entry, and sets bits in `flags` to
 * indicate which fields were filled. We only ask for and rely on the
 * subset relevant to Stage 1 (memory info + mmap).
 *
 * Field offsets are nailed down by the spec; do not reorder.
 */

#ifndef PORTS_PC386_MULTIBOOT1_H
#define PORTS_PC386_MULTIBOOT1_H

#include <stdint.h>

#define MULTIBOOT1_BOOTLOADER_MAGIC 0x2BADB002u

/* Bits in mb1_info_t::flags. Each bit means "the corresponding field
 * group in this structure was filled in by the bootloader". The spec
 * (section 3.3) numbers bits 0..12. */
#define MB1_INFO_MEMORY      (1u << 0)   /* mem_lower / mem_upper           */
#define MB1_INFO_BOOTDEV     (1u << 1)   /* boot_device                     */
#define MB1_INFO_CMDLINE     (1u << 2)   /* cmdline                         */
#define MB1_INFO_MODS        (1u << 3)   /* mods_count / mods_addr          */
#define MB1_INFO_AOUT_SYMS   (1u << 4)   /* a.out symbol table              */
#define MB1_INFO_ELF_SHDR    (1u << 5)   /* ELF section header table        */
#define MB1_INFO_MMAP        (1u << 6)   /* mmap_length / mmap_addr         */
#define MB1_INFO_DRIVES      (1u << 7)
#define MB1_INFO_CONFIG      (1u << 8)
#define MB1_INFO_BOOTLDR     (1u << 9)
#define MB1_INFO_APM_TABLE   (1u << 10)
#define MB1_INFO_VBE         (1u << 11)
#define MB1_INFO_FRAMEBUFFER (1u << 12)

/* Top-level info structure pointed to by EBX on entry. */
typedef struct __attribute__((packed)) {
    uint32_t flags;

    /* If MB1_INFO_MEMORY (bit 1): conventional + extended in KB. */
    uint32_t mem_lower;          /* KB below 1 MB (max 640)          */
    uint32_t mem_upper;          /* KB above 1 MB                    */

    /* If MB1_INFO_BOOTDEV (bit 2). */
    uint32_t boot_device;

    /* If MB1_INFO_CMDLINE (bit 3): physical addr of NUL-terminated string. */
    uint32_t cmdline;

    /* If MB1_INFO_MODS (bit 4): array of mb1_module_t. */
    uint32_t mods_count;
    uint32_t mods_addr;

    /* If MB1_INFO_AOUT_SYMS (bit 5) or MB1_INFO_ELF_SHDR (bit 5 alt): unioned. */
    uint32_t syms[4];

    /* If MB1_INFO_MMAP (bit 6): byte length + base of mb1_mmap_entry_t list. */
    uint32_t mmap_length;
    uint32_t mmap_addr;

    /* The remaining fields exist but Stage 1 doesn't use them. */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} mb1_info_t;

/* mmap entries are variable-size. The `size` field gives the size of
 * the entry minus the size of the `size` field itself, so to advance
 * to the next entry: `next = (uint8_t *) entry + entry->size + 4`. */
typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t length;
    uint32_t type;
} mb1_mmap_entry_t;

#define MB1_MEM_AVAILABLE        1
#define MB1_MEM_RESERVED         2
#define MB1_MEM_ACPI_RECLAIMABLE 3
#define MB1_MEM_NVS              4
#define MB1_MEM_BADRAM           5

#endif
