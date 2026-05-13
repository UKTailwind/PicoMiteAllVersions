# Runtime Contract Inventory

Phase 0 inventory for the common runtime spine migration. This document records
the interpreter-facing symbols currently supplied by the runtime shell files
named in `docs/common-runtime-spine-plan.md`; it is descriptive only and does
not move code.

Classification terms:

- **common runtime**: behavior or state the MMBasic interpreter expects as part
  of its lifecycle, console, program-memory, interrupt, abort, or reset model.
- **runtime adapter operation**: a port-specific operation a future common
  runtime should call through an adapter hook.
- **port state**: backing storage required by interpreter headers or shared
  modules, but whose value is currently owned by a port.
- **driver state**: hardware or driver backing storage and compatibility entry
  points that should stay below the runtime adapter.
- **obsolete shim**: compatibility stub or historical name kept only so shared
  code links.

## Cross-Port Runtime Symbols

These symbols are the current runtime contract surface. Multiple ports provide
local implementations.

| Symbol(s) | Classification | Current providers | Notes |
|---|---|---|---|
| `MMPrintString`, `MMputchar`, `putConsole`, `SSPrintString` | common runtime / port-local where needed | `runtime/runtime_console.c` for host-derived ports; `ports/pico_sdk_common/pico_console.c`, ESP32, and PC386 keep port-local bodies | Console output contract. Adapter should own byte write/display routing. |
| `MMInkey`, `MMgetchar`, `MMgetline`, `getConsole`, `kbhitConsole` | common runtime / port-local where needed | `runtime/runtime_console.c` for host-derived ports; `ports/pico_sdk_common/pico_console.c`, ESP32, and PC386 keep port-local bodies | Input, blocking key wait, file/console line input, and console queue access. |
| `CheckAbort`, `routinechecks` | common runtime | `ports/pico_sdk_common/pico_fault.c`, `host_runtime.c`, `esp32_runtime.c`, `pc386_runtime.c` | Abort/background-service poll points. Pico/ESP32 longjmp on abort; host checks timeout/network; PC386 is no-op. |
| `check_interrupt`, `cmd_ireturn` | common runtime | `host_runtime.c`, `esp32_runtime.c`; `pc386_peripheral_stubs.c` has a no-op `cmd_ireturn` | TCP/UDP/MQTT interrupt dispatch and interrupt return state. PC386 has no real interrupt support yet. |
| `SaveProgramToFlash`, `mmbasic_tokenise_source_to_progmem`, `mmbasic_save_loaded_source` | common runtime / port persistence | `runtime/runtime_program.c`; non-Pico `SaveProgramToFlash` wrappers call the common saver; Pico keeps CFunction/DefineFont extraction | Source-to-`ProgMemory` tokenization and LOAD/save behavior. The old `load_basic_source`/`load_source` symbols are retired. |
| `read_basic_source_file` | common runtime | `host_main.c`, `host_wasm_main.c` | Host/WASM file-to-source helper used by VM/file paths. |
| `MMfopen`, `MMfclose`, `MMgetline` | common runtime | `host_runtime.c`, `pc386_runtime.c`; Pico supplies `MMgetline` only in this file set | File dispatch handles expected by shared file/input code. |
| `cmd_files_save_program_context`, `cmd_files_restore_program_context`, `cmd_files_pump_console_key`, `cmd_load_post_cleanup` | common runtime | `host_runtime.c`, `pc386_runtime.c` | File command lifecycle hooks. `cmd_load_post_cleanup` longjmps after LOAD to avoid resuming with clobbered buffers. |
| `mmbasic_runtime_port_begin`, `host_runtime_configure`, `host_runtime_finish`, `host_runtime_timed_out` | runtime adapter operation | `host_runtime.c`; PC386 provides `mmbasic_runtime_port_begin` plus local finish/timed-out stubs | Current lifecycle surface. The old `host_runtime_begin` compatibility symbol is retired. |
| `main`, `app_main`, `kmain`, `wasm_boot` | runtime adapter operation | `ports/pico_sdk_common/pico_boot.c`, `host_main.c`, `host_wasm_main.c`, `mmbasic_stdio/main.c`, `ansi_main.c`, `app_main.c`, `kmain.c` | Port entry policies and boot sequencing. |
| `uSec`, `readusclock`, `__get_MSP` | runtime adapter operation | `ports/pico_sdk_common/pico_timer.c`, `host_runtime.c`, `esp32_compat.c`, `pc386_runtime.c`, `pc386_peripheral_stubs.c` | Delay/time/stack-overflow hooks used by shared interpreter code. |
| `CallCFunction`, `CallExecuteProgram` | runtime adapter operation | `host_runtime.c`, `esp32_runtime.c`, `pc386_runtime.c`; Pico references real device implementations outside this set | CFunction/editor execution trampoline surface. ESP32/host/PC386 are no-op shims. |
| `mmbasic_timegm`, `mmbasic_gmtime`, `timegm` | obsolete shim | `host_runtime.c`, `esp32_compat.c`, `pc386_runtime.c` | Time ABI compatibility around `host_platform.h` macro renames and libc differences. |

## Pico SDK Firmware Runtime Split

