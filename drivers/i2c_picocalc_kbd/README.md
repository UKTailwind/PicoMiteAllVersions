# drivers/i2c_picocalc_kbd — PicoCalc I²C keyboard driver

Initialises the I²C bus that the PicoCalc's onboard keyboard controller
(address 0x1F) sits on, and provides helpers for battery / charging /
keyboard-backlight reads that share the same bus.

Entry points:
- `init_i2c_kbd()` — idempotent one-time I²C bus setup (pull-ups, baud).
  Called from MM_Misc.c's `MM.BATTERY` / `MM.CHARGING` / `OPTION BACKLIGHT KB`
  handlers before issuing each read.
- `read_battery()`, `set_kbd_backlight()` — utility I²C reads/writes
  for the battery-gauge and keyboard-backlight registers.
- `read_i2c_kbd()` — scancode poll; currently not wired up (keyboard
  polling on PicoCalc goes through the generic `I2C.c::CheckI2CKeyboard`
  from `PicoMite.c::routinechecks`, not through this function).

Linked only when CMake is invoked with `-DPICOCALC=true` per
CMakeLists.txt.

## Lifted from

`picocalc/i2ckbd.c` + `picocalc/i2ckbd.h` (repo root, pre-Phase-5
refactor). The `#include "../MMBasic.h"` / `"../Hardware_Includes.h"`
relative paths were changed to plain `"MMBasic.h"` / `"Hardware_Includes.h"`
since repo root is already on the compiler's `-I` path.

## Future work

- Fold `CheckI2CKeyboard` (currently in `I2C.c`) into this driver, so
  the PicoCalc keyboard poll path stops living inside the generic I²C
  command file.
- Driver conformance tests per `docs/real-hal-plan.md`.
