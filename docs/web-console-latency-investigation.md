# Web Console Latency Investigation

**Date:** 2026-05-14
**Branch:** `web-console-driver`
**Scope:** static-only inspection of the ESP32-S3 web console pipeline. No
firmware was reflashed; no measurements were taken on hardware.

## Symptoms

User reports that typing in the browser-side web console feels noticeably
slower than they'd expect, citing 115200 baud (~10 ms per character floor) as
the mental baseline. The pipeline is BASIC source → MMputchar (per byte) →
DisplayPutC (per byte) → 320x240 RGB24 framebuffer → dirty-bounds coalesce →
RGB332 RLE pack → WebSocket binary frame → browser canvas. The user wants a
diagnosis, not yet a fix.

## Send-side trace

Per-character path for `MMputchar('x')` on the ESP32 web-console port:

1. `MMputchar` — `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:69-74`.
   Calls `putConsole(c, flush)`.
2. `putConsole` — same file, `:62-67`. Because `OptionConsole == 3` after
   `esp32_web_console_display_init` sets it
   (`ports/esp32_s3_metro/main/hal_vm_framebuffer_esp32_stub.c:97`), this calls
   **both** `DisplayPutC` and `SerialConsolePutC` for every byte.
3. `SerialConsolePutC` — `esp32_mmbasic_console_glue.c:52-60`. Writes to USB
   serial via `esp32_console_write_bytes` (a `fwrite(stdout, 1)`) and to
   `esp32_telnet_putc`. Not the slow path, but unconditional.
4. `DisplayPutC` — `gfx_console_shared.c:148-190`. After CRLF/tab/backspace
   handling, calls `GUIPrintChar` which iterates the glyph bitmap and calls
   the registered `DrawBitmap` / `DrawRectangle` / `DrawPixel` hooks
   (`hal_vm_framebuffer_esp32_stub.c:41-54, 100-108`). Each hook calls
   `web_console_display_*` which updates `s_web_display.pixels[]` and unions
   the rect into `dirty_x1..dirty_y2`
   (`drivers/web_console/web_console_display.c:100-122`).
5. After the glyph is drawn, `DisplayPutC` invokes `routinechecks()`
   (`gfx_console_shared.c:189`).
6. `routinechecks` → `mmbasic_runtime_routinechecks` → `runtime_abort_common`
   → `esp32_runtime_service` → `ProcessWeb(0)` (`esp32_runtime.c:70-77`).
7. `ProcessWeb` → `esp32_tcp_server_poll` → `esp32_web_console_poll` →
   `esp32_web_console_drain_display`
   (`ports/esp32_s3_metro/main/esp32_tcp_server.c:481-535`).
8. `esp32_web_console_drain_display`:
   - If a previous TX is mid-flight: progress it, return.
   - If no dirty bounds: return.
   - If `next_frame_us == 0`: set it to `now + 33 333 µs` and **return without
     sending** (`:527-530`). This is the **first-byte penalty**: after each
     idle period, the first dirty character waits one full 30 fps frame
     interval before *any* bytes hit the wire.
   - Otherwise, if `now < next_frame_us`, return (waits for the 30 fps slot).
   - Otherwise pack and start sending the dirty rectangle as a single
     RGB332 RLE binary WebSocket frame.
9. Pack & send (`:364-407`): for the dirty rectangle, encode `CMDS` +
   `BLIT_RGB332_RLE` body, build a single WebSocket binary header
   (`:345-362`), then `hal_net_tcp_conn_send_some` repeatedly in chunks
   (`:409-449`). Buffer size 4096; one socket-level `send()` per call up to
   `ESP32_WEB_CONSOLE_SEND_CHUNK = 4096`.
10. `hal_net_tcp_conn_send_some` — `hal_net_esp32.c:655-671`. Bare lwIP
    `send()` on a non-blocking socket. **No `TCP_NODELAY`, no `TCP_CORK`,
    no manual flush** is set anywhere on the accepted FD; search:
    `grep -n TCP_NODELAY` in `hal_net_esp32.c` returns nothing. The accepted
    fd is opened in `hal_net_tcp_accept_conn` (`:577-603`) and only flipped
    to non-blocking — Nagle stays at lwIP defaults (enabled).

