# Root Directory Cleanup Plan

## Goal

Reduce the repository root to project entry points and top-level build metadata.
Move source, demos, documentation, generated outputs, and hardware collateral into
purpose-specific directories without changing behavior.

This is a staged cleanup plan. Each stage should be reviewed independently and
verified before moving to the next one.

## Current Shape

The root currently mixes:

- active C and header sources
- MMBasic demo programs
- web demo pages
- planning/reference documentation
- build scripts and generated build output
- Pico SDK build metadata
- PIO and linker-script inputs
- hardware manufacturing collateral
- local/generated artifacts

The biggest source of visual noise is the active `.c`/`.h` code, but those moves
have the largest blast radius because many includes and CMake source lists assume
root-relative paths.

## Ground Rules

- Do not call active MMBasic code `legacy`.
- Use `core/mmbasic/` for active MMBasic interpreter, command, editor, and REPL
  code.
- Prefer small, reviewable moves by file type or subsystem.
- After every source-moving stage, update build metadata in the same commit.
- Preserve root compatibility only where users or scripts plausibly depend on it.
- Do not move generated build output into tracked directories.
- Keep generated artifacts ignored by `.gitignore`.

## Root Files To Keep

These should remain in root unless there is a specific reason to change them:

- `.gitignore`
- `CMakeLists.txt`
- `README.md`
- `README-local.md`
- `CLAUDE.md`
- `Doxyfile`
- `pico_sdk_import.cmake` until the CMake re-layout stage

Potential later cleanup:

- Move `Doxyfile` to `docs/` only if the documentation generation command is
  updated and documented.
- Move `pico_sdk_import.cmake` to `cmake/` only after updating
  `CMakeLists.txt`.

## Stage 1: Generated And Local Artifacts

Status: partially done.

Delete ignored build/output artifacts from root:

- `build/`
- `build2350/`
- `build2350_vm_only/`
- `build_a0_vgawifi/`
- `build_all/`
- `build_dvi_wifi/`
- `build_lfs/`
- `build_picocalc/`
- `build_rp2040/`
- `build_test/`
- `build_web2350_picocalc/`
- `backup_firmware.uf2`

Remaining generated/local artifacts to remove:

- `.DS_Store`
- `docs/.DS_Store`
- `hardcopy.0`
- `out.bmp`

Keep `.gitignore` entries covering:

- `build*/`
- `*.uf2`
- `.DS_Store`
- `hardcopy.*`
- `out.bmp`

Verification:

- `find . -maxdepth 1 -type d -name 'build*' -print`
- `find . -name '*.uf2' -print`
- `git status --short --ignored -- 'build*' '*.uf2'`

## Stage 2: Documentation Files

Move root planning/reference docs into `docs/`.

Targets:

- `BYTECODE_VM_PLAN.md` -> `docs/bytecode-vm-plan.md`
- `FASTGFX_PLAN.md` -> `docs/fastgfx-plan.md`
- `FLASH_LAYOUT_NOTE.md` -> `docs/flash-layout-note.md`
- `PicoMite_readme.txt` -> `docs/reference/PicoMite_readme.txt`

Handle duplicate manual:

- Root `PicoMite_User_Manual.pdf` appears duplicated by
  `docs/reference/PicoMite_User_Manual.pdf`.
- Confirm checksums.
- If identical, remove the root copy.
- If different, keep the newer or more complete copy under `docs/reference/` and
  record the provenance in `docs/reference/README.md`.

Updates:

- Update any links in `README.md`, docs, scripts, or release notes that point to
  root-level document names.

Verification:

- `rg 'BYTECODE_VM_PLAN|FASTGFX_PLAN|FLASH_LAYOUT_NOTE|PicoMite_readme|PicoMite_User_Manual' .`
- `git status --short`

## Stage 3: BASIC Demo Programs

Move root `.bas` demos into `demos/`, grouped by use.

Suggested layout:

- `demos/graphics/`
- `demos/sound/`
- `demos/bench/`
- `demos/apps/`

Candidate moves:

- `demo_draw_*.bas` -> `demos/graphics/`
- `demo_gfx_*.bas` -> `demos/graphics/`
- `demo_sound_*.bas` -> `demos/sound/`
- `demo_melody.bas` -> `demos/sound/`
- `mand.bas` -> `demos/bench/`
- `sieve.bas` -> `demos/bench/`
- `sieveasm.bas` -> `demos/bench/`
- `pico_blocks.bas` -> `demos/apps/`

