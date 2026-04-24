/* Stub for host build */
#ifndef _HARDWARE_WATCHDOG_H
#define _HARDWARE_WATCHDOG_H
static inline void watchdog_update(void) {}
static inline void watchdog_enable(int ms, int pause) { (void)ms; (void)pause; }
static inline void watchdog_reboot(int a, int b, int c) { (void)a; (void)b; (void)c; }
#endif
