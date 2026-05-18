import { handleAudioMessage } from "./audio.js";

// PicoCalc simulator frontend.
//
// Wire protocol (v3):
//   server → client: binary WS message, two variants
//     FRMB (full frame, sent once per client on connect):
//       bytes 0..3   : magic "FRMB"
//       bytes 4..5   : width  (LE u16)
//       bytes 6..7   : height (LE u16)
//       bytes 8..    : RGBA8 pixel data, row-major, w*h*4 bytes
//     CMDS (graphics-command stream — the default path for everything):
//       bytes 0..3   : magic "CMDS"
//       bytes 4..5   : canvas width  (LE u16)
//       bytes 6..7   : canvas height (LE u16)
//       bytes 8..    : sequence of opcodes. Each opcode is:
//         0x01 CLS    : u32 color                                    (5 bytes total)
//         0x02 RECT   : i16 x, i16 y, u16 w, u16 h, u32 color       (13)
//         0x03 PIXEL  : i16 x, i16 y, u32 color                     (9)
//         0x04 SCROLL : i16 lines, u32 bg                           (7)
//         0x05 BLIT   : i16 x, i16 y, u16 w, u16 h, RGBA[w*h]       (9 + w*h*4)
//   client → server: JSON
//     { "op": "key", "code": <0..255> }   one byte, device-style codes
//       - 0x08 BKSP  0x09 TAB   0x0d ENTER  0x1b ESC   0x7f DEL
//       - 0x80 UP    0x81 DOWN  0x82 LEFT   0x83 RIGHT
//       - 0x84 INSERT 0x86 HOME 0x87 END    0x88 PGUP  0x89 PDOWN
//       - 0x91..0x9c  F1..F12

const canvas = document.getElementById("display");
const ctx = canvas.getContext("2d");
const statusEl = document.getElementById("status");

function setStatus(text, cls) {
    statusEl.textContent = text;
    statusEl.className = "status" + (cls ? " " + cls : "");
}

let imageData = null;
let lastW = 0, lastH = 0;

// Integer pixel-doubling: each framebuffer pixel renders as 2x2 CSS px
// regardless of resolution, keeping aspect ratio and sharp edges. The
// canvas's internal pixel buffer always matches the server-reported
// framebuffer (1:1 in device pixels); CSS scales that up by exactly 2x.
const DISPLAY_SCALE = 2;

function ensureCanvas(w, h) {
    if (w !== lastW || h !== lastH) {
        canvas.width = w;
        canvas.height = h;
        canvas.style.width  = (w * DISPLAY_SCALE) + "px";
        canvas.style.height = (h * DISPLAY_SCALE) + "px";
        imageData = ctx.createImageData(w, h);
        lastW = w;
        lastH = h;
    }
}

function cssColor(rgb) {
    // rgb is 0x00RRGGBB (server stores little-endian, we just got a LE u32).
    const r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return "rgb(" + r + "," + g + "," + b + ")";
}

function replayCmds(buf) {
    const view = new DataView(buf);
    const w = view.getUint16(4, true);
    const h = view.getUint16(6, true);
    ensureCanvas(w, h);

    const opCounts = [0, 0, 0, 0, 0, 0];
    let p = 8;
    const end = buf.byteLength;
    try {
        while (p < end) {
            const op = view.getUint8(p); p += 1;
            switch (op) {
                case 0x01: {
                    const c = view.getUint32(p, true); p += 4;
                    ctx.fillStyle = cssColor(c);
                    ctx.fillRect(0, 0, w, h);
                    opCounts[1]++;
                    break;
                }
                case 0x02: {
                    const rx = view.getInt16(p, true); p += 2;
                    const ry = view.getInt16(p, true); p += 2;
                    const rw = view.getUint16(p, true); p += 2;
                    const rh = view.getUint16(p, true); p += 2;
                    const c = view.getUint32(p, true); p += 4;
                    ctx.fillStyle = cssColor(c);
                    ctx.fillRect(rx, ry, rw, rh);
                    opCounts[2]++;
                    break;
                }
                case 0x03: {
                    const px = view.getInt16(p, true); p += 2;
                    const py = view.getInt16(p, true); p += 2;
                    const c = view.getUint32(p, true); p += 4;
                    ctx.fillStyle = cssColor(c);
                    ctx.fillRect(px, py, 1, 1);
                    opCounts[3]++;
                    break;
                }
                case 0x04: {
                    const lines = view.getInt16(p, true); p += 2;
                    const bg = view.getUint32(p, true); p += 4;
                    if (lines > 0) {
                        ctx.drawImage(canvas, 0, lines, w, h - lines, 0, 0, w, h - lines);
                        ctx.fillStyle = cssColor(bg);
                        ctx.fillRect(0, h - lines, w, lines);
                    } else if (lines < 0) {
                        const n = -lines;
                        ctx.drawImage(canvas, 0, 0, w, h - n, 0, n, w, h - n);
                        ctx.fillStyle = cssColor(bg);
                        ctx.fillRect(0, 0, w, n);
                    }
                    opCounts[4]++;
                    break;
                }
                case 0x05: {
                    const bx = view.getInt16(p, true); p += 2;
                    const by = view.getInt16(p, true); p += 2;
                    const bw = view.getUint16(p, true); p += 2;
                    const bh = view.getUint16(p, true); p += 2;
                    const nbytes = bw * bh * 4;
                    const pixels = new Uint8ClampedArray(buf, p, nbytes);
                    p += nbytes;
                    const img = new ImageData(pixels, bw, bh);
                    ctx.putImageData(img, bx, by);
                    opCounts[5]++;
                    break;
                }
                default:
                    console.warn("CMDS: unknown opcode", op.toString(16), "at byte", p - 1,
                                 "prior ops:", opCounts.join(","));
                    return;
            }
        }
    } catch (e) {
        console.error("CMDS: exception at byte", p, "msg=", e.message,
                      "opCounts=", opCounts.join(","));
        return;
    }
    // One-line summary per message so we can see the stream live.
    const summary = ["CLS", "RECT", "PIXEL", "SCROLL", "BLIT"]
        .map((name, i) => opCounts[i + 1] ? name + "×" + opCounts[i + 1] : null)
        .filter(Boolean).join(" ");
    console.log("CMDS", buf.byteLength + "B", summary || "(empty)");
}