Required updates:

- `tools/build_lfs.py`
- `ports/pc386/build_disks.sh`
- docs that mention root demo paths
- any host/wasm preload list if it intentionally consumes root demos

Compatibility option:

- If SD-card demo names must remain flat, update packaging scripts to flatten
  files while reading them from grouped source directories.

Verification:

- `rg 'demo_|mand.bas|sieve.bas|sieveasm.bas|pico_blocks.bas' tools ports docs host`
- Run the LFS packaging path used by the project.
- Run host tests that depend on demo paths.

## Stage 4: Web Demo Pages

Consolidate web demo pages under `demos/`.

Root candidates:

- `about.htm`
- `files.htm`
- `gpio.htm`
- `index.htm`
- `status.htm`

There are already matching files in `demos/`. Confirm whether root copies are
duplicates.

Plan:

- Compare root files against `demos/*.htm`.
- If identical, delete root copies.
- If root copies differ, merge intentionally into `demos/`.
- Update packaging scripts to use `demos/`.

Verification:

- `cmp about.htm demos/about.htm`, repeated for each page before deletion
- `rg 'about.htm|files.htm|gpio.htm|index.htm|status.htm' .`

## Stage 5: Scripts

Move non-entry-point shell helpers under `tools/` or `tools/build/`.

Likely root entry points to keep for now:

- `buildall.sh`
- `build_firmware.sh`
- `buildesp32.sh`

Likely moves:

- `bisect_tilemap.sh` -> `tools/bisect_tilemap.sh`
- `flash.sh` -> `tools/flash.sh`
- `validate_all.sh` -> `tools/validate_all.sh`

Compatibility option:

- Leave thin root wrappers for commands that users commonly run directly.
- Or document new command paths in `README-local.md`.

Verification:

- `rg 'bisect_tilemap.sh|flash.sh|validate_all.sh|buildall.sh|build_firmware.sh|buildesp32.sh' .`

## Stage 6: Build Metadata And Low-Level Inputs

Move CMake support files, linker scripts, and PIO files after script/demo cleanup.

Targets:

- `pico_sdk_import.cmake` -> `cmake/pico_sdk_import.cmake`
- `memmap_default_rp2040.ld` -> `cmake/linker/memmap_default_rp2040.ld`
- `memmap_default_rp2350.ld` -> `cmake/linker/memmap_default_rp2350.ld`
- `PicoMiteI2S.pio` -> `drivers/pio/PicoMiteI2S.pio`
- `PicoMiteVGA.pio` -> `drivers/pio/PicoMiteVGA.pio`

Required updates:

- `CMakeLists.txt`
- `ports/pico/port_sources.cmake`
- `ports/pico_rp2350/port_sources.cmake`
- `ports/web_rp2350/port_sources.cmake`
- `ports/vga/port_sources.cmake`
- `ports/vga_wifi_rp2350/port_sources.cmake`
- any includes or generated-header assumptions around `PicoMite*.pio.h`

Verification:

- Configure/build at least one RP2040 and one RP2350 target.
- `rg 'pico_sdk_import|memmap_default|PicoMiteI2S|PicoMiteVGA' .`

## Stage 7: Third-Party C Libraries

Move bundled third-party libraries out of root before moving project-owned core
code. This gives an immediate root reduction with relatively clear ownership.

Suggested layout:

- `third_party/fatfs/`
- `third_party/littlefs/`
- `third_party/cjson/`
- `third_party/miniaudio_codecs/` or `third_party/dr_libs/`
- `third_party/picojpeg/`
- `third_party/upng/`
- `third_party/hxcmod/`
- `third_party/aes/`
- `third_party/regex/`

Candidate files:

- `ff.c`, `ff.h`, `ffconf.h`, `ffsystem.c`, `ffunicode.c`, `diskio.h`
- `lfs.c`, `lfs.h`, `lfs_util.c`, `lfs_util.h`
- `cJSON.c`, `cJSON.h`
- `dr_flac.h`, `dr_mp3.h`, `dr_wav.h`
- `picojpeg.c`, `picojpeg.h`
- `upng.c`, `upng.h`
- `hxcmod.c`, `hxcmod.h`
- `aes.c`, `aes.h`
- `regex.c`, `regex.h`, `re.c`, `re.h`, `xregex.h`, `xregex2.h`

Required updates:

- `CMakeLists.txt`
- include paths
- source-specific compile flags currently set by filename
- any host build files that compile the same libraries