`PicoMite.c` now holds Pico global backing storage and declarations. Pico
runtime behavior lives in focused files under `ports/pico_sdk_common/`, wired
by the root `CMakeLists.txt`.

### Symbol Classification

| Symbol(s) | Classification | Notes |
|---|---|---|
| `main` | runtime adapter operation | Full Pico firmware boot shell and REPL entry in `pico_boot.c`. |
| `getConsole`, `putConsole`, `SerialConsolePutC`, `MMputchar`, `MMPrintString`, `SSPrintString`, `MMInkey`, `MMgetchar`, `MMgetline`, `kbhitConsole` | common runtime / port-local where needed | Device console contract in `pico_console.c` over USB CDC, UART, telnet, display console, and keyboard service. |
| `routinechecks`, `CheckAbort`, `timer_callback`, `mT4IntEnable` | common runtime / port-local where needed | Abort and fault handling in `pico_fault.c`; timer maintenance in `pico_timer.c`. `timer_callback` also drives driver timers. |
| `SaveProgramToFlash` | common runtime / port persistence | Pico implementation in `pico_program_flash.c` tokenizes source into flash, handles continuation lines, writes program bytes, extracts CSUB/CFunction/DefineFont payloads, updates CFunction area, and reports saved size. |
| `uSec`, `uSecFunc`, `uSecTimer`, `sigbus` | runtime adapter operation | Pico delay/timer behavior in `pico_timer.c`; fatal fault/reset path in `pico_fault.c`. |
| `updatebootcount`, `modclock`, `PIOExecute`, `PinReadFunc` | runtime adapter operation | Pico-specific boot bookkeeping and hardware helpers exposed to CFunction/driver paths; boot is in `pico_boot.c`, RP2040 `modclock()` is in `pico_clock.c`, and CFunction bridge helpers are in `pico_cfunction_bridge.c`. |
| `CallTable`, `IntToFloat`, `FMul`, `FAdd`, `FSub`, `FDiv`, `IDiv`, `FCmp`, `LoadFloat`, `CFuncRam`, `CFunc_delay_us` | common runtime | CFunction ABI table and helper storage. |
| `PSRAMsize`, `rp2350a`, `restart_reason`, `adc_clk_div`, `ticks_per_second`, `_excep_code`, `_persistent`, `_excep_peek`, `busfault` | port state | Boot/CPU/diagnostic state read by shared code. |
| `MMCharPos`, `MMAbort`, `BreakKey`, `WatchdogSet`, `ExitMMBasicFlag`, `EchoOption` equivalent state via globals, `ListCnt`, `lastchar` | common runtime | Prompt/console/abort state that should become runtime-owned. |
| `ConsoleRxBuf`, `ConsoleRxBufHead`, `ConsoleRxBufTail`, `ConsoleTxBuf`, `ConsoleTxBufHead`, `ConsoleTxBufTail` | common runtime | Console ring buffers. |
| `mSecTimer`, `PauseTimer`, `IntPauseTimer`, `Timer1`..`Timer5`, `InkeyTimer`, `WDTimer`, `ScrewUpTimer`, `SecondsTimer`, `diskchecktimer`, `clocktimer`, `MouseTimer`, `AHRSTimer`, `I2CTimer`, `GPSTimer`, `ds18b20Timer`, `KeyCheck`, `ClassicTimer`, `NunchuckTimer`, `day_of_week` | common runtime | Timer/timebase globals consumed by commands, functions, and drivers. |
| `PulsePin`, `PulseDirection`, `PulseCnt`, `PulseActive`, `InterruptUsed`, `calibrate` | common runtime | Runtime pulse/interrupt/control state. |
| `flash_option_contents`, `SavedVarsFlash`, `flash_target_contents`, `flash_progmemory`, `flash_libmemory`, `fs`, `lfs`, `lfs_dir`, `lfs_info` | runtime adapter operation | Pico flash/LittleFS/FatFS backing pointers and work areas. |
| `DISPLAY_TYPE`, `PromptFont`, `PromptFC`, `PromptBC`, `VCC`, `id_out`, `alive`, `banner` | port state | Display/identity/prompt state initialized by boot. |
| `transparent`, `transparents`, `RGBtransparent`, `MODE1SIZE`..`MODE5SIZE`, `QVGA_CLKDIV`, `I2SOff`, `processtick`, `SPIatRisk` | driver state | VGA/HDMI/I2S/display-driver state. |
| `IgnorePIN` | port state | Pin/runtime compatibility flag. |

## Native Host: `ports/host_native/host_runtime.c`

### Symbol Classification

