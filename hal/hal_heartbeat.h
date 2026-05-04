/*
 * hal/hal_heartbeat.h — onboard heartbeat-LED service hook.
 *
 * Real impl in drivers/heartbeat/heartbeat_real.c (linked on ports
 * whose heartbeat LED is owned by GPIO) toggles the configured GPIO
 * once per second. Stub in drivers/heartbeat/heartbeat_stub.c is a
 * no-op (linked on ports where the LED is owned by another peripheral
 * — CYW43 on WiFi ports, etc.). Linkage is selected per-port in
 * port_sources.cmake.
 */

#ifndef HAL_HEARTBEAT_H
#define HAL_HEARTBEAT_H

#ifdef __cplusplus
extern "C" {
#endif

void hal_heartbeat_tick(void);

/* Error if the port doesn't support a heartbeat LED — called from
 * the EXT_HEARTBEAT case in External.c's ExtCfg dispatch. Real impl
 * is a no-op; stub errors with "Invalid configuration". */
void hal_heartbeat_assert_supported(void);

/* Boot-time pin claim for the heartbeat LED + the CYW43-shadow pins
 * (41/42/44) on PicoMite/VGA/HDMI/PSRAM ports. WiFi ports stub this
 * out — the CYW43 radio owns those pins. */
void hal_heartbeat_init_pins(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_HEARTBEAT_H */
