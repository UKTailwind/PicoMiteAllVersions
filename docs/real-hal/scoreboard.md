# Real HAL — Scoreboard

Per-file count of **every** preprocessor conditional (`#if` / `#ifdef` / `#ifndef` / `#elif`) in the tracked core files. Measured by `tools/hal_scoreboard.sh`. **Trend indicator only — the hard gate is `tools/check_hal_purity.sh`.**

The old script grepped for specific macro names (`rp2350`, `PICOMITE`, …), which made renamed gates (`#ifdef rp2350` → `#if HAL_PORT_PWM_SLICE_COUNT > 8`) invisible. The rewrite counts all conditional-compilation directives regardless of macro name, so renaming can't help the score — only moving bodies out of the core file does. See `../real-hal-fixup-plan.md` for the standard and corrective sequence.

Every row below must be measured by running the scoreboard script after the phase lands. If the number didn't go down, the ifdef elimination didn't happen.

```
Phase    Draw    MM_Misc  External  FileIO  Commands  Memory  Functions  Audio   Total
F1       164     139      123       62      45        38      17         14      602  (honest baseline — revert of 2c034d7+61cb08e)
─── fixup (real elimination, per ../real-hal-fixup-plan.md) ───
F2a step 1 164     139     107      62      45        38      17         14      586  (per-port port_config.h + first cull; −16)
F2a step 2 164     139      58      62      45        38      17         14      537  (unconditional globals + case arms; −49)
F2a step 3a 164    139      50      62      45        38      17         14      529  (hal_fast_timer + HEARTBEAT; −8)
F2a step 3b 164    139      38      62      45        38      17         14      517  (keyboard HAL + ADC_CHANNEL_MAX; −12)
F2a step 3c 164    139      27      62      45        38      17         14      506  (rp2350a runtime split + camera stubs; −11)
F2a step 3d 164    139      22      62      45        38      17         14      501  (PINMAP + codemap to per-port .c; −5)
F2a step 3e 164    139      19      62      45        38      17         14      498  (ADC OPEN rp2350a + MOUSE stub; −3)
F2a step 3f 164    140      16      62      45        38      17         14      496  (MQTT + Collision unconditional; −2)
F2a step 3g 164    140      10      62      45        38      17         14      490  (setBacklight unified + NEXTGEN; −6)
F2a step 3h 164    140       2      62      39        38      17         14      476  (F2 close: MATHS.c dims widening + KEYPAD unification + PicoCalc keymap per-port; −14)
            External.c: 0 target-macro ifdefs (only 2 #ifdef GUICONTROLS
            remain — not in scope). Promoted to STRICT_FILES in the purity
            gate. F2 CLOSED.
─── F3 (FileIO.c) ───
F3 step 1  164    140       2      52      39        38      17         14      466  (PSRAM cache→port file + mergedread/a_dlist/MemLoadProgram uncond; −10)
F3 step 2  164    140       2      50      39        38      17         14      464  (disable/enable_interrupts_pico MMBASIC_HOST gate off; −2)
F3 step 3  164    140       2      48      39        38      17         14      462  (heartbeat rp2350a runtime + LoadPNG HAS_UPNG; −2)
F3 step 4  164    140       2      44      39        38      17         14      458  (ProcessWeb stub + 4 PICOMITEWEB sites; −4)
F3 step 5  164    140       2      39      39        38      17         14      453  (TCP recv-buffer helpers + closeall3d stub; −5)
F3 step 6  164    140       2      32      39        38      17         14      446  (ResetOptions → 7 per-port port_defaults.c; −7)
F3 step 7  164    140       2      31      39        38      17         14      445  (cmd_psram → ports/pico_sdk_common/cmd_psram.c; −1)
F3 step 8  164    140       2      30      39        38      17         14      444  (DEFINES loader → ports/pico_sdk_common/defines_loader.c; −1)
F3 step 9  164    140       2      28      39        38      17         14      442  (MemWriteBlock + LoadPNG → port files; −2)
F3 step 10 164    140       2      17      39        38      17         14      431  (SD read-cache → hal_filesystem_pico per-slot + getc/putc; −11)
F3 step 11 164    140       2      12      39        38      17         14      426  (cmd_files save/restore + key-pump + cmd_load post-cleanup hooks; −5)
F3 step 12 164    140       2       8      39        38      17         14      422  (dead FileLoadSourceProgramVM + MAXFILES port-config + InitSDCard split; −4)
F3 step 13 164    140       2       4      39        38      17         14      418  (dead multicore extern + cmd_disk + LoadOptions PICOCALC overrides; −4)
F3 step 14 164    140       2       2      39        38      17         14      416  (FatFS dir-walker dispatch via hal/hal_fatfs_dispatch.h; −2)
F3 step 15 164    140       2       2      39        38      17         14      416  (cmd_LoadJPGImage host link, picojpeg.c → host CORE_SRCS; 0 ifdefs but no scoreboard delta — 2 leftovers are #ifndef max/min macro defs)
            FileIO.c: 60 → 2 (target-macro 60 → 0). The two leftover
            conditionals are `#ifndef max` / `#ifndef min` macro
            definitions — neither target nor port-config. F3 CLOSED.
            FileIO.c promoted to STRICT_FILES in the purity gate.