Verification:

- Configure/build device target.
- Run host tests if host build consumes moved files.
- `rg '#include \"(ff|lfs|cJSON|dr_|picojpeg|upng|hxcmod|aes|regex|re)\\.' .`

## Stage 8: VM And Bytecode Source

Move VM/compiler files into an active VM directory.

Suggested target:

- `runtime/vm/`

Candidate files:

- `bc_alloc.c`, `bc_alloc.h`
- `bc_bridge.c`
- `bc_compiler_core.c`
- `bc_compiler_internal.h`
- `bc_debug.c`
- `bc_run_diag.h`
- `bc_runtime.c`
- `bc_source.c`, `bc_source.h`
- `bc_vm.c`
- `bytecode.h`
- `vm_core.h`
- `vm_device_*.c`, `vm_device_*.h`
- `vm_host_fat.c`, `vm_host_fat.h`
- `vm_sys_*.c`, `vm_sys_*.h`

Open question:

- Decide whether `vm_sys_*` belongs under `runtime/vm/` or under
  `runtime/sys/` if those files are intentionally system-call adapters rather
  than VM internals.

Required updates:

- `CMakeLists.txt`
- host build files
- docs that mention VM source paths

Verification:

- Host VM tests.
- Device compile.
- `rg 'bc_|vm_' CMakeLists.txt host ports docs`

## Stage 9: Shared Graphics Source

Move reusable graphics helpers into `shared/gfx/`.

Candidate files:

- `gfx_box_shared.c`, `gfx_box_shared.h`
- `gfx_circle_shared.c`, `gfx_circle_shared.h`
- `gfx_cls_shared.c`, `gfx_cls_shared.h`
- `gfx_console_shared.c`, `gfx_console_shared.h`
- `gfx_line_shared.c`, `gfx_line_shared.h`
- `gfx_pixel_shared.c`, `gfx_pixel_shared.h`
- `gfx_text_shared.c`, `gfx_text_shared.h`
- `RGB121.c`
- possibly `shared/gfx/Tilemap.c` depending on ownership

Required updates:

- `CMakeLists.txt`
- host/wasm builds
- includes from display and host framebuffer code

Verification:

- Host framebuffer/graphics tests.
- At least one device build.

## Stage 10: Active MMBasic Core

Move active interpreter and command code into `core/mmbasic/`.

Candidate files:

- `core/mmbasic/MMBasic.c`, `core/mmbasic/MMBasic.h`
- `core/mmbasic/MMBasic_Includes.h`
- `core/mmbasic/MMBasic_Print.c`
- `core/mmbasic/MMBasic_Prompt.c`
- `core/mmbasic/MMBasic_REPL.c`
- `core/mmbasic/Commands.c`, `core/mmbasic/Commands.h`
- `core/mmbasic/Functions.c`, `core/mmbasic/Functions.h`
- `core/mmbasic/Operators.c`, `core/mmbasic/Operators.h`
- `core/mmbasic/Custom.c`, `core/mmbasic/Custom.h`
- `core/mmbasic/FileIO.c`, `core/mmbasic/FileIO.h`
- `core/mmbasic/Draw.c`, `core/mmbasic/Draw.h`
- `core/mmbasic/Editor.c`, `core/mmbasic/Editor.h`
- `core/mmbasic/External.c`, `core/mmbasic/External.h`
- `core/mmbasic/MATHS.c`, `core/mmbasic/MATHS.h`
- `core/mmbasic/Memory.c`, `core/mmbasic/Memory.h`
- `core/mmbasic/MM_Misc.c`, `core/mmbasic/MM_Misc.h`
- `core/mmbasic/OptionCommands.c`, `core/mmbasic/OptionCommands.h`
- `core/mmbasic/CFunction.c`
- `core/mmbasic/XModem.c`, `core/mmbasic/XModem.h`
- `core/mmbasic/AllCommands.h`
- `core/mmbasic/PicoCFunctions.h`
- `core/mmbasic/Version.h`
- `core/mmbasic/configuration.h`

Required updates:

- `CMakeLists.txt`
- host build files
- include paths
- docs that reference root source paths

Risk:

- This is the largest include-path and build-list change. Do it after the
  lower-risk moves have established the pattern.

Verification:

- Full host test suite.
- At least one RP2040 device build.
- At least one RP2350 device build.

## Stage 11: Device Drivers And Hardware-Facing Code

Move remaining hardware-facing root sources into `drivers/` or port-specific
directories.

