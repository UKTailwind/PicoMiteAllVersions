# Stage 3 — MMBasic on bare metal (serial REPL + LOAD/SAVE on A:/C:)

**Goal:** A `mmbasic.elf` that boots through Limine, brings up the interpreter, and runs a BASIC program — input/output over COM1, file I/O against the FAT volumes Stage 2 mounted (A:/C:). At stage close, the same `.bas/.ok` corpus that drives `ports/mmbasic_stdio/run_tests.sh` runs under QEMU and passes.

This stage does NOT add interrupts, PS/2 keyboard, VGA graphics, or PC speaker — those are Stages 4/5/6. Stage 3 is the HAL plumbing stage.

## Non-negotiables

1. **Fresh HAL impls, no link-time overrides.** Each pc386 HAL surface gets its own implementation file in `ports/pc386/` (or shared spot — see "Restructure"). We do *not* compile `hal_*_host.c` and silently override host_*-named symbols from a port-local TU. If a host HAL impl is genuinely identical to what pc386 needs (watchdog no-op is the canonical case), it gets relocated into a port-neutral spot that both ports compile.
2. **HAL purity stays green.** `tools/check_hal_purity.sh` must stay clean — no new `#if PORT_PC386` in core or VM. Behavior differences are expressed by which HAL impl gets linked, not by ifdefs.
3. **`host/run_tests.sh` doesn't regress.** Same source files, no new options that change shared semantics.
4. **No reach-back into mmbasic_stdio.** That port is the litmus for HAL purity in a POSIX environment; pc386 is the litmus in a bare-metal one. They're peers, not parent/child.

## HAL surface audit

What every port has to satisfy to link the MMBasic core + bytecode VM (derived from `ports/mmbasic_stdio/Makefile` source list):

| HAL header | host_native file | pc386 plan | Shareable? |
|---|---|---|---|
| `hal_audio.h` | `hal_audio_host.c` (45 LOC, forwards to host_sim_audio) | error stubs in `ports/pc386/hal_audio_pc386.c` | No — host bridges to JS/SDL backends; pc386 has no audio until Stage 6 |
| `hal_filesystem.h` | `hal_filesystem_host.c` (393 LOC, POSIX) | new `hal_filesystem_pc386.c` over FatFs (Stage 2 already mounted A:/C:) | No |
| `hal_flash.h` | `hal_flash_host.c` (103 LOC, RAM buffer in `host_fs_shims.c`) | new `hal_flash_pc386.c` with BSS-resident program + options buffers | No, but the **logic** is identical — copy-then-tweak, not refactor |
| `hal_keyboard.h` | `hal_keyboard_host.c` (59 LOC, termios) | new `hal_keyboard_pc386.c` polling COM1 RX register; Stage 4 swaps for PS/2 | No |
| `hal_pin.h` | `hal_pin_host.c` (168 LOC, RAM table for SETPIN/PIN() introspection) | new `hal_pin_pc386.c` with error stubs for stage 3; LPT1 mapping lands in stage 6.5 | No — host's RAM-table behavior isn't what pc386 wants; LPT1 will be its own thing |
| `hal_random.h` | `hal_random_host.c` (9 LOC, `rand()`) | new `hal_random_pc386.c`, RDTSC-seeded xorshift32 (no libc) | No |
| `hal_storage.h` | `hal_storage_host.c` (29 LOC, `HAL_STORAGE_ERR_NODISK`) | new `hal_storage_pc386.c` over `drivers/ata_pio` | No |
| `hal_time.h` | `hal_time_host.c` (35 LOC, clock_gettime) | new `hal_time_pc386.c` — TSC for monotonic µs, PIT-channel-2 polling for sleep | No |
| `hal_vm_framebuffer.h` | `hal_vm_framebuffer_host.c` (37 LOC, host_fb-backed) | error stubs in `ports/pc386/hal_vm_framebuffer_pc386.c` until Stage 5 | No |
| `hal_watchdog.h` | `hal_watchdog_host.c` (9 LOC, no-op) | **same no-op** | **YES — extract** |

### Restructure proposed

Two HAL impls are now duplicated noise across host_native + esp32 + pc386:

- `hal_watchdog`: pure no-op everywhere except RP2040/RP2350. Move the body to `hal/generic/hal_watchdog_noop.c` and have host_native, esp32, and pc386 all compile it.
- `hal_pin` *error-stub* posture: host_native, mmbasic_stdio, esp32, pc386 all want "every entry errors loudly because there's no GPIO." host_native's 168-LOC file is mostly that. Worth extracting `hal/generic/hal_pin_unsupported.c` as the canonical error-stub impl; host_native keeps any genuinely-host-specific bits (if any) in `hal_pin_host.c` on top.

