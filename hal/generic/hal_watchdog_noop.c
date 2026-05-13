/*
 * hal/generic/hal_watchdog_noop.c — no-op watchdog impl.
 *
 * Linked by every port that has no hardware watchdog timer to manage:
 * host_native, mmbasic_stdio, esp32_s3_metro, pc386. The Pico SDK ports
 * have their own real impl in ports/pico_sdk_common/.
 *
 * Don't add behavior here — if a future port needs a real watchdog,
 * write its own hal_watchdog_<port>.c and drop this from its source list.
 */

#include "hal/hal_watchdog.h"

void hal_watchdog_disable(void) {}

void hal_watchdog_enable_ms(unsigned int ms, int pause_on_debug)
{
    (void)ms;
    (void)pause_on_debug;
}
