/*
 * core/state/pin_state.c — hoisted cross-cutting pin globals.
 *
 * See docs/real-hal-plan.md § "Cross-cutting state — hoisted in Phase 0.5".
 *
 * Phase 0.5 hoist: mutable pin-state tables that are read and written from
 * multiple translation units (External.c, vm_sys_pin.c, I2C.c, SPI.c,
 * SPI-LCD.c, Touch.c, mouse.c, MM_Misc.c, Onewire.c, Custom.c, SSD1963.c,
 * PicoMite.c, Functions.c).
 *
 * Storage moved here so Phase 3's per-MCU hal_pin implementations in
 * ports/rp2040/ and ports/rp2350/ can read/write pin state without pulling
 * in External.c's BASIC-level command bodies.
 *
 * Note: the const `PinDef[]` lookup table (board-level metadata — pin →
 * GPIO mapping, capability bits) stays in PicoMite.c for device builds and
 * host/host_runtime.c for the host build. That's per-board data; the plan's
 * earlier sketch conflated it with mutable pin state.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

volatile int ExtCurrentConfig[NBRPINS + 1];