| Symbol(s) | Classification | Notes |
|---|---|---|
| `mmbasic_runtime_port_begin`, `host_runtime_configure`, `host_runtime_finish`, `host_runtime_timed_out` | runtime adapter operation | Current host lifecycle API. `mmbasic_runtime_port_begin` initializes display-function pointers and snapshots options. |
| `CheckAbort`, `routinechecks`, `check_interrupt`, `cmd_ireturn` | common runtime | Service, timeout, network interrupt, and interrupt-return implementation. |
| `ClearExternalIO`, `SoftReset`, `uSec`, `__get_MSP`, `restorepanel`, `closeframebuffer`, `clear320`, `initMouse0` | runtime adapter operation | Port hooks required by shared interpreter modules; some are no-op host shims. |
| `MMInkey`, `MMgetchar`, `putConsole`, `MMputchar`, `MMPrintString`, `SSPrintString`, `MMgetline`, `getConsole`, `kbhitConsole`, `SerialConsolePutC`, `myprintf` | common runtime | Host console implementation, including raw terminal, scripted keys, sim/websocket input, stdout capture, telnet flush, and display-console routing. |
| `MMfopen`, `MMfclose` | common runtime | File dispatch handle stubs; real work is in shared `FileIO.c`. |
| `printoptions` | runtime adapter operation | Delegates to port web option printer. |
| `CallCFunction`, `CallExecuteProgram` | obsolete shim | No-op trampolines so shared code links. |
| `cmd_files_save_program_context`, `cmd_files_restore_program_context`, `cmd_files_pump_console_key`, `cmd_load_post_cleanup` | common runtime | File command lifecycle hooks; LOAD cleanup longjmps to prompt. |
| `port_mount_sd_drive`, `port_apply_load_overrides`, `port_drivecheck_remap`, `port_filesystem_prefix`, `port_drive_check` | runtime adapter operation | Filesystem adapter surface for host. |
| `port_picocalc_*`, `port_print_supported_boards`, `port_factory_reset_board`, `port_display_option_setter`, `port_print_display_options`, `port_print_lcd_spi`, `port_print_keyboard_heartbeat`, `port_print_usb_kb_repeat`, `port_clear_lcd_spi_if_shares_system`, `port_pinno_alias_for_name`, `port_pin_is_reserved_alias`, `port_pin_reserved_label`, `port_lcd320_option_setter`, `port_keyboard_option_setter`, `port_misc_option_setter`, `port_pico_pins_option_setter`, `port_heartbeat_option_setter`, `port_apply_default_console_colors`, `port_system_lcd_spi_option_setter`, `port_audio_i2s_pio_slice`, `port_mminfo_interrupts`, `port_mminfo_touch_status`, `port_mminfo_scroll_start`, `port_mminfo_screenbuff`, `port_pio_for_index`, `port_poke_display_panel`, `port_select_error_prompt_font`, `port_clear_runtime_display_reset`, `port_error_restore_console_surface`, `port_error_show_lcd_banner`, `port_try_find_subfun_hash`, `port_prepare_program_finalize_subfun`, `port_try_find_label_hash`, `port_try_check_var_subfun_collision`, `port_bc_bridge_clear_subfun_hash`, `port_bc_bridge_rehash_subfun`, `port_vm_time_get_tm` | runtime adapter operation | Port hook surface currently supplied as no-op/error host implementations. |
| `port_usb_count`, `port_usb_hid_field` | obsolete shim | Host has no USB host stack. |
| `mmbasic_timegm`, `mmbasic_gmtime` | obsolete shim | UTC-preserving libc wrappers. |
| `_excep_code`, `_persistent`, `_excep_peek`, `core1stack`, `InterruptReturn`, `InterruptUsed`, `MMCharPos`, `MMAbort`, `BreakKey`, `WatchdogSet`, `TickInt`, `TickTimer`, `TickPeriod`, `TickActive`, `mSecTimer`, `PauseTimer`, `IntPauseTimer`, `Timer1`..`Timer5`, `diskchecktimer`, `clocktimer`, `ds18b20Timer`, `I2CTimer`, `MouseTimer`, `SecondsTimer`, `day_of_week`, `PulsePin`, `PulseDirection`, `PulseCnt`, `PulseActive`, `ClickTimer`, `calibrate`, `InkeyTimer`, `ConsoleRxBuf`, `ConsoleRxBufHead`, `ConsoleRxBufTail`, `ConsoleTxBuf`, `ConsoleTxBufHead`, `ConsoleTxBufTail`, `ExitMMBasicFlag`, `OptionErrorCheck`, `EchoOption`, `ScrewUpTimer`, `WDTimer`, `ticks_per_second`, `SavedVarsFlash`, `flash_progmemory`, `host_repl_mode`, `host_sim_active` | common runtime | Runtime and prompt/global backing storage. Much of this should move to common state or runtime globals. |
| `FileTable`, `FSerror`, `lfs`, `lfs_dir`, `lfs_info`, `FatFSFileSystem` side effects, `realflashpointer`, `host_saved_vars_flash_buf`, `host_cfunction_flash_buf` | runtime adapter operation | Host storage/persistence compatibility state. |
| `PinDef`, `ExtCurrentConfig`, `PINMAP`, `PinFunction`, `IgnorePIN`, `CurrentCpuSpeed`, `PeripheralBusSpeed` | port state | Pin/board model backing. |
| `DISPLAY_TYPE`, `gui_bcolour`, `gui_fcolour`, `gui_font`, `PromptFont`, `PromptFC`, `PromptBC`, `DisplayHRes`, `DisplayVRes`, `ScreenSize`, `DisplayBuf`, `SecondLayer`, `SecondFrame`, `LCDAttrib`, `RGB121map`, `map16`, `tilefcols`, `tilebcols`, `QVGA_CLKDIV`, `X_TILE`, `Y_TILE`, `camera`, `buff320`, `screen320` | driver state | Display/framebuffer/tile/video compatibility storage. |
| `ADC_dma_chan`, `ADC_dma_chan2`, `ADCDualBuffering`, `last_adc`, `ADCopen`, `ADCmax`, `ADCInterrupt`, `ADCbuffer`, `adcint`, `adcint1`, `adcint2`, `ADCscale`, `ADCbottom`, `VCC`, `AUDIO_L_PIN`, `AUDIO_R_PIN`, `AUDIO_SLICE`, `AUDIO_WRAP`, `PWM0Apin`..`PWM7Bpin`, `UART*pin`, `SPI*pin`, `I2C*S*pin`, `slice0`..`slice7`, `SPI0locked`, `SPI1locked`, `I2C0locked`, `I2C1locked`, `BacklightSlice`, `BacklightChannel`, `CameraSlice`, `CameraChannel`, `SD_*_PIN`, `MOUSE_CLOCK`, `MOUSE_DATA`, `mouse0`, `PS2code`, `PS2int`, `mmI2Cvalue`, `mmOWvalue`, `GPSchannel`, `GPSTimer`, `Ir*`, `CFuncInt1`..`CFuncInt4`, `p100interrupts`, `CallBackEnabled`, `dma_hw`, `watchdog_hw` | driver state | Hardware-facing compatibility storage for modules that still build on host. |
| `optionangle`, `optionfastaudio`, `optionfulltime`, `optionlogging`, `useoptionangle`, `id_out` | port state | Option/identity compatibility fields. |

