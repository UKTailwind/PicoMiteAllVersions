// Headless smoke for host-WASM proxy-mode TCP client streams.
//
// Starts a local TCP peer, serves the app through ports/host_native/build/wasm_network_proxy, then
// verifies WEB OPEN TCP STREAM / WEB TCP CLIENT STREAM exchange bytes through
// the C proxy.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import net from 'node:net';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8134;
const PEER_PORT = 8135;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

async function startPeer(log) {
    const server = net.createServer((socket) => {
        let data = Buffer.alloc(0);
        socket.on('data', (chunk) => {
            data = Buffer.concat([data, chunk]);
            if (!log.request && data.includes(0x0a)) {
                log.request = data;
                socket.write(Buffer.concat([Buffer.from('ACK '), data]));
                for (let i = 0; i < 4; i++) {
                    setTimeout(() => {
                        if (!socket.destroyed) socket.write(`STREAM${i}\n`);
                    }, 60 * (i + 1));
                }
            }
        });
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(PEER_PORT, '127.0.0.1', resolve);
    });
    return server;
}

function startProxy() {
    const proxyPath = new URL('../../host_native/build/wasm_network_proxy', import.meta.url).pathname;
    const webRoot = new URL('.', import.meta.url).pathname;
    const child = spawn(proxyPath, ['--port', String(PROXY_PORT), '--web-root', webRoot], {
        cwd: new URL('../..', import.meta.url).pathname,
        stdio: ['ignore', 'pipe', 'inherit'],
    });
    child.stdout.on('data', (buf) => process.stdout.write(buf));
    child.unref();
    return child;
}

async function waitForProxy() {
    const url = `${PAGE_URL}__picomite_proxy/caps`;
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(url);
            if (r.ok) {
                const caps = await r.json();
                if (caps?.features?.tcp_stream === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with tcp_stream capability');
}

const log = { request: null };
const peer = await startPeer(log);
const proxy = startProxy();

try {
    await waitForProxy();
    const browser = await chromium.launch({
        headless: true,
        args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
    });
    try {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        const logs = [];
        page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
        page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));

        await page.goto(PAGE_URL, { waitUntil: 'load' });
        await page.waitForFunction(() => !!window.picomite?.memoryU32, { timeout: 25000 });
        await page.waitForFunction(() => window.picomite?.proxy?.online === true, { timeout: 5000 });
        await page.waitForFunction(
            () => window.picomite?.proxy?.caps?.features?.tcp_stream === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const prog = [
            '10 OPEN "net_proxy_stream.txt" FOR OUTPUT AS #1',
            '20 DIM INTEGER S%(512/8)',
            '30 DIM INTEGER R%,W%',
            '40 R%=0:W%=0',
            `50 WEB OPEN TCP STREAM "127.0.0.1",${PEER_PORT},5000`,
            '60 WEB TCP CLIENT STREAM "INLINE"+CHR$(10),S%(),R%,W%',
            '70 FOR I%=1 TO 25',
            '80 PAUSE 100',
            '90 WEB TCP CLIENT STREAM "",S%(),R%,W%',
            '100 NEXT I%',
            '110 S%(0)=W%',
            '120 PRINT #1,"PTR=";R%;",";W%',
            '130 PRINT #1,"DATA=";LGETSTR$(S%(),1,W%)',
            '140 WEB CLOSE TCP CLIENT',
            '150 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/net_proxy_stream.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('RUN "net_proxy_stream.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        let output = '';
        for (let i = 0; i < 80; i++) {
            output = await page.evaluate(async () => {
                try {
                    const bytes = await window.picomite.fsRead('/sd/net_proxy_stream.txt');
                    return new TextDecoder().decode(bytes);
                } catch {
                    return '';
                }
            });
            if (output.includes('DATA=')) break;
            await sleep(200);
        }
        if (!output.includes('DATA=')) {
            fail(`timed out waiting for BASIC output file; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        if (!log.request || !log.request.equals(Buffer.from('INLINE\n'))) {
            fail(`peer did not receive INLINE newline request: ${log.request?.toString('latin1')}`);
        }
        if (!/ACK INLINE/.test(output) || !/STREAM3/.test(output)) {
            fail(`stream response missing from output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        console.log('OK - proxy mode detected tcp_stream capability.');
        console.log('OK - WEB TCP CLIENT STREAM exchanged INLINE and streamed chunks through C proxy.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
    await new Promise((resolve) => peer.close(resolve));
}
