// Headless smoke for the worker-backed test page (worker_test.html).
//
// Verifies:
//   1. COOP/COEP are set → crossOriginIsolated = true, SAB available.
//   2. Worker reports 'ready' within 20 s.
//   3. After boot, the main-thread view into the worker's wasm heap
//      is writable by the worker (framebuffer contains non-zero bytes
//      from the boot banner).
//   4. requestAnimationFrame is firing on main thread, independent of
//      the worker's interpretation loop.
//
// Success = exit 0.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8126;
const PAGE_URL = `http://127.0.0.1:${PORT}/worker_test.html`;

function startServer() {
    const cwd = new URL('.', import.meta.url).pathname;
    const child = spawn('python3', ['./serve.py', String(PORT)], {
        cwd, stdio: ['ignore', 'ignore', 'inherit'],
    });
    child.unref();
    return child;
}
async function waitForPort() {
    for (let i = 0; i < 40; i++) {
        try { const r = await fetch(`http://127.0.0.1:${PORT}/worker_test.html`); if (r.ok) return; } catch {}
        await sleep(200);
    }
    throw new Error('server did not come up');
}
function fail(msg) { console.error('FAIL —', msg); process.exit(1); }

const server = startServer();
try {
    await waitForPort();
    const browser = await chromium.launch({
        headless: true,
        args: [
            '--use-angle=swiftshader',
            '--enable-unsafe-swiftshader',
            '--ignore-gpu-blocklist',
        ],
    });
    try {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        const logs = [];
        page.on('console',    (msg) => logs.push(`[${msg.type()}] ${msg.text()}`));
        page.on('pageerror',  (e)   => logs.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });

        // Check 1: cross-origin isolation
        const isolated = await page.evaluate(() => window.crossOriginIsolated);
        if (!isolated) fail('crossOriginIsolated = false (COOP/COEP headers missing?)');
        console.log('OK — crossOriginIsolated = true.');

        // Check 2: worker reports ready
        await page.waitForFunction(() => {
            const s = document.getElementById('status');
            return s && s.textContent.startsWith('Worker ready');
        }, { timeout: 20000 });
        console.log('OK — worker reported ready.');

        // Check 3: rAF is firing on main thread
        const rafTicks = await page.evaluate(() => new Promise((resolve) => {
            let n = 0;
            const deadline = performance.now() + 500;
            const tick = () => {
                if (performance.now() < deadline) { n++; requestAnimationFrame(tick); }
                else resolve(n);
            };
            requestAnimationFrame(tick);
        }));
        if (rafTicks < 20) fail(`main-thread rAF only fired ${rafTicks} times / 500 ms`);
        console.log(`OK — main-thread rAF @ ~${rafTicks * 2} Hz (${rafTicks} ticks in 500 ms).`);

        // Give the worker another second so the REPL prompt has painted
        // at least a few characters' worth of non-zero pixels.
        await sleep(2000);

        // Check 4: the worker's wasm wrote banner pixels into shared
        // memory. We read the SAB view directly (not the canvas) via
        // the dev hook exposed by worker_test.mjs.
        const nonZeroBytes = await page.evaluate(() => {
            const h = window.__picomiteWorker;
            if (!h || !h.memoryBytes) return -1;
            const bytes = new Uint8Array(h.memoryBytes.buffer, h.fbPtr, h.fbWidth * h.fbHeight * 4);
            let count = 0;
            for (let i = 0; i < bytes.length; i++) if (bytes[i] !== 0) count++;
            return count;
        });
        if (nonZeroBytes < 0) fail('worker hook not installed');
        if (nonZeroBytes === 0) fail('shared framebuffer is all zero — worker did not draw anything');
        console.log(`OK — shared framebuffer populated (${nonZeroBytes} non-zero bytes).`);

        // Check 5: audio proxy. Wrap window.picomiteAudio in a logging
        // proxy, type PLAY TONE into the REPL, confirm the wrapped
        // tone() fires on main thread (i.e. the worker postMessaged
        // the audio event and our dispatcher relayed it).
        await page.evaluate(() => {
            window.__audioCalls = [];
            const orig = window.picomiteAudio;
            window.picomiteAudio = new Proxy(orig, {
                get(t, p) {
                    const v = t[p];
                    if (typeof v !== 'function') return v;
                    return (...args) => { window.__audioCalls.push({ op: p, args }); return v.apply(t, args); };
                },
            });
        });
        await page.click('#screen');
        await page.keyboard.type('PLAY TONE 440,440,500', { delay: 20 });
        await page.keyboard.press('Enter');
        await sleep(600);
        const toneCalls = await page.evaluate(() =>
            window.__audioCalls.filter((c) => c.op === 'tone'));
        if (!toneCalls.length) {
            fail('PLAY TONE did not reach window.picomiteAudio — worker audio proxy broken');
        }
        console.log(`OK — worker → main audio proxy: ${toneCalls.length} tone() call(s).`);

        console.log('All worker smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
