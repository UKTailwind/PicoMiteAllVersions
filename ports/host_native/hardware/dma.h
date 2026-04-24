/* Stub for host build */
#ifndef _HARDWARE_DMA_H
#define _HARDWARE_DMA_H
#include <stdint.h>
#include <stdbool.h>
#include "hardware/irq.h"
static inline bool dma_channel_is_busy(int ch) { (void)ch; return false; }
static inline void dma_channel_abort(int ch) { (void)ch; }
static inline void dma_channel_unclaim(int ch) { (void)ch; }
static inline int  dma_claim_unused_channel(bool required) { (void)required; return 0; }
static inline void dma_channel_wait_for_finish_blocking(int ch) { (void)ch; }
typedef struct { uint32_t abort; uint32_t intf0; uint32_t inte0; } dma_hw_t;
extern dma_hw_t *dma_hw;
#define DMA_IRQ_0 0
#define DMA_IRQ_1 1
static inline void dma_channel_set_irq0_enabled(int ch, bool en) { (void)ch; (void)en; }
static inline void dma_channel_set_irq1_enabled(int ch, bool en) { (void)ch; (void)en; }
#endif
