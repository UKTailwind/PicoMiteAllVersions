#include "hal/hal_watchdog.h"

void hal_watchdog_disable(void) {}

void hal_watchdog_enable_ms(unsigned int ms, int pause_on_debug)
{
    (void)ms;
    (void)pause_on_debug;
}
