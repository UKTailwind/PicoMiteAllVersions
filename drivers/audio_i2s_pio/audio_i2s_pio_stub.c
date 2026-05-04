/*
 * drivers/audio_i2s_pio/audio_i2s_pio_stub.c — no-op stub for
 * pure-VGA ports. The QVGA scanout (drivers/vga_pio/vga_qvga_modes.c)
 * already calls pio_add_program(&i2s_program) during scanout init
 * because audio I²S shares PIO 0 with the QVGA state machine on
 * pure-VGA boards. Custom.c's start_i2s would otherwise reload the
 * program and clobber the QVGA layout.
 */

#include "hal/hal_main_init.h"

void port_audio_i2s_pio_add_program(void *pio) {
    (void)pio;
}
