// WebAudio engine for the PicoMite WASM host.
//
// The C side (host_wasm_audio.c) calls into this module through
// window.picomiteAudio via EM_ASM. No JSON layer, no WebSocket — the
// wasm module and this module share a process.
//
// API exposed on window.picomiteAudio:
//   tone(leftHz, rightHz, ms)  ms<0 = play forever until stop
//   stop()                     stops TONE + all SOUND slots
//   sound(slot, ch, type, f, v)
//     slot: 1..4
//     ch:   "L"|"R"|"B"
//     type: "S"|"Q"|"T"|"W"|"O"|"P"|"N"|"U"
//     f:    Hz
//     v:    0..25
//   volume(l, r)               0..100 per channel (PLAY VOLUME)
//   pause(), resume()          suspend/resume the whole AudioContext
//
// PLAY TONE and PLAY SOUND are independent in PicoMite — a TONE in the
// middle of a SOUND should not disturb the running slots, and vice
// versa. We keep the graphs separate for that reason.

const WAVE_FOR_TYPE = {
    "S": "sine",
    "Q": "square",
    "T": "triangle",
    "W": "sawtooth",
};

let ctx = null;
let masterL = null, masterR = null;
let merger = null;

// PLAY TONE state.
let toneL = null, toneR = null;
let toneGainL = null, toneGainR = null;
let toneStopTimer = null;

// PLAY SOUND state — 4 slots × {L, R} oscillators/buffers.
const soundSlots = [null, null, null, null];

// Shared white-noise buffer (used for type "N").
let whiteNoiseBuf = null;

// Browsers block AudioContext.state from becoming "running" unless the
// context was created or resumed during a user gesture. We eagerly wire
// capture-phase listeners so the first keydown / click / touch primes
// the context, and keep them on for later tab-switches that suspend it.
let gestureArmed = false;

function ensureCtx() {
    if (ctx) return ctx;
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) {
        console.warn("[picomite-audio] WebAudio not supported");
        return null;
    }
    ctx = new AC();

    // Master L/R fed into a stereo merger. Default master gain 1.0 =
    // PLAY VOLUME 100. Map "logarithmic" 0..100 as gain=(v/100)^2 so
    // 50 sounds roughly half as loud as 100.
    masterL = ctx.createGain();
    masterR = ctx.createGain();
    masterL.gain.value = 1.0;
    masterR.gain.value = 1.0;
    merger = ctx.createChannelMerger(2);
    masterL.connect(merger, 0, 0);
    masterR.connect(merger, 0, 1);
    merger.connect(ctx.destination);
    return ctx;
}

function armAudioOnGesture() {
    if (gestureArmed) return;
    gestureArmed = true;
    // Safari quirk: even after resume() inside a user gesture, the
    // output stays silent until something actually plays through
    // destination. Kick a zero-amplitude buffer on first gesture to
    // unblock the pipeline. Harmless on Chrome / Firefox.
    let primed = false;
    const prime = () => {
        ensureCtx();
        if (!ctx) return;
        if (ctx.state === "suspended") ctx.resume();
        if (!primed) {
            primed = true;
            try {
                const buf = ctx.createBuffer(1, 1, ctx.sampleRate);
                const src = ctx.createBufferSource();
                src.buffer = buf;
                src.connect(ctx.destination);
                src.start();
            } catch (_) {}
        }
        updateBanner();
    };
    window.addEventListener("keydown",     prime, { capture: true });
    window.addEventListener("mousedown",   prime, { capture: true });
    window.addEventListener("touchstart",  prime, { capture: true });
    window.addEventListener("pointerdown", prime, { capture: true });
}

// ---- "Click to enable audio" banner -------------------------------------
//
// Browsers silently drop audio until a user gesture. We toggle a hint
// element so the failure mode is visible instead of silent.

function updateBanner() {
    const el = document.getElementById("audio-hint");
    if (!el) return;
    if (ctx && ctx.state === "running") {
        el.hidden = true;
    } else {
        el.hidden = false;
    }
}

// ---- PLAY TONE ----------------------------------------------------------

function stopTone() {
    if (toneStopTimer) { clearTimeout(toneStopTimer); toneStopTimer = null; }
    for (const n of [toneL, toneR]) {
        if (n) try { n.stop(); n.disconnect(); } catch (_) {}
    }
    for (const g of [toneGainL, toneGainR]) {
        if (g) try { g.disconnect(); } catch (_) {}
    }
    toneL = toneR = toneGainL = toneGainR = null;
}

