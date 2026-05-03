/*
 * drivers/heartbeat/heartbeat_stub.c — heartbeat-LED no-op stub.
 * Linked on ports where the onboard LED is owned by another
 * peripheral (CYW43 radio on WiFi ports, host-platform display, etc.).
 */

#include "hal/hal_heartbeat.h"

void hal_heartbeat_tick(void) {}
