// Headless WASM smoke for MMgetline / INPUT in the browser port.
//
// Drives the shared runtime/runtime_getline.c through the browser key-ring
// path: keys are pushed via `worker.postMessage({type:'key', code})` (the
// same path the keyboard handler uses, just bypassing the DOM) and results
// are pulled back through the worker's FS bridge — BASIC writes the captured
// string to /sd/r.txt and we read it with fsRead.
//
// The Pico-side smoke (porttools/pico_input_smoke.py) covers all 20 corner
// cases on hardware; this smoke is a slimmer integration check that the
// WASM key-ring → MMfgetc → MMInkey → MMgetline path terminates correctly,
// guarding against the spine-extraction regression that left INPUT hanging
// because Enter (0x0D) was dropped as a '\r' on the console path.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8131;
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

// Hardware_Includes.h key codes (single-byte ring entries).
const F2 = 0x92;
const ENTER = 0x0D;
const BKSP_RAW = 0x7F;  // MMInkey turns 0x7F into BKSP (0x08)
const TAB = 0x09;
const STX = 0x02;
const ETX = 0x03;

// BASIC's OPEN resolves relative to host_sd_root = "/sd" — so the BASIC
// statement opens "r.txt" while the JS side reads /sd/r.txt back.
const BASIC_RESULT_NAME = 'r.txt';
const RESULT_PATH = '/sd/r.txt';
const RESULT_RE = new RegExp(String.fromCharCode(STX) + '(.*?)' + String.fromCharCode(ETX), 's');

const CASES = [
    { name: 'basic_cr',     payload: [...'hello'].map(c => c.charCodeAt(0)),           expected: 'hello' },
    { name: 'empty',        payload: [],                                               expected: '' },
    { name: 'backspace',    payload: [...'abcd'].map(c => c.charCodeAt(0))
                                       .concat([BKSP_RAW, BKSP_RAW])
                                       .concat([...'xy'].map(c => c.charCodeAt(0))),  expected: 'abxy' },
    { name: 'tab_pad_3',    payload: ['x'.charCodeAt(0), TAB, ...'ab'].map(c => typeof c === 'number' ? c : c.charCodeAt(0)),
                                                                                       expected: 'x   ab' },
    { name: 'tab_pad_2',    payload: ['x'.charCodeAt(0), 'y'.charCodeAt(0), TAB, 'a'.charCodeAt(0), 'b'.charCodeAt(0)],
                                                                                       expected: 'xy  ab' },
    { name: 'f2_macro',     payload: [F2],                                             expected: 'RUN' },
];

async function pushKeyCodes(page, codes) {
    await page.evaluate((arr) => {
        for (const c of arr) {
            window.picomite.worker.postMessage({ type: 'key', code: c });
        }
    }, codes);
}

async function fsRead(page, path) {
    return await page.evaluate(async (p) => {
        try {
            const u8 = await window.picomite.fsRead(p);
            return Array.from(u8);
        } catch (e) { return null; }
    }, path);
}

async function fsUnlink(page, path) {
    await page.evaluate((p) => {
        window.picomite.worker.postMessage({ type: 'fs-unlink', path: p });
    }, path);
}

// Quick command runner: push the immediate-mode text + Enter, then wait a
// settle window so the interpreter has time to execute and the framebuffer
// to update. The smoke doesn't need precise prompt detection — the FS write
// at the end of each case is the synchronisation point.
async function runImmediate(page, command, settleMs = 250) {
    const bytes = [...command].map(c => c.charCodeAt(0)).concat([ENTER]);
    await pushKeyCodes(page, bytes);
    await sleep(settleMs);
}

async function runCase(page, c) {
    // Each iteration starts from a clean prompt: drop any stale result file
    // so a failed write surfaces as "no file" instead of a stale match.
    await fsUnlink(page, RESULT_PATH);
    await sleep(100);

    // Drive INPUT a$
    await pushKeyCodes(page, [...'INPUT a$'].map(c => c.charCodeAt(0)).concat([ENTER]));
    await sleep(250);

    // Push the test payload + the closing Enter.
    await pushKeyCodes(page, c.payload.concat([ENTER]));
    await sleep(300);

    // Write a$ sandwiched in STX/ETX so we can pluck it out of the file.
    await runImmediate(
        page,
        `OPEN "${BASIC_RESULT_NAME}" FOR OUTPUT AS #1:PRINT #1,CHR$(${STX})+a$+CHR$(${ETX});:CLOSE #1`,
        500,
    );

    // Poll briefly for the file to materialise; FS write goes through the
    // worker's emscripten MEMFS and is normally available within a frame.
    let bytes = null;
    for (let i = 0; i < 20; i++) {
        bytes = await fsRead(page, RESULT_PATH);
        if (bytes && bytes.length >= 2) break;
        await sleep(100);
    }
    if (!bytes) return { ok: false, detail: 'no result file written' };
    const text = String.fromCharCode(...bytes);
    const m = RESULT_RE.exec(text);
    if (!m) return { ok: false, detail: `no sentinel match in ${JSON.stringify(text)}` };
    const captured = m[1];
    const ok = captured === c.expected;
    return { ok, detail: `captured=${JSON.stringify(captured)} expected=${JSON.stringify(c.expected)}` };
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
        const errors = [];
        page.on('pageerror', (e) => errors.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });
        const isolated = await page.evaluate(() => window.crossOriginIsolated);
        if (!isolated) fail('crossOriginIsolated = false');
        await page.waitForFunction(() => !!window.picomite?.worker, { timeout: 25000 });

        // Give the REPL a moment after Ready to finish printing the banner.
        await sleep(1500);

        // Focus the screen so any DOM-routed key events also work, though
        // the smoke pushes via worker.postMessage directly.
        await page.click('#screen');

        let passed = 0;
        const failures = [];
        for (const c of CASES) {
            const { ok, detail } = await runCase(page, c);
            const status = ok ? 'OK  ' : 'FAIL';
            console.log(`  ${status}  ${c.name.padEnd(14)}  ${detail}`);
            if (ok) passed += 1;
            else failures.push({ name: c.name, detail });
        }

        const total = passed + failures.length;
        console.log(`\n${passed}/${total} passed`);
        if (failures.length) {
            console.log('\nFailures:');
            for (const f of failures) console.log(`  - ${f.name}: ${f.detail}`);
            process.exitCode = 1;
        }
        if (errors.length) {
            console.log('\nPage errors:');
            for (const e of errors) console.log('  ' + e);
        }
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
