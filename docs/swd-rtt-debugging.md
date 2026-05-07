# SWD + RTT debugging on RP2350

How to use a CMSIS-DAP debug probe (Pico Probe / picoprobe firmware on a
spare Pico) plus OpenOCD plus SEGGER RTT to debug the firmware without
relying on the BASIC console, HDMI display, or USB stdio. Useful when
the chip is in a hard fault, when display init has crashed, or when you
need to capture log output that's hostile to MMPrintString (IRQ context,
display-down state, etc).

## Wiring

| Probe pin | Target pin |
|-----------|------------|
| SWCLK     | SWCLK on RP2350 (board-specific) |
| SWDIO     | SWDIO |
| GND       | GND |
| (optional) UART RX | TX of target's serial console |

Power the target board separately. Don't drive 3V3 from the probe.

## OpenOCD setup

```sh
~/.pico-sdk/openocd/0.12.0+dev/openocd \
    -s ~/.pico-sdk/openocd/0.12.0+dev/scripts \
    -f interface/cmsis-dap.cfg -c "adapter speed 5000" \
    -f target/rp2350.cfg
```

Listens on:

- `:3333` — gdb remote protocol
- `:4444` — telnet command interface (used by everything below)
- `:6666` — Tcl interface

Lower `adapter speed` (e.g. 1000) if SWD reads return errors or
"cannot read IDR".

## Talking to OpenOCD

The telnet interface accepts one command per line. Driving it from a
shell script is brittle — the conventional `nc` heredoc with `==` in a
prompt will trip Zsh's history-expansion. Two reliable patterns:

```sh
# Single command
echo "halt" | nc -w 3 localhost 4444

# Script via file
cat > /tmp/cmd.txt <<'EOF'
halt
reg pc
exit
EOF
nc -w 4 localhost 4444 < /tmp/cmd.txt
```

Always end with `exit` so `nc` returns promptly.

## Halt / resume / reset

```
halt                        # stop the CPU at its current PC
resume                      # continue from where it stopped
reset run                   # POR-style reset, run from reset vector
reset halt                  # reset and halt at reset vector (PC = 0xa0)
```

`reset halt` is the only way to reliably get a fresh CPU state when the
chip is wedged in a HardFault loop. SoftReset (`sysresetreq`) does
**not** invalidate the XIP cache; `reset halt` via OpenOCD does the
same SoftReset under the hood, so cache is also stale after it. See
"XIP cache invalidation" below.

## Reading registers

```
reg pc
reg lr
reg sp
reg msp
reg xpsr
reg r0
reg r4
```

`reg` lists everything if called with no argument.

After halting in HardFault, the relevant decoded values:

- `xPSR & 0x1ff` is `IPSR` — the active exception number. 3 = HardFault,
  others are external IRQ + 16. T-bit at position 24 must always be 1
  for a valid Cortex-M state; T=0 in a stacked frame is a smoking gun
  for stack corruption.
- `LR` after exception entry holds `EXC_RETURN`:
  - `0xFFFFFFE9` = Thread mode, MSP, FP context stacked
  - `0xFFFFFFF9` = Thread mode, MSP, no FP context
  - `0xFFFFFFFD` = Thread mode, PSP
  - `0xEFFFFFFE` = LOCKUP signal (CPU double-faulted, escalated to lockup state)

## Reading memory

```
mdw <addr> <count>          # words (4 bytes each)
mdh <addr> <count>          # halfwords
mdb <addr> <count>          # bytes
```

`mdw` shows the LE word value at each address. To get bytes in memory
order, reverse each shown word: `mdw 0x10001234 1` showing `aabbccdd`
means bytes `dd cc bb aa` at increasing addresses.

For PIO programs, addresses, function pointers — `mdw` is right (you
want the word value). For ASCII strings or instruction byte streams,
think in bytes.

## Setting hardware breakpoints

```
bp <addr> 2 hw              # 2-byte instruction breakpoint
bp <addr> 4 hw              # 4-byte (Thumb-2 32-bit instruction)
rbp all                     # remove all
rbp <addr>                  # remove specific
```

Cortex-M FPB only supports 2-byte breakpoint widths on RP2350 — even on
Thumb-2 32-bit instructions, use `2`. The HW BP fires on instruction
fetch at the address regardless of the instruction's actual width.