## Native Host: `ports/host_native/host_main.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `main` | runtime adapter operation | CLI policy for source compare, interpreter-only, VM-only, immediate, try-compile, REPL, and simulator modes. |
| `flash_prog_buf` | runtime adapter operation | RAM backing for `flash_progmemory`: first half program, second half erased CFunction tail. |
| `host_output_hook`, `host_capture_hook` | runtime adapter operation | Output capture contract used by tests and stdout routing. |
| `read_basic_source_file`, `mmbasic_tokenise_source_to_progmem` | common runtime | File read helper plus shared tokenizer for interpreter paths. |

Boot variants:

- **Batch compare/default**: parse CLI, allocate/initialize `flash_prog_buf`,
  set `flash_progmemory`, `InitBasic()`, set `bc_opt_level`, configure runtime
  timeout/screenshot/keys, optionally set `host_sd_root`, read source, tokenize
  for interpreter path, run interpreter and/or VM with capture.
- **Interpreter run**: `vm_host_fat_reset()`, `vm_sys_file_reset()`,
  `vm_sys_pin_reset()`, `ClearRuntime(true)`, clear error globals,
  `mmbasic_runtime_port_begin()`, `PrepareProgram(1)`, `setjmp(mark)`,
  `ExecuteProgram(ProgMemory)`, `host_runtime_finish()`.
- **VM source run**: same reset/clear/runtime-begin flow, then
  `bc_run_source_string(source, source_name)`.
- **Immediate**: reset file state, clear errors, `mmbasic_runtime_port_begin()`,
  `setjmp(mark)`, `bc_run_immediate(immediate_line)`, finish and print capture.
- **Try compile**: no runtime boot; calls `bc_try_compile_line()`.
- **REPL**: set `host_repl_mode`, reset VM/file/pin state, `ClearRuntime(true)`,
  clear errors, derive terminal size into `Option.Width/Height`,
  `mmbasic_runtime_port_begin()`, `MMBasic_PrintBanner()`, enter raw mode,
  `MMBasic_RunPromptLoop()`.
- **Simulator**: set `host_sd_root`, start sim tick, configure framebuffer
  size if requested, set display console options/font/palette, start web
  server, then reuse the REPL path.

## WASM Host: `ports/host_wasm/host_wasm_main.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `main` | runtime adapter operation | Empty Emscripten entry; JS calls `wasm_boot()`. |
| `wasm_boot`, `wasm_break`, `wasm_set_heap_size`, `wasm_set_slowdown_us` | runtime adapter operation | Browser/worker control API. `wasm_boot` starts the runtime pthread. |
| `flash_prog_buf`, `host_output_hook` | runtime adapter operation | WASM-owned replacements for host_main globals. |
| `read_basic_source_file`, `mmbasic_tokenise_source_to_progmem` | common runtime | MEMFS file reader plus shared tokenizer. |

Boot sequence:

1. JS calls `wasm_boot()`.
2. `wasm_boot()` creates and detaches `wasm_runtime_thread`.
3. Thread initializes `flash_prog_buf` to program-zero/erased-tail and sets
   `flash_progmemory`.
4. `InitBasic()`, set `bc_opt_level = 1`, configure host runtime and no keys.
5. Set `host_sd_root = "/sd"`, `host_repl_mode = 1`.
6. Reset host FAT/file/pin state, `ClearRuntime(true)`, clear errors.
7. `mmbasic_runtime_port_begin()`.
8. Configure screen-only display console, font, dimensions, and palette.
9. `host_options_snapshot()` after display overrides.
10. `MMBasic_PrintBanner()` and `MMBasic_RunPromptLoop()`.

