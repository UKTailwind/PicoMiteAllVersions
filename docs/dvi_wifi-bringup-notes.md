# dvi_wifi_rp2350 — bring-up debugging notes

Snapshot of what's been traced, what's confirmed broken/fixed, and where
the open problem sits. The port combines HDMI HSTX scanout + USB host
keyboard + CYW43 WiFi (RM2 module) + I²S audio on RP2350B — a four-way
combination that has never been validated together in any predecessor
build.

## Hardware / build context

- Board: Pimoroni Pico Plus 2 W (RP2350B + RM2 / CYW43439)
- RM2 wires CYW43 SPI on dedicated GPIOs GP23/24/25/29 (not on QSPI)
- HDMI on HSTX, claims DMA channels 0 + 1 (chained scanout)
- USB host on RP2350 native USB controller (TinyUSB)
- I²S audio on PIO2 (SM 1) with PWM_IRQ_WRAP for sample tick
- CYW43 SPI on PIO0 or PIO1 (dynamic claim)

## Confirmed fixed

### Pin-table out-of-bounds (`PinDef[]` vs `PINMAP[]`)

`ports/dvi_wifi_rp2350/pin_tables.c` originally included only:

```
PINDEF_BLOCK_HEADER_AND_GP0_15
PINDEF_BLOCK_PINS_16_25_HDMI
PINDEF_BLOCK_PINS_26_40
```

`PINMAP[]` (the GPIO→PinDef-index lookup) is shared across ports and
references entries in `PSEUDO_GP23_29` (indices 41-44) and
`PSEUDO_RP2350_EXTRAS` (indices 45-62). With those blocks omitted,
`PINMAP[33] = 48` → `PinDef[48]` reads past the array end OR returns the
wrong row (because `PinDef[]` is indexed by C array position, not by the
`pin` field stored inside each row).

Symptom: `OPTION AUDIO I2S GP33, GP32` was accepted but the resulting
`PinDef[48].slice` returned garbage, persisted to `Option.AUDIO_SLICE`
in flash. On boot, `start_i2s` programmed PWM slice 0 (a garbage value)
which collided with `hal_fast_timer`'s use of slice 0. Other GP30+
operations would silently target the wrong physical pin.

Fix: include both pseudo-pin blocks. GP23-29 entries stay as
placeholders (CYW43 owns them at runtime; users can't usefully OPTION
them) so array positions 41-44 are filled and PINMAP indexing stays
dense. Documented in `docs/adding-a-new-port.md` under Step A2.

### XIP cache stale across `sysresetreq`

Tangential but cost time during this session. RP2350's XIP cache is
not invalidated by `sysresetreq`. After re-flashing via SWD, the CPU
fetched stale instruction bytes from cache for code lines that hadn't
been re-touched by execution since the prior firmware. Surfaced as
`UNDEFINSTR` HardFault at addresses whose ELF disasm decoded to valid
instructions. Documented in `docs/adding-a-new-port.md`. Workaround
during debug: write byte 0 to each address in `0x18FFC000..0x18FFFFF8`
step 8 (invalidate-by-set-way) before `reset run`.

## Misconceptions corrected

### `MM.INFO(WIFI STATUS) = 1` is the steady-state value

`cyw43_wifi_link_status()` returns `CYW43_LINK_JOIN` (=1) when
`wifi_join_state` has only the `WIFI_JOIN_STATE_ACTIVE` bit set. After
a *successful* full join, the driver state machine sets
`WIFI_JOIN_STATE_ALL` (= ACTIVE | AUTH | LINK | KEYED), then
**resets back to `ACTIVE` alone** while calling
`cyw43_cb_tcpip_set_link_up`. So `WIFI STATUS = 1` is post-success
steady state, not "associated but not yet up".

The header's docstring `// Connected to wifi` is misleading without
this context. The genuinely-up indicator is
`MM.INFO(TCPIP STATUS) = 3` (`CYW43_LINK_UP`).

We chased the SPI clock divider for ~hours under the wrong assumption
that 1 was a failure state.

### Legacy "this worked" is not apples-to-apples

The user's recollection that legacy firmware ran `server.bas` reliably
is true for `WEBRP2350` (WiFi + SPI LCD, no HSTX, no USB host, no
I²S). It is **not** a baseline for the dvi_wifi triple-combo:

| Subsystem        | Legacy WEBRP2350 | dvi_wifi_rp2350 |
|------------------|------------------|-----------------|
| Display          | SPI LCD          | HSTX HDMI       |
| Display DMA load | bursty           | continuous chained |
| USB host         | no               | yes             |
| I²S audio        | no               | yes             |
| CYW43 backend    | `_lwip_poll`     | started threadsafe-bg, swapped to `_lwip_poll` in this branch |

The continuous HSTX scanout DMA is the single biggest difference.