The `hal/generic/` directory is new but small (probably 2 files at end of stage). Anything that grows hair gets split back into per-port impls.

**Out of scope for restructure in stage 3:** flash buffer extraction (host_native ties it to `host_fs_shims.c`'s naming), HAL header changes, audio bridge cleanup. We re-evaluate after stage 8 lands.

## Files

```
ports/pc386/
  hal_audio_pc386.c           # error-stub family
  hal_filesystem_pc386.c      # FatFs-backed; uses Stage 2's f_mount setup
  hal_flash_pc386.c           # BSS-resident prog/options buffers
  hal_keyboard_pc386.c        # COM1 RX polling
  hal_pin_pc386.c             # (or compiles hal/generic/hal_pin_unsupported.c)
  hal_random_pc386.c          # RDTSC + xorshift32
  hal_storage_pc386.c         # drivers/ata_pio passthrough
  hal_time_pc386.c            # TSC + PIT
  hal_vm_framebuffer_pc386.c  # error-stub family
  hal_watchdog_pc386.c        # (or compiles hal/generic/hal_watchdog_noop.c)

  pc386_runtime.c             # the host_runtime.c equivalent: provides
                              # host_output_hook (→ serial_16550_putc), 
                              # host_read_byte_*, host_runtime_begin,
                              # MMputchar wiring. Replaces — does not 
                              # override — host_runtime.c.
  pc386_qemu_exit.c           # writes to QEMU's isa-debug-exit port 0xf4
                              # for clean test-harness exits.

  kmain.c                     # extends Stage 2: after mounts, calls
                              # pc386_runtime_begin() and either runs an
                              # embedded test program or LOADs A:\AUTOEXEC.BAS

hal/generic/                  # NEW
  hal_watchdog_noop.c
  hal_pin_unsupported.c

ports/host_native/Makefile    # drops hal_watchdog_host.c + most of 
                              # hal_pin_host.c, picks up generic versions
ports/mmbasic_stdio/Makefile  # same
ports/esp32_s3_metro/main/    # same
```

## Test harness

Mirrors `ports/mmbasic_stdio/run_tests.sh`. Per-test loop:

1. Build a fresh boot disk image: clone the Stage 2 A: image template, `mcopy` the test's `.bas` in as `A:\AUTOEXEC.BAS`.
2. Boot it under QEMU:
   ```
   qemu-system-i386 -nographic -no-reboot \
     -drive file=test_a.img,format=raw,if=ide,index=0,media=disk \
     -drive file=test_c.img,format=raw,if=ide,index=1,media=disk \
     -serial mon:stdio -display none \
     -device isa-debug-exit,iobase=0xf4,iosize=0x04
   ```
3. Capture serial output.
4. Diff against `.ok`.
5. Exit code from `isa-debug-exit` distinguishes PASS (`exit(0x21)` = success) from kernel-side panic (any other code).

Triple-fault is *not* the exit mechanism — `isa-debug-exit` is cleaner, gives QEMU an explicit exit status, and avoids the IDT bring-up tarpit (which belongs in Stage 4 anyway).

The test corpus reuses `ports/mmbasic_stdio/tests/01_hello.bas..06_sub_function.bas` byte-for-byte. The hardware-error tests (`07_hardware_error_pixel.bas`, `08_hardware_error_box.bas`) port over but exercise the pc386 stub error messages; new `.ok` files.

## Boot script convention

`pc386_runtime_begin()` looks for `A:\AUTOEXEC.BAS` and, if present, LOADs and RUNs it. If absent, it sits on a serial REPL. The REPL itself is light — no MMBasic_REPL.c (that's Editor / line editing, which is Stage 4 territory). For Stage 3, the REPL is "read line over COM1, tokenise, run, print". When Stage 4 brings up the keyboard + IDT, the real Editor.c-driven REPL drops in.

## Validation

A stage-3 close requires all of:

1. `cd ports/pc386 && make` — clean build, no warnings.
2. `ports/pc386/run_tests.sh` — green for the stage-3 corpus (6 of the 8 mmbasic_stdio tests; the two pin/pixel error tests need pc386-specific `.ok` files).
3. `host/run_tests.sh` — still green (no regression in host_native or mmbasic_stdio).
4. `tools/check_hal_purity.sh` — green.
5. `qemu -kernel build/mmbasic.elf -serial stdio -display none` boots into the REPL prompt manually, accepts `PRINT 1+1` and prints `2`.

## Sub-stages

Each lands as its own commit so a regression bisects cleanly.

- **3a ✅ — HAL skeleton.** 10 pc386 HAL files in place; every entry that has no boot-safe semantics calls `pc386_panic("...not yet implemented")`. Boot-safe entries (audio_init, watchdog, vm_framebuffer service/shutdown, keyboard_service, etc.) no-op so unconditional boot calls don't fault. Kernel still does Stage 2's mount-and-list. Builds clean (mmbasic.elf 206 KB), boots, Stage 2 functionality verified intact. Adds `ports/pc386/sys/types.h` shim for `off_t`/`ssize_t` (i686-elf has no `<sys/types.h>`).
- **3b ✅ — `hal/generic/` extraction.** Watchdog no-op moved to `hal/generic/hal_watchdog_noop.c`. host_native, mmbasic_stdio, esp32_s3_metro, pc386 all compile the shared file; per-port copies deleted. `host/run_tests.sh` 243/243 (no regression).
- **3c.0 ✅ — Real `hal_time` + `hal_random`.** TSC for monotonic µs, calibrated at first-call against PIT channel 2 via the speaker-control port (0x61) status bit. xorshift32 seeded from RDTSC. Both standalone — no MMBasic include surface needed.
- **3c.1 ⏳ — MMBasic core source list + freestanding libc skeleton.** Add CORE_SRCS + BC_SRCS to pc386 Makefile (mirroring mmbasic_stdio's list, minus the host_native runtime files). Iterate the link until every undef symbol is either (a) in pc386 HAL impls, (b) in `pc386_freestanding_libc.c` (memcpy, memset, strlen, strchr, snprintf, isdigit, etc.), or (c) in a not-yet-written stub TU. The output of this sub-stage is a list of symbols still needing TUs in 3c.2/3c.3.
- **3c.2 ⏳ — `pc386_runtime.c` (state + lifecycle).** Equivalent of `ports/host_native/host_runtime.c` (~600-1000 LOC). Owns the BSS state arrays the MMBasic core declares `extern` (Option, FontTable, FileTable, ConsoleRxBuf, etc.), provides `host_output_hook`/`MMputchar`/`MMInkey` skeleton (output stays panicking for now — wired in 3d), `host_runtime_begin/finish`. Also includes the `pc386_peripheral_stubs.c` for cmd_*/fun_* hardware no-ops (no SPI/I²C/PWM/PIO on a 386).
- **3c.3 ⏳ — `hal_flash` + state buffers.** Real BSS-resident program-area + options buffer in a new `pc386_state.c` (equivalent of `host_fs_shims.c`). hal_flash impl wraps them. ProgMemory now has a real backing store.
- **3c.4 ⏳ — First BASIC instantiation.** kmain calls `LoadOptions()` → `InitBasic()` → `InitHeap(true)` → `host_runtime_begin()` and reaches the point where it could call ExecuteProgram (which would still fault on the panic'd output hook). Verify the runtime stands up cleanly under `qemu-system-i386 -kernel`.
- **3d ⏳ — output: PRINT to serial.** `host_output_hook` → `serial_16550_putc`. kmain runs an embedded `PRINT "hello"` program; first BASIC output over COM1.
- **3e ⏳ — input: REPL skeleton.** COM1 polling for `host_read_byte_*`. Kernel sits in `read line → tokenise → run → print` after mount. Manual smoke test.
- **3f ⏳ — filesystem + storage HAL real impls.** `hal_filesystem_pc386.c` over FatFs (Stage 2 already mounted A:/C:); `hal_storage_pc386.c` over ATA-PIO. `LOAD`, `SAVE`, `OPEN`, `CLOSE`, `PRINT #`, `INPUT #`, `FILES` work.
- **3g ⏳ — AUTOEXEC + test harness.** Boot-script convention (kernel LOADs A:\AUTOEXEC.BAS); `run_tests.sh` builds per-test boot disks via mtools, runs QEMU with `isa-debug-exit`, golden-compares. Whole stage-3 corpus green.
- **3h ⏳ — error-stub corpus.** `.ok` files for pin/pixel hardware-error tests. Confirms pc386 reports clear errors for surfaces that aren't wired yet (PLAY → "stage 6", FRAMEBUFFER → "stage 5", PIN → "stage 6.5").

## Open questions

- **Output mirroring:** Stage 0 has both serial + VGA-text wired. Should `host_output_hook` write to both, or just serial? Default: serial only during tests (since VGA-text isn't captured by the harness anyway), both during interactive boot. Decide at 3d.
- **REPL line editor:** Stage 3's REPL is going to be ugly — no echo handling, no backspace, no history. That's fine for stage-3 tests (which feed AUTOEXEC.BAS, not interactive input) but the manual smoke test will be rough. If it bothers us at 3e, add minimal echo + backspace; full Editor.c is Stage 4.
- **Slowdown / cooperative-yield:** mmbasic_stdio uses `host_sim_apply_slowdown` to keep CPU-bound BASIC from saturating the host. Bare metal has no scheduler — slowdown is a no-op until we have IRQs. Wire it as a no-op for stage 3.