So a single `MMputchar('x')` cost on the wire is:

- ~0..33 ms gate on first dirty byte after an idle period (line 527-530).
- ~0..33 ms gate for each subsequent burst (line 531).
- Pack the *entire* dirty bounding rectangle (smallest possible: one
  character cell = ~8x12 = 96 pixels, ~96 bytes RLE-encoded; first byte
  enlarges bounds enough that subsequent chars on the same line all get
  re-encoded each frame).
- One WebSocket frame per drain pass; one or more `send()` syscalls. lwIP
  Nagle waits up to 200 ms for coalescing if previous unacknowledged segment
  is in flight (typical for tiny payloads).

## Receive-side trace

The page that loads in the browser is the inline JS in
`drivers/web_console/web_console_assets.c:42-276`. Key behaviour:

- WebSocket binary frames are decoded by `drawFrame` (`:153-222`). For each
  inbound frame the JS:
  - Reads the `CMDS` header.
  - Decodes ops (`CLS`, `RECT`, `PIXEL`, `SCROLL`, `BLIT_RGBA`,
    `BLIT_RGB332_RLE`) into an off-screen `Uint8ClampedArray` backed
    `ImageData`.
  - Calls `scheduleFlush()` which sets `flushPending` and does
    `requestAnimationFrame(flushDisplay)` (`:87-90`).
- `flushDisplay` calls `ctx.putImageData(fbImage, 0, 0)` on the canvas
  (`:78-86`). This re-blits the **entire 320x240 framebuffer** every flush —
  not just the dirty subregion. For a 320x240 canvas this is fine, but it is
  not incremental.
- Coalescing is good: multiple frames inside one rAF tick share a single
  `putImageData`.
- The canvas element has CSS `image-rendering: pixelated` and is scaled to
  720px wide. Per-glyph round-trip through the browser is ≤16 ms wall (one
  rAF tick), which is not the bottleneck.

Keyboard input (`:266-272`): one WebSocket text frame per `keydown` event,
each ~20-byte JSON `{"op":"key","code":N}`. Sent client→server, so the frame
includes a 4-byte mask. Server decodes in `esp32_web_console_handle_text`
(`esp32_tcp_server.c:561-588`) and pushes into the input queue. This path is
fine on its own; the user's typing latency is dominated by the *echo*, which
is on the slow send-side path described above.

## Likely root causes (ordered by suspected impact)

1. **30 fps frame-pacing gate in `esp32_web_console_drain_display`**
   (`esp32_tcp_server.c:486-531`). Every time the display goes idle and
   becomes dirty again, the first packet waits up to 33 ms before being sent.
   Subsequent bursts also align to the 30 fps grid. This is the dominant
   user-perceived latency: typing `H E L L O` produces five separate dirty
   bursts and every one pays the 33 ms gate. Worst case: ~33 ms/char ≈ 30 cps
   ≈ 240 baud-equivalent (well below the 115200 baud reference).
2. **No `TCP_NODELAY` on the accepted WebSocket FD**. `hal_net_esp32.c:577-603`
   accepts and only sets `O_NONBLOCK`. lwIP defaults Nagle on, which coalesces
   tiny tail segments and adds up to ~200 ms when ACKs are slow. A small RLE
   frame split across an MTU boundary will be held by Nagle waiting for ACK
   of the previous segment. Confirmed by absence: `grep -n TCP_NODELAY
   ports/esp32_s3_metro/main/hal_net_esp32.c` returns nothing.
3. **Dirty-bounds union grows the encode cost super-linearly during a line of
   typing**. `mark_dirty` (`web_console_display.c:100-122`) takes the union of
   all touched rects. After 10 characters on one line the bounding box is the
   entire row (~320x12 = 3840 pixels). The RLE encoder
   (`esp32_tcp_server.c:312-343`) re-walks that whole rectangle every frame,
   even though only the most recent glyph changed. Cost grows with line
   length, then drops on `\r\n` scroll. Combined with #1, each frame ships
   several KB instead of a few hundred bytes.
