/*
 * core/state/pin_state.c — cross-cutting mutable pin state.
 *
 * ExtCurrentConfig[] is the per-pin mode/config table written by cmd_setpin
 * and read from External.c / vm_sys_pin.c / I2C.c / SPI.c / SPI-LCD.c /
 * Touch.c / mouse.c / MM_Misc.c / Onewire.c / Custom.c / SSD1963.c /
 * PicoMite.c / Functions.c. Storage lives here so the per-MCU hal_pin
 * implementations under ports/ can read/write pin state without pulling
 * in External.c's command-level code.
 *
 * Extern declaration lives in External.h.
 *
 * Note: `PinDef[]` (const pin → GPIO mapping) is board-level data, not
 * mutable state. Device builds carry it in PicoMite.c; host builds in
 * host/host_runtime.c. It is deliberately not hoisted here.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

volatile int ExtCurrentConfig[NBRPINS + 1];