## Open problem

`server.bas` (WebMite framework) doesn't reliably serve requests on
dvi_wifi. Observed:

1. Cold boot, REPL only — `curl http://chip:8080/` returns
   `HTTP/1.0 404` from the firmware-side fallback in
   `tcp_server_recv` (the `if(!CurrentLinePtr)` branch). **TCP path
   from L2 up through send works** in this state.
2. `RUN "B:server.bas"` → curl times out. Earlier RTT logs showed
   `[TCP] accept fired` + `[TCP] recv pcb=0 totlen=80 err=0 clp=...`
   but no completion / response. So the request reaches the BASIC
   path (`buffer_recv` allocated, `inttrig=1` set) but the response
   never gets back to the client.
3. `CTRL-C` server.bas → curl still times out, even after returning
   to REPL. Reboot needed to recover. **Indicates state leak across
   program exit.**

### Asymmetry diagnostic

Why the firmware-side 404 path works but the BASIC-side response doesn't:

```c
if (!CurrentLinePtr) {
    // 404 path: static const httpheadersfail string,
    // immediate tcp_write + tcp_close. No BASIC-heap allocation.
} else {
    // BASIC path: GetMemory(p->tot_len) for buffer_recv,
    // sets inttrig, BASIC framework reads later via WEB TCP READ.
}
```

The 404 path is stateless and uses static strings. The BASIC path
allocates from the MMBasic heap inside the lwIP recv callback.

Originally on `_lwip_threadsafe_background`, the recv callback fires
from the alarm-pool timer IRQ — async with the main thread that
"owns" the BASIC heap. Calling `GetMemory()` from that context is
not safe.

Switching to `_lwip_poll` (matching legacy) makes recv fire on the
main thread inside `cyw43_arch_poll()`. That should fix the heap
race. After the switch, request still hangs — the bug is no longer
heap reentrance.

### State leak after CTRL-C

`do_end()` in `Commands.c` doesn't tear down WEB-server state. After
CTRL-C, the following stay set:

- `state->client_pcb[]` entries from any in-flight connections
- `state->buffer_recv[]` allocated buffers (memory leak, not a
  correctness blocker by itself)
- `state->inttrig[]`
- `TCPreceived`, `TCPreceiveInterrupt`
- `CurrentLinePtr` — *not* reset when longjmp lands in REPL setjmp,
  so subsequent recv handlers see non-zero clp and route to the
  BASIC path even though no program is running. This makes the
  firmware-side 404 path **unreachable** post-CTRL-C.

Tried adding `cleanserver()` + `TCPreceived = 0` +
`TCPreceiveInterrupt = NULL` + `CurrentLinePtr = NULL` in `do_end`.
Did not visibly change behavior in the latest test. May not have
been hit if the program exit path doesn't go through `do_end` (e.g.,
error landing might use a different path).

## Configurations tried

CYW43 PIO clock divider sweep (CPU range 252-378 MHz):

| div | SPI rate @252 MHz | @360 MHz | observation |
|-----|-------------------|----------|-------------|
| 3   | 84 MHz            | 120 MHz  | over spec; user reports "errored out" |
| 4   | 63 MHz            | 90 MHz   | associates; bus errors visible (`hdr mismatch`, `do_ioctl timeout`) |
| 5   | 50 MHz            | 72 MHz   | at spec ceiling; intermittent recv |
| 6   | 42 MHz            | 60 MHz   | needs `spi_gap0_sample1` PIO program; with that, accepts fire reliably |
| 8   | 31 MHz            | 45 MHz   | low cpu speed program; chip alive but no TCP events delivered |
| 9   | 28 MHz            | 40 MHz   | failed to associate at one CPU |

CYW43 PIO program: `spi_gap01_sample0` (default, "for high cpu speed")
vs `spi_gap0_sample1` ("for lower cpu speed", commented out in SDK).
Switching to `spi_gap0_sample1` was needed for any divider ≥ 6.

`pico_cyw43_arch_lwip_threadsafe_background` vs `pico_cyw43_arch_lwip_poll`:

- threadsafe_background: ARP/SYN/recv fires sometimes; race on
  `CurrentLinePtr` because callbacks fire from IRQ context; race on
  BASIC heap from `GetMemory()` in recv callback.
- poll: ProcessWeb pumps `cyw43_arch_poll()` every CheckAbort tick
  (~30k+/s observed via RTT). Callbacks fire on main thread, so
  `GetMemory()` is safe. Earlier failures with poll mode were before
  the priority/divider tuning was right.

DMA priority bump on CYW43 channels (in MMsetwifi.c WebConnect after
cyw43_arch_init):

```c
for (int ch = 2; ch < 12; ch++) {
    if (dma_channel_is_claimed(ch)) {
        dma_hw->ch[ch].al1_ctrl |= DMA_CH0_CTRL_TRIG_HIGH_PRIORITY_BITS;
    }
}
```

