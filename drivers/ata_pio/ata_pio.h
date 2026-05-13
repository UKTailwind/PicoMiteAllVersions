/*
 * drivers/ata_pio/ata_pio.h — IDE / ATA in PIO mode (no DMA).
 *
 * Covers up to four drives across two channels:
 *   primary master   = 0
 *   primary slave    = 1
 *   secondary master = 2
 *   secondary slave  = 3
 *
 * LBA28 mode, 512-byte sectors. That's sufficient for any image size
 * up to ~128 GB — far beyond the largest disk this port will ever
 * realistically use. LBA48 is not needed.
 *
 * Read/write are blocking: we spin-wait on BSY/DRQ. Throughput is
 * ~1 MB/s per drive, fine for boot and BASIC file I/O. If a future
 * stage needs faster bulk I/O, switch to UDMA and add an IRQ handler
 * — but that's a Stage 8+ optimisation, not a correctness gate.
 */

#ifndef DRIVERS_ATA_PIO_H
#define DRIVERS_ATA_PIO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ATA_DRIVE_COUNT          4
#define ATA_PRIMARY_MASTER       0
#define ATA_PRIMARY_SLAVE        1
#define ATA_SECONDARY_MASTER     2
#define ATA_SECONDARY_SLAVE      3

#define ATA_SECTOR_SIZE          512

typedef struct {
    bool        present;
    uint32_t    sector_count;          /* LBA28 max — capped at 0x0FFFFFFF */
    char        model[41];             /* 40 chars + NUL */
} ata_drive_info_t;

void ata_init(void);
const ata_drive_info_t *ata_drive(unsigned drive);

/* Returns 0 on success, non-zero on failure. count is in sectors. */
int  ata_read_sectors (unsigned drive, uint32_t lba, uint8_t count, void *buf);
int  ata_write_sectors(unsigned drive, uint32_t lba, uint8_t count, const void *buf);

#endif
