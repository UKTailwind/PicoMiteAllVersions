# Stage 6.6 — LPT1 / Centronics

Status: complete.

The PC386 port supports printer output through the existing MMBasic file commands:

```basic
OPEN "LPT1:" FOR OUTPUT AS #1
PRINT #1, "hello";
CLOSE #1
```

There is no `LPRINT` command in the current shared MMBasic command table, and this stage deliberately does not add one. `LPT1:` is handled as a PC386-only special file target inside the filesystem HAL, so `OPEN`, `PRINT #`, and `CLOSE` remain the integration surface.

Implementation notes:

- `drivers/lpt_centronics/` owns the raw parallel-port write path.
- The driver pulses STROBE and waits for the printer-ready status bit with a bounded timeout.
- The same LPT latch/control helpers back both printer output and user GPIO.
- QEMU validation uses `-parallel file:<tmp>/lpt1.out` and byte-compares the captured printer stream.

Validation:

- `make -C ports/pc386`
- `python3 ports/pc386/tests/repl_expect.py lpt1_print`
- Full PC386 harness: `31/31`