let __wire_stats = { FRMB: 0, CMDS: 0, unknown: 0 };
window.__wireStats = () => __wire_stats;

function drawFrame(buf) {
    const view = new DataView(buf);
    const magic = String.fromCharCode(
        view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
    if (magic === "FRMB") {
        const w = view.getUint16(4, true);
        const h = view.getUint16(6, true);
        if (w <= 0 || h <= 0) { console.warn("FRMB bad dims", w, h); return; }
        ensureCanvas(w, h);
        const pixels = new Uint8ClampedArray(buf, 8, w * h * 4);
        imageData.data.set(pixels);
        ctx.putImageData(imageData, 0, 0);
        console.log("FRMB bootstrap", w + "x" + h, buf.byteLength + "B");
        return;
    }
    if (magic === "CMDS") {
        replayCmds(buf);
        return;
    }
    console.warn("unknown magic", magic, "bytes=" + buf.byteLength);
}

// Map a DOM KeyboardEvent to a PicoMite-style byte code (0..255) or -1
// to let the browser handle it. Printable characters come through as
// `event.key` (one code point); special keys are named.
function mapKey(ev) {
    const k = ev.key;
    if (k.length === 1) {
        const c = k.charCodeAt(0);
        // Ctrl-<letter> → control code
        if (ev.ctrlKey && !ev.altKey && !ev.metaKey) {
            const lower = k.toLowerCase();
            if (lower >= "a" && lower <= "z") return lower.charCodeAt(0) - 96;
        }
        return c < 256 ? c : -1;
    }
    switch (k) {
        case "Backspace": return 0x08;
        case "Tab":       return 0x09;
        case "Enter":     return 0x0d;
        case "Escape":    return 0x1b;
        case "Delete":    return 0x7f;
        case "ArrowUp":   return 0x80;
        case "ArrowDown": return 0x81;
        case "ArrowLeft": return 0x82;
        case "ArrowRight":return 0x83;
        case "Insert":    return 0x84;
        case "Home":      return 0x86;
        case "End":       return 0x87;
        case "PageUp":    return 0x88;
        case "PageDown":  return 0x89;
        case "F1":  return 0x91;
        case "F2":  return 0x92;
        case "F3":  return 0x93;
        case "F4":  return 0x94;
        case "F5":  return 0x95;
        case "F6":  return 0x96;
        case "F7":  return 0x97;
        case "F8":  return 0x98;
        case "F9":  return 0x99;
        case "F10": return 0x9a;
        case "F11": return 0x9b;
        case "F12": return 0x9c;
    }
    return -1;
}

let ws = null;

function connect() {
    const url = (location.protocol === "https:" ? "wss://" : "ws://") +
                location.host + "/ws";
    ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => setStatus("connected", "ok");
    ws.onclose = () => {
        setStatus("disconnected — retrying…", "err");
        ws = null;
        setTimeout(connect, 500);
    };
    ws.onerror = () => setStatus("error", "err");
    ws.onmessage = (ev) => {
        if (ev.data instanceof ArrayBuffer) {
            drawFrame(ev.data);
            return;
        }
        // TEXT frames carry our JSON audio protocol.
        if (typeof ev.data === "string") {
            try {
                const msg = JSON.parse(ev.data);
                handleAudioMessage(msg);
            } catch (e) {
                console.warn("bad JSON from server", ev.data, e);
            }
        }
    };
}

// Browser auto-repeat fires much faster than the PicoCalc's hardware
// keyboard (macOS ~30/sec vs device ~6/sec). If we forwarded every
// repeat, games that accelerate per-keypress (pico_blocks' paddle, etc.)
// would overshoot immediately. Pace repeats ourselves to a device-like
// rate while still firing the initial keydown instantly.
const REPEAT_INITIAL_MS = 150;   // delay before first repeat
const REPEAT_INTERVAL_MS = 70;   // then ~14 keys/sec
const heldKeys = new Map();      // code -> { timer, next }

function sendKey(code) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ op: "key", code }));
    }
}

// Track holds by physical key (ev.code) instead of character (ev.key) so
// Shift+key pairs release cleanly — otherwise keydown has key='"' and
// keyup has key="'" (Shift released first) and the hold never clears.
window.addEventListener("keydown", (ev) => {
    const code = mapKey(ev);
    if (code < 0) return;
    ev.preventDefault();
    if (ev.repeat) return;
    sendKey(code);
    if (heldKeys.has(ev.code)) return;
    const state = { code };
    state.timer = setTimeout(function tick() {
        sendKey(state.code);
        state.timer = setTimeout(tick, REPEAT_INTERVAL_MS);
    }, REPEAT_INITIAL_MS);
    heldKeys.set(ev.code, state);
});

window.addEventListener("keyup", (ev) => {
    const state = heldKeys.get(ev.code);
    if (state) {
        clearTimeout(state.timer);
        heldKeys.delete(ev.code);
    }
});

// Safety net: drop all held-key timers when the window loses focus so
// keys don't "stick" if keyup never fires (e.g. tab switch mid-press).
window.addEventListener("blur", () => {
    for (const s of heldKeys.values()) clearTimeout(s.timer);
    heldKeys.clear();
});

setStatus("connecting…");
connect();
