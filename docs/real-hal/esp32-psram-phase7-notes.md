# ESP32 PSRAM Phase 7 — Display-Buffer Parity Notes

**Status:** Software portion of Phase 7 landed; display-buffer parity
and the 1-hour cache-coherency stress are hardware-gated follow-ups.
See `docs/real-hal/esp32-psram-realign-plan.md` for the parent plan.

## What Phase 7 was supposed to validate

From the plan, Phase 7 has three bullets:

1. A BASIC program that allocates >24 KB lands in PSRAM (observable via
   `MM.INFO(PSRAM)` + `MM.INFO(MEMORY)`).
2. A real display framebuffer on ESP32 lands in PSRAM the same way the
   `dvi_wifi_rp2350` variant does.
3. A 1-hour `mand.bas` loop with PSRAM allocations exposes any
   cache-coherency surprise.

## What landed in software

- **Bullet 1**: `examples/psram_alloc_check.bas`. Paste it into the
  REPL on any port that exposes PSRAM (RP2350 variants, ESP32) and it
  prints `OK` only when both invariants hold: PSRAM free dropped by at
  least the array byte count *and* SRAM heap free dropped by less than
  8 KB. The expected output is byte-identical between RP2350 and
  ESP32 once the realignment is complete.
- **Bullet 3**: `porttools/psram_smoke.py --stress-hours N`. Runs a
  wall-clock-bounded RAM SAVE/RAM RUN loop with an embedded
  PSRAM-allocating program (`stress_program_lines()`). Each iteration
  exercises both the bitmap allocator (DIM `big%(4096)` inside the
  pasted program) and the slot region (RAM SAVE writes to the
  PSRAMblock tail). Progress logs every `--stress-progress-minutes`
  (default 5). Exits non-zero on the first error or missing marker.
  Invocation: `python3 porttools/psram_smoke.py --target esp32 --port
  /dev/ttyACM0 --stress-hours 1`. Combine with `--stress-only` to skip
  the smoke checks.

## What's hardware-gated — bullet 2

Wiring a real display framebuffer on ESP32 requires the
`web-console-driver` branch to land its native framebuffer (the
current commits give a virtual web-console display and an optimized
display path, but no real RGB scanout yet — see `git log` on
`web-console-driver`). Until that lands there is no allocation to
observe.

### What RP2350's `dvi_wifi_rp2350` does today

The DVI/HDMI variant pre-allocates the framebuffer **inside the
`AllMemory[]` SRAM slab** as a fixed trailer:

```c
// Memory.c
unsigned char __attribute__((aligned(HAL_PORT_ALLMEMORY_ALIGN)))
    AllMemory[HEAP_MEMORY_SIZE + 256 + HAL_PORT_FRAMEBUFFER_TRAILER_BYTES];

// drivers/vga_pio/vga_memory.c
unsigned char *FRAMEBUFFER     = AllMemory + HEAP_MEMORY_SIZE + 256;
uint32_t       framebuffersize = HAL_PORT_FRAMEBUFFER_TRAILER_BYTES;
```

For `dvi_wifi_rp2350` the trailer is `320 * 240 * 2 = 153600` bytes
(`HAL_PORT_FRAMEBUFFER_TRAILER_BYTES` in
`ports/dvi_wifi_rp2350/port_config.h`). The framebuffer therefore
lives in **SRAM, not PSRAM**, on RP2350 — PSRAM holds the user heap
overflow allocations (DIM big%() etc.) plus the RAM SAVE slot region,
not the scanout buffer.

**Important correction to the plan wording:** the Phase 7 bullet says
"confirm it allocates against PSRAM exactly like dvi_wifi_rp2350" —
that phrasing is loose. `dvi_wifi_rp2350` actually keeps its
framebuffer in SRAM. What the parity check needs to verify is:

- ESP32's display framebuffer, once wired, sits in the analogous
  *unified slab* (either AllMemory trailer if it fits in SRAM, or an
  explicit PSRAM allocation via `GetMemory()` / `heap_caps_aligned_alloc`
  with `MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA` for DMA-capable scanout).
- Whichever path is chosen, `MM.INFO(PSRAM FREE)` and `MM.INFO(HEAP)`
  must move by the framebuffer size in the expected direction (PSRAM
  on ESP32 since the AllMemory slab is too small for a real
  framebuffer, vs. SRAM trailer on the larger RP2350 SRAM budget).

### Invariant Phase 7 will check once the ESP32 display lands

A BASIC program identical in form to `examples/psram_alloc_check.bas`,
but invoked **after a MODE/SCREENMODE that activates the
framebuffer**, should observe:

- `MM.INFO(PSRAM FREE)` drops by the framebuffer byte count (likely
  ≥150 KB depending on the chosen resolution) — assuming the ESP32
  path allocates from PSRAM, which is the expected design given the
  48 KB AllMemory heap.
- `MM.INFO(HEAP)` drops by <8 KB of bookkeeping.
- `MM.INFO(VRES)`, `MM.INFO(HRES)` report the expected dimensions.
- The framebuffer base address is contained within `[PSRAMbase,
  PSRAMbase + PSRAMsize)`, observable through whatever pointer
  inspection BASIC exposes (typically none — verification will rely on
  the delta arithmetic).

### Why it cannot be tested now

1. No `web-console-driver` framebuffer impl exists in
   `ports/esp32_s3_metro/` yet — `HAL_PORT_FRAMEBUFFER_TRAILER_BYTES`
   is 0 and the port links `vm_framebuffer_stub.c`.
2. There is no DMA-capable scanout path on ESP32 yet, so the
   `MALLOC_CAP_DMA` decision is pending.
3. No hardware is attached to this work session; even bullets 1 and 3
   are software-deliverable but unverified on silicon.

## Pending hardware follow-up

When an ESP32-S3 (Metro N16R8 or equivalent) and an RP2350 PSRAM board
are both available on the bench:

1. Paste `examples/psram_alloc_check.bas` on each board and confirm
   identical `OK` output and matching delta values.
2. Run `python3 porttools/psram_smoke.py --target esp32 --port
   /dev/tty.usbmodem... --stress-hours 1` on the ESP32 (and similarly
   on a Pico variant for cross-comparison) — expect PASS with
   progress logs every 5 minutes.
3. Once the ESP32 framebuffer lands, re-run a framebuffer-mode variant
   of `psram_alloc_check.bas` and confirm the invariant in
   "Invariant Phase 7 will check…" above.

Record device IDs and serial-port paths in
`docs/real-hal/esp32-s3-port-log.md` alongside the Phase 6 entry.
