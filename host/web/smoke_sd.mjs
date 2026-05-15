// Headless WASM smoke for the SD-backed filesystem path (B: drive,
// host_sd_root="/sd", vm_host_fat in-RAM FAT).
//
// Covers:
//   - drive switch + MKDIR/CHDIR
//   - OPEN/PRINT/CLOSE write
//   - OPEN/INPUT/CLOSE read-back
//   - cmd_files (FILES) in NAME / SIZE / TYPE / TIME sort modes
//   - KILL / RMDIR cleanup
//
// Strategy: drive BASIC commands through the worker key-ring (same
// pattern as smoke_input.mjs / smoke_editor.mjs). After each step,
// have BASIC write a small sentinel to /smoke/r.txt and read it back
// via window.picomite.fsRead("/sd/smoke/r.txt"). FILES output goes
// to the terminal canvas which is impractical to parse headlessly,
// so the FILES tests verify "interpreter still alive after FILES"
// via the sentinel-after — if cmd_files crashed or hung, the
// follow-up OPEN/PRINT/CLOSE would never run and the sentinel never
// appears.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8137;
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

const ENTER = 0x0D;
// BASIC writes to "r.txt" which on B: drive resolves to /sd/smoke/r.txt
// (after MKDIR /smoke + CHDIR /smoke).
const RESULT_NAME = 'r.txt';
const RESULT_PATH = '/sd/smoke/r.txt';

// Five fixture files with distinct sizes + 2 subdirectories. All ≤7
// chars to keep things simple. Sizes all-distinct so SIZE sort has no
// ties to break.
const FIXTURE_FILES = [
    { name: 'aaa.bas', content: 'AA' },          //  2 bytes
    { name: 'bbb.bas', content: 'BBBB' },        //  4
    { name: 'ccc.txt', content: 'CCCCCC' },      //  6
    { name: 'ddd.dat', content: 'DDDDDDDD' },    //  8
    { name: 'eee.bas', content: '0123456789' },  // 10
];
const FIXTURE_DIRS = ['sub1', 'sub2'];

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

async function runImmediate(page, command, settleMs = 400) {
    const bytes = [...command].map(c => c.charCodeAt(0)).concat([ENTER]);
    await pushKeyCodes(page, bytes);
    await sleep(settleMs);
}

// Run `command`, then write a known literal to r.txt, then read back
// to confirm the literal appeared. The literal write only runs if
// `command` returned to the REPL prompt — so this catches FILES (or
// any other command) hanging or crashing the interpreter.
async function runSentinelStep(page, command, sentinel, commandSettleMs = 800) {
    await fsUnlink(page, RESULT_PATH);
    await sleep(120);
    await runImmediate(page, command, commandSettleMs);
    await runImmediate(
        page,
        `OPEN "${RESULT_NAME}" FOR OUTPUT AS #1:PRINT #1,"${sentinel}";:CLOSE #1`,
        500,
    );
    // Poll for the result file.
    let bytes = null;
    for (let i = 0; i < 30; i++) {
        bytes = await fsRead(page, RESULT_PATH);
        if (bytes && bytes.length >= sentinel.length) break;
        await sleep(100);
    }
    if (!bytes) return { ok: false, detail: 'no result file (cmd may have hung)' };
    const text = String.fromCharCode(...bytes);
    const ok = text === sentinel;
    return { ok, detail: `captured=${JSON.stringify(text)} expected=${JSON.stringify(sentinel)}` };
}

// Read-back variant: instead of confirming a fixed literal, the
// follow-up command writes the value of a BASIC expression we want
// to verify. Used for the file_read_back test.
async function runReadBackStep(page, setupCommand, valueExpr, expected, settleMs = 600) {
    await fsUnlink(page, RESULT_PATH);
    await sleep(120);
    if (setupCommand) await runImmediate(page, setupCommand, settleMs);
    await runImmediate(
        page,
        `OPEN "${RESULT_NAME}" FOR OUTPUT AS #1:PRINT #1,${valueExpr};:CLOSE #1`,
        500,
    );
    let bytes = null;
    for (let i = 0; i < 30; i++) {
        bytes = await fsRead(page, RESULT_PATH);
        if (bytes && bytes.length > 0) break;
        await sleep(100);
    }
    if (!bytes) return { ok: false, detail: 'no result file' };
    const text = String.fromCharCode(...bytes);
    return { ok: text === expected, detail: `captured=${JSON.stringify(text)} expected=${JSON.stringify(expected)}` };
}

