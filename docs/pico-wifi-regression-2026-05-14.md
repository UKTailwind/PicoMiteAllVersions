# Pico WiFi connect — first-board failure (2026-05-14)

**Resolved: hardware issue on one specific WebMite unit, not a software regression.**

## What happened

On WebMite RP2350B unit A (ID `63F0D5EC5CA`), `WEB CONNECT "<SSID>","<pass>"`
returned `failed to connect.` within ~5 s. `WEB SCAN` saw the AP at −51 dBm.
ESP32-S3 on the same network connected with the same credentials.

A different WebMite RP2350B unit, same firmware (`VMMBA 1.0.0`), same SSID and
password, connected immediately and got `192.168.4.74`. Confirmed the firmware
is good; unit A has a hardware fault on the CYW43 side (radio / antenna / PIO
gSPI bus to the module).

## Speculation I made before swapping boards (all wrong — retracted)

1. **AES-only auth theory.** I misread `sec: 5` from `WEB SCAN` as
   "mixed AES/TKIP." It is not. `cyw43_ll.c:573-587` builds that byte as a
   bitmask: `4 (WPA2) | 1 (privacy bit) = 5`, i.e. plain WPA2. The hardcoded
   `CYW43_AUTH_WPA2_AES_PSK` in `drivers/net_lwip_raw/hal_net_lwip.c:499` was
   not the cause. **Do not apply a change to that constant.**

2. **PSRAM-enable regression theory.** I pointed at `e681672 Enable PSRAM on
   WebMite RP2350B` as the recent change that flipped CYW43 behavior. The user
   confirmed WiFi and PSRAM have run together on this firmware already, so
   PSRAM enable is not the cause. **Do not revert e681672 over this.**

Both guesses were "what changed recently" without a mechanism. Recording the
retraction here so a future reader doesn't pick them back up.

## Honest diagnostic gap

The one piece of information that would have shortcut all of the above is the
raw `rc` from `cyw43_arch_wifi_connect_timeout_ms` — `PICO_ERROR_BADAUTH (-3)`
vs `PICO_ERROR_CONNECT_FAILED (-4)` vs `PICO_ERROR_TIMEOUT (-1)` vs other.
`drivers/net_lwip_raw/hal_net_lwip.c:504` collapses any non-zero to
`HAL_NET_TIMEOUT` and the "failed to connect." print loses the distinction.

Consider a small change: print the cyw43 `rc` value alongside the "failed to
connect." message (debug-only or always — the failure path is already
user-facing and a small integer doesn't hurt). That single line would have
told us in one round trip whether the chip rejected the auth, didn't find the
SSID, or fell off the bus.

## Action items

- [x] Retract speculation, document.
- [ ] Optional: surface `cyw43_arch_wifi_connect_timeout_ms` rc in the
  "failed to connect." path. Decide whether this is worth a centralized
  change to `drivers/net_lwip_raw/hal_net_lwip.c` after thinking through the
  blast radius across vga_wifi_rp2350, dvi_wifi_rp2350, web_rp2350.
- Set unit A aside; do not flash experimental firmware to it during WiFi
  debugging.
