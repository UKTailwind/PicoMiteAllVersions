# ESP32-S3 serial console latency — static-analysis investigation

Branch: `web-console-driver`. No hardware was run. Findings come from reading
`ports/esp32_s3_metro/main/esp32_console.c`,
`ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c`,
`ports/esp32_s3_metro/main/app_main.c`,
`ports/esp32_s3_metro/main/hal_vm_framebuffer_esp32_stub.c`,
`ports/esp32_s3_metro/sdkconfig`, the matching ESP-IDF sources under
`~/esp/esp-idf/components/esp_driver_usb_serial_jtag/`, and the MMBasic
core (`MMBasic_Prompt.c`, `gfx_console_shared.c`, `MMBasic_REPL.c`).

## Symptom and reference frame

The user reports typing in the BASIC REPL feels sluggish on the host serial
terminal (`idf.py monitor`, `screen /dev/cu.usbmodem… 115200`). The 115200
baud rate is irrelevant: ESP-IDF is configured with
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (sdkconfig confirms it; see below).
The serial pipe is USB-CDC over the chip's built-in USB-Serial-JTAG block,
so the wire-level rate is whatever the USB host negotiates (typically Full
Speed, 12 Mbit/s) and the bottleneck is USB host polling cadence (~1 ms per
microframe) plus whatever the firmware does per byte. The number reported
by `screen`/`cu` is purely cosmetic. The real floor on echo round-trip is
~1–2 ms (USB OUT + USB IN); anything noticeably above that is firmware.

## Send-side trace — MMputchar to UART byte

The output path for one character (Editor echo of a typed key):

1. `EditInputLine()` in `MMBasic_Prompt.c:386` calls `MMputchar(c, 0)` then
   `fflush(stdout)`.
2. `MMputchar` → `putConsole` → checks `OptionConsole`. If bit 1 is set,
   calls `SerialConsolePutC(c, 0)`
   (`ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:69-74`).
3. `SerialConsolePutC` calls `esp32_console_write_bytes(&c, 1)`
   (same file, lines 52-60).
4. `esp32_console_write_bytes` is `fwrite(text, 1, len, stdout)`
   (`esp32_console.c:50-52`).
5. `esp32_console_init` set `setvbuf(stdout, NULL, _IONBF, 0)`
   (`esp32_console.c:39`) — stdout is fully **unbuffered**, so every
   `fwrite` is a syscall.
6. Newlib `fwrite` → IDF VFS → `usb_serial_jtag_write` in
   `~/esp/esp-idf/components/esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c:177`.
   That function takes a recursive lock, then **loops one byte at a time**
   (lines 189-198), calling `usbjtag_tx_char_via_driver` per byte.
7. `usbjtag_tx_char_via_driver` (same file, line 660) calls
   `usb_serial_jtag_write_bytes(&ch, 1, 0)` — pushes a single byte into
   the IDF TX ring buffer (256 bytes, configured in `esp32_console_init`),
   then enables the `SERIAL_IN_EMPTY` interrupt
   (`esp_driver_usb_serial_jtag/src/usb_serial_jtag.c:241-244`).
8. When the USB host issues the next IN token (every ~1 ms on Full-Speed
   USB), the ESP32 USB-JTAG block raises `SERIAL_IN_EMPTY`. The ISR
   (`usb_serial_jtag.c:61`) drains up to 64 bytes from the ring buffer
   into the TX FIFO and calls `usb_serial_jtag_ll_txfifo_flush()`.

For a single character: lock acquire, 1-byte ringbuffer enqueue, interrupt
arm, then up to 1 ms wait for the next USB IN poll, then one USB packet
carrying 1 byte. **The minimum first-byte echo latency is ~1 ms**, which is
the USB SOF interval, before macOS CDC and `screen` add anything.

`PRINT "Hello, world"` is 13 individual fwrites at 13 individual lock+enqueue
operations. The bytes do coalesce inside the ringbuffer once a few are
queued (the ISR can drain up to 64 in one ISR pass), but every byte still
pays the CPU cost of: locking, the VFS write loop, `xRingbufferSend`,
interrupt unmask. That CPU cost is what matters for the "MMputchar isn't
free" complaint, not the wire.

