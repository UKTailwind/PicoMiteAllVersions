/* Stub for host build — shared source (FileIO.c, MM_Misc.c, etc.) pulls in
 * pico-sdk GPIO headers transitively. On host there is no GPIO peripheral
 * so all calls are wrapped in #ifndef MMBASIC_HOST. This header just
 * satisfies the include. */
#ifndef _HARDWARE_GPIO_H
#define _HARDWARE_GPIO_H
#include <stdint.h>
#endif