─── F4 (MM_Misc.c USBKEYBOARD → HAL) ───
F4 step 1  164    138       2       2      39        38      17         14      414  (drop dead pico/rand.h + mouse0 externs; −2)
F4 step 2  164    133       2       2      39        38      17         14      409  (PicoCalc HW ops → port_picocalc_*() hooks; −5)
F4 step 3  164    131       2       2      39        38      17         14      407  (PS2 stubs on USB builds → drop PS2 USBKEYBOARD gates; −2)
F4 step 4  164    124       2       2      39        38      17         14      400  (fun_device → HAL_PORT_DEVICE_NAME compile-time string; −7)
F4 step 5  164    115       2       2      39        38      17         14      391  (CONFIGURE LIST → port_print_supported_boards(); −9)
F4 step 6  164    113       2       2      39        38      17         14      389  (setwifi → MMsetwifi.c + WEB interrupt-dispatch unconditional; −2)
F4 step 7  164     98       2       2      39        38      17         14      374  (OPTION RESET <BOARD> → port_factory_reset_board() per port_defaults.c; −15)
F4 step 8  164     96       2       2      39        38      17         14      372  (USB device-info + KEYBOARD$ runtime check via Option.{USBKeyboard,KeyboardConfig}; −2)
F4 step 9  164     91       2       2      39        38      17         14      367  (WEB OPTION setters + MM.* info + printoptions → MMsetwifi.c + new MMweb_stubs.c + USB hooks; −5)
F4 step 10 164     82       2       2      39        38      17         14      358  (display OPTION setters → port_display_option_setter() per port_defaults.c; −9)
F4 step 11 164     78       2       2      39        38      17         14      354  (HAL_PORT_IS_VGA runtime branches in OPTION SDCARD/SYSTEM I2C; −4)
F4 step 12 164     77       2       2      39        38      17         14      354  (MM.INFO TILE HEIGHT unconditional + ytileheight non-VGA default; −1)
F4 step 13 164     67       2       2      39        39      17         14      344  (printoptions display section → port_print_display_options() shared file; −10)
F4 step 14 164     61       2       2      39        39      17         14      338  (PWM slice + GPIO + power + audio rp2350 gates → runtime checks; −6)
F4 step 15 164     57       2       2      39        39      17         14      334  (VGArecovery + LCD SPI printoptions → port files; −4)
F4 step 16 164     53       2       2      39        39      17         14      330  (OPTION KEYBOARD setter → port_keyboard_option_setter(); −4)
F4 step 17 164     52       2       2      39        39      17         14      329  (SSD1963data extern unconditional via VGA stubs; −1)
F4 step 18 164     50       2       2      39        39      17         14      327  (SYSTEM SPI printoptions via HAL_PORT_IS_VGA + dead `i` decl; −2)
F4 step 19 164     46       2       2      39        39      17         14      323  (PIN reserved-name hooks → port_pin_is_reserved_alias / port_pinno_alias_for_name; −4)
F4 step 20 164     34       2       2      39        39      17         14      311  (printoptions LCD320/USB-keyboard/heartbeat → per-port helper files; −12)
F4 step 21 164     32       2       2      39        39      17         14      309  (disable_lcdspi → port hook + LCD320 SYSTEM-shared cleanup → port hook; −2)
F4 step 22 164     29       2       2      39        39      17         14      306  (ConfigDisplayUser/clear320/OPTION LCD320 → ports/pico_sdk_common/spi_lcd_options.c; −3)
F4 step 23 164     24       2       2      39        39      17         14      301  (OPTION HDMI PINS / KEYBOARD BACKLIGHT / PSRAM PIN / KEYBOARD REPEAT / PS2 PINS / MOUSE → port_misc_option_setter(); −5)
F4 step 24 164     21       2       2      39        39      17         14      298  (OPTION PICO + HEARTBEAT → port_pico_pins_option_setter / port_heartbeat_option_setter; −3)
F4 step 25 164     17       2       2      39        39      17         14      294  (LCDPANEL CONSOLE tile-color reset → port_apply_default_console_colors() per port_defaults.c; −4)
F4 step 26 164     13       2       2      39        39      17         14      290  (OPTION SYSTEM SPI / LCD SPI → port_system_lcd_spi_option_setter() + SDCARD COMBINED CS → HAL_PORT_IS_VGA runtime; −4)
F4 step 27 164     11       2       2      39        39      17         14      288  (OPTION AUDIO I2S PIO check + slice selection → port_audio_i2s_pio_slice() in misc_option_setters.c; −2)
F4 step 28 164      8       2       2      39        39      17         14      285  (fun_device chip suffix → HAL_PORT_PIO_COUNT runtime + MM.INFO INTERRUPTS → port_mminfo_interrupts() + MM.INFO LCDPANEL/LCD320 → HAL_PORT_IS_VGA runtime; −3)
F4 step 29 164      6       2       2      39        39      17         14      283  (MM.INFO TOUCH/SCROLL/SCREENBUFF → port_mminfo_touch_status / port_mminfo_scroll_start / port_mminfo_screenbuff hooks; −2)
F4 step 30 164      4       2       2      39        39      17         14      281  (PIO instance lookup → port_pio_for_index() + POKE DISPLAY raw panel → port_poke_display_panel(); −2)
            MM_Misc.c: 140 → 4 across 30 sub-steps (target-macro 135 → 0).
            Remaining 4 are #ifdef GUICONTROLS — feature flag (not a target
            or port-config macro), out of scope. F4 CLOSED. MM_Misc.c
            promoted to STRICT_FILES in the purity gate.
