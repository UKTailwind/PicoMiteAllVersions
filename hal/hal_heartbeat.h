/*
 * hal/hal_heartbeat.h — onboard heartbeat-LED service hook.
 *
 * Real impl in drivers/heartbeat/heartbeat_real.c (linked on ports
 * with HAL_PORT_HAS_HEARTBEAT=1) toggles the configured GPIO once per
 * second. Stub in drivers/heartbeat/heartbeat_stub.c is a no-op
 * (linked on ports where the LED is owned by another peripheral —
 * CYW43 on WiFi ports, etc.).
 */

#ifndef HAL_HEARTBEAT_H
#define HAL_HEARTBEAT_H

#ifdef __cplusplus
extern "C" {
#endif

void hal_heartbeat_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_HEARTBEAT_H */