Candidate groups:

- storage/display/input/audio/network drivers
- `shared/audio/Audio.c`, `Audio.h`
- `drivers/bmp_decoder/BmpDecoder.c`
- `drivers/gps/GPS.c`, `GPS.h`
- `drivers/gui_controls/GUI.c`, `GUI.h`
- `drivers/i2c_bus/I2C.c`, `I2C.h`
- `drivers/onewire/Onewire.c`, `Onewire.h`
- `drivers/spi_bus/SPI.c`, `SPI.h`, `SPI-111.c`, `SPI-LCD.h`
- `drivers/ssd1963/SSD1963.c`, `SSD1963.h`
- `drivers/serial/Serial.c`, `Serial.h`
- `drivers/gui_touch/Touch.c`, `Touch.h`
- `goodix.c`
- `drivers/ps2_mouse/mouse.c`
- `shared/net/mqtt.c`
- `shared/net/MMMqtt.c`, `shared/net/MMTCPclient.c`, `shared/net/MMntp.c`, `shared/net/MMsetwifi.c`,
  `shared/net/MMtcpserver.c`, `shared/net/MMtelnet.c`, `shared/net/MMtftp.c`, `shared/net/MMudp.c`, `shared/net/MMweb_stubs.c`
- `drivers/psram_heap/psram.c`, `psram.h`
- `shared/cmd_ws2812_shared.c`
- `shared/mmbasic/mm_misc_shared.c`

Open question:

- Network command files may belong under `shared/net/` rather than `drivers/`
  if they are command-layer glue over multiple network backends.

Verification:

- Device builds for ports with display, audio, storage, and network coverage.
- Any available smoke tests for SD, network, keyboard, and display.

## Stage 12: Fonts And Embedded Header Assets

Move font and asset headers into an asset-specific directory.

Suggested target:

- `assets/fonts/`

Candidate target files:

- `assets/fonts/ArialNumFontPlus.h`
- `assets/fonts/Fnt_10x16.h`
- `assets/fonts/Font_8x6.h`
- `assets/fonts/Hom_16x24_LE.h`
- `assets/fonts/Inconsola.h`
- `assets/fonts/Misc_12x20_LE.h`
- `assets/fonts/X_8x13_LE.h`
- `assets/fonts/arial_bold.h`
- `assets/fonts/font-8x10.h`
- `assets/fonts/font1.h`
- `assets/fonts/smallfont.h`
- `assets/fonts/tinyfont.h`

Required updates:

- include paths
- any font registration tables
- docs if font file paths are mentioned

Verification:

- Build display-capable targets.
- Run or inspect font-related host/display tests if available.

## Stage 13: Hardware Collateral

Status: done.

Move hardware design/manufacturing files out of root.

Target:

- `hardware/pico-computer/`

Moved directory:

- `hardware/pico-computer/`

Notes:

- The current directory name contains a space and nested repeated name. Normalize
  only if references can be updated cleanly.
- Keep schematic, BOM, pick-and-place, Gerber, DXF, and project files together.

Verification:

- Run the Stage 13 cleanup search for the old root path and hardware file
  identifiers.

## Final Root Target

After all stages, root should contain only:

- repository metadata directories: `.git/`, `.github/`
- project guidance/config: `.gitignore`, `README.md`, `README-local.md`,
  `CLAUDE.md`
- top-level build/docs entry points: `CMakeLists.txt`, `Doxyfile` if retained
- temporary root build entry scripts retained by Stage 5: `buildall.sh`,
  `build_firmware.sh`, `buildesp32.sh`
- major source directories: `assets/`, `boards/`, `cmake/`, `core/`, `demos/`,
  `docs/`, `drivers/`, `examples/`, `hal/`, `hardware/`, `host/`, `picocalc/`,
  `ports/`, `porttools/`, `runtime/`, `shared/`, `third_party/`, `toolchain/`,
  `tools/`, `usb_host_files/`, `web/`

## Verification Matrix

Minimum checks after each source-moving stage:

- `git status --short`
- `rg` for old paths
- CMake configure for affected targets
- build one representative RP2040 target
- build one representative RP2350 target
- run host tests when host files or shared code moved

Full cleanup exit gate:

- root contains no generated artifacts
- root contains no demo `.bas` or `.htm` files
- root contains no third-party library sources
- root contains no active `.c`/`.h` implementation files except intentionally
  retained top-level compatibility headers, if any
- all known build and packaging scripts pass with updated paths