Address must be the actual instruction byte (clear the Thumb LSB if
you're working from a function-pointer value): `0x10001235` is a valid
function pointer to a Thumb instruction at `0x10001234`; the BP goes
on `0x10001234`.

There are 8 hardware breakpoint slots on RP2350. Software breakpoints
in flash require flash patching and are best avoided.

## Vector catch (halt at fault entry)

```
cortex_m vector_catch hard_err int_err bus_err state_err chk_err nocp_err mm_err
```

Halts the CPU at the fault handler's first instruction *before* the
handler runs. Lets you see the fresh stacked frame (R0..R3, R12, LR,
PC, xPSR pushed by hardware) at the exact moment of the fault, before
any custom HardFault handler corrupts the state.

Don't include `reset` in the catch list unless you specifically want to
halt at the reset vector — it'll halt every reset including the one
you're about to do.

## Reading SCB fault registers

```
mdw 0xE000ED28 1            # CFSR — composite of MMFSR/BFSR/UFSR
mdw 0xE000ED2C 1            # HFSR
mdw 0xE000ED30 1            # DFSR
mdw 0xE000ED34 1            # MMFAR — valid only if CFSR.MMARVALID
mdw 0xE000ED38 1            # BFAR  — valid only if CFSR.BFARVALID
```

CFSR layout:
- bits 0-7   = MMFSR (MemManage)
- bits 8-15  = BFSR (BusFault) — bit 9 PRECISERR, bit 15 BFARVALID
- bits 16-25 = UFSR (UsageFault) — bit 16 UNDEFINSTR, bit 17 INVSTATE,
  bit 18 INVPC, bit 19 NOCP, bit 20 STKOF (CM33 only)

CFSR is sticky — flags persist until you write 1-to-clear. So a
"BFSR + UFSR" reading can be one fault, two faults, or stale state from
an earlier fault. HFSR.FORCED (bit 30) means a configurable fault
escalated to HardFault because the corresponding handler was disabled
or chained.

## Decoding the stacked exception frame

After exception entry on Cortex-M33:

| LR (EXC_RETURN) | FType (bit 4) | Frame size |
|-----------------|---------------|------------|
| `0xFFFFFFE9`    | 0 (FP stacked) | 104 bytes  |
| `0xFFFFFFF9`    | 1 (no FP)      | 32 bytes   |

Without FP frame, basic frame is at MSP+0..MSP+31:

```
MSP+0  : R0
MSP+4  : R1
MSP+8  : R2
MSP+12 : R3
MSP+16 : R12
MSP+20 : LR (caller's LR at fault time)
MSP+24 : PC (faulting instruction)
MSP+28 : xPSR (must have T-bit set if valid)
```

With FP frame, basic frame is **at MSP+0..MSP+31** still, FP regs
follow at MSP+32..MSP+103 (S0-S15, FPSCR, reserved). Some references
get this order backwards; verify by checking the T-bit in stacked
xPSR — if the supposed frame puts T=0 in xPSR position, you've got
the offsets wrong.

Reading PC + LR from the stacked frame and resolving with addr2line
identifies the faulting instruction:

```sh
arm-none-eabi-addr2line -fie build/PicoMite.elf 0xPC 0xLR
```

## SEGGER RTT for log output

RTT (Real-Time Transfer) gives the chip a way to write log strings
into a ring buffer in RAM. OpenOCD reads the buffer over SWD and
forwards bytes to a TCP socket. The chip writes are non-blocking,
IRQ-safe, and don't depend on display / USB / serial / WiFi being
up. Ideal for fault diagnosis where MMPrintString isn't usable.

### Setup at link time

In the port's `port_sources.cmake`:

```cmake
target_link_libraries(PicoMite ... pico_stdio_rtt)
pico_enable_stdio_rtt(PicoMite 1)
```

`pico_stdio_rtt` registers itself as a stdio driver on `stdio_init_all()`,
so anything that writes to `stdout` (including bare `printf`) lands in
the RTT ring buffer.

For redirecting CYW43 driver chatter — the SDK's `CYW43_PRINTF` macro
defaults to `printf`, so it already routes to RTT once `pico_stdio_rtt`
is linked. To enable the SDK's `CYW43_DEBUG` calls (NDEBUG-gated by
default):

```cmake
target_compile_options(PicoMite PRIVATE -DCYW43_DEBUG=printf)
```

Caveat: enabling `CYW43_VERBOSE_DEBUG=1` produces logs from inside the
PIO SPI per-byte path. With a 1 KB ring buffer, that overflows
instantly and overwrites useful messages. Stick with `CYW43_DEBUG`.

### Setup at runtime (OpenOCD side)

```
rtt setup 0x20000000 0x80000 "SEGGER RTT"
rtt start
rtt server start 9090 0
```

`rtt setup` searches the given address range for the magic string
`SEGGER RTT` to locate the control block. Once found, `rtt start`
begins polling. `rtt server start <port> <channel>` exposes channel
0 (= stdio output) on TCP `<port>`.

```sh
nc localhost 9090
```

Reads the streamed log. RTT polling defaults to 100 ms; tighten with
`rtt polling_interval 50`.

The control-block address is in the ELF as `_SEGGER_RTT`:

```sh
arm-none-eabi-nm build/PicoMite.elf | grep '_SEGGER_RTT$'
```

The address moves between builds. Re-run `rtt setup` after each
re-flash. Servers from a prior run linger across `rtt setup` and need
explicit `rtt server stop <port>`.

### Reading the buffer directly

When the OpenOCD RTT server is uncooperative (it sometimes is), read
the buffer via `mdw`:

