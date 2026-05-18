# Modularity and Relevance Cleanup Plan

## Audit Summary

The repository currently has 218 tracked files at the root: 82 `.c` files,
90 `.h` files, plus demos, web assets, scripts, docs, fonts, PIO/linker
files, and vendored libraries.

The main blocker to reorganizing the tree is that the build system still
assumes many files live at the root. `CMakeLists.txt`, host Makefiles, ESP32,
PC386, WASM, and CI all hard-code root source paths. The umbrella headers
`MMBasic_Includes.h` and `Hardware_Includes.h` are also used across nearly
every module, so moving files should happen with compatibility include paths
in place first.

There is also a relevance mismatch. `README.md` presents this as a broad
multi-port project, while `build_firmware.sh` is PicoCalc-focused and only
builds the RP2040/RP2350 PicoCalc targets. Before deleting ports or drivers,
decide whether this repo is:

- PicoCalc firmware plus host tests.
- MMBasic Anywhere, a broad multi-port platform.

That decision controls what is irrelevant.

## Recommended Target Layout

- `src/mmbasic/`: interpreter/runtime language files such as `MMBasic*`,
  `Commands`, `Functions`, `Operators`, `Custom`, `MATHS`, `Memory`, and
  `Editor`.
- `src/runtime/`: `PicoMite.c`, `External`, `FileIO`, `MM_Misc`,
  `OptionCommands`, `CFunction`, and `XModem`.
- `src/vm/`: all `bc_*`, `bytecode.h`, and `vm_*` files.
- `src/graphics/`: `Draw`, `RGB121`, `Tilemap`, and `gfx_*`.
- `src/device/`: `Audio`, `I2C`, `SPI`, `Serial`, `Onewire`, `GPS`, and
  command-facing device glue.
- `third_party/`: FatFs, LittleFS, picojpeg, regex, `dr_*`, hxcmod, upng,
  AES, cJSON, and MQTT.
- `assets/fonts/`: root font headers.
- `demos/`: BASIC demos grouped by use.
- `demos/web/` or `assets/web/`: root `.htm` demo files.
- root/tool scripts: root entry points `buildall.sh`, `build_firmware.sh`,
  and `buildesp32.sh`; helper scripts such as `tools/validate_all.sh` and
  `tools/flash.sh`.
- `hardware/pico-computer/`: board manufacturing files.
- `docs/archive/`: stale plans and upstream notes.

## Removal Plan

1. Define the supported matrix.

   Keep by default:

   - `PICO`
   - `PICORP2350`
   - `ports/pico`
   - `ports/pico_rp2350`
   - `ports/pico_sdk_common`
   - `ports/host_native`
   - `ports/host_wasm`, if GitHub Pages remains supported

   Reconfirm before keeping:

   - `WEB`
   - `WEBRP2350`
   - VGA variants
   - HDMI variants
   - DVI WiFi variants
   - ESP32-S3
   - PC386
   - ANSI
   - stdio

2. Delete or archive obvious root clutter first.

   Initial candidates:

   - `SPI-111.c` appears unreferenced.
   - `goodix.c` is not built.
   - The PicoMite user manual lives at
     `docs/reference/PicoMite_User_Manual.pdf`.
   - Root HTML and BASIC demo files should move under demos/examples.
   - Root plan docs should move to `docs/archive/` or be deleted if
     superseded.

3. Refactor build metadata before moving code.

   Add shared CMake variables such as:

   - `PICOMITE_SRC_DIR`
   - `PICOMITE_VM_DIR`
   - `PICOMITE_THIRD_PARTY_DIR`

   For Makefile ports, add a shared source-list include such as
   `make/source-groups.mk` so host, WASM, ANSI, and PC386 stop duplicating
   root file lists.

4. Move lowest-risk files first.

   Start with demos, docs, manuals, web assets, scripts, fonts, PIO files,
   and linker scripts. Update `Doxyfile`, `tools/build_lfs.py`, workflows,
   and README links.

5. Move vendored third-party code.

   Move FatFs, LittleFS, picojpeg, regex, `dr_*` audio headers, hxcmod, upng,
   cJSON, and MQTT. Update source paths and include directories, but keep
   existing include style such as `#include "ff.h"` initially.

6. Move VM and shared graphics.

   Move `bc_*`, `vm_*`, and `gfx_*` groups. These files are relatively
   coherent and already identifiable in source lists.

7. Move legacy interpreter/runtime last.

   Move `MMBasic*`, `Commands`, `Functions`, `Hardware_Includes.h`, and
   adjacent legacy core files only after the lower-risk groups are stable.
   Keep compatibility include paths at first. Replace umbrella includes with
   narrower module headers later as a separate refactor.

8. Prune unsupported ports and drivers.

   After the supported matrix is final, remove entire dead port directories
   and their exclusive drivers. If the repo becomes PicoCalc-only, likely
   candidates include PC386, ESP32-S3, VGA/HDMI/DVI/WiFi display variants,
   and drivers only used by those ports.

## Verification Gates

After each phase, run the smallest relevant gate first, then broaden:

```sh
./host/build.sh
./host/run_tests.sh
./build_firmware.sh rp2040
./build_firmware.sh rp2350
```

Run these when the affected scope requires them:

```sh
./buildall.sh
cd ports/host_wasm && ./build.sh
make -C ports/pc386
```

`./buildall.sh` is only required while keeping the full multi-port target
matrix. The WASM and PC386 gates are only required if those ports remain
supported.

## Implementation Strategy

Do this as several small `git mv` commits, not one large restructure. The
first implementation commit should introduce build-system source grouping
without moving files. That creates a stable source map before paths start
changing.

Suggested commit sequence:

1. Add shared source groups for CMake and Makefile builds.
2. Move docs, manuals, scripts, examples, and web assets.
3. Move third-party libraries.
4. Move VM and shared graphics.
5. Move interpreter/runtime source groups.
6. Remove or archive unsupported ports and drivers.
7. Tighten include paths and replace umbrella includes where practical.
