// Headless smoke for the BASIC `NEW` command in the browser build.
//
// Background: cmd_new calls hal_flash_erase(realflashpointer, MAX_PROG_SIZE)
// after FlashWriteInit(PROGRAM_FLASH) sets realflashpointer = PROGSTART
// (~2.5 MB on wasm). The legacy flash_range_erase / _program shims in
// host_fs_shims.c silently drop offsets past 2 * MAX_PROG_SIZE, so the
// program region (which the LOAD path writes to at offset 0 in
// flash_prog_buf) is NEVER cleared by NEW. Symptom: after LOAD then
// NEW, the program is still there and RUN runs it again.
//
// This smoke proves the bug via a file-side-effect probe (a loaded
// "polluter" program appends a known marker line to /sd/log.txt — if
// NEW worked, the polluter never runs after NEW and the marker stays
// absent). The host_native equivalent lives in
// porttools/host_new_smoke.py.

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

        async function typeLine(line) {
            await page.keyboard.type(line, { delay: 5 });
            await page.keyboard.press('Enter');
        }
        async function readUtf8(path) {
            return await page.evaluate(async (p) => {
                try {
                    const bytes = await window.picomite.fsRead(p);
                    return new TextDecoder().decode(bytes);
                } catch (e) { return null; }
            }, path);
        }
        async function writeUtf8(path, text) {
            await page.evaluate(({ p, t }) => {
                window.picomite.fsWrite(p, new TextEncoder().encode(t));
            }, { p: path, t: text });
            // Let the fs-write postMessage settle (matches phase4 smoke).
            await sleep(200);
        }
        async function rm(path) {
            await page.evaluate(async (p) => {
                try { await window.picomite.fsWrite(p, new Uint8Array()); } catch {}
            }, path);
        }

        // ------ Probe: load a "polluter" program that appends to log.txt,
        //        NEW, then RUN. If NEW worked, polluter never runs and the
        //        marker stays absent. -----------------------------------
        await writeUtf8('/sd/polluter.bas', [
            '10 OPEN "log.txt" FOR APPEND AS #1',
            '20 PRINT #1, "POLLUTED"',
            '30 CLOSE #1',
            '',
        ].join('\n'));
        await rm('/sd/log.txt');

        await typeLine('LOAD "polluter.bas"');
        await sleep(150);
        await typeLine('NEW');
        await sleep(150);
        await typeLine('RUN');
        await sleep(400);

        const logAfterNew = (await readUtf8('/sd/log.txt')) ?? '';
        if (logAfterNew.includes('POLLUTED')) {
            fail(`polluter ran after NEW (log.txt = ${JSON.stringify(logAfterNew)}); recent logs:\n${logs.slice(-20).join('\n')}`);
        }
        console.log('OK — polluter did not run after NEW.');

        // ------ LOAD-after-NEW still works (regression guard against
        //        the fix going too far the other way). ------------------
        await writeUtf8('/sd/sentinel.bas', [
            '10 OPEN "sentinel.txt" FOR OUTPUT AS #1',
            '20 PRINT #1, "SENTINEL-RAN"',
            '30 CLOSE #1',
            '',
        ].join('\n'));
        await rm('/sd/sentinel.txt');

        await typeLine('NEW');
        await sleep(150);
        await typeLine('LOAD "sentinel.bas"');
        await sleep(150);
        await typeLine('RUN');
        await sleep(400);

        const sentinel = (await readUtf8('/sd/sentinel.txt')) ?? '';
        if (!sentinel.includes('SENTINEL-RAN')) {
            fail(`sentinel program did not run after NEW + LOAD (sentinel.txt = ${JSON.stringify(sentinel)}); recent logs:\n${logs.slice(-20).join('\n')}`);
        }
        console.log('OK — LOAD after NEW still runs a fresh program.');

        // ------ Double NEW is idempotent (no error / no crash). --------
        await typeLine('LOAD "polluter.bas"');
        await sleep(150);
        await typeLine('NEW');
        await sleep(150);
        await typeLine('NEW');
        await sleep(150);
        // After two NEWs, RUN should be a no-op (or surface an error).
        // The robust assertion is the polluter side-effect counter.
        const before = (await readUtf8('/sd/log.txt')) ?? '';
        await typeLine('RUN');
        await sleep(400);
        const after = (await readUtf8('/sd/log.txt')) ?? '';
        if (after !== before) {
            fail(`double-NEW didn't clear program; log.txt grew (${JSON.stringify(before)} -> ${JSON.stringify(after)})`);
        }
        console.log('OK — double NEW is idempotent (no polluter run).');

        console.log('PASS');
    } finally {
        await browser.close();
    }
} finally {
    server.kill();
}
