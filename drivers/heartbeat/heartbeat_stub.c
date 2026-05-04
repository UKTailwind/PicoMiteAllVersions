/*
 * drivers/heartbeat/heartbeat_stub.c — heartbeat-LED no-op stub.
 * Linked on ports where the onboard LED is owned by another
 * peripheral (CYW43 radio on WiFi ports, host-platform display, etc.).
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "hal/hal_heartbeat.h"

void hal_heartbeat_tick(void) {}

void hal_heartbeat_assert_supported(void) {
    error("Invalid configuration");
}

void hal_heartbeat_init_pins(void) {
    /* Web rp2040: CYW43 owns GPIO 23/24/25 — leave them for the radio. */
}