## stdio Port: `ports/mmbasic_stdio/main.c`, `stdio_runtime.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `main` | runtime adapter operation | Pure-stdio batch entry; reads file or stdin and executes via interpreter. |
| `mmbasic_runtime_run_source` | common runtime | Batch source loading and execution through the shared runtime helper. |
| `host_output_hook` | runtime adapter operation | Points to stdout writer. |
| `host_raw_mode_is_active`, `host_read_byte_nonblock`, `host_read_byte_blocking_ms`, `host_push_back_byte` | runtime adapter operation | Minimal stdin adapter for host console code. |
| `host_framebuffer_*`, `host_runtime_get_pixel`, `host_fb_*`, `host_fb_width`, `host_fb_height`, `host_fastgfx_reset_state`, `bc_fastgfx_*`, `cmd_fastgfx`, `cmd_framebuffer`, `cmd_edit`, `cmd_editfile` | obsolete shim | Link/error stubs for unsupported framebuffer, fastgfx, and editor features. |
| `editactive`, `StartEditPoint`, `StartEditChar`, `flash_prog_buf` | port state | Editor compatibility globals and program-memory backing. |

Boot sequence:

1. Read `argv[1]` or stdin to a NUL-terminated source buffer.
2. Initialize `flash_prog_buf`: first `MAX_PROG_SIZE` zero, second
   `MAX_PROG_SIZE` erased (`0xff`); set `flash_progmemory`.
3. `LoadOptions()`, `InitBasic()`, `InitHeap(true)`.
4. Clear `MMerrno`/`MMErrMsg`; `mmbasic_runtime_port_begin()` via the runtime adapter.
5. `mmbasic_runtime_run_source()` tokenises, resets host FAT/file/pin state,
   clears runtime state, prepares, and executes `ProgMemory`.
9. Print errors to stderr and exit with status.

## ANSI Port: `ports/mmbasic_ansi/ansi_main.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `main` | runtime adapter operation | ANSI half-block terminal entry for REPL or script mode. |
| `flash_prog_buf`, `host_output_hook` | runtime adapter operation | Program-memory backing and stdout swallow hook. |
| `mmbasic_runtime_run_source` | common runtime | Script-mode source tokenizer and interpreter runner. |

Boot sequence:

1. Parse `--resolution`, `--interp`/`--vm`, optional program path.
2. Read terminal size and validate minimum dimensions.
3. `ansi_boot(width, height)`: initialize `flash_prog_buf`, set
   `flash_progmemory`, `LoadOptions()`, `InitBasic()`, `InitHeap(true)`,
   clear errors, configure framebuffer console/font/palette,
   `mmbasic_runtime_port_begin()`, print banner into framebuffer, install
   stdout-swallow hook, enter ANSI terminal, start render thread.
4. **Script mode**: read file, reset host FAT/file/pin state,
   `ClearRuntime(true)`, then either `mmbasic_runtime_run_source()` or
   `bc_run_source_string()`. Finish runtime.
5. **REPL mode**: set `host_sd_root` to cwd, set `host_repl_mode`, reset
   host FAT/file/pin state, `ClearRuntime(true)`, then
   `MMBasic_RunPromptLoop()`.
6. Stop ANSI display on exit.

## ESP32-S3 Metro

### `app_main.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `app_main` | runtime adapter operation | ESP-IDF entry and REPL boot policy. |

Boot sequence:

1. `esp32_console_init()`, reduce GPIO log level, print five attach-delay dots.
2. Set `flash_progmemory = flash_prog_buf`.
3. `esp32_flash_storage_load_options()`, `LoadOptions()`.
4. Validate `Option.Magic`, `Width`, `Height`, `Tab`, and
   `PROG_FLASH_SIZE`; on invalid options: `ResetOptions(true)`, apply
   terminal defaults, `SaveOptions()`.
5. Set default font dimensions, `ApplyDefaultConsoleColours()`,
   `esp32_flash_storage_init()`.
6. `InitBasic()`, `InitHeap(true)`, clear errors.
7. Reset ESP32 SD diskio, VM file state, and VM pin state.
8. `ClearRuntime(true)`.
9. Mount LittleFS with `esp32_lfs_mount()`.
10. `MMBasic_PrintBanner()` and `MMBasic_RunPromptLoop()`.

### `esp32_runtime.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `CheckAbort`, `routinechecks`, `check_interrupt`, `cmd_ireturn` | common runtime | ESP32 service/yield, Ctrl-C abort longjmp, TCP/UDP/MQTT interrupt dispatch. |
| `port_bc_runtime_free_source` | runtime adapter operation | Frees BC-owned source buffers. |
| `CallCFunction`, `CallExecuteProgram` | obsolete shim | No-op CFunction/editor trampolines. |
| `core1stack`, `InterruptReturn`, `InterruptUsed`, `ScrewUpTimer` | common runtime | Runtime backing state for stack sentinel, interrupts, and timeout compatibility. |

