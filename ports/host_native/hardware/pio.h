/* Stub for host build */
#ifndef _HARDWARE_PIO_H
#define _HARDWARE_PIO_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { int dummy; } pio_hw_t;
typedef pio_hw_t *PIO;
typedef struct { uint32_t dummy; } pio_sm_config;
typedef struct { uint16_t *instructions; uint8_t length; uint8_t origin; } pio_program_t;
#define pio0 ((PIO)0)
#define pio1 ((PIO)0)
#define pio2 ((PIO)0)
#endif
