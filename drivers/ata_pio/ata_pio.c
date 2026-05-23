/*
 * drivers/ata_pio/ata_pio.c — IDE / ATA in PIO mode.
 *
 * Register layout (per channel base I/O port):
 *   +0  data            (16-bit; 256 reads/writes per sector)
 *   +1  features (W) / error (R)
 *   +2  sector count
 *   +3  LBA low  / sector number
 *   +4  LBA mid  / cylinder low
 *   +5  LBA high / cylinder high
 *   +6  drive/head: bits 0-3 = LBA bits 24-27,
 *                   bit  4   = 0:master 1:slave,
 *                   bit  5   = 1 (legacy, must be 1),
 *                   bit  6   = 1:LBA mode 0:CHS,
 *                   bit  7   = 1 (legacy, must be 1)
 *   +7  command (W) / status (R)
 *
 * Status bits:
 *   0x80 BSY       device busy; ignore other bits while set
 *   0x40 DRDY      device ready
 *   0x20 DF        device fault
 *   0x10 DSC       drive seek complete
 *   0x08 DRQ       data request — driver should transfer next word(s)
 *   0x04 CORR      corrected data
 *   0x02 IDX       index pulse (deprecated)
 *   0x01 ERR       error; details in error register
 *
 * Control register at <channel_base + 0x206> (or 0x3F6 / 0x376):
 *   bit 1 nIEN: 1 disables IRQs (we use polling, so set to 1)
 *   bit 2 SRST: software reset pulse
 *
 * The 400 ns "wait for status to settle" idiom is to read the
 * alternate status register four times — that's a classic since the
 * original ATA spec.
 */

#include "ata_pio.h"

#include "../../ports/pc386/io.h"

#define ATA_PRIMARY_BASE     0x1F0
#define ATA_PRIMARY_CTL      0x3F6
#define ATA_SECONDARY_BASE   0x170
#define ATA_SECONDARY_CTL    0x376

#define ATA_REG_DATA         0
#define ATA_REG_ERROR        1
#define ATA_REG_FEATURES     1
#define ATA_REG_SECCOUNT     2
#define ATA_REG_LBA_LO       3
#define ATA_REG_LBA_MID      4
#define ATA_REG_LBA_HI       5
#define ATA_REG_DRIVE_HEAD   6
#define ATA_REG_STATUS       7
#define ATA_REG_COMMAND      7

#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_CACHE_FLUSH  0xE7

#define ATA_SR_BSY           0x80
#define ATA_SR_DRDY          0x40
#define ATA_SR_DF            0x20
#define ATA_SR_DRQ           0x08
#define ATA_SR_ERR           0x01

#define ATA_DRIVEHEAD_LBA    0xE0   /* base value for master + LBA */
#define ATA_DRIVEHEAD_SLAVE  0x10   /* OR-in for slave */

typedef struct {
    uint16_t base;
    uint16_t ctl;
    uint8_t  drive;     /* 0 master, 1 slave */
} ata_channel_t;

static ata_drive_info_t drives[ATA_DRIVE_COUNT];

static const ata_channel_t channels[ATA_DRIVE_COUNT] = {
    [ATA_PRIMARY_MASTER]   = { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTL,   0 },
    [ATA_PRIMARY_SLAVE]    = { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTL,   1 },
    [ATA_SECONDARY_MASTER] = { ATA_SECONDARY_BASE, ATA_SECONDARY_CTL, 0 },
    [ATA_SECONDARY_SLAVE]  = { ATA_SECONDARY_BASE, ATA_SECONDARY_CTL, 1 },
};

/* ~400 ns "let the status latch update" delay: read alt-status 4x. */
static void io_settle(uint16_t ctl) {
    (void) inb(ctl);
    (void) inb(ctl);
    (void) inb(ctl);
    (void) inb(ctl);
}

static int wait_busy_clear(uint16_t base) {
    /* Bounded spin so a missing/dead drive doesn't lock the boot. */
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t s = inb(base + ATA_REG_STATUS);
        if ((s & ATA_SR_BSY) == 0) {
            return 0;
        }
    }
    return -1;
}

static int wait_drq_or_err(uint16_t base) {
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint8_t s = inb(base + ATA_REG_STATUS);
        if (s & (ATA_SR_DRQ | ATA_SR_ERR | ATA_SR_DF)) {
            return (s & (ATA_SR_ERR | ATA_SR_DF)) ? -1 : 0;
        }
    }
    return -1;
}

static void select_drive(const ata_channel_t *ch) {
    /* For IDENTIFY / non-LBA selects, set master/slave only with
     * legacy bit 7 + bit 5 set; LBA-form selects come later. */
    uint8_t v = 0xA0 | (ch->drive ? 0x10 : 0x00);
    outb(ch->base + ATA_REG_DRIVE_HEAD, v);
    io_settle(ch->ctl);
}

static void disable_irqs(const ata_channel_t *ch) {
    /* nIEN = 1 suppresses interrupts — we poll. */
    outb(ch->ctl, 0x02);
}

static void copy_id_string(char *dst, const uint16_t *src, int word_count) {
    /* ATA IDENTIFY strings are big-endian byte pairs within little-
     * endian 16-bit words. Swap each pair while copying. */
    for (int i = 0; i < word_count; i++) {
        uint16_t w = src[i];
        dst[i * 2 + 0] = (char) ((w >> 8) & 0xFF);
        dst[i * 2 + 1] = (char) (w & 0xFF);
    }
    dst[word_count * 2] = '\0';
    /* Trim trailing spaces. */
    for (int i = word_count * 2 - 1; i >= 0 && dst[i] == ' '; i--) {
        dst[i] = '\0';
    }
}

