/* Stub for host build */
#ifndef _PICO_STDLIB_H
#define _PICO_STDLIB_H
#include <stdint.h>
#include <stdbool.h>

uint64_t host_time_us_64(void);
void host_sleep_us(uint64_t us);

static inline void stdio_init_all(void) {}
static inline void sleep_ms(uint32_t ms) { host_sleep_us((uint64_t)ms * 1000ULL); }
static inline void sleep_us(uint64_t us) { host_sleep_us(us); }
static inline void busy_wait_us(uint64_t us) { host_sleep_us(us); }
static inline void busy_wait_us_32(uint32_t us) { host_sleep_us((uint64_t)us); }
static inline void busy_wait_ms(uint32_t ms) { host_sleep_us((uint64_t)ms * 1000ULL); }
static inline uint64_t time_us_64(void) { return host_time_us_64(); }
static inline uint32_t time_us_32(void) { return (uint32_t)host_time_us_64(); }
typedef uint64_t absolute_time_t;
static inline absolute_time_t get_absolute_time(void) { return host_time_us_64(); }
static inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return (int64_t)(b - a); }
#define PICO_DEFAULT_LED_PIN 25
#endif
