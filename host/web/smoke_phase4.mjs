// Headless smoke for the shipping-app performance path.
//
// Verifies:
//   1. Main-thread rAF advances at display rate.
//   2. PAUSE 1000 round-trips within 950–1200 ms (ASYNCIFY unwound in
//      the worker, not busy-wait).
//   3. Framebuffer generation counter (read from shared memory, not
//      via a ccall) advances on draws.
//   4. A FASTGFX FPS 50 loop produces ~50 Hz SWAP rate with no rAF
//      gap > 55 ms — proxy for main-thread smoothness while allowing
//      occasional headless-browser scheduling jitter.
//
// Uses the worker-path convenience API on window.picomite (fsWrite,
// fsRead, memoryU32, fbGenerationIdx).

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8125;
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
        await page.waitForFunction(() => !!window.picomite?.memoryU32, { timeout: 25000 });
        await page.click('#screen');

        // ------ Check 1: main-thread rAF ------------------------------
        const rafTicks = await page.evaluate(() => new Promise((resolve) => {
            let n = 0;
            const deadline = performance.now() + 500;
            const tick = () => {
                if (performance.now() < deadline) { n++; requestAnimationFrame(tick); }
                else resolve(n);
            };
            requestAnimationFrame(tick);
        }));
        if (rafTicks < 20 || rafTicks > 120) {
            fail(`rAF fired ${rafTicks} times / 500 ms; expected 20–120`);
        }
        console.log(`OK — rAF @ ~${rafTicks * 2} Hz (${rafTicks} ticks in 500 ms).`);

        // ------ Check 2: PAUSE 1000 round-trip ------------------------
        async function typeLine(line) {
            await page.keyboard.type(line, { delay: 5 });
            await page.keyboard.press('Enter');
        }
        const genBefore = await page.evaluate(() =>
            window.picomite.memoryU32[window.picomite.fbGenerationIdx]);

        await typeLine('NEW');
        await sleep(100);
        await typeLine('10 OPEN "out.txt" FOR OUTPUT AS #1');
        await typeLine('20 T0=TIMER');
        await typeLine('30 PAUSE 1000');
        await typeLine('40 PRINT #1, STR$(TIMER - T0, 0, 0)');
        await typeLine('50 CLOSE #1');
        await typeLine('RUN');
        await sleep(2500);

        const pauseResult = await page.evaluate(async () => {
            try {
                const bytes = await window.picomite.fsRead('/sd/out.txt');
                return new TextDecoder().decode(bytes);
            } catch (e) { return null; }
        });
        if (!pauseResult) fail(`could not read /sd/out.txt; logs:\n${logs.slice(-20).join('\n')}`);
        const pauseMs = parseInt(pauseResult.trim(), 10);
        if (!Number.isFinite(pauseMs) || pauseMs < 950 || pauseMs > 1200) {
            fail(`PAUSE 1000 measured ${pauseMs} ms (expected 950–1200)`);
        }
        console.log(`OK — PAUSE 1000 measured ${pauseMs} ms.`);

        const genAfter = await page.evaluate(() =>
            window.picomite.memoryU32[window.picomite.fbGenerationIdx]);
        if (genAfter === genBefore) fail(`generation stuck at ${genBefore}`);
        console.log(`OK — framebuffer generation advanced (${genBefore} → ${genAfter}).`);

        // ------ Check 3: FASTGFX FPS 50 loop --------------------------
        await page.evaluate(() => {
            const prog = [
                '10 FASTGFX CREATE',
                '15 FASTGFX FPS 50',
                '20 I = 0',
                '30 DO',
                '40 BOX (I*3) MOD 300, 50, (I*3) MOD 300 + 10, 60, 1, RGB(WHITE), RGB(BLACK)',
                '50 FASTGFX SWAP',
                '55 FASTGFX SYNC',
                '60 I = I + 1',
                '70 LOOP UNTIL I >= 500',
                '80 FASTGFX CLOSE',
            ].join('\n') + '\n';
            window.picomite.fsWrite('/sd/phase4.bas', new TextEncoder().encode(prog));
        });
        await sleep(200);  // let the fs-write postMessage settle
        await typeLine('NEW');
        await sleep(100);
        await typeLine('RUN "phase4.bas"');
        await sleep(500);  // stabilise

        const sample = await page.evaluate(async () => {
            const p = window.picomite;
            const genAt = () => p.memoryU32[p.fbGenerationIdx];
            const t0 = performance.now();
            const gen0 = genAt();
            const rafGaps = [];
            let last = performance.now();
            const loop = (t) => {
                rafGaps.push(t - last);
                last = t;
                if (performance.now() - t0 < 3000) requestAnimationFrame(loop);
            };
            requestAnimationFrame(loop);
            await new Promise((r) => setTimeout(r, 3200));
            const t1 = performance.now();
            const gen1 = genAt();
            return {
                genRate: (gen1 - gen0) / ((t1 - t0) / 1000),
                rafGaps: rafGaps.slice(1),
            };
        });
        if (sample.genRate < 40 || sample.genRate > 65) {
            fail(`gen rate = ${sample.genRate.toFixed(1)} Hz (expected 40–65 for FPS 50)`);
        }
        const maxGap = Math.max(...sample.rafGaps);
        const meanGap = sample.rafGaps.reduce((a, b) => a + b, 0) / sample.rafGaps.length;
        if (maxGap > 55) fail(`worst rAF gap = ${maxGap.toFixed(1)} ms (expected ≤ 55)`);
        console.log(`OK — FASTGFX@50: gen rate ${sample.genRate.toFixed(1)} Hz; rAF gap mean ${meanGap.toFixed(1)} ms, worst ${maxGap.toFixed(1)} ms over ${sample.rafGaps.length} frames.`);

        console.log('All Phase 4 smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
