# mmbasic_ansi — MMBasic on a terminal via half-block graphics

A HAL port that renders MMBasic's 320×320 framebuffer to the terminal
using Unicode `▀` half-block glyphs with 24-bit ANSI truecolor. Every
pixel MMBasic paints is visible; **text output never reaches stdout
directly** — `PRINT`, error messages, banners, and the REPL prompt all
rasterise into the framebuffer as glyphs, and the render thread
paints them as half-block pixels just like any other graphic.

## Build

```
cd ports/mmbasic_ansi
make
```

Produces `./mmbasic_ansi`.

## Run

```
./mmbasic_ansi                          # interactive REPL
./mmbasic_ansi demos/demo_hello.bas     # run a script via the bytecode VM
./mmbasic_ansi --interp prog.bas        # run via the legacy interpreter
./mmbasic_ansi --resolution 320x240 demos/demo_mandel.bas
```

Requires a TTY on both stdin and stdout; the binary refuses to run in
a pipe.

### Options

| Flag | Meaning |
|---|---|
| `--resolution WxH` | Framebuffer size (default: auto-fit terminal). |
| `--modes N:WxH,...` | Override MODE-N table entries (1..5). e.g. `--modes 1:320x200,2:640x480`. |
| `--repeat INIT,RATE` | Opt-in per-key rate limiter (50..2000, 10..1000 ms). Holding the same key emits the first repeat after `INIT` ms, then one per `RATE` ms. Off by default — the OS terminal drives repeat. e.g. `--repeat 600,200`. Single-byte keys only; arrow keys + function keys (multi-byte escape sequences) use the OS rate. |
| `--slowdown US` | Per-tick microsleep for device-like pacing (0 disables). Accumulator-based so sub-ms values still take effect on Windows. |
| `--memory KB` | MMBasic heap size in KB (16..2048). Can only shrink — `AllMemory[]` is sized at compile time. |
| `--interp` / `--vm` | Run via the interpreter or the bytecode VM (default: `--vm`). |
| `--help`, `-h` | Show all flags with examples. |

### Built-in BASIC commands (this port)

- `MODE 1..5` — switch the framebuffer to the size at that slot in the
  mode table. Defaults: 320×240, 640×480, 800×600, 320×200, 480×320.
  Override with `--modes` at launch.
- `QUIT` — clean process exit. Useful in the alt-screen REPL where the
  user can't always see what they typed.

Ctrl-C also exits cleanly (POSIX via SIGINT, Windows via
`SetConsoleCtrlHandler`).

### Keyboard repeat

By default the OS terminal layer drives the repeat rate when a key is
held. Pass `--repeat INIT,RATE` to opt into a per-key rate limiter
that runs in front of the stdin byte reader (`host_keyrepeat.c`).
While armed, identical bytes arriving from stdin are dropped until
either `INIT` ms (first repeat) or `RATE` ms (subsequent repeats)
have elapsed since the last byte passed through; a different byte
always passes immediately and resets the held-key state. Useful for
games that want device-style pacing on top of whatever the OS is
already doing.

## Terminal size

Default framebuffer is 320×320, which needs **320 columns × 160 rows**
of terminal cells (half-blocks pair two pixels vertically into one
cell). If the terminal is smaller the display is letterboxed to the
top-left; shrink your font or enlarge the window until it fits.

On a 13" MacBook you'll typically need ~7pt font in fullscreen; on a
27" monitor ~9pt; on 4K ~11pt works comfortably.

## What works

- All framebuffer graphics (LINE, CIRCLE, BOX, PIXEL, BLIT, …).
- Bytecode VM and legacy interpreter behind `--vm` / `--interp`.
- REPL + EDIT (arrow keys, function keys, Ctrl-C via stdin raw mode —
  decoded by host_native's MMInkey escape handler).
- FRAMEBUFFER CREATE/LAYER/MERGE/COPY, FASTGFX.
- `PAUSE`, `TIMER`, `INKEY$`, scripted keys via env vars.
- Full FAT RAM disk plus POSIX passthrough (CWD = SD root).

## What does not work

- **Audio**. Terminals only expose `\x07` (BEL) — far too crude to
  represent PLAY TONE or PLAY SOUND. `PLAY` statements run without
  error but produce no sound. If you need audio, use `host_native`
  (`--sim` WebSocket bridge) or the WASM web host.
- Real 300 Hz refresh — terminals cap around 30–60 frames/sec on a
  full repaint depending on emulator. Dirty-cell batching keeps the
  byte rate down, but running a 60 FPS FASTGFX demo full-screen
  through iTerm2 will miss frames.

## Architecture

- `ansi_main.c` — entry. Forces `OptionConsole = SCREEN` + green-phosphor
  palette + 8×12 font, installs a null `host_output_hook` so stray
  stdout writes get swallowed, then runs the REPL or a script.
- `ansi_terminal.c` — alt-screen enter/exit (`\x1b[?1049h/l`), cursor
  hide (`\x1b[?25l/h`), SIGWINCH handler. Chains onto host_native's
  `host_terminal.c` for stdin raw mode.
- `ansi_display.c` — pthread that polls `host_fb_generation`, repaints
  only changed cells. Tracks a per-cell `(top, bot)` shadow, elides
  redundant cursor moves and SGR changes, flushes each frame in one
  `write(2)`.

Everything else (MMBasic core, VM, filesystem, keyboard decoding) is
linked straight from `ports/host_native/` and the repo-root shared
sources. The port adds ~450 lines of new code.
