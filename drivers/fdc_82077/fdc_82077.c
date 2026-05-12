/*
 * drivers/fdc_82077/fdc_82077.c - polling read-only FDC path.
 *
 * Minimal 1.44 MB 3.5" support for pc386. Uses the standard PC FDC
 * ports and DMA channel 2. Enough for FatFs to mount/read A:/B: in
 * QEMU and PC emulators.
 */

#include "fdc_82077.h"

#include <stddef.h>
#include <string.h>

#include "../../ports/pc386/io.h"

extern void hal_time_sleep_us(uint32_t us);

#define FDC_DOR       0x3F2
#define FDC_MSR       0x3F4
#define FDC_FIFO      0x3F5
#define FDC_CCR       0x3F7

#define FDC_MSR_RQM   0x80
#define FDC_MSR_DIO   0x40

#define FDC_CMD_SPECIFY 0x03
#define FDC_CMD_SENSEI  0x08
#define FDC_CMD_RECAL   0x07
#define FDC_CMD_SEEK    0x0F
#define FDC_CMD_READ    0xE6

#define DMA_MASK      0x0A
#define DMA_MODE      0x0B
#define DMA_CLEAR_FF  0x0C
#define DMA_CH2_ADDR  0x04
#define DMA_CH2_CNT   0x05
#define DMA_CH2_PAGE  0x81

#define FDC_SECTORS_PER_TRACK 18
#define FDC_HEADS             2

static bool fdc_probe_done;
static bool fdc_controller_ready;
static bool fdc_drive_ready[2];
static uint8_t current_cyl[2] = { 0xFF, 0xFF };
static uint8_t current_drive = 0xFF;

static uint8_t dma_buf[FDC_1440_SECTOR_SIZE] __attribute__((aligned(65536)));

static int wait_write_ready(void)
{
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & (FDC_MSR_RQM | FDC_MSR_DIO)) == FDC_MSR_RQM) return 0;
        io_wait();
    }
    return -1;
}

static int wait_read_ready(void)
{
    for (uint32_t i = 0; i < 4000000u; i++) {
        uint8_t msr = inb(FDC_MSR);
        if ((msr & (FDC_MSR_RQM | FDC_MSR_DIO)) == (FDC_MSR_RQM | FDC_MSR_DIO)) return 0;
        io_wait();
    }
    return -1;
}

static int cmd(uint8_t v)
{
    if (wait_write_ready() < 0) return -1;
    outb(FDC_FIFO, v);
    return 0;
}

static int result(uint8_t *v)
{
    if (wait_read_ready() < 0) return -1;
    *v = inb(FDC_FIFO);
    return 0;
}

static int sense(uint8_t *st0, uint8_t *cyl)
{
    if (cmd(FDC_CMD_SENSEI) < 0) return -1;
    if (result(st0) < 0) return -1;
    if (result(cyl) < 0) return -1;
    return 0;
}

static void select_drive(unsigned drive)
{
    if (current_drive == drive) return;
    uint8_t motor = (uint8_t)(0x10u << (drive & 1u));
    outb(FDC_DOR, (uint8_t)(0x0C | motor | (drive & 1u)));
    current_drive = (uint8_t)drive;
    hal_time_sleep_us(300000);
}

static void reset_controller(void)
{
    outb(FDC_DOR, 0x00);
    hal_time_sleep_us(20000);
    outb(FDC_DOR, 0x0C);
    current_drive = 0xFF;
    hal_time_sleep_us(20000);
    outb(FDC_CCR, 0x00); /* 500 kbps */
    uint8_t st0, cyl;
    for (int i = 0; i < 4; i++) (void)sense(&st0, &cyl);
}

static int specify(void)
{
    if (cmd(FDC_CMD_SPECIFY) < 0) return -1;
    if (cmd(0xDF) < 0) return -1;
    if (cmd(0x02) < 0) return -1;
    return 0;
}

static int recalibrate(unsigned drive)
{
    select_drive(drive);
    if (cmd(FDC_CMD_RECAL) < 0) return -1;
    if (cmd((uint8_t)drive) < 0) return -1;
    hal_time_sleep_us(20000);
    uint8_t st0 = 0, cyl = 0xFF;
    if (sense(&st0, &cyl) < 0) return -1;
    if ((st0 & 0xC0) != 0 || (st0 & 0x20) == 0) return -1;
    current_cyl[drive] = cyl;
    return 0;
}

