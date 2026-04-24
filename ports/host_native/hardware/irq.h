/* Stub for host build */
#ifndef _HARDWARE_IRQ_H
#define _HARDWARE_IRQ_H
typedef void (*irq_handler_t)(void);
static inline void irq_set_enabled(int num, int en) { (void)num; (void)en; }
static inline void irq_set_exclusive_handler(int num, irq_handler_t h) { (void)num; (void)h; }
#endif