static bool identify_one(unsigned drive_idx) {
    const ata_channel_t *ch = &channels[drive_idx];
    ata_drive_info_t   *info = &drives[drive_idx];

    info->present = false;

    disable_irqs(ch);
    select_drive(ch);

    /* Zero out the LBA / count regs as the spec requires. */
    outb(ch->base + ATA_REG_SECCOUNT, 0);
    outb(ch->base + ATA_REG_LBA_LO,   0);
    outb(ch->base + ATA_REG_LBA_MID,  0);
    outb(ch->base + ATA_REG_LBA_HI,   0);

    outb(ch->base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    io_settle(ch->ctl);

    uint8_t s = inb(ch->base + ATA_REG_STATUS);
    if (s == 0) {
        /* Floating bus — no drive on this slot. */
        return false;
    }

    if (wait_busy_clear(ch->base) < 0) {
        return false;
    }

    /* Per spec, after IDENTIFY: if LBA_MID/HI are non-zero the device
     * is ATAPI / SATA — we only handle ATA disks here. */
    uint8_t lm = inb(ch->base + ATA_REG_LBA_MID);
    uint8_t lh = inb(ch->base + ATA_REG_LBA_HI);
    if (lm != 0 || lh != 0) {
        return false;
    }

    if (wait_drq_or_err(ch->base) < 0) {
        return false;
    }

    /* Read the 256-word IDENTIFY block. */
    uint16_t id[256];
    for (int i = 0; i < 256; i++) {
        id[i] = inw(ch->base + ATA_REG_DATA);
    }

    /* Words 60-61: LBA28 sector count (little-endian dword). */
    uint32_t lba28 = (uint32_t) id[60] | ((uint32_t) id[61] << 16);
    if (lba28 == 0 || lba28 > 0x0FFFFFFFu) {
        lba28 &= 0x0FFFFFFFu;  /* cap to LBA28 limit */
    }

    info->present      = true;
    info->sector_count = lba28;
    /* Words 27..46: 40-byte model string, big-endian byte pairs. */
    copy_id_string(info->model, &id[27], 20);
    return true;
}

void ata_init(void) {
    for (unsigned i = 0; i < ATA_DRIVE_COUNT; i++) {
        identify_one(i);
    }
}

const ata_drive_info_t *ata_drive(unsigned drive) {
    if (drive >= ATA_DRIVE_COUNT) {
        return NULL;
    }
    return &drives[drive];
}

static int do_pio_xfer(unsigned drive, uint32_t lba, uint8_t count,
                       void *buf, bool is_write)
{
    if (drive >= ATA_DRIVE_COUNT || !drives[drive].present) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (lba > 0x0FFFFFFFu) {
        return -1;
    }

    const ata_channel_t *ch = &channels[drive];
    uint16_t base = ch->base;
    uint8_t  drv  = (uint8_t)
        (ATA_DRIVEHEAD_LBA | (ch->drive ? ATA_DRIVEHEAD_SLAVE : 0)
         | ((lba >> 24) & 0x0F));

    if (wait_busy_clear(base) < 0) return -1;

    outb(base + ATA_REG_DRIVE_HEAD, drv);
    io_settle(ch->ctl);

    outb(base + ATA_REG_SECCOUNT, count);
    outb(base + ATA_REG_LBA_LO,  (uint8_t) (lba         & 0xFF));
    outb(base + ATA_REG_LBA_MID, (uint8_t) ((lba >>  8) & 0xFF));
    outb(base + ATA_REG_LBA_HI,  (uint8_t) ((lba >> 16) & 0xFF));

    outb(base + ATA_REG_COMMAND, is_write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

    uint16_t       *rdbuf =       (uint16_t *) buf;
    const uint16_t *wrbuf = (const uint16_t *) buf;

    for (unsigned s = 0; s < count; s++) {
        if (wait_busy_clear(base) < 0) return -1;
        if (wait_drq_or_err(base) < 0) return -1;
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            if (is_write) {
                outw(base + ATA_REG_DATA, *wrbuf++);
            } else {
                *rdbuf++ = inw(base + ATA_REG_DATA);
            }
        }
        if (is_write) {
            /* Flush after each sector ensures the device commits before
             * we report success. Slower than batching, but correct. */
        }
    }

    if (is_write) {
        /* After the last sector's data words are written, the drive
         * raises BSY while it commits to media. Issuing a new command
         * (CACHE_FLUSH) while BSY is asserted is undefined per ATA
         * spec — most CF cards hang. Wait for the drive to finish the
         * write before kicking off the flush. */
        if (wait_busy_clear(base) < 0) return -1;
        outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        if (wait_busy_clear(base) < 0) return -1;
    }
    return 0;
}

int ata_read_sectors(unsigned drive, uint32_t lba, uint8_t count, void *buf) {
    return do_pio_xfer(drive, lba, count, buf, false);
}

int ata_write_sectors(unsigned drive, uint32_t lba, uint8_t count, const void *buf) {
    return do_pio_xfer(drive, lba, count, (void *) buf, true);
}