─── post-fixup phases ───
─── Phase 6b (Audio.c device body → HAL) ───
6b step 1  164      4        2         2      39        39        17         13       280  (drop PICOMITEWEB guard around ProcessWeb — F3 step 4 stub is unconditional; −1)
6b step 2  164      4        2         2      39        39        17         12       279  (FLAC max sample-rate cap → HAL_PORT_AUDIO_FLAC_MAX_BASE_HZ port constant; −1)
6b step 3a 164      4        2         2      39        39        17         11       278  (dr_mp3 amalgamated impl → drivers/audio_mp3/{real,stub}.c; dr_mp3.h unconditional in Audio.c; MOD_BUFFER_SIZE → HAL_PORT_AUDIO_MOD_BUFFER_SIZE; HAL_PORT_HAS_MP3 defined; −1)
6b step 3b 164      4        2         2      39        39        17          5       272  (drop 6 rp2350 guards around MP3 code in Audio.c — drmp3 *mymp3 decl, CloseAudio MP3+PSRAM cleanup, mp3callback body, PLAY MP3 precondition → HAL_PORT_HAS_MP3 runtime branch, checkWAVinput pump, audio_checks cleanup; PSRAMsize definition promoted to unconditional in PicoMite.c and extern in Hardware_Includes.h; −6)
6b step 4  164      4        2         2      39        39        17          1       268  (drop 4 rp2350 guards around PSRAM MOD buffer code — CloseAudio modbuff reset, MODFILE "not enabled" check, and the paired flash/PSRAM load branches — PSRAMsize=0 on RP2040 collapses them to the flash path at runtime; −4)
6          .       .        .        .       .         .       .          0        .  (Audio.c → HAL)
7a         .       .        .        .       .         .       .          .        .  (Draw.c ILI9341 → HAL)
7b         .       .        .        .       .         .       .          .        .  (Draw.c VGA → HAL)
7c         .       .        .        .       .         .       .          .        .  (Draw.c HDMI → HAL)
7d         0       .        .        .       .         .       .          .        .  (Draw.c SSD1963 → HAL)
8          .       .        .        .       .         .       .          .        .  (multicore)
9          .       .        0        .       .         .       .          .        .  (net — External.c + FileIO.c PICOMITEWEB)
10         .       .        .        .       .         0       .          .        .  (heap)
11         0       0        0        0       0         0       0          0        0  (sweep)
```

Dots (`.`) mean "not targeted by this phase — carry forward from previous." Zeros are the exit-gate targets.

## F1 baseline breakdown (per `tools/hal_scoreboard.sh --breakdown`)

| file | total | target-macro | port-config | other |
|------|-------|--------------|-------------|-------|
| Draw.c      | 164 | 160 | 0 | 4 |
| MM_Misc.c   | 139 | 134 | 0 | 5 |
| External.c  | 123 | 120 | 0 | 3 |
| FileIO.c    |  62 |  60 | 0 | 2 |
| Commands.c  |  45 |  45 | 0 | 0 |
| Memory.c    |  38 |  37 | 0 | 1 |
| Functions.c |  17 |  17 | 0 | 0 |
| Audio.c     |  14 |  14 | 0 | 0 |
| **total**   | **602** | **587** | **0** | **15** |

The "other" column counts conditionals that reference neither a target macro nor a port-config macro (e.g. `#ifdef EXTERNAL_INCLUDES`, `#if defined(__GNUC__)`). Those are out of scope for the HAL-clean standard — the goal is zero target-macro ifdefs and zero port-config-macro ifdefs. A phase closes by driving its file's target+port columns to 0, not by touching the "other" column.

