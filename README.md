# MMBasic Anywhere

A portable build of [PicoMite](https://github.com/UKTailwind/PicoMiteAllVersions) MMBasic. Same BASIC dialect; the same source compiles for fourteen RP2040 / RP2350 device variants, native macOS, WebAssembly in a browser, and a pure-stdio Unix binary.

**[Try it in your browser](https://jvanderberg.github.io/PicoMiteAllVersions/)**

## Origins

MMBasic was written by Geoff Graham. He created it for the Maximite in 2011, a small board that ran BASIC. Peter Mather ported MMBasic to the Raspberry Pi Pico starting in 2016, and that port, [PicoMite](https://github.com/UKTailwind/PicoMiteAllVersions), is what this fork is based on.

Between them, they wrote the BASIC language, the interpreter, the VGA and HDMI display drivers, the audio engine, the SD card support, every graphics and audio command, the low-level hardware drivers, the user manuals, and the bundled demo programs. If you're running a BASIC feature, it almost certainly came from their work. The [PicoMite User Manual](https://geoffg.net/Documents/PicoMite/PicoMite_User_Manual.pdf) is the reference.

## What this fork adds

The BASIC language is unchanged. What's added is mostly under the hood:

- A bytecode compiler and VM. BASIC source is compiled to a compact instruction stream that runs faster than the interpreter, especially on tight numeric loops (roughly 10x). Mixed workloads see smaller wins.
- A clean separation between the BASIC core and the hardware. The core source files compile the same way for every target; what changes between targets is which driver directories link in, not which compile-time flags are set.
- Builds for places PicoMite doesn't normally run: native macOS, WebAssembly in a browser, and a pure-stdio command-line tool. The browser build is what you get when you click the link at the top.
- A test suite that runs 240 BASIC programs through both the original interpreter and the new VM, comparing their output on every commit. The 14 device firmware images are also rebuilt on every commit, with size checks that fail the build if RAM use creeps up.

This isn't a perfect port of every upstream feature. Some of the newer commands are tracked, others aren't. Upstream is treated as a reference, not as something to merge from directly.

## Available builds

### RP2040 (Pico, Pico W)

| Target | Display | Keyboard | WiFi | Notes |
|---|---|---|---|---|
| `PICO` | optional SPI LCD | USB CDC console | no | PicoCalc shell linked by default |
| `PICOUSB` | optional SPI LCD | USB host keyboard | no | |
| `VGA` | VGA via PIO | USB CDC console | no | |
| `VGAUSB` | VGA via PIO | USB host keyboard | no | |
| `WEB` | optional SPI LCD | USB CDC console | yes | Pico W; PicoCalc shell linked by default |

### RP2350 (Pico 2, Pico 2 W)

| Target | Display | Keyboard | WiFi | Notes |
|---|---|---|---|---|
| `PICORP2350` | optional SPI LCD | USB CDC console | no | PicoCalc shell linked by default |
| `PICOUSBRP2350` | optional SPI LCD | USB host keyboard | no | |
| `VGARP2350` | VGA via PIO | USB CDC console | no | |
| `VGAUSBRP2350` | VGA via PIO | USB host keyboard | no | |
| `HDMI` | HDMI via HSTX | USB CDC console | no | |
| `HDMIUSB` | HDMI via HSTX | USB host keyboard | no | |
| `WEBRP2350` | optional SPI LCD | USB CDC console | yes | Pico 2 W; PicoCalc shell linked by default |
| `VGAWIFIRP2350` | VGA via PIO | USB CDC console | yes | Pico 2 W |
| `DVIWIFIRP2350` | HDMI via HSTX | USB CDC console | yes | Pico 2 W |

After `./buildall.sh`, the `.uf2` for each target lands in `build_all/<TARGET>/PicoMite.uf2`. The `.elf` is alongside it (used by SWD flashing).

## Flashing

Three ways to write a firmware image to a Pico.

### USB BOOTSEL (no extra tools)

Hold the BOOTSEL button while plugging the Pico into USB. The board mounts as a removable drive (`RPI-RP2` for RP2040, `RP2350` for RP2350). Copy the `.uf2` file onto the drive. The Pico reboots into the new firmware automatically.

### picotool (USB)

```
picotool load build_all/PICORP2350/PicoMite.uf2 -f
picotool reboot
```

The `-f` flag puts a running Pico into BOOTSEL mode automatically.

### probe-rs (SWD with a debug probe)

Requires a Raspberry Pi Debug Probe, picoprobe, or compatible CMSIS-DAP.

```
probe-rs download --chip RP235x build_all/PICORP2350/PicoMite.elf
probe-rs reset --chip RP235x
```

For RP2040 targets use `--chip RP2040`.

## Building from source

### macOS / Linux native

```
cd host && ./build.sh
./run_tests.sh
```

Produces `host/mmbasic_test`. Default mode compares the interpreter and VM outputs across the test corpus.

### Browser (WebAssembly)

```
cd ports/host_wasm && ./build.sh
cd ../../host/web && python3 serve.py
```

Open http://localhost:8000.

### Pico hardware

Install the [Pico SDK 2.1.1](https://github.com/raspberrypi/pico-sdk) and export `PICO_SDK_PATH`. Then:

```
./buildall.sh
```

Single-target build:

```
mkdir build && cd build && cmake -DCOMPILE=PICORP2350 .. && make
```

## PicoCalc

Builds for the [ClockworkPi PicoCalc](https://www.clockworkpi.com/picocalc) shell are automatic on the `PICO`, `WEB`, `PICORP2350`, and `WEBRP2350` targets. The I2C keypad, SPI LCD, and PicoCalc options link in.

| Command | Description |
|---|---|
| `MM.INFO(BATTERY)` | Battery percentage (0 to 100) |
| `MM.INFO(CHARGING)` | 1 if charging, else 0 |
| `OPTION BACKLIGHT KB <brightness>` | Keyboard backlight (0 to 255) |

If the PicoCalc keyboard firmware is older than 1.4, follow the [official guide](https://github.com/clockworkpi/PicoCalc/wiki/Setting-Up-Arduino-Development-for-PicoCalc-keyboard) to update before flashing. The `MM.INFO` commands above won't work on older firmware.

## Documentation

- [docs/real-hal-plan.md](docs/real-hal-plan.md): the hardware abstraction refactor.
- [docs/upstream-catchup-plan.md](docs/upstream-catchup-plan.md): how upstream features are ported in.
- [docs/vm-architecture.md](docs/vm-architecture.md): bytecode VM design notes.
- [docs/adding-a-new-port.md](docs/adding-a-new-port.md): how to add a new device or simulation port.

For the BASIC language itself, see Geoff Graham's MMBasic manuals and Peter Mather's PicoMite User Manual.

## License and copyright

MMBasic is licensed under the terms in [Version.h](Version.h). The license requires that the name MMBasic be used when referring to the interpreter and that the original copyright lines be displayed at startup. Both conditions are honored on every target's boot banner.

```
Copyright 2011-2026 Geoff Graham
Copyright 2016-2026 Peter Mather
MMBasic Anywhere - Copyright 2025-2026 Josh Vanderberg
```

Upstream PicoMite (Peter Mather): https://github.com/UKTailwind/PicoMiteAllVersions  
Original MMBasic (Geoff Graham): https://geoffg.net/maximite.html