Sets the HIGH_PRIORITY bit on every claimed DMA channel except 0/1
(which are HDMI scanout). Theory: HSTX chained-DMA was starving
CYW43's TX/RX DMA channels → PIO TX FIFO underruns mid-frame → the
"hdr mismatch" errors. Effect not yet conclusively tested in
isolation.

## Diagnostic infrastructure

### SEGGER RTT over SWD

Linked `pico_stdio_rtt` and routed `CYW43_PRINTF` to `printf` so all
`CYW43_INFO` / `CYW43_WARN` / (with override) `CYW43_DEBUG` lines land
in an in-RAM ring buffer that openocd reads via SWD. No HDMI / display
side effects, no IRQ-context concerns, no risk of crashing the chip.

Setup commands (openocd telnet on 4444):

```
rtt setup 0x20000000 0x80000 "SEGGER RTT"
rtt start
rtt server start <port> 0
```

Then `nc localhost <port>` from the host. Or read the buffer
directly via `mdw 0x<rtt_buffer_ptr> 256` — control block is at
`_SEGGER_RTT` (look up via `arm-none-eabi-nm`); buffer descriptor at
offset 0x18, contents at `*(addr+0x1c)`.

### CYW43 driver async-event tracer

```c
cyw43_state.trace_flags |= CYW43_TRACE_ASYNC_EV;
```

Set in `WebConnect()` before `cyw43_arch_wifi_connect_timeout_ms`.
Logs every firmware async event (AUTH / ASSOC / LINK / PSK_SUP /
JOIN / DISASSOC / etc) with status / reason / interface. This is how
we confirmed the join sequence completes cleanly.

Decoded full join sequence on this board:

```
ASYNC(0000,ASSOC_REQ_IE,0,0,0)
ASYNC(0000,AUTH,0,0,0)
ASYNC(0000,ASSOC_RESP_IE,0,0,0)
ASYNC(0000,ASSOC,0,0,0)
ASYNC(0001,LINK,0,0,0)
ASYNC(0000,PSK_SUP,6,0,0)    <- WLC_SUP_KEYED, sets KEYED bit
ASYNC(0000,JOIN,0,0,0)
ASYNC(0000,SET_SSID,0,0,0)
```

After PSK_SUP=6, state hits `WIFI_JOIN_STATE_ALL` and the driver
calls `cyw43_cb_tcpip_set_link_up`. Confirms WiFi join is fully
healthy.

## Suggested directions for next time

1. **Bisect by disabling subsystems.** Build with HSTX scanout
   commented out (no HDMI), confirm `server.bas` works. Then re-enable
   HSTX and see if it specifically breaks. If yes, HSTX is the
   variable that needs handling.
2. **Logic analyzer on GP23/24/25/29.** See whether SPI frames are
   actually corrupted on the wire. `hdr mismatch` warnings in the
   driver suggest yes, but a scope confirms vs internal-state issue.
3. **Pre-allocate `state->buffer_recv[]` once at server-start.**
   Removes the `GetMemory()` call from the recv callback entirely.
   Avoids both heap reentrance and allocator-failure-under-load
   issues. ~2 KB × MaxPcb fixed cost in BSS.
4. **Audit `do_end()` cleanup.** Confirm with SWD that the cleanup
   added in this branch actually executes on CTRL-C. The hang-after-
   CTRL-C symptom suggests it doesn't.
5. **Review HDMI scanout DMA chain pacing.** If the chain has any
   knob to insert idle gaps (frame-pacing, CB rate limit), that gives
   CYW43 DMA more bus headroom without lowering pixel rate.

## Local-only changes accumulated in this branch

(May want to reduce to just the load-bearing one before merging.)

- `ports/dvi_wifi_rp2350/pin_tables.c` — added GP23-29 + EXTRAS
  blocks. **Real bug fix, keep.**
- `ports/dvi_wifi_rp2350/port_config.h` — `HAL_PORT_DEFAULT_CPU_SPEED_KHZ = 378000`.
- `ports/dvi_wifi_rp2350/port_sources.cmake` — `CYW43_PIO_CLOCK_DIV_INT=8`,
  `CYW43_SPI_PROGRAM_NAME=spi_gap0_sample1`, `pico_cyw43_arch_lwip_poll`,
  `pico_stdio_rtt`, `CYW43_PRINTF=printf` debug overrides.
- `MMsetwifi.c` — DMA priority bump, async-event trace flags.
- `MMtelnet.c` — `cyw43_arch_poll()` + heartbeat in ProcessWeb.
- `MMtcpserver.c` — `printf` traces in accept / open / recv.
- `Commands.c` — TCP cleanup added to `do_end()` (cleanserver,
  TCPreceive*, CurrentLinePtr).
