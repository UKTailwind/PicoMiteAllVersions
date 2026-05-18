# VM Command Coverage

This document is the current command-coverage matrix for the VM BASIC program path.

It is derived from the legacy command table in [AllCommands.h](../core/mmbasic/AllCommands.h) and uses only these status terms:

- `implemented`
- `partial`
- `unimplemented`

Status is conservative. If only a subset of the legacy command family is supported on the VM path, the command stays `partial`.

## Scope

- This is about the VM BASIC program path, not the prompt shell.
- A command can still work at the prompt through legacy shell code and remain `unimplemented` here.
- The device shell uses the legacy prompt; shell command coverage is tracked in `vm-architecture.md` / `vm-cutover-plan.md`, not here.
- Hybrid command/function entries from the command table such as `Pin(`, `Timer`, `Date$`, `Time$`, and `MID$(` are included because they appear in `INCLUDE_COMMAND_TABLE`.

## Implemented

### Core language and control flow

- `Data`
- `Dim`
- `Do`
- `Else If`
- `End If`
- `Exit Do`
- `ElseIf`
- `Case Else`
- `Else`
- `Select Case`
- `End Select`
- `Case`
- `EndIf`
- `End Function`
- `End Sub`
- `End`
- `Error`
- `Exit For`
- `Exit Sub`
- `Exit Function`
- `Exit`
- `For`
- `Function`
- `GoSub`
- `GoTo`
- `Inc`
- `If`
- `Let`
- `Local`
- `Loop`
- `Next`
- `Print`
- `Read`
- `Rem`
- `Restore`
- `Return`
- `Static`
- `Sub`
- `While`
- `Const`
- `Randomize`

### Graphics and display

- `Pixel`
- `Circle`
- `Line`
- `Box`
- `RBox`
- `CLS`
- `Font`
- `Triangle`
- `Arc`
- `Polygon`
- `FASTGFX`
- `Color`
- `Colour`
- `Text`

### Runtime and utility

- `Pause`

## Partial

### Core language and program statements

- `Line Input`
  VM support exists for the forms currently used by tests and file I/O, not the full legacy family.
- `MID$(`
  The string-function surface exists, but the full legacy command/function-hybrid behavior should be treated as incomplete.

### File and storage

- `Open`
  Sequential file forms are implemented. Random-access, `COM`, `GPS`, and other legacy forms are not.
- `Close`
  File-handle close is implemented. Broader legacy close-family behavior is not.
- `Kill`
  Native file delete exists, but the full legacy storage surface is broader.
- `Rmdir`
  Basic directory removal exists, but the legacy family is broader.
- `Chdir`
  Basic directory change exists, but the legacy family is broader.
- `Mkdir`
  Basic directory creation exists, but the legacy family is broader.
- `Copy`
  Basic file copy exists, but the legacy family is broader.
- `Rename`
  Basic rename exists, but the legacy family is broader.
- `Seek`
  Basic file seek exists, but the legacy family is broader.
- `Drive`
  Basic drive selection/path handling exists, but the legacy family is broader.
- `Files`
  Native VM listing support exists now, but the full legacy behavior and option surface should still be treated as incomplete.
- `Save`
  Only `SAVE IMAGE` is currently frontended.

### Time, date, and misc hybrids

- `Timer`
  The function form is implemented; broader legacy behavior is not fully matched.
- `Date$`
  The function/value form is implemented; broader legacy command-family behavior is not fully matched.
- `Time$`
  The function/value form is implemented; broader legacy command-family behavior is not fully matched.

### Graphics and display (partial)

- `FRAMEBUFFER`
  Current VM slice covers the LCD-style `CREATE`, `LAYER [colour]`, `WRITE N/F/L`,
  `CLOSE [F/L]`, `COPY N/F/L, N/F/L [,B]`, `MERGE [colour][,B|R|A][,rate]`,
  `SYNC`, and `WAIT` forms. RP2350/HDMI/VGA extensions such as `CREATE 2`,
  `LAYER TOP`, and `WRITE/CLOSE/COPY` targets `2`/`T` remain unimplemented.