function playTone(leftHz, rightHz, ms) {
    if (!ensureCtx()) return;
    stopTone();

    // Zero frequency = "no tone this side"; don't create an oscillator.
    if (leftHz > 0) {
        toneL = ctx.createOscillator();
        toneL.type = "sine";
        toneL.frequency.value = leftHz;
        toneGainL = ctx.createGain();
        toneGainL.gain.value = 0.35;
        toneL.connect(toneGainL).connect(masterL);
        toneL.start();
    }
    if (rightHz > 0) {
        toneR = ctx.createOscillator();
        toneR.type = "sine";
        toneR.frequency.value = rightHz;
        toneGainR = ctx.createGain();
        toneGainR.gain.value = 0.35;
        toneR.connect(toneGainR).connect(masterR);
        toneR.start();
    }
    if (typeof ms === "number" && ms > 0) {
        toneStopTimer = setTimeout(stopTone, ms);
    }
}

// ---- PLAY SOUND ---------------------------------------------------------

function getWhiteNoiseBuffer() {
    if (whiteNoiseBuf) return whiteNoiseBuf;
    const sec = 1.0;
    whiteNoiseBuf = ctx.createBuffer(1, sec * ctx.sampleRate, ctx.sampleRate);
    const data = whiteNoiseBuf.getChannelData(0);
    for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
    return whiteNoiseBuf;
}

function makePeriodicNoiseBuffer(freq) {
    const samples = Math.max(8, Math.floor(ctx.sampleRate / Math.max(1, freq)));
    const buf = ctx.createBuffer(1, samples, ctx.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < samples; i++) data[i] = Math.random() * 2 - 1;
    return buf;
}

function stopSlotSide(slot, side) {
    if (!slot) return;
    const n = slot[side].node;
    const g = slot[side].gain;
    if (n) try { n.stop(); n.disconnect(); } catch (_) {}
    if (g) try { g.disconnect(); } catch (_) {}
    slot[side].node = null;
    slot[side].gain = null;
}

function stopAllSounds() {
    for (let i = 0; i < soundSlots.length; i++) {
        const s = soundSlots[i];
        if (!s) continue;
        stopSlotSide(s, "L");
        stopSlotSide(s, "R");
        soundSlots[i] = null;
    }
}

function setSoundSide(slotIdx, side, type, freq, vol) {
    if (!soundSlots[slotIdx]) {
        soundSlots[slotIdx] = {
            L: { node: null, gain: null },
            R: { node: null, gain: null },
        };
    }
    const slot = soundSlots[slotIdx];
    stopSlotSide(slot, side);

    // "O" = silence this side. We already stopped; done.
    if (type === "O") return;

    // 4 slots × per-slot max 25 volume, /4 headroom so a full chord
    // doesn't clip the output.
    const gainVal = (vol / 25) * 0.25;
    const gain = ctx.createGain();
    gain.gain.value = gainVal;

    let src;
    if (type === "N") {
        src = ctx.createBufferSource();
        src.buffer = getWhiteNoiseBuffer();
        src.loop = true;
    } else if (type === "P") {
        src = ctx.createBufferSource();
        src.buffer = makePeriodicNoiseBuffer(freq);
        src.loop = true;
    } else if (type === "U") {
        // User waveform not supported; degrade to sine.
        src = ctx.createOscillator();
        src.type = "sine";
        src.frequency.value = freq;
    } else {
        src = ctx.createOscillator();
        src.type = WAVE_FOR_TYPE[type] || "sine";
        src.frequency.value = freq;
    }

    const master = (side === "L") ? masterL : masterR;
    src.connect(gain).connect(master);
    src.start();
    slot[side].node = src;
    slot[side].gain = gain;
}

function playSound(slot1, ch, type, freq, vol) {
    if (!ensureCtx()) return;
    const idx = slot1 - 1;
    if (idx < 0 || idx >= 4) return;
    if (ch === "L" || ch === "B") setSoundSide(idx, "L", type, freq, vol);
    if (ch === "R" || ch === "B") setSoundSide(idx, "R", type, freq, vol);
}

// ---- PLAY VOLUME / PAUSE / RESUME / STOP --------------------------------

function playStop() {
    stopTone();
    stopAllSounds();
}

function volToGain(v) {
    const x = Math.max(0, Math.min(100, v)) / 100;
    return x * x;
}

function setVolume(l, r) {
    if (!ensureCtx()) return;
    masterL.gain.value = volToGain(l);
    masterR.gain.value = volToGain(r);
}

function pauseAudio() {
    if (ctx) ctx.suspend().then(updateBanner).catch(() => {});
}
function resumeAudio() {
    if (ctx) ctx.resume().then(updateBanner).catch(() => {});
}

// ---- Install the window.picomiteAudio bridge ----------------------------

window.picomiteAudio = {
    tone(leftHz, rightHz, ms) {
        const duration = (typeof ms === "number" && ms >= 0) ? ms : undefined;
        playTone(+leftHz || 0, +rightHz || 0, duration);
    },
    stop:   playStop,
    sound:  playSound,
    volume: setVolume,
    pause:  pauseAudio,
    resume: resumeAudio,
};

armAudioOnGesture();
updateBanner();
