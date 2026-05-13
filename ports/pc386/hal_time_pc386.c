/*
 * ports/pc386/hal_time_pc386.c — monotonic clock + sleep over TSC.
 *
 * Calibration: PIT channel 2 (speaker channel; safe to use because
 * we always disable the speaker output bit in port 0x61 first) gets
 * loaded for a known interval — 50 ms at 1.193182 MHz = 59659
 * counts. We read TSC before/after waiting for PIT OUT to assert
 * (visible on port 0x61 bit 5), and divide to get TSC ticks per µs.
 *
 * Calibration runs lazily on first hal_time_us_64 call. Until then,
 * timestamps are zero — kmain doesn't need real time before the
 * runtime starts.
 *
 * No interrupts, no IDT — we're stage 3, no IRQ subsystem yet.
 */

#include <stdint.h>

#include "hal/hal_time.h"

#define PIT_FREQ_HZ        1193182u
#define PIT_CHAN2_DATA     0x42
#define PIT_MODE_REG       0x43
#define KBD_CTRL_PORT      0x61

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t tsc_at_boot       = 0;
static uint64_t tsc_ticks_per_us  = 0;  /* 0 = uncalibrated */

static void calibrate(void) {
    /* Calibration window: 50 ms = 59659 PIT counts. Long enough for
     * a few hundred TSC samples to dominate any single-sample jitter. */
    const uint16_t pit_count = 59659;

    /* Disable speaker (bit 1 = 0), enable channel 2 gate (bit 0 = 1). */
    uint8_t kc = inb(KBD_CTRL_PORT);
    outb(KBD_CTRL_PORT, (kc & ~0x02) | 0x01);

    /* PIT channel 2, mode 0 (one-shot), lobyte/hibyte access, binary. */
    outb(PIT_MODE_REG, 0xB0);
    outb(PIT_CHAN2_DATA, (uint8_t)(pit_count & 0xFF));
    outb(PIT_CHAN2_DATA, (uint8_t)(pit_count >> 8));

    uint64_t t0 = rdtsc();
    /* Wait for PIT OUT to assert (bit 5 of port 0x61). */
    while ((inb(KBD_CTRL_PORT) & 0x20) == 0) { }
    uint64_t t1 = rdtsc();

    /* delta_us = pit_count * 1e6 / PIT_FREQ_HZ ≈ 50000 */
    uint64_t delta_us = (uint64_t)pit_count * 1000000ull / PIT_FREQ_HZ;
    tsc_ticks_per_us = (t1 - t0) / delta_us;
    if (tsc_ticks_per_us == 0) tsc_ticks_per_us = 1;  /* paranoia */

    tsc_at_boot = t1;
}

uint64_t hal_time_us_64(void)
{
    if (tsc_ticks_per_us == 0) calibrate();
    return (rdtsc() - tsc_at_boot) / tsc_ticks_per_us;
}

void hal_time_sleep_us(uint32_t us)
{
    if (tsc_ticks_per_us == 0) calibrate();
    uint64_t deadline = rdtsc() + (uint64_t)us * tsc_ticks_per_us;
    while (rdtsc() < deadline) {
        __asm__ volatile("pause");
    }
}

uint32_t hal_time_ms_tick(void)
{
    return (uint32_t)(hal_time_us_64() / 1000ull);
}

void hal_time_slowdown_tick(void)
{
    /* No scheduler to yield to on bare metal. */
}
