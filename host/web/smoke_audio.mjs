// Headless smoke test: boot the WASM page, wait for the REPL to accept
// input, issue `PLAY TONE 440,500`, and verify the audio bridge fires.
//
// Uses the Playwright install under the pico-gamer sidecar — we don't
// want to pull a full node_modules into this repo for a one-off check.
//
// Success = exit 0, printed "OK — audio bridge received tone(…)".

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8124;
const PAGE_URL = `http://127.0.0.1:${PORT}/`;

function startServer() {
    const cwd = new URL('.', import.meta.url).pathname;
    const child = spawn('python3', ['./serve.py', String(PORT)], {
        cwd,
        stdio: ['ignore', 'ignore', 'inherit'],
    });
    child.unref();
    return child;
}

async function waitForPort() {
    for (let i = 0; i < 30; i++) {
        try {
            const r = await fetch(PAGE_URL);
            if (r.ok) return;
        } catch (_) {}
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

        const consoleMsgs = [];
        page.on('console', (msg) => consoleMsgs.push(`[${msg.type()}] ${msg.text()}`));
        page.on('pageerror', (e) => consoleMsgs.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });

        await page.waitForFunction(() => !!window.picomite?.memoryBytes, { timeout: 25000 });

        // Wrap window.picomiteAudio so we can observe calls. The audio
        // module has already installed its real implementation on first
        // script eval; we preserve it and add a call log.
        await page.evaluate(() => {
            window.__audioCalls = [];
            const orig = window.picomiteAudio;
            window.picomiteAudio = new Proxy(orig, {
                get(target, prop) {
                    const v = target[prop];
                    if (typeof v !== 'function') return v;
                    return (...args) => {
                        window.__audioCalls.push({ op: prop, args });
                        return v.apply(target, args);
                    };
                }
            });
        });

        // Focus the canvas (clicks count as user gesture, unblocking audio).
        await page.click('#screen');

        // Type PLAY TONE 440,440,500 then Enter. `.type()` fires keydown
        // events for each character — app.mjs's key handler forwards them
        // to wasm_push_key.
        await page.keyboard.type('PLAY TONE 440,440,500', { delay: 20 });
        await page.keyboard.press('Enter');

        // Let the C side execute and call through to picomiteAudio.tone.
        await sleep(300);

        const calls = await page.evaluate(() => window.__audioCalls);
        const toneCalls = calls.filter((c) => c.op === 'tone');

        if (!toneCalls.length) {
            console.error('FAIL — no tone() call observed. All calls:', calls);
            console.error('Console log tail:');
            for (const line of consoleMsgs.slice(-40)) console.error('  ' + line);
            process.exit(1);
        }
        const last = toneCalls[toneCalls.length - 1];
        const [l, r, ms] = last.args;
        if (l !== 440 || r !== 440 || ms !== 500) {
            console.error(`FAIL — unexpected tone args: l=${l} r=${r} ms=${ms}`);
            process.exit(1);
        }
        console.log(`OK — audio bridge received tone(${l}, ${r}, ${ms}).`);

        // PLAY SOUND 1,B,Q,220,20 — verify slot/ch/type/freq/vol all arrive.
        await page.keyboard.type('PLAY SOUND 1,B,Q,220,20', { delay: 20 });
        await page.keyboard.press('Enter');
        await sleep(200);
        let calls2 = await page.evaluate(() => window.__audioCalls);
        const soundCalls = calls2.filter((c) => c.op === 'sound');
        if (!soundCalls.length) {
            console.error('FAIL — no sound() call after PLAY SOUND. Calls:', calls2);
            process.exit(1);
        }
        const sc = soundCalls[soundCalls.length - 1];
        const [slot, ch, type, f, vol] = sc.args;
        if (slot !== 1 || ch !== 'B' || type !== 'Q' || f !== 220 || vol !== 20) {
            console.error(`FAIL — unexpected sound args: slot=${slot} ch=${ch} type=${type} f=${f} vol=${vol}`);
            process.exit(1);
        }
        console.log(`OK — audio bridge received sound(${slot}, "${ch}", "${type}", ${f}, ${vol}).`);

        // PLAY STOP routes through.
        await page.keyboard.type('PLAY STOP', { delay: 20 });
        await page.keyboard.press('Enter');
        await sleep(200);
        calls2 = await page.evaluate(() => window.__audioCalls);
        const stopCalls = calls2.filter((c) => c.op === 'stop');
        if (!stopCalls.length) {
            console.error('FAIL — no stop() call observed after PLAY STOP');
            process.exit(1);
        }
        console.log(`OK — audio bridge received stop() (${stopCalls.length} time(s)).`);

    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
