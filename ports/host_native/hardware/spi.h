/* Stub for host build */
#ifndef _HARDWARE_SPI_H
#define _HARDWARE_SPI_H
#include <stdint.h>
typedef struct { int dummy; } spi_inst_t;
#define spi0 ((spi_inst_t *)0)
#define spi1 ((spi_inst_t *)0)
static inline int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, int len) { (void)spi; (void)src; return len; }
static inline int spi_read_blocking(spi_inst_t *spi, uint8_t tx, uint8_t *dst, int len) { (void)spi; (void)tx; (void)dst; return len; }
#endif