bool fdc_init(void)
{
    if (fdc_probe_done) return fdc_controller_ready;
    fdc_probe_done = true;
    reset_controller();
    if (specify() < 0) return false;
    fdc_controller_ready = true;
    for (unsigned d = 0; d < 2; d++) {
        fdc_drive_ready[d] = (recalibrate(d) == 0);
    }
    return true;
}

bool fdc_present(unsigned drive)
{
    if (drive > 1) return false;
    if (!fdc_init()) return false;
    return fdc_drive_ready[drive];
}

static int seek(unsigned drive, uint8_t cyl, uint8_t head)
{
    if (current_cyl[drive] == cyl) return 0;
    select_drive(drive);
    if (cmd(FDC_CMD_SEEK) < 0) return -1;
    if (cmd((uint8_t)((head << 2) | drive)) < 0) return -1;
    if (cmd(cyl) < 0) return -1;
    hal_time_sleep_us(20000);
    uint8_t st0 = 0, sensed_cyl = 0xFF;
    if (sense(&st0, &sensed_cyl) < 0) return -1;
    if ((st0 & 0xC0) != 0 || (st0 & 0x20) == 0) return -1;
    if (sensed_cyl != cyl) return -1;
    current_cyl[drive] = cyl;
    return 0;
}

static void dma_setup_read(void)
{
    uintptr_t addr = (uintptr_t)dma_buf;
    uint16_t last = (uint16_t)(FDC_1440_SECTOR_SIZE - 1);

    outb(DMA_MASK, 0x06);
    outb(DMA_CLEAR_FF, 0x00);
    outb(DMA_MODE, 0x46);
    outb(DMA_CH2_ADDR, (uint8_t)(addr & 0xFF));
    outb(DMA_CH2_ADDR, (uint8_t)((addr >> 8) & 0xFF));
    outb(DMA_CH2_PAGE, (uint8_t)((addr >> 16) & 0xFF));
    outb(DMA_CH2_CNT, (uint8_t)(last & 0xFF));
    outb(DMA_CH2_CNT, (uint8_t)(last >> 8));
    outb(DMA_MASK, 0x02);
}

static int read_one(unsigned drive, uint32_t lba, void *buf)
{
    if (drive > 1 || lba >= FDC_1440_SECTORS) return -1;
    if (!fdc_present(drive)) return -1;

    uint8_t cyl = (uint8_t)(lba / (FDC_SECTORS_PER_TRACK * FDC_HEADS));
    uint8_t tmp = (uint8_t)(lba % (FDC_SECTORS_PER_TRACK * FDC_HEADS));
    uint8_t head = (uint8_t)(tmp / FDC_SECTORS_PER_TRACK);
    uint8_t sector = (uint8_t)((tmp % FDC_SECTORS_PER_TRACK) + 1);

    select_drive(drive);
    if (seek(drive, cyl, head) < 0) return -1;
    dma_setup_read();

    if (cmd(FDC_CMD_READ) < 0) return -1;
    if (cmd((uint8_t)((head << 2) | drive)) < 0) return -1;
    if (cmd(cyl) < 0) return -1;
    if (cmd(head) < 0) return -1;
    if (cmd(sector) < 0) return -1;
    if (cmd(2) < 0) return -1;
    if (cmd(FDC_SECTORS_PER_TRACK) < 0) return -1;
    if (cmd(0x1B) < 0) return -1;
    if (cmd(0xFF) < 0) return -1;

    uint8_t r[7];
    for (int i = 0; i < 7; i++) {
        if (result(&r[i]) < 0) return -1;
    }
    if ((r[0] & 0xC0) != 0 || r[1] != 0 || r[2] != 0) return -1;
    memcpy(buf, dma_buf, FDC_1440_SECTOR_SIZE);
    return 0;
}

int fdc_read_sectors(unsigned drive, uint32_t lba, uint8_t count, void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    while (count--) {
        if (read_one(drive, lba++, p) < 0) return -1;
        p += FDC_1440_SECTOR_SIZE;
    }
    return 0;
}
