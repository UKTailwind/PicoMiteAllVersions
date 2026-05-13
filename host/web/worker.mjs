// worker.mjs — hosts the MMBasic runtime on a dedicated thread.
//
// Chrome on macOS halves rAF cadence when continuous keyboard input
// contends with setTimeout-driven ASYNCIFY sleeps on the main thread.
// Running the interpreter on a worker gives it an independent event
// loop. Main thread does canvas + keyboard + audio + FS UI only;
// this worker owns the wasm runtime.
//
// Shared memory: the wasm build uses `-pthread`, so wasm's linear
// memory is a SharedArrayBuffer. After boot we postMessage the buffer
// + framebuffer pointer back to main thread so its rAF loop can blit
// the same bytes the interpreter is writing. Zero-copy across threads.
//
// Communication:
//   inbound  'init'        — config: res, heap, slowdown, persistSd
//   inbound  'key'         — keydown from main; code = MMBasic key value
//   inbound  'fs-write'    — main writes a file (drag-drop import)
//   inbound  'fs-unlink'   — main deletes a file
//   inbound  'fs-list'     — read a directory; responds fs-list-result
//   inbound  'fs-read'     — read a file; responds fs-read-result
//   inbound  'fs-reset'    — wipe /sd/ + re-populate from /bundle/
//   inbound  'fs-syncfs'   — force-flush IDBFS to IndexedDB
//   outbound 'ready'       — wasm ready; memory + fb info + ptrs
//   outbound 'log'         — print / printErr passthrough
//   outbound 'audio'       — wasm called a PLAY primitive (main relays
//                            to Web Audio)
//   outbound 'fs-list-result', 'fs-read-result', 'fs-reset-result',
//            'fs-syncfs-result' — responses carrying reqId
//
// Single WASM binary — cfg.heap picks which device memory profile to
// simulate. _wasm_set_heap_size caps the runtime MMBasic heap so
// TryGetMemory + MEMORY output reflect the simulated profile.

import Module from './picomite.mjs';

const SD_ROOT = '/sd';
const BUNDLE_ROOT = '/bundle';

let instance = null;
let sdDirty = false;            // set by FS.trackingDelegate on writes
let flushTimer = 0;             // periodic syncfs scheduler

function post(msg, transfer) {
    if (transfer && transfer.length) self.postMessage(msg, transfer);
    else self.postMessage(msg);
}

function syncfs(populate) {
    return new Promise((resolve, reject) => {
        instance.FS.syncfs(populate, (err) => err ? reject(err) : resolve());
    });
}

function populateFromBundle() {
    let names = [];
    try {
        names = instance.FS.readdir(BUNDLE_ROOT).filter(n => n !== '.' && n !== '..');
    } catch (_) { return 0; }
    for (const n of names) {
        try {
            const data = instance.FS.readFile(`${BUNDLE_ROOT}/${n}`);
            instance.FS.writeFile(`${SD_ROOT}/${n}`, data);
        } catch (e) {
            post({ type: 'log', level: 'warn', line: `bundle copy skipped ${n}: ${e}` });
        }
    }
    return names.length;
}

function installFsTracker() {
    const isSd = (p) => typeof p === 'string' && p.startsWith(SD_ROOT + '/');
    const delegate = instance.FS.trackingDelegate || {};
    delegate.onWriteToFile = (path) => { if (isSd(path)) sdDirty = true; };
    delegate.onDeletePath  = (path) => { if (isSd(path)) sdDirty = true; };
    delegate.onMovePath    = (a, b) => { if (isSd(a) || isSd(b)) sdDirty = true; };
    instance.FS.trackingDelegate = delegate;
}

// Lightweight periodic flush: if something wrote to /sd/ since the
// last check, commit the MEMFS mirror to IndexedDB. Running in the
// worker keeps main thread free from the 5-30 ms IDB transaction hit.
function schedulePeriodicFlush() {
    if (flushTimer) return;
    flushTimer = setInterval(async () => {
        if (!sdDirty) return;
        sdDirty = false;
        try { await syncfs(false); }
        catch (e) {
            sdDirty = true;
            post({ type: 'log', level: 'warn', line: `syncfs failed: ${e}` });
        }
    }, 2000);
}