4. **`ConsoleTxBufHead`/`ConsoleTxBufTail` exist but are never used on this
   port** (`esp32_mmbasic_console_glue.c:263-267`). There is no per-byte
   ring buffer between MMputchar and the framebuffer that could backpressure;
   the framebuffer itself is the buffer. No issue today, but worth noting if
   you ever introduce a text-echo fast path.
5. **`ProcessWeb` is called on the BASIC thread synchronously**
   (`esp32_runtime.c:70-77`, `esp32_mmbasic_console_glue.c:212-219`). Each
   `MMInkey` and each glyph-emit blocks the interpreter long enough to walk
   the entire TCP service hook table. With a dirty frame queued, the
   interpreter waits up to 33 ms for the gate even though `routinechecks`
   could return immediately and let a parallel task drain.
6. **`SerialConsolePutC` unconditionally writes every byte to USB CDC** even
   when the web console is the only consumer (`esp32_mmbasic_console_glue.c:52-60`).
   Each `fwrite(stdout, 1)` is one syscall through the IDF VFS into the USB
   Serial/JTAG driver. Not catastrophic, but it is one extra blocking write
   per char on the BASIC thread. The user can disable it with
   `OPTION CONSOLE SCREEN` (which sets `OptionConsole & ~1`) but the port
   forces `OptionConsole = 3` in `esp32_web_console_display_init:97`.
7. **First-frame-after-idle bug**. Lines `527-530` set `next_frame_us = now +
   33333` and **return without scheduling**. This is the worst-case 33 ms
   penalty on the very first character, exactly the symptom the user would
   feel as "slow to start typing".
8. **Browser-side full-frame `putImageData` per flush** (assets.c:82). For
   320x240 this is ~25 ms worst case on a slow GPU but is amortised by rAF
   coalescing. Not the bottleneck today.

## Proposed fixes (ordered by expected impact)

### Fix 1 — Send immediately on first dirty bounds after idle, then rate-limit
**File:** `ports/esp32_s3_metro/main/esp32_tcp_server.c:521-534`.
**Change:** when `next_frame_us == 0` and the connection is idle, send the
frame *now* and arm the timer for `now + 33333`. Today the code arms the
timer and discards the dirty bounds for one frame.

```c
/* before */
if (s_web_console_ws.next_frame_us == 0) {
    s_web_console_ws.next_frame_us = now + ESP32_WEB_CONSOLE_FRAME_INTERVAL_US;
    return;
}
if (now < s_web_console_ws.next_frame_us) return;
esp32_web_console_start_blit_and_clear_dirty(display, x1, y1, x2, y2);

/* after */
if (s_web_console_ws.next_frame_us != 0 && now < s_web_console_ws.next_frame_us)
    return;
esp32_web_console_start_blit_and_clear_dirty(display, x1, y1, x2, y2);
/* start_blit_and_clear_dirty already arms next_frame_us */
```

**Expected impact:** removes the 0..33 ms gate on the very first character
after every idle period (the most visible part of "slow"). Subsequent typing
still rate-limits to 30 fps.

### Fix 2 — Set `TCP_NODELAY` on every accepted WebSocket FD
**File:** `ports/esp32_s3_metro/main/hal_net_esp32.c:577-603`, immediately
after `accept()` succeeds (or specifically when accepting the
`/__web_console/ws` endpoint in `esp32_web_console_accept_ws`).

```c
int one = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
```

**Why:** WebSocket binary frames for one glyph are ~30-300 bytes. Without
NODELAY they're queued for up to ~200 ms by Nagle waiting for the previous
segment's ACK. With NODELAY, lwIP sends immediately.
**Expected impact:** removes the second-largest contributor to perceived
latency on networks where Nagle is hurting. Needs verification on hardware,
but cheap and well-understood.

### Fix 3 — Lower the frame-pacing gate to ~10 ms (100 fps) for tiny payloads
**File:** `ports/esp32_s3_metro/main/esp32_tcp_server.c:33`.
**Change:** make `ESP32_WEB_CONSOLE_FRAME_INTERVAL_US` adaptive — short
interval (e.g. 10000 µs) when the dirty rect is small (< 8 KB encoded), 33333
µs only when the dirty rect is large enough that the encode + WiFi airtime
actually need the budget. Alternatively, drop the unconditional 33 fps gate
when output is text-only (small bounding rect) and keep it for full-screen
graphics.