async function setupFixture(page) {
    // Switch to B:, root, build a clean /smoke directory.
    await runImmediate(page, 'B:', 300);
    await runImmediate(page, 'CHDIR "/"', 300);
    // Best-effort cleanup of any prior run.
    await runImmediate(page, 'ON ERROR SKIP : CHDIR "/smoke"', 200);
    for (const d of FIXTURE_DIRS) {
        await runImmediate(page, `ON ERROR SKIP : RMDIR "${d}"`, 100);
    }
    for (const f of FIXTURE_FILES) {
        await runImmediate(page, `ON ERROR SKIP : KILL "${f.name}"`, 100);
    }
    await runImmediate(page, 'ON ERROR SKIP : KILL "r.txt"', 100);
    await runImmediate(page, 'CHDIR "/"', 200);
    await runImmediate(page, 'ON ERROR SKIP : RMDIR "smoke"', 200);
    // Create fresh.
    await runImmediate(page, 'MKDIR "/smoke"', 300);
    await runImmediate(page, 'CHDIR "/smoke"', 300);
    for (const d of FIXTURE_DIRS) {
        await runImmediate(page, `MKDIR "${d}"`, 200);
    }
    for (const f of FIXTURE_FILES) {
        await runImmediate(
            page,
            `OPEN "${f.name}" FOR OUTPUT AS #1 : PRINT #1,"${f.content}"; : CLOSE #1`,
            250,
        );
    }
}

async function teardownFixture(page) {
    for (const f of FIXTURE_FILES) {
        await runImmediate(page, `ON ERROR SKIP : KILL "${f.name}"`, 100);
    }
    await runImmediate(page, 'ON ERROR SKIP : KILL "r.txt"', 100);
    for (const d of FIXTURE_DIRS) {
        await runImmediate(page, `ON ERROR SKIP : RMDIR "${d}"`, 100);
    }
    await runImmediate(page, 'CHDIR "/"', 200);
    await runImmediate(page, 'ON ERROR SKIP : RMDIR "smoke"', 200);
}

async function main(page) {
    const cases = [];

    await setupFixture(page);

    // Read-back the smallest file as a basic file I/O smoke.
    cases.push(['file_read_back',
        await runReadBackStep(
            page,
            'OPEN "aaa.bas" FOR INPUT AS #1 : LINE INPUT #1, x$ : CLOSE #1',
            'x$',
            'AA',
        )]);

    // FILES with each sort mode. cmd_files's multi-pass selection
    // sort is shared with Pico/ESP32; the sentinel-after proves the
    // interpreter survived the call.
    const sortVariants = [
        ['files_default',   'FILES'],
        ['files_name_sort', 'FILES "*", NAME'],
        ['files_size_sort', 'FILES "*", SIZE'],
        ['files_type_sort', 'FILES "*", TYPE'],
        ['files_time_sort', 'FILES "*", TIME'],
    ];
    for (const [label, command] of sortVariants) {
        cases.push([label, await runSentinelStep(page, command, 'FILES_OK', 2000)]);
    }

    // Append to an existing file + read back to verify the FS state
    // wasn't corrupted by the FILES calls.
    cases.push(['append_after_files',
        await runReadBackStep(
            page,
            'OPEN "aaa.bas" FOR APPEND AS #1 : PRINT #1, "ZZ"; : CLOSE #1 : ' +
            'OPEN "aaa.bas" FOR INPUT AS #1 : LINE INPUT #1, x$ : CLOSE #1',
            'x$',
            'AAZZ',
        )]);

    await teardownFixture(page);
    return cases;
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

        await sleep(1800);
        await page.click('#screen');

        const cases = await main(page);

        let passed = 0;
        const failures = [];
        for (const [name, { ok, detail }] of cases) {
            const status = ok ? 'OK  ' : 'FAIL';
            console.log(`  ${status}  ${name.padEnd(28)}  ${detail}`);
            if (ok) passed += 1;
            else failures.push({ name, detail });
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
    try { server.kill(); } catch {}
}