async function mountSd(persist) {
    try { instance.FS.mkdir(SD_ROOT); } catch (_) { /* already exists */ }
    if (!persist) {
        // Transient mode: no IDBFS mount, just copy the bundle in and
        // leave everything in MEMFS for the session.
        populateFromBundle();
        return { populated: true };
    }
    instance.FS.mount(instance.FS.filesystems.IDBFS, {}, SD_ROOT);
    try { await syncfs(true); } catch (e) {
        post({ type: 'log', level: 'warn', line: `initial syncfs(pull) failed: ${e}` });
    }
    // Self-heal: if /sd/ is empty (first boot, or IDB was wiped), copy
    // the bundle. Can't use localStorage from a worker — checking
    // emptiness is the simplest equivalent.
    let empty = false;
    try {
        empty = instance.FS.readdir(SD_ROOT).filter(n => n !== '.' && n !== '..').length === 0;
    } catch (_) { empty = true; }
    if (empty) {
        const n = populateFromBundle();
        if (n > 0) await syncfs(false);
    }
    return { populated: true };
}

async function initInstance(cfg) {
    instance = await Module({
        print:    (line) => post({ type: 'log', level: 'log',  line }),
        printErr: (line) => post({ type: 'log', level: 'warn', line }),
    });

    if (cfg.res)      instance._wasm_set_framebuffer_size(cfg.res.w, cfg.res.h);
    if (cfg.heap)     instance._wasm_set_heap_size(cfg.heap);
    if (cfg.slowdown) instance._wasm_set_slowdown_us(cfg.slowdown);
    if (cfg.proxy && typeof instance._wasm_set_proxy_mode === 'function') {
        instance._wasm_set_proxy_mode(cfg.proxy.online ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_http_capability === 'function') {
        const httpProxy = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.http_proxy === true;
        instance._wasm_set_proxy_http_capability(httpProxy ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_tcp_stream_capability === 'function') {
        const tcpStream = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.tcp_stream === true;
        instance._wasm_set_proxy_tcp_stream_capability(tcpStream ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_tcp_server_capability === 'function') {
        const tcpServer = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.tcp_server === true;
        instance._wasm_set_proxy_tcp_server_capability(tcpServer ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_udp_capability === 'function') {
        const udp = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.udp === true;
        instance._wasm_set_proxy_udp_capability(udp ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_ntp_capability === 'function') {
        const ntp = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.ntp === true;
        instance._wasm_set_proxy_ntp_capability(ntp ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_tftp_capability === 'function') {
        const tftp = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.tftp === true;
        instance._wasm_set_proxy_tftp_capability(tftp ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_telnet_capability === 'function') {
        const telnet = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.telnet === true &&
            cfg.proxy.caps?.features?.tcp_server === true;
        instance._wasm_set_proxy_telnet_capability(telnet ? 1 : 0);
    }
    if (cfg.proxy && typeof instance._wasm_set_proxy_mqtt_plain_capability === 'function') {
        const mqttPlain = cfg.proxy.online === true &&
            cfg.proxy.caps?.features?.mqtt_plain === true &&
            cfg.proxy.caps?.features?.tcp_stream === true;
        instance._wasm_set_proxy_mqtt_plain_capability(mqttPlain ? 1 : 0);
    }
    if (cfg.proxy?.tftpPort && typeof instance._wasm_set_proxy_tftp_port === 'function') {
        instance._wasm_set_proxy_tftp_port(cfg.proxy.tftpPort);
    }
    if (cfg.proxy?.telnetPort && typeof instance._wasm_set_proxy_telnet_port === 'function') {
        instance._wasm_set_proxy_telnet_port(cfg.proxy.telnetPort);
    }

    await mountSd(!!cfg.persistSd);
    installFsTracker();
    if (cfg.persistSd) schedulePeriodicFlush();

    post({
        type: 'ready',
        memoryBuffer:    instance.HEAPU8.buffer,  // SharedArrayBuffer (with -pthread)
        fbPtr:           instance._wasm_framebuffer_ptr(),
        fbWidth:         instance._wasm_framebuffer_width(),
        fbHeight:        instance._wasm_framebuffer_height(),
        fbGenerationPtr: instance._wasm_framebuffer_generation_ptr(),
        keyRingPtr:      instance._wasm_key_ring_ptr(),
        keyRingHeadPtr:  instance._wasm_key_ring_head_ptr(),
        keyRingTailPtr:  instance._wasm_key_ring_tail_ptr(),
        keyRingSize:     instance._wasm_key_ring_size(),
    });

    // wasm_boot spawns a pthread that runs MMBasic_RunPromptLoop and
    // returns immediately. The pthread blocks for real on nanosleep
    // (Atomics.wait under the hood) without stalling this worker's
    // main event loop, which keeps processing FS postMessages.
    try { instance._wasm_boot(); }
    catch (e) { post({ type: 'log', level: 'warn', line: 'wasm_boot threw: ' + e }); }
}

self.onmessage = async (e) => {
    const msg = e.data;
    if (!msg || !msg.type) return;

    if (msg.type === 'init') {
        initInstance(msg.cfg || {});
        return;
    }
    if (!instance) return;  // everything else needs a running instance

    switch (msg.type) {
        case 'key':
            instance._wasm_push_key(msg.code);
            break;

        case 'fs-write':
            try { instance.FS.writeFile(msg.path, new Uint8Array(msg.data)); }
            catch (err) { post({ type: 'log', level: 'warn', line: 'fs-write failed: ' + err }); }
            break;

        case 'fs-unlink':
            try { instance.FS.unlink(msg.path); }
            catch (err) { post({ type: 'log', level: 'warn', line: 'fs-unlink failed: ' + err }); }
            break;

        case 'fs-list':
            try {
                const raw = instance.FS.readdir(msg.dir).filter(n => n !== '.' && n !== '..');
                const entries = [];
                for (const name of raw) {
                    let isDir = false;
                    let size = 0;
                    try {
                        const st = instance.FS.stat(`${msg.dir}/${name}`.replace(/\/+/g, '/'));
                        isDir = (st.mode & 0xF000) === 0x4000;   // S_IFDIR
                        size = st.size;
                    } catch (_) { /* unreadable — list anyway */ }
                    entries.push({ name, isDir, size });
                }
                post({ type: 'fs-list-result', reqId: msg.reqId, dir: msg.dir, entries });
            } catch (err) {
                post({ type: 'fs-list-result', reqId: msg.reqId, dir: msg.dir, entries: [], error: String(err) });
            }
            break;

        case 'fs-read':
            try {
                const data = instance.FS.readFile(msg.path);
                post({ type: 'fs-read-result', reqId: msg.reqId, path: msg.path, data: data.buffer }, [data.buffer]);
            } catch (err) {
                post({ type: 'fs-read-result', reqId: msg.reqId, path: msg.path, error: String(err) });
            }
            break;

        case 'fs-reset':
            try {
                const names = instance.FS.readdir(SD_ROOT).filter(n => n !== '.' && n !== '..');
                for (const n of names) {
                    try { instance.FS.unlink(`${SD_ROOT}/${n}`); } catch (_) {}
                }
                const count = populateFromBundle();
                try { await syncfs(false); } catch (_) { /* best-effort */ }
                post({ type: 'fs-reset-result', reqId: msg.reqId, count });
            } catch (err) {
                post({ type: 'fs-reset-result', reqId: msg.reqId, count: 0, error: String(err) });
            }
            break;

        case 'fs-syncfs':
            try { await syncfs(false); post({ type: 'fs-syncfs-result', reqId: msg.reqId }); }
            catch (err) { post({ type: 'fs-syncfs-result', reqId: msg.reqId, error: String(err) }); }
            break;

        case 'set-slowdown':
            instance._wasm_set_slowdown_us(msg.us);
            break;
    }
};
