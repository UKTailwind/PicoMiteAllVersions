// Headless end-to-end for the in-app editor. Boots the page, waits
// for ready, clicks the edit button on a bundled .bas demo, verifies
// CodeMirror shows up with the file's contents, types a change,
// Ctrl+S saves, and reads back via fsRead to confirm the save landed.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';

const PORT = 8131;
const PAGE_URL = `http://127.0.0.1:${PORT}/`;

function startServer() {
    const cwd = new URL('.', import.meta.url).pathname;
    const child = spawn('python3', ['./serve.py', String(PORT)], { cwd, stdio: ['ignore', 'ignore', 'inherit'] });
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
        await page.waitForFunction(() => !!window.picomite?.memoryBytes, { timeout: 25000 });
        console.log('OK — app booted.');

        // Click the edit button on demo_hello.bas (first editable .bas
        // in the list; edit button has title starting with "Edit ").
        await page.waitForSelector('#files-list li');
        const editBtn = await page.evaluateHandle(() => {
            for (const li of document.querySelectorAll('#files-list li')) {
                if (li.querySelector('.file-name')?.textContent === 'demo_hello.bas') {
                    return li.querySelector('.file-actions button[title^="Edit "]');
                }
            }
            return null;
        });
        if (!(await editBtn.asElement())) fail('edit button for demo_hello.bas not found');
        await editBtn.asElement().click();

        // Wait for CodeMirror to mount.
        await page.waitForSelector('#editor-host .cm-editor', { timeout: 15000 });
        console.log('OK — CodeMirror mounted.');

        // Read what CodeMirror shows — first few lines of demo_hello.bas.
        const content = await page.evaluate(() =>
            document.querySelector('#editor-host .cm-content')?.textContent || '');
        if (!content.length) fail('CodeMirror content is empty');
        console.log(`OK — editor has ${content.length} chars of demo_hello.bas.`);

        const original = await page.evaluate(async () => {
            const bytes = await window.picomite.fsRead('/sd/demo_hello.bas');
            return new TextDecoder().decode(bytes);
        });

        // Replace the buffer with a marked copy. Using insertText avoids
        // platform-specific Home/Cmd/Ctrl cursor behavior in CodeMirror.
        await page.click('#editor-host .cm-content');
        await page.keyboard.press(process.platform === 'darwin' ? 'Meta+A' : 'Control+A');
        await page.keyboard.insertText("' EDIT_TEST_MARKER\n" + original);

        const inBuffer = await page.evaluate(() =>
            document.querySelector('#editor-host .cm-content')?.textContent || '');
        console.log('  editor buffer first line:', JSON.stringify(inBuffer.slice(0, 40)));

        await page.keyboard.press(process.platform === 'darwin' ? 'Meta+S' : 'Control+S');
        await sleep(400);

        const after = await page.evaluate(async () => {
            const bytes = await window.picomite.fsRead('/sd/demo_hello.bas');
            return new TextDecoder().decode(bytes);
        });
        if (!after.startsWith("' EDIT_TEST_MARKER")) {
            fail(`save did not round-trip; got first line: ${JSON.stringify(after.slice(0, 60))}`);
        }
        console.log('OK — Ctrl+S saved through fsWrite and the marker is in /sd/.');

        // Esc closes.
        await page.keyboard.press('Escape');
        await sleep(100);
        const hidden = await page.evaluate(() => document.getElementById('editor-area').hidden);
        if (!hidden) fail('editor did not close on Escape');
        console.log('OK — editor closed on Escape.');

        console.log('All editor smoke checks passed.');
    } finally {
        await browser.close();
    }
} finally {
    server.kill('SIGTERM');
}
