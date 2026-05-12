# Stage 6.5 — LPT1 GPIO

Status: complete.

PC386 exposes the first parallel port (`LPT1`, base I/O address `0x378`) as user-addressable BASIC GPIO. The BASIC pin number is the DB-25 connector pin number:

- Data outputs: `2..9`
- Control outputs: `1`, `14`, `16`, `17`
- Status inputs: `10`, `11`, `12`, `13`, `15`

The implementation hides the PC parallel-port inversions. `PIN(11)` returns the connector-level BUSY signal, not the inverted status-register bit, and control-pin `PIN()`/`PIN()=` also use connector-level logic.

Implemented surfaces:

- `SETPIN n, DOUT` for data/control pins.
- `SETPIN n, DIN` for status pins.
- `SETPIN n, OFF`.
- `PIN(n)=value` writes output pins.
- `PIN(n)` reads input pins or the output latch.
- Bytecode VM pin syscalls use the same backend as the REPL commands.

Invalid direction requests intentionally error: status pins are read-only, and data/control pins are output-only on this first implementation. Bidirectional/ECP/EPP data mode is not part of this stage.

Validation:

- `make -C ports/pc386`
- `python3 ports/pc386/tests/repl_expect.py lpt1_gpio`
- Full PC386 harness: `31/31`
