# PicoCalc Simulator

A browser-based simulator for the PicoCalc / PicoMite. Runs the same
MMBasic interpreter and bytecode VM as the device firmware — only the
I/O backend changes (framebuffer → `<canvas>`, keyboard → DOM events,
PWM audio → WebAudio, FatFS → RAM-backed FAT).

Useful for: iterating on BASIC programs without flashing, running
demos headlessly, CI, testing graphics/audio code paths before flashing.

For the design and phase-by-phase history see
[`simulator-plan.md`](simulator-plan.md).

## Quick start

```sh
cd host
./build_sim.sh          # builds ./mmbasic_sim
./run_sim.sh            # launches server on http://localhost:5150/
```

Then open [http://localhost:5150/](http://localhost:5150/) in any modern
browser. You'll see the 320×320 framebuffer and a terminal REPL in the
shell. Type BASIC in either the terminal or the browser — both reach
the interpreter.

Try a demo:

```
> RUN "graphics/demo_gfx_plasma"      ' interpreter (slow per-pixel trig)
> FRUN "graphics/demo_gfx_plasma"     ' VM - watchably fast
> RUN "sound/demo_sound_sfx"          ' click the canvas first to unlock audio
```

Press **Ctrl-D** in the terminal to exit the simulator.

## Build

The simulator binary is a superset of the test harness. They share
source but live in separate object trees.

| Target | Script | Notes |
|---|---|---|
| `mmbasic_test` | `./build.sh` | Test-harness / CLI driver. CI uses this. |
| `mmbasic_sim`  | `./build_sim.sh` | Adds Mongoose HTTP+WS server. |

Both accept `clean` / `rebuild` arguments for from-scratch builds.
Neither touches the device firmware build (`build_rp2040/`, `cmake`).

## Run

`./run_sim.sh` passes sensible defaults to `./mmbasic_sim`:

- Port **5150**, listen on **127.0.0.1**
- `--web-root` → `../web/` (served statically)
- `--sd-root`  -> `../demos/` (so `RUN "graphics/demo_gfx_shapes"` finds demos)
- `--resolution 320x320`

Any extra flags are forwarded to the binary. Common overrides:

```sh
./run_sim.sh --port 8080                      # different port
./run_sim.sh --listen 0.0.0.0                 # expose on LAN
./run_sim.sh --resolution 480x320             # larger canvas
./run_sim.sh --slowdown 20                    # throttle execution
```

### `--slowdown N` (microseconds per poll tick)

The host CPU runs MMBasic *much* faster than the RP2040 — tight loops
in BASIC finish in single-digit ms rather than seconds. Pass
`--slowdown N` to sleep `N` µs on every interpreter / VM poll checkpoint
(roughly once per statement or loop iteration). A few starting points:

| `--slowdown` | Feel |
|---:|---|
| `0`   | Uncapped. Demos blaze. Default. |
| `5`   | Gentle pacing — good for arcade-style games. |
| `20`  | Device-ish pacing for graphics loops. |
| `200` | Slow motion; mostly for observing single-step behaviour. |

Time-based primitives (`PAUSE`, `TIMER`, `ON INTERRUPT TICK`) still run
on real wall clock, so they stay honest regardless of slowdown.

## Runtime controls

### Terminal (`host` shell)

Full MMBasic REPL — `EDIT`, `LOAD`, `SAVE`, `FILES`, `RUN`, `FRUN`, history
(↑/↓), line edit (←/→/Home/End/Backspace/Delete), F1–F12. **Ctrl-D** at
the prompt exits cleanly.

### Browser

- **All keystrokes** in the canvas window reach MMBasic — alphanumerics,
  F1–F12, arrows, Home/End/PgUp/PgDn, Ctrl-\<letter\>, Insert, Delete.
- **Auto-repeat is paced** at device-ish speed (150 ms initial, 70 ms
  interval), so games that accelerate per-keypress don't overshoot.
- **Audio** requires a user gesture before the browser unlocks the
  WebAudio context — tap/click the canvas once or press any key to
  prime it.
- **Multiple browser tabs** all see the same framebuffer. Last-open
  tab joins with a full-frame bootstrap; after that everyone shares
  the live command stream.

## What's simulated

| Feature | Status |
|---|---|
| MMBasic interpreter (RUN) | ✅ full |
| Bytecode VM (FRUN) | ✅ full |
| Graphics (BOX, LINE, CIRCLE, TEXT, PIXEL, TRIANGLE, POLYGON, RBOX) | ✅ |
| `FRAMEBUFFER CREATE / WRITE / COPY / MERGE` | ✅ |
| `FASTGFX` double-buffered animation | ✅ |
| Audio (`PLAY TONE`, `PLAY SOUND 1..4`, `PLAY VOLUME`, `PLAY STOP`, `PAUSE`, `RESUME`) | ✅ |
| `PLAY WAV / FLAC / MP3 / MODFILE` | ❌ (Phase 5) |
| `PLAY LOAD SOUND` (user waveforms) | ❌ |
| File system (`LOAD`, `SAVE`, `FILES`, `RUN file`, `OPEN`) | ✅ (POSIX via `--sd-root`) |
| GPIO / SPI / I²C / PWM | ❌ (stub; no real pins) |
| RTC / `DATE$` / `TIME$` | ✅ (host wall clock; `MMBASIC_HOST_DATE` / `MMBASIC_HOST_TIME` env vars pin deterministic values for tests) |

## Troubleshooting

**Nothing is drawn / static image** — make sure you ran `build_sim.sh`
after any source change. `build.sh` only rebuilds `mmbasic_test`;
`build_sim.sh` rebuilds `mmbasic_sim`. Object files live in `sim_obj/`.

**Audio silent on first run** — browser hasn't unlocked WebAudio yet.
Click the canvas or press any key; subsequent runs will play.

**Port already in use** — pass `--port N` to pick a different one.

**Filename dropped characters in REPL** — WS keystroke forwarding has
a known ~16-char cap for rapid typing (Phase 4 polish). Keep filenames
short, or use the terminal for long input.

**Browser looks stale after rebuild** — a hard refresh (Cmd-Shift-R /
Ctrl-Shift-R) reloads `app.js` / `audio.js`. Cached old versions are
the usual culprit.

## Testing without a browser

The test harness binary `./mmbasic_test` runs BASIC programs headlessly
and compares interpreter vs VM output. This is what CI uses:

```sh
./run_tests.sh                     # run every host/tests/*.bas
./run_tests.sh --vm                # VM only
./run_tests.sh tests/t12_print.bas # single test
```

See [`host-build.md`](host-build.md) for the full test-harness story.

## Demos bundled in the repo

All under `demos/`; `./run_sim.sh` mounts that directory as the SD root. Run
with `RUN "category/name"` (or `FRUN` where noted).

**Graphics (framebuffer-based):**
- `graphics/demo_gfx_shapes` - static primitives showcase
- `graphics/demo_gfx_bounce` - 8 bouncing balls, FASTGFX
- `graphics/demo_gfx_stars` - perspective starfield warp
- `graphics/demo_gfx_plasma` - classic sine plasma (`FRUN` for speed)
- `graphics/demo_gfx_mandel` - Mandelbrot, per-pixel plot

**Graphics (direct-draw, no framebuffer):**
- `graphics/demo_draw_bounce` - erase-old / draw-new bouncing balls
- `graphics/demo_draw_clock` - analog clock
- `graphics/demo_draw_paint` - phyllotaxis rosette

**Audio:**
- `sound/demo_sound_tones` - `PLAY TONE` sweeps, stereo splits
- `sound/demo_sound_waves` - all six waveforms (S/Q/T/W/P/N)
- `sound/demo_sound_chord` - 4-slot polyphony + volume fade
- `sound/demo_melody` - "Ode to Joy" opening phrase
- `sound/demo_sound_sfx` - laser / coin / explosion / alarm

**Game:**
- `apps/pico_blocks` - breakout clone (FASTGFX-based)
