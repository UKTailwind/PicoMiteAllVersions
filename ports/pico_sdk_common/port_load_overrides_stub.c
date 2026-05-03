/*
 * ports/pico_sdk_common/port_load_overrides_stub.c — no-op
 * port_apply_load_overrides for ports without a board-specific
 * LoadOptions override. Linked everywhere except the PicoCalc
 * profile.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"

void port_apply_load_overrides(void) {}