When the web console is enabled (see "Root causes" below) the same call
also runs `DisplayPutC` → `GUIPrintChar` → `DrawBitmap` (a software-rendered
8×12 glyph blit into a 320×240 RGB888 framebuffer in PSRAM) plus a final
`routinechecks()` call — *per character*. The framebuffer lives in PSRAM
(`hal_vm_framebuffer_esp32_stub.c:36`, `MALLOC_CAP_SPIRAM`), so each
character is hundreds of PSRAM writes plus dirty-rect bookkeeping.

## Receive-side trace — UART byte to MMgetchar return

1. USB host sends an OUT packet containing the keystroke.
2. The USB-JTAG hardware raises `SERIAL_OUT_RECV_PKT`. The ISR
   (`usb_serial_jtag.c:131`) drains the RX FIFO with
   `usb_serial_jtag_ll_read_rxfifo` and calls `xRingbufferSendFromISR`
   into the RX ring (256 bytes). The ISR sets `xTaskWoken = pdTRUE` and
   calls `portYIELD_FROM_ISR()` if needed — i.e. it wakes any task
   blocked on the RX ringbuffer immediately.
3. `EditInputLine` is blocked in `MMgetchar`
   (`esp32_mmbasic_console_glue.c:233-255`). The poll loop is:

   ```
   do {
       ProcessWeb(0);
       c = esp32_console_ring_pop();         // telnet/web byte
       if (c < 0) c = esp32_web_console_pop_key();
       if (c < 0) c = esp32_console_read_byte_blocking_ms(1);
   } while (c < 0);
   ```

4. `esp32_console_read_byte_blocking_ms(1)` (`esp32_console.c:74-81`) does:

   ```
   TickType_t ticks = pdMS_TO_TICKS(1);
   if (ms > 0 && ticks == 0) ticks = 1;  // ms < tick period → round up
   usb_serial_jtag_read_bytes(&c, 1, ticks);
   ```

   With `CONFIG_FREERTOS_HZ=100` (sdkconfig confirms), the tick period is
   **10 ms**. `pdMS_TO_TICKS(1) == 0`, the floor clamps that to 1 tick,
   so this call blocks for **up to 10 ms** per spin around the idle poll
   loop. *However*: when a byte arrives, the ISR's `xRingbufferSendFromISR`
   wakes the task immediately, so the 10 ms is a ceiling on the
   wait-for-byte case, not on the byte-arrival path. Echo latency for a
   single keystroke is not noticeably affected by this value.

   The 10 ms tick does cap the cadence at which `ProcessWeb` runs (and at
   which the cursor blinks, if cursor blinking ever applied here). On a
   completely idle terminal the task wakes ~100 Hz, which is fine.

5. The decoded byte is post-processed: 0x7f→BKSP, 0x0a→ENTER. If the byte
   is ESC and the rx ring is empty, the escape-sequence decoder
   (`esp32_decode_escape_sequence`) reads up to several more bytes with
   `esp32_console_read_byte_blocking_ms(30)` between them. A standalone
   ESC keystroke therefore stalls for **up to 30 ms** in the decoder
   before being delivered to the Editor. This is by design but worth
   noting.

## sdkconfig facts that matter

From `ports/esp32_s3_metro/sdkconfig`:

