/* Stub for host build.
 *
 * The test-harness host runs MMBasic on a single thread, so the
 * frameBufferMutex has no contention and mutex_enter_blocking/mutex_exit
 * collapse to no-ops. The simulator (`--sim`) has a background Mongoose
 * thread reading the framebuffer with memcpy under no lock; a torn frame
 * survives 16ms before the next broadcast overwrites it, which matches
 * existing behavior and does not require mutex semantics here.
 */
#ifndef _PICO_MUTEX_H
#define _PICO_MUTEX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct { int _placeholder; } mutex_t;

static inline void mutex_init(mutex_t *m) { (void)m; }
static inline void mutex_enter_blocking(mutex_t *m) { (void)m; }
static inline void mutex_exit(mutex_t *m) { (void)m; }
static inline bool mutex_try_enter(mutex_t *m, uint32_t *owner_out) {
    (void)m;
    if (owner_out) *owner_out = 0;
    return true;
}
static inline void __dmb(void) {}

#endif