### Audio

- `Play`
  Only `PLAY TONE` and `PLAY STOP` are implemented.

### Pin and peripheral control

- `Pin(`
  Basic digital read/write support exists. The full legacy `PIN()` family does not.
- `SetPin`
  Basic digital plus the currently implemented PWM/servo-related slice exist. The full legacy mode family does not.
- `PWM`
  The currently needed command slice exists, including sync/off forms. The full legacy family does not.
- `Servo`
  The currently needed command slice exists, including off/basic channel control. The full legacy family does not.

### Options

- `Option`
  Only the source forms already needed by the VM path are handled, not the broad legacy `OPTION` surface.

## Unimplemented

### Core/editor/program control

- `Call`
- `Clear`
- `Continue`
- `Erase`
- `Input`
- `List`
- `Load`
- `On`
- `Run`
- `Trace`
- `Execute`
- `New`
- `Edit File`
- `Edit`
- `Autosave`
- `Chain`

### File, storage, and OS families

- `Flash`
- `VAR`
- `Flush`
- `XModem`
- `Cat`

### Graphics, framebuffer, UI, and display families

- `Sprite`
- `Blit`
- `Blit Memory`
- `GUI`
- `SYNC`
- `Device`
- `LCD`
- `Refresh`
- `TILE`
- `MODE`
- `Map(`
- `Map`
- `Colour Map`
- `Camera`
- `CtrlVal(`
- `Backlight`
- `Draw3D`

### Peripheral, comms, and hardware families

- `PIO`
- `ADC`
- `Pulse`
- `Port(`
- `IR`
- `I2C`
- `I2C2`
- `RTC`
- `Math`
- `Memory`
- `IReturn`
- `Poke`
- `SetTick`
- `WatchDog`
- `CPU`
- `Sort`
- `DefineFont`
- `End DefineFont`
- `LongString`
- `Interrupt`
- `Library`
- `OneWire`
- `TEMPR START`
- `SPI`
- `SPI2`
- `WS2812`
- `Keypad`
- `Humid`
- `Wii Classic`
- `Wii Nunchuck`
- `Wii`
- `Mouse`
- `Gamepad`
- `Configure`
- `WEB`
- `Update Firmware`
- `CMM2 Load`
- `CMM2 Run`
- `Ram`

### CSUB / native extension families

- `CSub`
- `End CSub`

### PIO assembler families

- `_wrap target`
- `_wrap`
- `_line`
- `_program`
- `_end program`
- `_side set`
- `_label`
- `Jmp`
- `Wait`
- `In`
- `Out`
- `Push`
- `Pull`
- `Mov`
- `Nop`
- `IRQ SET`
- `IRQ WAIT`
- `IRQ CLEAR`
- `IRQ NOWAIT`
- `IRQ`
- `Set`

### Command/function hybrids and utility families

- `Byte(`
- `Flag(`
- `Bit(`
- `Flags`
- `Help`
- `Array Slice`
- `Array Insert`
- `Array Add`
- `Array Set`
- `/*`
- `*/`

## Notes

- `RUN` is intentionally `unimplemented` as a BASIC program command on the VM path even though device shell `RUN` exists. The shell command loads source and hands execution to the VM; that is not the same thing as supporting `RUN` inside a BASIC program.
- `FILES` is no longer a VM runtime no-op. It remains `partial` because the command family is not yet treated as full legacy parity.
- `FRAMEBUFFER` is `partial` because the LCD-style dual-buffer forms are implemented but HDMI/VGA extensions remain unimplemented.
- `SETPIN`, `PIN(`, `PWM`, and `Servo` all have meaningful native VM support now, but only for the currently implemented slice.
- This matrix is about commands only. Function backlogs outside the command table, such as the wider `MM.INFO()` surface, are tracked separately.
