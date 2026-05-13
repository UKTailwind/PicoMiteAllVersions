#ifndef HAL_WATCHDOG_H
#define HAL_WATCHDOG_H

void hal_watchdog_disable(void);
void hal_watchdog_enable_ms(unsigned int ms, int pause_on_debug);

#endif
