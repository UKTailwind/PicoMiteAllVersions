// worker_test.mjs — minimum A/B page that runs the wasm interpreter
// in a worker (see worker.mjs) and blits the shared-memory framebuffer
// from the main thread. A/B test target against the shipping app.mjs
// when the main-thread path has input-driven stutter under Chrome.
//
// Scope intentionally narrow:
//   - One fixed 320×320 canvas, no resolution dropdown
//   - Keyboard via postMessage (SAB ring is a later optimisation)
//   - No audio, no FS drag-drop, no IDBFS (the bundle demos still
//     live under /bundle/ in the worker; RUN "mandelbrot.bas" etc.
//     work if the path is /bundle/…)
//
// Compare-and-contrast: open both /index.html and /worker_test.html
// side-by-side, run pico_blocks in each, hold a direction key, watch
// for the Chrome stutter. Whichever is smoother wins.

const canvas = document.getElementById('screen');
const statusEl = document.getElementById('status');
const setStatus = (text, isError = false) => {
    statusEl.textContent = text;
    statusEl.classList.toggle('error', isError);
};

// ---- WebGL setup (same shader path as app.mjs) ----
const gl = canvas.getContext('webgl2', {
    alpha: false, antialias: false, depth: false, stencil: false,
    preserveDrawingBuffer: false, desynchronized: true,
    powerPreference: 'high-performance',
});
if (!gl) { setStatus('WebGL2 required', true); throw new Error('no webgl2'); }
gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);

const VERT = `#version 300 es
in vec2 a_pos; out vec2 v_uv;
void main() { v_uv = a_pos*0.5+0.5; gl_Position = vec4(a_pos,0,1); }`;
const FRAG = `#version 300 es
precision mediump float;
in vec2 v_uv; out vec4 o; uniform sampler2D u_tex;
void main() { vec4 c = texture(u_tex, v_uv); o = vec4(c.b, c.g, c.r, 1.0); }`;

function compile(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
}
const prog = gl.createProgram();
gl.attachShader(prog, compile(gl.VERTEX_SHADER,   VERT));
gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, FRAG));
gl.linkProgram(prog);
gl.useProgram(prog);

const vb = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, vb);
gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1,
]), gl.STATIC_DRAW);
const aPos = gl.getAttribLocation(prog, 'a_pos');
gl.enableVertexAttribArray(aPos);
gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);
gl.uniform1i(gl.getUniformLocation(prog, 'u_tex'), 0);

const tex = gl.createTexture();
gl.activeTexture(gl.TEXTURE0);
gl.bindTexture(gl.TEXTURE_2D, tex);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

// ---- Worker spawn + boot ----
if (typeof SharedArrayBuffer === 'undefined') {
    setStatus('SharedArrayBuffer not available — page must be served with COOP/COEP headers.', true);
    throw new Error('no SAB');
}

const worker = new Worker(new URL('./worker.mjs', import.meta.url), { type: 'module' });

let memoryBytes = null;  // Uint8Array view over the worker's shared wasm heap
let fbPtr = 0, fbWidth = 0, fbHeight = 0;
let fbTexAllocated = false;
let ready = false;

worker.onmessage = (e) => {
    const m = e.data;
    if (!m) return;
    if (m.type === 'log') {
        console[m.level === 'warn' ? 'warn' : 'log']('[worker]', m.line);
        return;
    }
    if (m.type === 'audio') {
        // Relay wasm's PLAY primitive to the main-thread Web Audio
        // bridge installed by ui/audio.js. The worker can't reach
        // window.picomiteAudio directly — AudioContext only lives on
        // main thread — so it postMessages here and we dispatch.
        const api = window.picomiteAudio;
        if (api && typeof api[m.op] === 'function') {
            api[m.op](...(m.args || []));
        }
        return;
    }
    if (m.type === 'ready') {
        memoryBytes = new Uint8Array(m.memoryBuffer);
        fbPtr    = m.fbPtr;
        fbWidth  = m.fbWidth;
        fbHeight = m.fbHeight;
        canvas.width  = fbWidth;
        canvas.height = fbHeight;
        canvas.style.width  = (fbWidth  * 2) + 'px';
        canvas.style.height = (fbHeight * 2) + 'px';
        gl.viewport(0, 0, fbWidth, fbHeight);
        ready = true;
        setStatus(`Worker ready — ${fbWidth}×${fbHeight} via SAB`);
        // Dev/test hook so smoke_worker.mjs can verify the shared
        // framebuffer has pixels without fighting canvas internals.
        window.__picomiteWorker = { memoryBytes, fbPtr, fbWidth, fbHeight };
        requestAnimationFrame(renderLoop);
        return;
    }
};

