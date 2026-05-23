/*
 * ports/pc386/hal_time_pc386.c - monotonic clock + sleep over PIT channel 0.
 *
 * The Pocket386 target is 386-class, so do not use RDTSC or PAUSE. BIOS
 * leaves PIT channel 0 running at 1.193182 MHz; we latch and read its
 * 16-bit down-counter, accumulating deltas between reads.
 */

#include <stdbool.h>
#include <stdint.h>

#include "hal/hal_time.h"

#define PIT_FREQ_HZ     1193182u
#define PIT_CHAN0_DATA  0x40
#define PIT_MODE_REG    0x43

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static bool pit_clock_started;
static uint16_t pit_last_count;
static uint64_t pit_total_ticks;

static uint16_t pit_read_counter0(void) {
    outb(PIT_MODE_REG, 0x00);  /* latch channel 0 count */
    uint8_t lo = inb(PIT_CHAN0_DATA);
    uint8_t hi = inb(PIT_CHAN0_DATA);
    return (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

static void pit_update(void) {
    uint16_t now = pit_read_counter0();
    if (!pit_clock_started) {
        pit_last_count = now;
        pit_clock_started = true;
        return;
    }
    pit_total_ticks += (uint16_t)(pit_last_count - now);
    pit_last_count = now;
}

uint64_t hal_time_us_64(void)
{
    pit_update();
    return pit_total_ticks * 1000000ull / PIT_FREQ_HZ;
}

void hal_time_sleep_us(uint32_t us)
{
    uint64_t deadline = hal_time_us_64() + us;
    while (hal_time_us_64() < deadline) {
        __asm__ volatile("nop");
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
