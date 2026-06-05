/*
 * drivers/fdc_82077/fdc_82077.h - NEC 765/Intel 82077-compatible FDC.
 */

#ifndef DRIVERS_FDC_82077_H
#define DRIVERS_FDC_82077_H

#include <stdbool.h>
#include <stdint.h>

#define FDC_1440_SECTOR_SIZE 512u
#define FDC_1440_SECTORS 2880u

bool fdc_init(void);
bool fdc_present(unsigned drive);
int fdc_read_sectors(unsigned drive, uint32_t lba, uint8_t count, void * buf);

#endif