### `esp32_compat.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `timegm`, `mmbasic_timegm`, `mmbasic_gmtime` | obsolete shim | newlib/host-platform time compatibility. |
| `flash_prog_buf`, `mmbasic_save_loaded_source` | common runtime | RAM program-memory backing and shared source tokenizer/saver. |
| `cmd_framebuffer` | obsolete shim | Unsupported command error. |
| `readusclock`, `uSec`, `__get_MSP` | runtime adapter operation | ESP timer, delay, and stack-check hooks. |
| `dma_hw`, `watchdog_hw` | obsolete shim | Dummy register windows for shared Pico-oriented code paths. |
| `PSRAMsize`, `mmbasic_save_psram_settings`, `mmbasic_restore_psram_settings` | port state | ESP32 stdio scope reports no PSRAM and no-ops Pico PSRAM flash guards. |

### `esp32_flash_storage.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `esp32_flash_storage_init`, `esp32_flash_storage_load_options`, `esp32_options_snapshot` | runtime adapter operation | ESP partition setup and option mirror lifecycle. |
| `flash_range_erase`, `flash_range_program` | runtime adapter operation | Routes program-memory writes to RAM and slot writes to `mmslots` partition. |
| `SaveProgramToFlash` | common runtime | LOAD save path: tokenizes text into `ProgMemory`, then `PrepareProgram(0)`. |
| `ExistsFile`, `ExistsDir`, `hal_ff_findfirst`, `hal_ff_findnext`, `hal_ff_closedir`, `hal_ff_unlink`, `hal_ff_chdir`, `hal_ff_getcwd` | runtime adapter operation | File/directory adapter surface for LittleFS/FatFS. |
| `esp32_flash_option_buf`, `flash_target_contents`, `flash_option_contents`, `SavedVarsFlash`, `flash_progmemory` | port state | ESP32 option/slot/saved-variable/program-memory backing pointers. |

## PC386

### `kmain.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `kmain` | runtime adapter operation | Bare-metal kernel entry and REPL boot policy. |
| `pc386_multiboot_info` | port state | Captured multiboot info pointer. |

Boot sequence:

1. Initialize serial COM1 and VGA text console; print stage banner.
2. Initialize IDT, PIC, PS/2 keyboard IRQ1, and COM1 RX IRQ4.
3. Validate Multiboot1 magic and store `pc386_multiboot_info`.
4. Print memory map and heap-region summary.
5. Probe ATA-PIO drives and FDC drives.
6. Mount/list FAT volumes `A:`, `B:`, and `C:`.
7. `pc386_flash_init()`.
8. Establish `setjmp(mark)` init fault guard.
9. `LoadOptions()`, apply keyboard typematic from `Option.repeat`.
10. `InitBasic()`, `InitHeap(true)`, clear errors.
11. `mmbasic_runtime_port_begin()`.
12. `MMBasic_PrintBanner()` and `MMBasic_RunPromptLoop()`.

### `pc386_runtime.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `mmbasic_runtime_port_begin`, `host_runtime_finish`, `host_runtime_timed_out` | runtime adapter operation | PC386 lifecycle. |
| `pc386_apply_runtime_option_defaults`, `pc386_keyboard_repeat_start_ms`, `pc386_keyboard_repeat_rate_ms` | runtime adapter operation | PC386 option defaults and keyboard repeat policy. |
| `CheckAbort`, `routinechecks`, `check_interrupt` | common runtime | Currently no-op/no interrupt support. |
| `ClearExternalIO`, `SoftReset`, `uSec`, `__get_MSP`, `restorepanel`, `closeframebuffer`, `clear320`, `initMouse0`, `Display_Refresh`, `DisplayNotSet`, `ScrollLCDSPISCR` | runtime adapter operation | PC386 runtime/display compatibility hooks; several are no-op or panic placeholders. |
| `SerialConsolePutC`, `putConsole`, `MMputchar`, `MMPrintString`, `SSPrintString`, `MMInkey`, `MMgetchar`, `MMgetline`, `getConsole`, `kbhitConsole`, `myprintf` | common runtime | PC386 console over VGA mode 13h plus COM1 and PS/2/serial input. |
| `MMfopen`, `MMfclose`, `printoptions` | common runtime | File handle dispatch and PC386 option listing. |
| `port_repl_wifi_arch_init_and_connect`, `port_drivecheck_remap`, `port_filesystem_prefix`, `port_drive_check`, `port_mount_sd_drive`, `port_apply_load_overrides` | runtime adapter operation | REPL/filesystem/load adapter surface. |
| `cmd_files_save_program_context`, `cmd_files_restore_program_context`, `cmd_files_pump_console_key`, `cmd_load_post_cleanup` | common runtime | File lifecycle hooks; LOAD cleanup longjmps to prompt. |
| `CallCFunction`, `CallExecuteProgram` | obsolete shim | No-op CFunction/editor trampolines. |
| `port_*` option/display/pin/audio/info/error/hash/VM/time hooks | runtime adapter operation | PC386 port hook surface, mostly no-op or minimal implementations. |
| `mmbasic_timegm`, `mmbasic_gmtime` | obsolete shim | Time compatibility wrappers. |
| `host_output_hook` | runtime adapter operation | Capture hook compatibility with host console contract. |

