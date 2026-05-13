/*
 * ports/pc386/heap_region.h — MMBasic heap region accessors.
 *
 * Reserves a fixed-size buffer in BSS sized by HAL_PORT_HEAP_MEMORY_SIZE
 * (port_config.h). Stage 3 hands this region to MMBasic — same pattern
 * as device ports where the heap is a static allocation rather than a
 * runtime-malloc'd region.
 *
 * BSS allocation means: zero file size, zero load cost, the loader
 * just zeroes the bytes when the kernel maps in. Suits a 386 with
 * 4 MB+ RAM trivially; on real-hardware deployments where this might
 * be tight, port_config.h is the knob to dial down.
 */

#ifndef PORTS_PC386_HEAP_REGION_H
#define PORTS_PC386_HEAP_REGION_H

#include <stddef.h>
#include <stdint.h>

void  *heap_region_base(void);
size_t heap_region_size(void);

#endif