## Historical (for context)

Pre-F1 measurement used the old name-based metric:

- Phase 0 baseline: 606 total (target macros only)
- 1–5a tip: 587 (infrastructure landed, ifdefs mostly unchanged)
- Post 2c034d7+61cb08e: claimed 508; the 79-gate drop was a rename, not elimination; reverted in F1

The F1 total (602) is higher than "5a" (587) by 15 because the new script also counts non-target conditionals. The target-only subtotal in the F1 breakdown (587) matches the pre-revert "5a" figure exactly — i.e. nothing was lost or gained in the revert at the level of target-macro ifdefs. The additional 15 were always there; the old script simply didn't see them.

## Methodology note

The scoreboard is a trend indicator. The gate is `tools/check_hal_purity.sh`. The gate enforces two things:

1. `hal/*.h` has zero preprocessor conditionals apart from the file's own include-guard `#ifndef` and the `#ifdef __cplusplus` `extern "C"` wrapper.
2. Any file in `STRICT_FILES` has zero target-macro ifdefs AND zero port-config-macro ifdefs.

As each phase lands, it promotes its targeted file(s) into `STRICT_FILES`. A phase closes only when the gate passes with that file promoted. Renaming a `#ifdef rp2350` to `#if HAL_PORT_PWM_SLICE_COUNT > 8` now fails the gate at both (1) and (2).