```
mdw 0x<rtt_cb_addr> 16          # control block: magic + sizes + descriptors
```

Buffer descriptor layout (one per UP channel):

```
+0  : char *sName            (e.g. "Terminal")
+4  : char *pBuffer          (start of ring storage)
+8  : uint32 SizeOfBuffer
+12 : uint32 WrOff           (writer position)
+16 : uint32 RdOff           (reader position)
+20 : uint32 Flags
```

`pBuffer` and `SizeOfBuffer` give you the actual log storage:

```
mdw <pBuffer> <SizeOfBuffer/4>
```

If `WrOff` is advancing but `RdOff` stays at 0, OpenOCD isn't reading.
If both stay at 0, the chip isn't writing — either no `printf` calls
fired or `pico_stdio_rtt` wasn't linked correctly. If both are equal
and not 0, OpenOCD has drained everything that was written.

Decode the bytes by interpreting each LE word as 4 ASCII bytes (LSB
first).

## XIP cache invalidation

RP2350's XIP cache is **not invalidated by `sysresetreq`** — the
SoftReset path firmware uses, and what OpenOCD's `reset` ultimately
issues. After re-flashing via SWD, the CPU may execute stale
instructions from cache for code lines that haven't been re-touched
since the prior firmware ran. Symptom: `UNDEFINSTR` HardFault at
addresses whose ELF disasm decodes to a valid instruction, with the
on-chip bytes (read via the cached XIP alias) differing from the bytes
in flash (read via the uncached alias).

Compare cached vs uncached:

```
mdw 0x1003eea0 4            # cached
mdw 0x1403eea0 4            # uncached XIP alias (XIP_NOCACHE_NOALLOC)
```

If they differ, the cache is stale.

Invalidate the entire 16 KB cache via the maintenance address space
(invalidate-by-set-way):

```sh
addr=$((0x18FFC000))
end=$((0x18FFFFF8))
{
  while [ $addr -le $end ]; do
    printf "mwb 0x%x 0\n" $addr
    addr=$((addr + 8))
  done
  echo exit
} | nc -w 25 localhost 4444
```

Each byte write at `XIP_MAINTENANCE_BASE + offset + op` invalidates one
cache line (op=0 = `BY_SET_WAY`). After the loop, `reset run` and the
chip will fetch fresh from flash.

## Working flash via OpenOCD

```
program <path-to-elf> verify        # erase + write + verify
program <path-to-elf> verify reset  # then reset and run
```

The Pico SDK's flash routines run from RAM during programming. OpenOCD
uses the same routines via the bootrom. ELF or BIN both work; the
ELF format encodes the load address.

Verify is fast and worth always including — without it, a partial
flash failure manifests as a corrupt-looking firmware later.

## gdb-side debugging

If you want full source-level debugging (single-step, breakpoint by
function name, locals, backtrace), connect gdb to OpenOCD's `:3333`:

```sh
arm-none-eabi-gdb -batch -nx \
    -ex "target extended-remote :3333" \
    -ex "monitor halt" \
    -ex "info registers" \
    -ex "bt 30" \
    build/PicoMite.elf
```

Note: gdb's `monitor` command pass-through to OpenOCD breaks in some
combinations of OpenOCD + gdb versions on RP2350 (you'll see "monitor
command not supported by this target"). When that happens, drive
OpenOCD via the telnet interface directly instead.

## Common pitfalls

- **Address moves after rebuild.** `_SEGGER_RTT`, ELF function
  addresses, BSS variable addresses all shift between builds. Always
  re-resolve via `nm` or `addr2line` after a clean rebuild — comparing
  the address from `arm-none-eabi-nm build_a/...` to behavior in a
  flash from `build_b/...` will mislead.
- **Stale OpenOCD RTT setup.** `rtt setup` after re-flash needs to be
  re-issued so OpenOCD re-locates the control block at its new
  address. Otherwise reads come from wherever the old block was —
  might decode as garbage or as old buffer contents.
- **SWD comms fail under load.** When the target is in a
  HardFault-loop or LOCKUP state, OpenOCD's halt request can time out
  ("target was in unknown state when halt was requested"). Lower
  `adapter speed`, retry `halt`, or power-cycle the board.
- **HW breakpoints don't survive `sysresetreq` on some configs.** If a
  BP set before `reset run` doesn't fire, re-issue the `bp` command
  after the reset.
- **MMPrintString is not IRQ-safe.** Don't redirect CYW43_PRINTF to
  MMPrintString or any MMBasic-touching function. The driver fires
  log macros from PIO/DMA/timer IRQ paths. RTT writes are pure
  memory pokes and IRQ-safe.
- **printf format mismatches in the SDK.** The pico-sdk has format
  strings using `%x` for `uint32_t` arguments — clean in NDEBUG
  (compiled out) but produce warnings (and at `-Werror=format`,
  errors) when CYW43_DEBUG is enabled. Add `-Wno-error=format
  -Wno-format` to compile options for the affected target.
