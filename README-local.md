# Local build notes

## Firmware gate — `./build_firmware.sh`

Canonical device build for local validation. Mirrors `.github/workflows/firmware.yml`: same Pico SDK 2.1.1, same `CMakeLists.txt` COMPILE switching, same artifacts. **Never mutates the SDK tree** — the historical `gpio.c`/`gpio.h` patches were eliminated on the `sdk-patch-removal` branch (the RAM-resident GPIO IRQ dispatcher now lives in `picomite_gpio_irq.c`).

- `./build_firmware.sh`              — build both (rp2040 + rp2350)
- `./build_firmware.sh rp2040`       — rp2040 only → `build/PicoMite.uf2`
- `./build_firmware.sh rp2350`       — rp2350 only → `build2350/PicoMite.uf2`
- `PICO_SDK_PATH=... ./build_firmware.sh` to override the SDK location (default `$HOME/pico/pico-sdk`)

The script rewrites `CMakeLists.txt`'s active `set(COMPILE …)` line in-place per target and restores the git-tracked version on any exit path (including Ctrl-C). Requires `arm-none-eabi-gcc`, `cmake`, and a stock Pico SDK 2.1.1.

Flashing: with the device in BOOTSEL mode, `cp build/PicoMite.uf2 /Volumes/RPI-RP2/` (macOS). The board auto-reboots once the copy completes.

## Local helper scripts
- `./tools/validate_all.sh` — full local pre-commit gate.
- `./tools/flash.sh` — build and flash the default RP2350 firmware with `picotool`.
- `./tools/bisect_tilemap.sh` — helper for bisecting the WASM tilemap regression.

## Host oracle / VM harness
- `make -C host`
- `./host/run_tests.sh` — primary gate, runs every test through both interpreter and VM and compares output.
- `./host/run_pixel_tests.sh`
- `./host/run_host_shim_tests.sh`
- `./host/run_frontend_tests.sh`
- `./host/run_optimizer_tests.sh`
- `bash host/run_unsupported_tests.sh`
- `./host/run_missing_syscall_tests.sh` (intentionally red syscall TODO inventory)

Host binary: `host/mmbasic_test`. If the host build links against stale command-table objects after header changes, use `make -B -C host`.

## Web build
- `./host/build_wasm.sh` → `host/web/picomite.{mjs,wasm}`.
- `./host/web/serve.sh` and open `http://localhost:8000/` to smoke test.

## rp2040 RAM overflow fix (historical note)
- Symptom: linking failed with `.heap` not fitting in `RAM` (`region 'RAM' overflowed by 20 bytes`) on rp2040 variants.
- Cause: rp2040 builds used `-DPICO_HEAP_SIZE=0x1000`, and the final layout exceeded the 256 KB RAM window by 20 bytes.
- Fix: `CMakeLists.txt` sets `PICOMITE_HEAP_SIZE` to `0x0fe0` for rp2040 (keeps `0x1000` elsewhere) and passes it via `-DPICO_HEAP_SIZE=${PICOMITE_HEAP_SIZE}`.