### `pc386_state.c`

All public definitions in this file are port/global backing storage.

| Symbol(s) | Classification | Notes |
|---|---|---|
| `_excep_code`, `_persistent`, `_excep_peek`, `core1stack`, `InterruptReturn`, `InterruptUsed`, `MMCharPos`, `MMAbort`, `BreakKey`, `WatchdogSet`, `TickInt`, `TickTimer`, `TickPeriod`, `TickActive`, `mSecTimer`, `PauseTimer`, `IntPauseTimer`, `Timer1`..`Timer5`, `diskchecktimer`, `clocktimer`, `ds18b20Timer`, `I2CTimer`, `MouseTimer`, `SecondsTimer`, `day_of_week`, `PulsePin`, `PulseDirection`, `PulseCnt`, `PulseActive`, `ClickTimer`, `calibrate`, `InkeyTimer`, `ConsoleRxBuf`, `ConsoleRxBufHead`, `ConsoleRxBufTail`, `ConsoleTxBuf`, `ConsoleTxBufHead`, `ConsoleTxBufTail`, `ExitMMBasicFlag`, `OptionErrorCheck`, `EchoOption`, `ScrewUpTimer`, `WDTimer`, `ticks_per_second`, `SavedVarsFlash`, `pc386_cfunction_buf`, `flash_progmemory`, `TCPreceived`, `TCPreceiveInterrupt` | common runtime | Runtime and interrupt/timer/console backing storage. |
| `lfs`, `lfs_dir`, `lfs_info`, `pc386_saved_vars_buf` | runtime adapter operation | Storage/persistence compatibility state. |
| `PinDef`, `PINMAP`, `PinFunction`, `IgnorePIN`, `CurrentCpuSpeed`, `PeripheralBusSpeed` | port state | PC386 pin/board model. |
| `DISPLAY_TYPE`, `PromptFont`, `PromptFC`, `PromptBC`, `ScreenSize`, `DisplayBuf`, `SecondLayer`, `SecondFrame`, `LCDAttrib`, `map16`, `tilefcols`, `tilebcols`, `QVGA_CLKDIV`, `X_TILE`, `Y_TILE`, `buff320`, `screen320` | driver state | Display/tile/video compatibility storage. |
| `ADC_dma_chan`, `ADC_dma_chan2`, `ADCDualBuffering`, `last_adc`, `ADCopen`, `ADCmax`, `ADCInterrupt`, `ADCbuffer`, `adcint`, `adcint1`, `adcint2`, `ADCscale`, `ADCbottom`, `VCC`, `AUDIO_L_PIN`, `AUDIO_R_PIN`, `AUDIO_SLICE`, `AUDIO_WRAP`, `PWM0Apin`..`PWM7Bpin`, `UART*pin`, `SPI*pin`, `I2C*S*pin`, `slice0`..`slice7`, `SPI0locked`, `SPI1locked`, `I2C0locked`, `I2C1locked`, `BacklightSlice`, `BacklightChannel`, `CameraSlice`, `CameraChannel`, `SD_*_PIN`, `MOUSE_CLOCK`, `MOUSE_DATA`, `mouse0`, `PS2code`, `PS2int`, `mmI2Cvalue`, `mmOWvalue`, `GPSchannel`, `GPSTimer`, `Ir*`, `CFuncInt1`..`CFuncInt4`, `p100interrupts`, `CallBackEnabled`, `dma_hw`, `watchdog_hw` | driver state | Hardware compatibility storage. |
| `optionangle`, `optionfastaudio`, `optionfulltime`, `optionlogging`, `useoptionangle`, `id_out` | port state | Option/identity compatibility fields. |

### `pc386_peripheral_stubs.c`

