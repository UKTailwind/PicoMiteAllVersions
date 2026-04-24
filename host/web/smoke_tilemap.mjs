// Headless reproducer for the pico_blocks_tilemap.bas ball-stuck-at-(0,0)
// bug.  Boots the app, FRUNs the demo for a few seconds, screenshots the
// canvas, then inspects the top-left 16x16 block for a red sprite that
// shouldn't be there.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8129;
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
        page.on('console',   (m) => console.log(`[${m.type()}]`, m.text()));
        page.on('pageerror', (e) => console.error('[pageerror]', e.message));
        await page.goto(PAGE_URL, { waitUntil: 'load' });

        await page.waitForFunction(() => window.picomite && window.picomite.worker, null, { timeout: 15000 });
        await sleep(2000);   // let the worker boot MMBasic + show prompt
        console.log('Boot ready');

        // Type FRUN "pico_blocks_tilemap.bas"<Enter>
        await page.focus('#screen');
        await page.keyboard.type('FRUN "pico_blocks_tilemap.bas"');
        await page.keyboard.press('Enter');

        // Wait for title screen, then press SPACE to launch the ball.
        await sleep(2500);
        await page.keyboard.press('Space');
        await sleep(4000);  // ball is launched; positions should be mid-flight

        // Screenshot the canvas.
        const path = '/tmp/wasm_tilemap.png';
        await page.locator('#screen').screenshot({ path });
        console.log('Screenshot:', path);

        // Sample pixel at (8, 8) of the canvas content — if the ball is stuck
        // there the pixel is bright red; on a correct render it's background
        // (black) or HUD text (white).
        const pixel = await page.evaluate(() => {
            const c = document.getElementById('screen');
            const tmp = document.createElement('canvas');
            tmp.width = c.width; tmp.height = c.height;
            const g = tmp.getContext('2d');
            g.drawImage(c, 0, 0);
            const d = g.getImageData(8, 8, 1, 1).data;
            return { r: d[0], g: d[1], b: d[2] };
        });
        console.log('Pixel at (8,8):', pixel);
        const isRed = pixel.r > 200 && pixel.g < 80 && pixel.b < 80;
        if (isRed) {
            console.log('REPRO — ball ghost present at top-left');
            process.exit(1);
        } else {
            console.log('OK — no red ghost at top-left');
        }
    } finally {
        await browser.close();
    }
} finally {
    server.kill();
}
