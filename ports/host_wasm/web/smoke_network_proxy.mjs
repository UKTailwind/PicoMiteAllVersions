// Headless smoke for host-WASM proxy-mode HTTP client requests.
//
// Starts a local HTTP peer on a different port with no CORS headers, serves the
// app through ports/host_native/build/wasm_network_proxy, then verifies WEB TCP CLIENT REQUEST
// returns the peer's raw HTTP response through the BASIC long-string buffer.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import http from 'node:http';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8132;
const PEER_PORT = 8133;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

async function startPeer() {
    const server = http.createServer((req, res) => {
        let body = '';
        req.setEncoding('utf8');
        req.on('data', (chunk) => { body += chunk; });
        req.on('end', () => {
            res.statusCode = 200;
            res.setHeader('Content-Type', 'text/plain');
            res.setHeader('Connection', 'close');
            res.end(`PROXY_SMOKE_BODY path=${req.url} method=${req.method} body=${body}`);
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
                if (caps?.features?.http_proxy === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with http_proxy capability');
}

const peer = await startPeer();
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
            () => window.picomite?.proxy?.caps?.features?.http_proxy === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const prog = [
            '10 OPEN "net_proxy.txt" FOR OUTPUT AS #1',
            '20 DIM INTEGER A%(8192/8)',
            '30 CR$=CHR$(13)+CHR$(10)',
            '40 PRINT #1,"TCPIP=";MM.INFO(TCPIP STATUS)',
            `50 WEB OPEN TCP CLIENT "127.0.0.1",${PEER_PORT},5000`,
            '60 WEB TCP CLIENT REQUEST "GET /proxy-smoke HTTP/1.0"+CR$+"Host: 127.0.0.1"+CR$+CR$,A%(),5000',
            '70 WEB CLOSE TCP CLIENT',
            '80 PRINT #1,"LEN=";LLEN(A%())',
            '90 PRINT #1,"HEAD=";LGETSTR$(A%(),1,12)',
            '100 PRINT #1,"BODY=";INSTR(LGETSTR$(A%(),1,LLEN(A%())),"PROXY_SMOKE_BODY")',
            '110 PRINT #1,"PATH=";INSTR(LGETSTR$(A%(),1,LLEN(A%())),"path=/proxy-smoke")',
            '120 CLOSE #1',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/net_proxy.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('RUN "net_proxy.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        let output = '';
        for (let i = 0; i < 50; i++) {
            output = await page.evaluate(async () => {
                try {
                    const bytes = await window.picomite.fsRead('/sd/net_proxy.txt');
                    return new TextDecoder().decode(bytes);
                } catch {
                    return '';
                }
            });
            if (output.includes('PATH=')) break;
            await sleep(200);
        }
        if (!output.includes('PATH=')) {
            fail(`timed out waiting for BASIC output file; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        const checks = [
            ['TCPIP online', /TCPIP=\s*1\b/],
            ['LEN positive', /LEN=\s*[1-9][0-9]*\b/],
            ['HEAD status', /HEAD=HTTP\/1\.1 200/],
            ['body marker', /BODY=\s*[1-9][0-9]*\b/],
            ['path marker', /PATH=\s*[1-9][0-9]*\b/],
        ];
        for (const [label, re] of checks) {
            if (!re.test(output)) {
                fail(`${label} missing from output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
            }
        }

        console.log('OK - proxy mode detected http_proxy capability.');
        console.log('OK - WEB TCP CLIENT REQUEST returned non-CORS peer response through C proxy.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
    await new Promise((resolve) => peer.close(resolve));
}