worker.onerror = (e) => {
    setStatus('Worker error: ' + (e.message || e), true);
    console.error(e);
};

worker.postMessage({
    type: 'init',
    cfg: { res: { w: 320, h: 320 }, heap: 2 * 1024 * 1024 },
});

// ---- Render loop ----
function renderLoop() {
    if (ready && fbPtr) {
        // Shared-memory view — reading directly from the worker's
        // wasm heap, no postMessage required.
        const bytes = new Uint8Array(memoryBytes.buffer, fbPtr, fbWidth * fbHeight * 4);
        if (!fbTexAllocated) {
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, fbWidth, fbHeight, 0,
                          gl.RGBA, gl.UNSIGNED_BYTE, bytes);
            fbTexAllocated = true;
        } else {
            gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, fbWidth, fbHeight,
                             gl.RGBA, gl.UNSIGNED_BYTE, bytes);
        }
        gl.drawArrays(gl.TRIANGLES, 0, 6);
    }
    requestAnimationFrame(renderLoop);
}

// ---- Keyboard → worker (postMessage for now; SAB ring is task #16) ----
const SPECIAL_KEYS = {
    Enter: 0x0D, Tab: 0x09, Backspace: 0x08, Delete: 0x7F, Escape: 0x1B,
    ArrowUp: 0x80, ArrowDown: 0x81, ArrowLeft: 0x82, ArrowRight: 0x83,
    Insert: 0x84, Home: 0x86, End: 0x87, PageUp: 0x88, PageDown: 0x89,
};
function mapKey(event) {
    const k = event.key;
    if (SPECIAL_KEYS[k] !== undefined) return SPECIAL_KEYS[k];
    if (/^F([1-9]|1[0-2])$/.test(k)) return 0x90 + parseInt(k.slice(1), 10);
    if (k.length === 1) {
        const code = k.charCodeAt(0);
        if (event.ctrlKey && code >= 0x40 && code < 0x80) return code & 0x1F;
        if (code < 0x80) return code;
    }
    return -1;
}

// Device-matched repeat: 400 ms delay, 100 ms interval, driven by
// setInterval (matching app.mjs's scheme). Keeps the paddle speed
// sane when holding a direction.
const held = new Map();
canvas.addEventListener('keydown', (event) => {
    const mm = mapKey(event);
    if (mm < 0) return;
    if (event.repeat) return;
    worker.postMessage({ type: 'key', code: mm });
    const prev = held.get(event.code);
    if (prev) { clearTimeout(prev.d); clearInterval(prev.i); }
    const s = { d: 0, i: 0 };
    s.d = setTimeout(() => {
        s.d = 0;
        s.i = setInterval(() => worker.postMessage({ type: 'key', code: mm }), 100);
    }, 400);
    held.set(event.code, s);
}, { passive: true });
canvas.addEventListener('keyup', (event) => {
    const s = held.get(event.code);
    if (s) { clearTimeout(s.d); clearInterval(s.i); held.delete(event.code); }
}, { passive: true });
window.addEventListener('blur', () => {
    for (const s of held.values()) { clearTimeout(s.d); clearInterval(s.i); }
    held.clear();
});
canvas.addEventListener('click', () => canvas.focus());
canvas.focus();