**Why:** the 30 fps cap exists to avoid swamping the WiFi link with full-frame
blits during heavy graphics, but for a few-character REPL echo the encode is
trivial. The user's reference of 115200 baud is 11.5 KB/s; even at 10ms per
small frame the link is nowhere near saturated.

**Expected impact:** per-char visual latency drops from ~33 ms to ~10 ms on
the steady-state path. Combined with fix #1, typing should feel close to
real-time.

### Fix 4 — Keep the dirty bounds tight by encoding character-cell rects directly
**File:** `drivers/web_console/web_console_display.c` (the `mark_dirty`
union behaviour). Either keep a small list of disjoint dirty rectangles, or
flush the existing rect before starting a new non-adjacent one.

**Why:** today after 30 characters on a line the dirty rect is ~3840 pixels
even if only the rightmost cell changed. RLE encoding still re-walks all of
them every frame. Encoding cost grows roughly with `line_length × frames`.

**Expected impact:** drops payload size per frame from a few KB to ~100
bytes for steady typing. Reduces WiFi airtime and packing CPU. Lower priority
than #1 / #2 because rAF coalescing on the browser side masks part of the
cost.

### Fix 5 — Disable `SerialConsolePutC` when the web console owns output
**File:** `ports/esp32_s3_metro/main/esp32_mmbasic_console_glue.c:62-67` and
`hal_vm_framebuffer_esp32_stub.c:97`. Either honour the user's
`OptionConsole` choice (don't force to `3`), or skip `SerialConsolePutC` when
`esp32_web_console_connected()` is true.

**Why:** one IDF VFS syscall per byte on the BASIC thread is wasted work
when the user isn't watching the USB CDC. Caller measured impact is small
(~10s of µs per char) but it stacks up across MMPrintString runs.

**Expected impact:** minor on its own; cumulative when multiple printlines
run back-to-back.

## Open questions / needs hardware measurement

1. **Is the dominant latency actually the 33 ms frame gate, or Nagle?** Both
   are present; static inspection can't tell which adds more. Capture a
   `tcpdump` of the WS frames during typing and measure inter-frame intervals.
2. **Does lwIP on ESP-IDF v5.x default Nagle on for accepted connections?**
   The `LWIP_TCP_KEEPALIVE` / `LWIP_TCP_NAGLE` sdkconfig defaults vary by
   release. Check `sdkconfig` for `LWIP_TCP_NAGLE` — if explicitly disabled
   globally, fix #2 is moot.
3. **Does the user see latency on full-screen graphics or only on text?** If
   only on text, fix #1 (first-frame gate) is almost certainly the cure. If
   graphics also feel slow, the WiFi airtime + packing budget matters and
   fix #3/#4 need real numbers.
4. **WebSocket frame sizes in the wild.** Per the 4096-byte send chunk, a
   320x240 RGB332 RLE frame for a busy screen could exceed 64 KB; the
   current header encoder caps at 65535 (`esp32_tcp_server.c:351-360` uses
   2-byte length only, but the start_blit path never builds payloads >65535
   in practice for text). Confirm with `?debug=1` and the browser console
   debug logs (`debug('frame #...bytes=...')`).
5. **Does `requestAnimationFrame` defer beyond 16 ms when the page is in
   the background?** Browsers throttle rAF to 1 fps when the tab is hidden.
   The user's "slow" feeling could partly be the tab not being foregrounded.
   Worth checking but not the primary fix.
6. **The Display_Refresh hook** (`hal_vm_framebuffer_esp32_stub.c:140-150`)
   is wired into graphics primitives in `Draw.c` / `vm_sys_graphics.c` but
   not into the text path (`gfx_console_shared.c` calls `routinechecks`
   instead). Confirm that the user's "slow" case is text-only — if mixed
   text+graphics, Display_Refresh's own 33 ms cap doubles up with the gate
   in `drain_display`.
