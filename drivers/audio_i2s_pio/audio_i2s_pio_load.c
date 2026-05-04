/*
 * drivers/audio_i2s_pio/audio_i2s_pio_load.c — load the I²S PIO
 * program on ports that don't share their PIO with the VGA scanout
 * (PicoMite SPI-LCD, Web, HDMI, VGA+WiFi rp2350). Pure-VGA ports
 * have already loaded i2s_program from drivers/vga_pio/vga_qvga_modes.c
 * during scanout init, so they link audio_i2s_pio_stub.c instead and
 * never call pio_add_program a second time.
 */

#include "Hardware_Includes.h"
#include "hal/hal_main_init.h"
#include "hardware/pio.h"
#include "PicoMiteI2S.pio.h"

extern uint I2SOff;

void port_audio_i2s_pio_add_program(void *pio_v) {
    PIO pio = (PIO)pio_v;
#ifdef rp2350
    if (PinDef[Option.audio_i2s_bclk].GPno + 1 > 31 ||
        PinDef[Option.audio_i2s_data].GPno > 31) {
        pio_set_gpio_base(pio, 16);
    }
#endif
    I2SOff = pio_add_program(pio, &i2s_program);
}
