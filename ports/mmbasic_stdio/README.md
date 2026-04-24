# mmbasic_stdio — pure-stdio MMBasic port

This port is the **HAL litmus test** (Phase 12.5 of the real-HAL
refactor). It links MMBasic core + VM against the narrowest possible
set of HAL implementations: a real libc clock, real POSIX filesystem,
stdin-blocking keyboard, null display (PRINT → stdout), and hard-error
stubs for every hardware-only HAL (pin / audio / multicore / flash /
net).

The point: **if the core is HAL-clean, `mmbasic_stdio` links without
pulling in `Editor.c`, `MMBasic_REPL.c`, `MMBasic_Prompt.c`, or any
display driver.** Any undefined-reference error is a HAL leak —
report it, don't work around it.

## Status

⏳ **Scaffolding only (2026-04-23).** Skeleton + docs in place. Real
work (main.c entry, HAL stub implementations, test corpus) pending.

## Planned layout

```
main.c                — argv[1] = .bas file, or read stdin to EOF
CMakeLists.txt        — links core + minimal HAL impls:
                          hal_time           → libc clock (reuse host_native)
                          hal_filesystem     → POSIX (reuse host_native)
                          hal_storage        → null (no block device)
                          hal_keyboard       → stdin
                          hal_display        → null (PRINT → stdout)
                          hal_audio          → hard-error stub
                          hal_pin            → hard-error stub
                          hal_flash          → hard-error stub
                          hal_multicore      → hard-error stub
                          hal_net            → hard-error stub
```

No editor, no REPL, no graphics, no MEMFS, no IDBFS, no canvas.

## Exit gate

1. `mmbasic_stdio` builds (static, stripped).
2. `tests/mmbasic_stdio/` corpus passes (PRINT/FOR/GOTO/basic math).
3. Link-line audit: no Editor.c / MMBasic_REPL.c / MMBasic_Prompt.c /
   any file under `drivers/` that implements a display panel.
4. Stripped binary under 500 KB on x86_64 macOS.
