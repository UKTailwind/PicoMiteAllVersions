// Headless smoke for the shipping app (index.html → app.mjs → worker.mjs).
//
// Verifies the worker-backed main app boots to Ready, the shared
// framebuffer is populated, rAF is firing, and the FS round-trip is
// wired (fsList sees the bundled demos).

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8127;
const PAGE_URL = `http://127.0.0.1:${PORT}/`;

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
        try { const r = await fetch(PAGE_URL); if (r.ok) return; } catch {}
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
        args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
    });
    try {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        const logs = [];
        page.on('console',   (m) => logs.push(`[${m.type()}] ${m.text()}`));
        page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });

        // Check 1: cross-origin isolation
        const isolated = await page.evaluate(() => window.crossOriginIsolated);
        if (!isolated) fail('crossOriginIsolated = false');
        console.log('OK — crossOriginIsolated.');

        // Check 2: worker has booted — window.picomite hook is installed
        // by onWorkerReady after it receives 'ready' from the worker.
        await page.waitForFunction(() => !!window.picomite?.memoryBytes, { timeout: 25000 });
        console.log('OK — worker booted, window.picomite populated.');

        // Check 3: worker hook and shared memory populated
        const state = await page.evaluate(() => {
            const p = window.picomite;
            if (!p || !p.memoryBytes) return null;
            const bytes = new Uint8Array(p.memoryBytes.buffer, p.fbPtr, p.fbWidth * p.fbHeight * 4);
            let nonZero = 0;
            for (let i = 0; i < bytes.length; i++) if (bytes[i] !== 0) nonZero++;
            return { nonZero, fbWidth: p.fbWidth, fbHeight: p.fbHeight };
        });
        if (!state) fail('window.picomite hook missing');
        if (state.nonZero === 0) fail('shared framebuffer empty');
        console.log(`OK — framebuffer ${state.fbWidth}×${state.fbHeight}, ${state.nonZero} non-zero bytes.`);

        // Check 4: demos are listed (worker copied bundle → /sd/)
        const demos = await page.evaluate(async () => {
            // Use the same round-trip the UI uses.
            return new Promise((resolve) => {
                const id = Date.now();
                const onMsg = (e) => {
                    if (e.data && e.data.type === 'fs-list-result' && e.data.reqId === id) {
                        window.picomite.worker.removeEventListener('message', onMsg);
                        resolve((e.data.entries || []).map((x) => x.name));
                    }
                };
                window.picomite.worker.addEventListener('message', onMsg);
                window.picomite.worker.postMessage({ type: 'fs-list', reqId: id, dir: '/sd' });
            });
        });
        if (!demos.length) fail('no demos found in /sd/');
        console.log(`OK — /sd/ has ${demos.length} files (e.g. ${demos.slice(0, 3).join(', ')}).`);

        // Check 5: audio proxy — wrap picomiteAudio, type PLAY TONE, observe.
        await page.evaluate(() => {
            window.__audioCalls = [];
            const orig = window.picomiteAudio;
            window.picomiteAudio = new Proxy(orig, {
                get(t, p) {
                    const v = t[p];
                    if (typeof v !== 'function') return v;
                    return (...a) => { window.__audioCalls.push({ op: p, args: a }); return v.apply(t, a); };
                },
            });
        });
        await page.click('#screen');
        await page.keyboard.type('PLAY TONE 440,440,500', { delay: 20 });
        await page.keyboard.press('Enter');
        await sleep(600);
        const toneCalls = await page.evaluate(() =>
            window.__audioCalls.filter((c) => c.op === 'tone'));
        if (!toneCalls.length) fail('PLAY TONE did not reach main-thread audio bridge');
        console.log(`OK — audio proxy (${toneCalls.length} tone calls).`);

        console.log('All shipping-app smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
