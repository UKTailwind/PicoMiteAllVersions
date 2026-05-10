#include "hal/hal_watchdog.h"

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

void hal_watchdog_disable(void)
{
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
}

void hal_watchdog_enable_ms(unsigned int ms, int pause_on_debug)
{
    watchdog_enable(ms, pause_on_debug);
}