| Key | Value | Implication |
|---|---|---|
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` | yes | Console is USB-CDC, not a real UART. |
| `CONFIG_ESP_CONSOLE_UART_DEFAULT` | not set | No physical UART console. |
| `CONFIG_ESP_CONSOLE_USB_CDC` | not set | Not the external TinyUSB-CDC stack either. |
| `CONFIG_ESP_CONSOLE_UART_NUM=-1` | -1 | Confirms no UART console. |
| `CONFIG_FREERTOS_HZ=100` | 100 Hz | 10 ms tick. Any `pdMS_TO_TICKS(1..9)` rounds to one tick = 10 ms. |
| `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y` | OPI @ 80 MHz | PSRAM is fast, not the per-glyph bottleneck. |

The "115200" reported by the host is the CDC line-coding the firmware
declares; on USB-Serial-JTAG it is ignored by hardware.

## Root causes, ordered by expected impact

### 1. Web-console framebuffer is in the per-character path (LARGEST when active)

`app_main` line 120 calls `esp32_web_console_display_init()`. That sets
`Option.DISPLAY_CONSOLE = 1` and `OptionConsole = 3` (BOTH = screen +
serial). From that point on, every `MMputchar`/`putConsole` call also runs
`DisplayPutC` (`gfx_console_shared.c:148`), which:

- Calls `GUIPrintChar` → `DrawBitmap` → `web_console_display_bitmap`
  (`drivers/web_console/web_console_display.c:183`). For an 8×12 glyph
  that is ~96 pixel-write operations into a PSRAM-backed 320×240 RGB888
  framebuffer, plus dirty-region bookkeeping.
- Calls `routinechecks()` at the end of `DisplayPutC`
  (`gfx_console_shared.c:189`). That funnel runs `esp32_runtime_service`
  (pumps stdin, calls `ProcessWeb` if it hasn't reentered) and may
  `vTaskDelay(1)` once per 10 ms.

The result is that every character of REPL output — every character of the
banner, every character of `PRINT`, every echoed keystroke — does a
framebuffer glyph blit even though the user is reading the serial
terminal. The user has no LCD attached on this port; the framebuffer is
only consumed by the WebSocket display, which they are not using.

`Option.DISPLAY_CONSOLE = 0` is what `esp32_apply_terminal_option_defaults`
sets on first-boot/factory-default (`app_main.c:43`), but
`esp32_web_console_display_init` unconditionally overwrites that to 1.
So even though the user is on `idf.py monitor`, every keystroke goes
through the framebuffer renderer.

### 2. stdout is `_IONBF` (every byte is its own VFS write)

`esp32_console_init` (`esp32_console.c:39`) calls
`setvbuf(stdout, NULL, _IONBF, 0)`. The host port deliberately does not do
this — it lets stdio block-buffer and uses `fflush(stdout)` when needed.

With `_IONBF`, `MMPrintString` (which is `while (*s) MMputchar(*s++, 0);
fflush(stdout);`) makes one syscall per character. Each syscall does:

- newlib stream-lock acquire/release,
- recursive `_lock_acquire_recursive(&s_ctx.write_lock)` inside the
  IDF VFS write,
- a per-byte loop in `usb_serial_jtag_write` (which already loops byte by
  byte even if you hand it a buffer, but at least the lock would be held
  once),
- `xRingbufferSend` for 1 byte,
- `usb_serial_jtag_ll_ena_intr_mask` to arm the TX interrupt.

The per-syscall CPU overhead is the *dominant* cost when printing long
strings (banner, multi-line `PRINT`). The bytes still coalesce inside the
TX ring buffer before going on the wire, but each *call* is expensive.

This is independent of root cause 1, but its perceived impact is smaller
because IDF batches the actual USB packets. Where it bites is during
banner output and any program that prints character-at-a-time.

### 3. `Option.DISPLAY_CONSOLE = 1` makes `routinechecks()` fire per character

Already noted in (1); calling out separately because even if the
framebuffer blit were free, the `routinechecks()` at the bottom of
`DisplayPutC` runs the input pump, abort check, and yield logic on *every
single character* of output. On a long `PRINT`, the inner loop is doing
serial admin work between every byte. Removing the framebuffer from the
hot path (fix below) removes this too.

### 4. Per-byte VFS write loop in IDF (out of our control, but worth knowing)

`usb_serial_jtag_write` (`usb_serial_jtag_vfs.c:177`) loops one byte at a
time even when handed a longer buffer. We can't fix this in IDF, but if we
batch into the lower-level `usb_serial_jtag_write_bytes` ourselves we
skip the per-byte VFS loop, the line-ending translation, and the per-byte
locking.

### 5. ESC keystroke deliberately stalls up to 30 ms

`esp32_decode_escape_sequence` waits up to 30 ms per intermediate byte
when an ESC arrives, to distinguish a standalone ESC keypress from a CSI
sequence. This is intentional. A single ESC will be delivered to the
Editor up to ~90 ms after the user presses it (worst case: ESC + `[` + final).
That is visible if the user is mashing ESC, but is not what "typing feels
slow" describes; documenting for completeness.

## Proposed fixes (file/lines + change + expected impact)

### Fix A — Stop putting the framebuffer in the serial REPL hot path

**File**: `ports/esp32_s3_metro/main/hal_vm_framebuffer_esp32_stub.c:94-97`.

Change `esp32_web_console_display_init` so it **does not** set
`Option.DISPLAY_CONSOLE = 1` / `OptionConsole = 3` until a web-console
client actually connects, or gate it behind a separate option.
Alternatively, hand the framebuffer plumbing a separate "shadow"
hook (when a web client is connected, render to framebuffer
in addition to serial, but only then).

Concretely the minimal change is: leave `Option.DISPLAY_CONSOLE = 0` and
`OptionConsole = 1` at boot; when `esp32_web_console_connected()` becomes
true, flip them to 1 and 3 (and back). The dispatch tables (`DrawPixel`
etc.) can be installed eagerly — they just shouldn't be exercised on
every MMputchar when no web client is listening.

Expected impact: removes ~96 framebuffer pixel writes + dirty-region
update + routinechecks per character. For users on `idf.py monitor` with
no web client, this is the single biggest restoration of perceived
responsiveness. Echo of one keystroke drops from
"framebuffer-blit-plus-USB" to just "USB" (~1 ms USB floor).

### Fix B — Stop disabling stdout buffering, and batch the per-byte syscall

**File**: `ports/esp32_s3_metro/main/esp32_console.c:38-39`.

Remove the `setvbuf(stdout, NULL, _IONBF, 0)` call. The host port leaves
stdout at newlib's default for a tty (line-buffered) and uses explicit
`fflush(stdout)` at flush points; the ESP32 port should do the same.

Better still: bypass stdout entirely in `esp32_console_write_bytes`. Call
`usb_serial_jtag_write_bytes(text, len, 0)` directly. That removes the
newlib stream lock, the VFS per-byte loop in
`usb_serial_jtag_write`, and skips line-ending translation (which we
already configured as LF-only via `usb_serial_jtag_vfs_set_tx_line_endings`).
A multi-byte `MMPrintString` then becomes one ringbuffer enqueue instead
of N.

```c
// esp32_console.c
void esp32_console_write_bytes(const char *text, int len) {
    if (len <= 0) return;
    usb_serial_jtag_write_bytes(text, (size_t)len, 0);
}
```

And then in the per-character paths (`MMputchar`/`SerialConsolePutC`), we
also want `MMPrintString` to pass the *whole* string in one shot rather
than looping `MMputchar` per byte. That is a bigger refactor; the
`esp32_console_write_bytes` change above already captures most of the
gain at the syscall layer.

Expected impact: large reduction in CPU time spent emitting long strings
(banner, `PRINT` output, error messages). Helps both the perceived
"sluggish print" and frees the REPL task to service keys faster.

### Fix C — Drop the 1 ms wait floor in `MMgetchar`'s poll

**File**: `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:249` and
`esp32_console.c:74-81`.

The "1 ms" passed in is silently rounded up to 10 ms by the
`if (ms > 0 && ticks == 0) ticks = 1;` clamp. This is *not* harmful to
echo latency (the ISR wakes the task immediately on RX), but it does mean
that the cursor-blink / `ProcessWeb` cadence in `MMgetchar` is 100 Hz
instead of the 1 kHz the surface API implies. Either accept it (it's
fine) and rename the function so it is honest, or change `MMgetchar`'s
idle wait to `esp32_console_read_byte_blocking_ms(10)` to make it
explicit. No latency impact, just a clarity fix.

### Fix D — Raise FreeRTOS tick rate (optional, low value)

**File**: `ports/esp32_s3_metro/sdkconfig` / `sdkconfig.defaults`.

`CONFIG_FREERTOS_HZ=1000` would make `pdMS_TO_TICKS(1)` honest and tighten
the worst-case ESC-decode timeout. But it also burns ~10× more CPU on
tick processing and is not what's making the console feel slow. Listed
for completeness; do not change unless you have an unrelated reason.

## What needs hardware measurement to confirm

1. Whether fix A alone restores acceptable feel. Likely yes based on
   static reasoning (framebuffer glyph blit + routinechecks per char is
   substantial), but only direct timing can confirm.
2. The actual macOS CDC stack latency on a USB-Serial-JTAG endpoint
   versus a TinyUSB-CDC endpoint. macOS has historically added 8–16 ms of
   buffering to CDC. If after fixes A and B the console still feels
   sluggish, suspect host-side buffering and try `idf.py monitor` vs
   `screen` vs `picocom -fn` and compare. Reading the source can't tell
   us anything about macOS.
3. Whether the per-byte CPU cost of `setvbuf(_IONBF)` is actually visible
   to the user (fix B) or whether the framebuffer cost (fix A) dwarfs it
   so completely that B isn't perceptible. Both are wins; the ordering
   only matters for prioritisation.
4. Whether the user's "slow" complaint is dominated by *echo* (key down →
   character appears) or by *throughput* (`PRINT` of long strings is slow
   to scroll). Fix A helps echo. Fix B helps throughput. The fix order
   depends on which dominates.
