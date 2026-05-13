# ESP32-S3 Port — D-decouple Step A: Contract Surface Inventory

**Generated:** Step A of the D-decouple plan (commit 0259955 baseline).

**Method:** `xtensa-esp-elf-nm` undefined-symbol set across non-host_native objs (PORT_LOCAL_SRCS + core/state/* + BC_SRCS + CORE_SRCS) intersected with defined-symbol set across host_native objs. Stale objs (`esp32_glue.c.obj`, `esp32_disk.c.obj`, the two superseded stubs) excluded.

**Result:** 277 symbols where host_native is currently the sole provider. These are the contract surface ESP32 must own to drop `HOST_NATIVE_REUSED` from its link line.

## Summary by target file

| Owner (existing or new) | Count | Notes |
|---|---|---|
| `esp32_peripheral_stubs.c` (new) | 150 | `cmd_*`, `fun_*`, `vm_sys_*`, plus AES/GPS/I2C/PS2/mouse/TCP — most should `error("X not supported on this port yet")` or no-op return |
| `esp32_compat.c` (existing) | 26 | mSecTimer / Tick* / mmbasic_timegm / Pico-SDK section-attr stubs (`dma_hw`, `watchdog_hw`, `__get_MSP`, `_excep_code`, etc.) |
| `esp32_console.c` (existing) | 25 | `MMputchar` / `MMPrintString` / `MMInkey` / `Serial*` / `ConsoleRx*` — extend the existing console glue |
| `esp32_default_hooks.c` (new) | 17 | Generic `port_*` no-ops (one-liners returning 0 or `void`) |
| `hal_vm_framebuffer_esp32_stub.c` (existing) | 14 | Display / framebuffer / FontTable globals + functions — extend the stub |
| `esp32_globals.c` (new) | 14 | Tentative-def globals not owned by another category (PEEK/POKE addrs, prompt fg/bg, regex, PinDef) |
| `hal_audio_esp32_stub.c` (existing) | 9 | ADC / dma_rx / dma_tx state — extend the audio stub |
| `esp32_runtime.c` (new) | 9 | `CheckAbort`, `routinechecks`, `CallCFunction`, `CallExecuteProgram`, `core1stack`, `InterruptReturn` — VM trampoline + abort hooks (mostly empty bodies on ESP32) |
| `esp32_flash_storage.c` (existing) | 8 | `flash_progmemory`, `LoadOptions`, `host_options_snapshot`, etc. — extend the existing flash file |
| `esp32_cmd_files_hooks.c` (new) | 3 | `port_drive_check`, `port_mount_sd_drive`, `port_apply_load_overrides` — A:-only filesystem hooks |
| `esp32_lfs.c` (existing) | 2 | `lfs_dir`, `lfs_info` — extend with the public structs |

**Total: 277 symbols across 11 owner files (5 new, 6 existing-extended).**

## Plan alignment

The plan's Step B target was *"<500 lines combined"* across new TUs. With 5 new files covering 200 symbols (and most being one-liner stubs), 500 lines is achievable: ~150 lines for `esp32_peripheral_stubs.c`, ~50 for `esp32_default_hooks.c`, ~50 for `esp32_runtime.c`, ~30 for `esp32_globals.c`, ~20 for `esp32_cmd_files_hooks.c` ≈ 300. The remaining 77 extensions to existing files are typically a few lines each.

## Per-symbol assignments

The full assignment table follows. Format: `owner | symbol`.

| Owner | Symbol |
|---|---|
| `esp32_cmd_files_hooks.c` | `port_apply_load_overrides` |
| `esp32_cmd_files_hooks.c` | `port_drive_check` |
| `esp32_cmd_files_hooks.c` | `port_mount_sd_drive` |
| `esp32_compat.c` | `__get_MSP` |
| `esp32_compat.c` | `_excep_code` |
| `esp32_compat.c` | `_persistent` |
| `esp32_compat.c` | `AHRSTimer` |
| `esp32_compat.c` | `day_of_week` |
| `esp32_compat.c` | `diskchecktimer` |
| `esp32_compat.c` | `dma_hw` |
| `esp32_compat.c` | `ds18b20Timers` |
| `esp32_compat.c` | `mmbasic_gmtime` |
| `esp32_compat.c` | `mmbasic_restore_psram_settings` |
| `esp32_compat.c` | `mmbasic_save_psram_settings` |
| `esp32_compat.c` | `mmbasic_timegm` |
| `esp32_compat.c` | `mSecTimer` |
| `esp32_compat.c` | `PSRAMsize` |
| `esp32_compat.c` | `readusclock` |
| `esp32_compat.c` | `rp2350a` |
| `esp32_compat.c` | `SecondsTimer` |
| `esp32_compat.c` | `TickActive` |
| `esp32_compat.c` | `TickInt` |
| `esp32_compat.c` | `TickPeriod` |
| `esp32_compat.c` | `TickTimer` |
| `esp32_compat.c` | `uSec` |
| `esp32_compat.c` | `VCC` |
| `esp32_compat.c` | `watchdog_hw` |
| `esp32_compat.c` | `WatchdogSet` |
| `esp32_compat.c` | `WDTimer` |
| `esp32_console.c` | `BreakKey` |
| `esp32_console.c` | `ConsoleRxBuf` |
| `esp32_console.c` | `ConsoleRxBufHead` |
| `esp32_console.c` | `ConsoleRxBufTail` |
| `esp32_console.c` | `ConsoleTxBufHead` |
| `esp32_console.c` | `ConsoleTxBufTail` |
| `esp32_console.c` | `EchoOption` |
| `esp32_console.c` | `getConsole` |
| `esp32_console.c` | `kbhitConsole` |
| `esp32_console.c` | `MMAbort` |
| `esp32_console.c` | `MMCharPos` |
| `esp32_console.c` | `MMgetchar` |
| `esp32_console.c` | `MMgetline` |
| `esp32_console.c` | `MMInkey` |
| `esp32_console.c` | `MMPrintString` |
| `esp32_console.c` | `MMputchar` |
| `esp32_console.c` | `putConsole` |
| `esp32_console.c` | `SerialClose` |
| `esp32_console.c` | `SerialConsolePutC` |
| `esp32_console.c` | `SerialGetchar` |
| `esp32_console.c` | `SerialOpen` |
| `esp32_console.c` | `SerialPutchar` |
| `esp32_console.c` | `SerialRxStatus` |
| `esp32_console.c` | `SerialTxStatus` |
| `esp32_console.c` | `SSPrintString` |
| `esp32_default_hooks.c` | `port_bc_bridge_clear_subfun_hash` |
| `esp32_default_hooks.c` | `port_bc_bridge_rehash_subfun` |
| `esp32_default_hooks.c` | `port_bc_crash_get_sp` |
| `esp32_default_hooks.c` | `port_bc_crash_save_fault_regs` |
| `esp32_default_hooks.c` | `port_clear_runtime_display_reset` |
| `esp32_default_hooks.c` | `port_error_restore_console_surface` |
| `esp32_default_hooks.c` | `port_error_show_lcd_banner` |
| `esp32_default_hooks.c` | `port_fun_mm_mqtt_copy` |
| `esp32_default_hooks.c` | `port_prepare_program_finalize_subfun` |
| `esp32_default_hooks.c` | `port_repl_wifi_arch_init_and_connect` |
| `esp32_default_hooks.c` | `port_select_error_prompt_font` |
| `esp32_default_hooks.c` | `port_set_default_options` |
| `esp32_default_hooks.c` | `port_try_check_var_subfun_collision` |
| `esp32_default_hooks.c` | `port_try_find_label_hash` |
| `esp32_default_hooks.c` | `port_try_find_subfun_hash` |
| `esp32_default_hooks.c` | `port_vm_time_get_tm` |
| `esp32_default_hooks.c` | `port_web_clear_runtime_state` |
| `esp32_flash_storage.c` | `flash_progmemory` |
| `esp32_flash_storage.c` | `optionangle` |
| `esp32_flash_storage.c` | `optionfastaudio` |
| `esp32_flash_storage.c` | `optionfulltime` |
| `esp32_flash_storage.c` | `optionlogging` |
| `esp32_flash_storage.c` | `printoptions` |
| `esp32_flash_storage.c` | `SavedVarsFlash` |
| `esp32_flash_storage.c` | `useoptionangle` |
| `esp32_globals.c` | `ClearExternalIO` |
| `esp32_globals.c` | `GetIntAddress` |
| `esp32_globals.c` | `GetPeekAddr` |
| `esp32_globals.c` | `GetPokeAddr` |
| `esp32_globals.c` | `PinDef` |
| `esp32_globals.c` | `PromptBC` |
| `esp32_globals.c` | `PromptFC` |
| `esp32_globals.c` | `PromptFont` |
| `esp32_globals.c` | `SoftReset` |
| `esp32_globals.c` | `startupcomplete` |
| `esp32_globals.c` | `xchg_byte` |
| `esp32_globals.c` | `xregcomp` |
| `esp32_globals.c` | `xregexec` |
| `esp32_globals.c` | `xregfree` |
| `esp32_lfs.c` | `lfs_dir` |
| `esp32_lfs.c` | `lfs_info` |
| `esp32_peripheral_stubs.c` | `AES_CBC_decrypt_buffer` |
| `esp32_peripheral_stubs.c` | `AES_CBC_encrypt_buffer` |
| `esp32_peripheral_stubs.c` | `AES_CTR_xcrypt_buffer` |
| `esp32_peripheral_stubs.c` | `AES_ECB_decrypt` |
| `esp32_peripheral_stubs.c` | `AES_ECB_encrypt` |
| `esp32_peripheral_stubs.c` | `AES_init_ctx` |
| `esp32_peripheral_stubs.c` | `AES_init_ctx_iv` |
| `esp32_peripheral_stubs.c` | `cleanserver` |
| `esp32_peripheral_stubs.c` | `close_tcpclient` |
| `esp32_peripheral_stubs.c` | `cmd_adc` |
| `esp32_peripheral_stubs.c` | `cmd_backlight` |
| `esp32_peripheral_stubs.c` | `cmd_camera` |
| `esp32_peripheral_stubs.c` | `cmd_cfunction` |
| `esp32_peripheral_stubs.c` | `cmd_Classic` |
| `esp32_peripheral_stubs.c` | `cmd_configure` |
| `esp32_peripheral_stubs.c` | `cmd_csubinterrupt` |
| `esp32_peripheral_stubs.c` | `cmd_device` |
| `esp32_peripheral_stubs.c` | `cmd_DHT22` |
| `esp32_peripheral_stubs.c` | `cmd_ds18b20` |
| `esp32_peripheral_stubs.c` | `cmd_endprogram` |
| `esp32_peripheral_stubs.c` | `cmd_files_pump_console_key` |
| `esp32_peripheral_stubs.c` | `cmd_files_restore_program_context` |
| `esp32_peripheral_stubs.c` | `cmd_files_save_program_context` |
| `esp32_peripheral_stubs.c` | `cmd_i2c` |
| `esp32_peripheral_stubs.c` | `cmd_i2c2` |
| `esp32_peripheral_stubs.c` | `cmd_in` |
| `esp32_peripheral_stubs.c` | `cmd_ir` |
| `esp32_peripheral_stubs.c` | `cmd_ireturn` |
| `esp32_peripheral_stubs.c` | `cmd_irq` |
| `esp32_peripheral_stubs.c` | `cmd_irqclear` |
| `esp32_peripheral_stubs.c` | `cmd_irqnowait` |
| `esp32_peripheral_stubs.c` | `cmd_irqset` |
| `esp32_peripheral_stubs.c` | `cmd_irqwait` |
| `esp32_peripheral_stubs.c` | `cmd_jmp` |
| `esp32_peripheral_stubs.c` | `cmd_keypad` |
| `esp32_peripheral_stubs.c` | `cmd_label` |
| `esp32_peripheral_stubs.c` | `cmd_lcd` |
| `esp32_peripheral_stubs.c` | `cmd_library` |
| `esp32_peripheral_stubs.c` | `cmd_load_post_cleanup` |
| `esp32_peripheral_stubs.c` | `cmd_mouse` |
| `esp32_peripheral_stubs.c` | `cmd_mov` |
| `esp32_peripheral_stubs.c` | `cmd_nop` |
| `esp32_peripheral_stubs.c` | `cmd_Nunchuck` |
| `esp32_peripheral_stubs.c` | `cmd_onewire` |
| `esp32_peripheral_stubs.c` | `cmd_option` |
| `esp32_peripheral_stubs.c` | `cmd_out` |
| `esp32_peripheral_stubs.c` | `cmd_pin` |
| `esp32_peripheral_stubs.c` | `cmd_pio` |
| `esp32_peripheral_stubs.c` | `cmd_PIOline` |
| `esp32_peripheral_stubs.c` | `cmd_poke` |
| `esp32_peripheral_stubs.c` | `cmd_port` |
| `esp32_peripheral_stubs.c` | `cmd_program` |
| `esp32_peripheral_stubs.c` | `cmd_pull` |
| `esp32_peripheral_stubs.c` | `cmd_pulse` |
| `esp32_peripheral_stubs.c` | `cmd_push` |
| `esp32_peripheral_stubs.c` | `cmd_pwm` |
| `esp32_peripheral_stubs.c` | `cmd_rtc` |
| `esp32_peripheral_stubs.c` | `cmd_Servo` |
| `esp32_peripheral_stubs.c` | `cmd_set` |
| `esp32_peripheral_stubs.c` | `cmd_setpin` |
| `esp32_peripheral_stubs.c` | `cmd_settick` |
| `esp32_peripheral_stubs.c` | `cmd_sideset` |
| `esp32_peripheral_stubs.c` | `cmd_spi` |
| `esp32_peripheral_stubs.c` | `cmd_spi2` |
| `esp32_peripheral_stubs.c` | `cmd_sync` |
| `esp32_peripheral_stubs.c` | `cmd_update` |
| `esp32_peripheral_stubs.c` | `cmd_wait` |
| `esp32_peripheral_stubs.c` | `cmd_watchdog` |
| `esp32_peripheral_stubs.c` | `cmd_wrap` |
| `esp32_peripheral_stubs.c` | `cmd_wraptarget` |
| `esp32_peripheral_stubs.c` | `cmd_WS2812` |
| `esp32_peripheral_stubs.c` | `cmd_xmodem` |
| `esp32_peripheral_stubs.c` | `disable_audio` |
| `esp32_peripheral_stubs.c` | `disable_sd` |
| `esp32_peripheral_stubs.c` | `disable_systemi2c` |
| `esp32_peripheral_stubs.c` | `disable_systemspi` |
| `esp32_peripheral_stubs.c` | `fun_dev` |
| `esp32_peripheral_stubs.c` | `fun_device` |
| `esp32_peripheral_stubs.c` | `fun_distance` |
| `esp32_peripheral_stubs.c` | `fun_ds18b20` |
| `esp32_peripheral_stubs.c` | `fun_GPS` |
| `esp32_peripheral_stubs.c` | `fun_info` |
| `esp32_peripheral_stubs.c` | `fun_peek` |
| `esp32_peripheral_stubs.c` | `fun_pin` |
| `esp32_peripheral_stubs.c` | `fun_pio` |
| `esp32_peripheral_stubs.c` | `fun_port` |
| `esp32_peripheral_stubs.c` | `fun_pulsin` |
| `esp32_peripheral_stubs.c` | `fun_spi` |
| `esp32_peripheral_stubs.c` | `fun_spi2` |
| `esp32_peripheral_stubs.c` | `fun_touch` |
| `esp32_peripheral_stubs.c` | `GPSadjust` |
| `esp32_peripheral_stubs.c` | `GPSaltitude` |
| `esp32_peripheral_stubs.c` | `gpsbuf` |
| `esp32_peripheral_stubs.c` | `gpsbuf1` |
| `esp32_peripheral_stubs.c` | `GPSchannel` |
| `esp32_peripheral_stubs.c` | `gpscount` |
| `esp32_peripheral_stubs.c` | `gpscurrent` |
| `esp32_peripheral_stubs.c` | `GPSdate` |
| `esp32_peripheral_stubs.c` | `GPSdop` |
| `esp32_peripheral_stubs.c` | `GPSfix` |
| `esp32_peripheral_stubs.c` | `GPSlatitude` |
| `esp32_peripheral_stubs.c` | `GPSlongitude` |
| `esp32_peripheral_stubs.c` | `gpsmonitor` |
| `esp32_peripheral_stubs.c` | `GPSsatellites` |
| `esp32_peripheral_stubs.c` | `GPSspeed` |
| `esp32_peripheral_stubs.c` | `GPStime` |
| `esp32_peripheral_stubs.c` | `GPStrack` |
| `esp32_peripheral_stubs.c` | `GPSvalid` |
| `esp32_peripheral_stubs.c` | `I2C0locked` |
| `esp32_peripheral_stubs.c` | `I2C1locked` |
| `esp32_peripheral_stubs.c` | `IgnorePIN` |
| `esp32_peripheral_stubs.c` | `initMouse0` |
| `esp32_peripheral_stubs.c` | `mmI2Cvalue` |
| `esp32_peripheral_stubs.c` | `mmOWvalue` |
| `esp32_peripheral_stubs.c` | `mouse0` |
| `esp32_peripheral_stubs.c` | `OnKeyGOSUB` |
| `esp32_peripheral_stubs.c` | `OnPS2GOSUB` |
| `esp32_peripheral_stubs.c` | `ProcessWeb` |
| `esp32_peripheral_stubs.c` | `PS2code` |
| `esp32_peripheral_stubs.c` | `SPIatRisk` |
| `esp32_peripheral_stubs.c` | `tcp_free_recv_buffers` |
| `esp32_peripheral_stubs.c` | `tcp_realloc_recv_buffers` |
| `esp32_peripheral_stubs.c` | `TCPreceived` |
| `esp32_peripheral_stubs.c` | `TCPreceiveInterrupt` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_chdir` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_close` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_copy` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_drive` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_eof` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_getc` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_kill` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_line_input` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_lof` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_mkdir` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_open` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_print_buf` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_print_newline` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_print_str` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_rename` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_reset` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_rmdir` |
| `esp32_peripheral_stubs.c` | `vm_sys_file_seek` |
| `esp32_peripheral_stubs.c` | `vm_sys_pin_read` |
| `esp32_peripheral_stubs.c` | `vm_sys_pin_reset` |
| `esp32_peripheral_stubs.c` | `vm_sys_pin_setpin` |
| `esp32_peripheral_stubs.c` | `vm_sys_pin_write` |
| `esp32_peripheral_stubs.c` | `vm_sys_pwm_configure` |
| `esp32_peripheral_stubs.c` | `vm_sys_pwm_off` |
| `esp32_peripheral_stubs.c` | `vm_sys_pwm_sync` |
| `esp32_peripheral_stubs.c` | `vm_sys_servo_configure` |
| `esp32_runtime.c` | `CallCFunction` |
| `esp32_runtime.c` | `CallExecuteProgram` |
| `esp32_runtime.c` | `check_interrupt` |
| `esp32_runtime.c` | `CheckAbort` |
| `esp32_runtime.c` | `core1stack` |
| `esp32_runtime.c` | `InterruptReturn` |
| `esp32_runtime.c` | `InterruptUsed` |
| `esp32_runtime.c` | `routinechecks` |
| `esp32_runtime.c` | `ScrewUpTimer` |
| `hal_audio_esp32_stub.c` | `ADC_dma_chan` |
| `hal_audio_esp32_stub.c` | `ADC_dma_chan2` |
| `hal_audio_esp32_stub.c` | `ADCDualBuffering` |
| `hal_audio_esp32_stub.c` | `dma_rx_chan` |
| `hal_audio_esp32_stub.c` | `dma_rx_chan2` |
| `hal_audio_esp32_stub.c` | `dma_tx_chan` |
| `hal_audio_esp32_stub.c` | `dma_tx_chan2` |
| `hal_audio_esp32_stub.c` | `dmarunning` |
| `hal_audio_esp32_stub.c` | `last_adc` |
| `hal_vm_framebuffer_esp32_stub.c` | `blitmerge` |
| `hal_vm_framebuffer_esp32_stub.c` | `closeframebuffer` |
| `hal_vm_framebuffer_esp32_stub.c` | `copyframetoscreen` |
| `hal_vm_framebuffer_esp32_stub.c` | `display_details` |
| `hal_vm_framebuffer_esp32_stub.c` | `Display_Refresh` |
| `hal_vm_framebuffer_esp32_stub.c` | `DISPLAY_TYPE` |
| `hal_vm_framebuffer_esp32_stub.c` | `DisplayNotSet` |
| `hal_vm_framebuffer_esp32_stub.c` | `FileLoadCMM2Program` |
| `hal_vm_framebuffer_esp32_stub.c` | `LoadPNG` |
| `hal_vm_framebuffer_esp32_stub.c` | `restorepanel` |
| `hal_vm_framebuffer_esp32_stub.c` | `screen320` |
| `hal_vm_framebuffer_esp32_stub.c` | `ScrollLCDSPISCR` |
| `hal_vm_framebuffer_esp32_stub.c` | `setframebuffer` |
| `hal_vm_framebuffer_esp32_stub.c` | `setterminal` |