| Symbol(s) | Classification | Notes |
|---|---|---|
| `cmd_fastgfx`, `bc_fastgfx_create`, `bc_fastgfx_close`, `bc_fastgfx_reset`, `bc_fastgfx_set_fps`, `bc_fastgfx_swap`, `bc_fastgfx_sync`, `host_runtime_get_pixel` | driver state | Real PC386 VGA mode 13h fast graphics/readback hooks. |
| `cmd_pin`, `cmd_setpin`, `fun_pin`, `codemap`, `codecheck`, `IsInvalidPin`, `ExtCfg`, `ExtSet`, `ExtInp`, `PinRead`, `GetPinBit`, `GetPinStatus`, `PinSetBit` | driver state | LPT/GPIO compatibility hooks over `vm_sys_pin`. |
| `cmd_option`, `cmd_sys`, `port_set_default_options`, `ExistsFile`, `ExistsDir`, `FileLoadCMM2Program`, `SaveProgramToFlash`, `readusclock`, `xchg_byte`, `vm_host_fat_mount`, `vm_host_fat_path`, `vm_host_fat_reset` | runtime adapter operation | PC386 options/system/files/load/time compatibility surface. |
| `cmd_framebuffer`, `cmd_pwm`, `cmd_Servo`, `GetIntAddress`, `GetPeekAddr`, `GetPokeAddr`, `xregcomp`, `xregexec`, `xregfree` | obsolete shim | Unsupported or deferred feature stubs that error/panic or return failure. |
| `cmd_adc`, `cmd_backlight`, `cmd_camera`, `cmd_cfunction`, `cmd_Classic`, `cmd_configure`, `cmd_cpu`, `cmd_csubinterrupt`, `cmd_device`, `cmd_DHT22`, `cmd_ds18b20`, `cmd_endprogram`, `cmd_i2c`, `cmd_i2c2`, `cmd_in`, `cmd_ir`, `cmd_ireturn`, `cmd_irq`, `cmd_irqclear`, `cmd_irqnowait`, `cmd_irqset`, `cmd_irqwait`, `cmd_jmp`, `cmd_keypad`, `cmd_label`, `cmd_lcd`, `cmd_library`, `cmd_mouse`, `cmd_mov`, `cmd_nop`, `cmd_Nunchuck`, `cmd_onewire`, `cmd_out`, `cmd_pio`, `cmd_PIOline`, `cmd_poke`, `cmd_port`, `cmd_program`, `cmd_pull`, `cmd_pulse`, `cmd_push`, `cmd_rtc`, `cmd_set`, `cmd_settick`, `cmd_spi`, `cmd_spi2`, `cmd_steppedstream`, `cmd_synth`, `cmd_temp`, `cmd_uart`, `cmd_watchdog`, `cmd_web`, `cmd_wii`, `cmd_ws2812`, `cmd_WS2812`, `cmd_sideset`, `cmd_sync`, `cmd_update`, `cmd_wait`, `cmd_wrap`, `cmd_wraptarget`, `cmd_xmodem` | obsolete shim | BASIC command stubs for unsupported or deferred peripherals. |
| `fun_adc`, `fun_classic`, `fun_cpuid`, `fun_device`, `fun_DHT22`, `fun_ds18b20`, `fun_keypad`, `fun_mmcmdline`, `fun_porta`, `fun_temp`, `fun_touch`, `fun_tpadlast`, `fun_wii`, `fun_ws2812`, `fun_dev`, `fun_distance`, `fun_GPS`, `fun_info`, `fun_peek`, `fun_pio`, `fun_port`, `fun_pulsin`, `fun_spi`, `fun_spi2` | obsolete shim | BASIC function stubs returning zero or empty string. |
| `blitmerge`, `cleanserver`, `close_tcpclient`, `close_udpclient`, `SerialOpen`, `SerialClose`, `SerialGetchar`, `SerialPutchar`, `SerialRxStatus`, `SerialTxStatus`, `ProcessWeb`, `port_web_clear_runtime_state`, `tcp_free_recv_buffers`, `tcp_realloc_recv_buffers`, `port_fun_mm_mqtt_copy`, `copyframetoscreen`, `disable_audio`, `disable_sd`, `disable_systemi2c`, `disable_systemspi`, `setframebuffer`, `setterminal`, `port_runtime_abort_dma`, `LoadPNG` | obsolete shim | Miscellaneous link stubs for drivers/network/display/audio not present on PC386. |
| `AES_init_ctx`, `AES_init_ctx_iv`, `AES_ECB_encrypt`, `AES_ECB_decrypt`, `AES_CBC_encrypt_buffer`, `AES_CBC_decrypt_buffer`, `AES_CTR_xcrypt_buffer` | obsolete shim | AES stubs. |
| `lfs_format`, `lfs_mount`, `lfs_unmount`, `lfs_remove`, `lfs_stat`, `lfs_getattr`, `lfs_file_open`, `lfs_file_close`, `lfs_file_read`, `lfs_file_write`, `lfs_dir_open`, `lfs_dir_close`, `lfs_dir_read`, `lfs_fs_size`, `pico_lfs_cfg` | obsolete shim | PC386 is FatFS-only; LittleFS calls return `LFS_ERR_IO`. |
| `GPSadjust`, `GPSaltitude`, `GPSdate`, `GPSdop`, `GPSfix`, `GPSlatitude`, `GPSlongitude`, `GPSsatellites`, `GPSspeed`, `GPStime`, `GPStrack`, `GPSvalid`, `gpsbuf`, `gpsbuf1`, `gpscount`, `gpscurrent`, `gpsmonitor` | driver state | GPS compatibility globals with no real GPS backend. |
| `startupcomplete`, `PSRAMsize`, `rp2350a`, `display_details` | port state | Miscellaneous board/runtime compatibility storage. |

## Migration Notes

- The largest common-runtime candidates are already duplicated: console I/O,
  abort/service polling, interrupt return, source tokenization, LOAD cleanup,
  erased-program-memory sentinels, and timer globals.
- `host_runtime.c` and `pc386_state.c` are state buckets, not just runtime
  code. Any extraction should separate runtime-owned globals from driver
  compatibility globals before deleting shims.
- `mmbasic_runtime_port_begin()` is the port-local lifecycle/display-console
  setup hook used by host-like ports and PC386.
- Pico boot is materially different from host-like boot: options are loaded
  and validated before clock/display setup, loaded again after clock setup,
  then display/keyboard/audio subsystems are initialized before entering the
  shared prompt loop.
- WASM and ANSI both depend on option snapshots after display-console
  overrides. Preserving this ordering is required for error recovery.
