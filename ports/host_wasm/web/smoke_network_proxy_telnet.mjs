// Headless smoke for host-WASM proxy-mode Telnet console.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import net from 'node:net';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8146;
const TELNET_PORT = 8147;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/?telnet_port=${TELNET_PORT}`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
}

function startProxy() {
    const proxyPath = new URL('../wasm_network_proxy', import.meta.url).pathname;
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
    const url = `http://127.0.0.1:${PROXY_PORT}/__picomite_proxy/caps`;
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(url);
            if (r.ok) {
                const caps = await r.json();
                if (caps?.features?.telnet === true &&
                    caps?.features?.tcp_server === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with telnet capability');
}

function connectTelnet() {
    return new Promise((resolve, reject) => {
        const sock = net.createConnection({ host: '127.0.0.1', port: TELNET_PORT });
        const timer = setTimeout(() => {
            sock.destroy();
            reject(new Error('telnet connect timeout'));
        }, 5000);
        sock.once('connect', () => {
            clearTimeout(timer);
            resolve(sock);
        });
        sock.once('error', (err) => {
            clearTimeout(timer);
            reject(err);
        });
    });
}

async function connectTelnetWithRetry() {
    let lastErr = null;
    for (let i = 0; i < 40; i++) {
        try {
            return await connectTelnet();
        } catch (e) {
            lastErr = e;
            await sleep(150);
        }
    }
    throw lastErr || new Error('telnet connect failed');
}

function readUntil(sock, needle, timeoutMs) {
    return new Promise((resolve) => {
        let data = Buffer.alloc(0);
        const timer = setTimeout(done, timeoutMs);
        function done() {
            clearTimeout(timer);
            sock.off('data', onData);
            resolve(data);
        }
        function onData(chunk) {
            data = Buffer.concat([data, chunk]);
            if (data.includes(needle)) done();
        }
        sock.on('data', onData);
    });
}

function waitForClose(sock, timeoutMs) {
    return new Promise((resolve) => {
        if (sock.destroyed) {
            resolve(true);
            return;
        }
        const timer = setTimeout(() => {
            cleanup();
            resolve(false);
        }, timeoutMs);
        function cleanup() {
            clearTimeout(timer);
            sock.off('close', onClose);
            sock.off('end', onClose);
        }
        function onClose() {
            cleanup();
            resolve(true);
        }
        sock.once('close', onClose);
        sock.once('end', onClose);
    });
}

async function telnetRoundTrip(command, marker) {
    const sock = await connectTelnetWithRetry();
    sock.setNoDelay(true);
    try {
        const initial = await Promise.race([
            readUntil(sock, Buffer.from([0]), 300),
            sleep(300).then(() => Buffer.alloc(0)),
        ]);
        if (initial.length) {
            throw new Error(`unexpected initial telnet bytes: ${initial.toString('hex')}`);
        }
        sock.write(command);
        const data = await readUntil(sock, Buffer.from(marker, 'latin1'), 8000);
        if (!data.includes(Buffer.from(marker, 'latin1'))) {
            throw new Error(`missing ${marker}; received ${data.toString('latin1')}`);
        }
        return sock;
    } catch (e) {
        sock.destroy();
        throw e;
    }
}

function telnetCommand() {
    if (process.env.TELNET) return process.env.TELNET;
    if (existsSync('/opt/homebrew/bin/telnet')) return '/opt/homebrew/bin/telnet';
    return 'telnet';
}

async function realTelnetClientRoundTrip() {
    const child = spawn(telnetCommand(), ['127.0.0.1', String(TELNET_PORT)], {
        stdio: ['pipe', 'pipe', 'pipe'],
    });
    let output = Buffer.alloc(0);
    let spawnError = null;
    child.stdout.on('data', (chunk) => {
        output = Buffer.concat([output, Buffer.from(chunk)]);
    });
    child.stderr.on('data', (chunk) => {
        output = Buffer.concat([output, Buffer.from(chunk)]);
    });
    child.once('error', (err) => {
        spawnError = err;
    });

    function write(text) {
        if (!child.stdin.writable) throw new Error('telnet stdin closed');
        child.stdin.write(text, 'latin1');
    }

    async function waitFor(marker, timeoutMs = 8000) {
        const needle = Buffer.from(marker, 'latin1');
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
            if (spawnError) throw spawnError;
            if (output.includes(needle)) return;
            if (child.exitCode !== null) break;
            await sleep(50);
        }
        throw new Error(`real telnet missing ${marker}; received ${output.toString('latin1')}`);
    }

    try {
        await waitFor('Connected', 5000);
        write('PRINT 2+3\r\n');
        await waitFor(' 5');
        write('a\r\n');
        await waitFor('Unknown command');
        if (child.exitCode !== null) {
            throw new Error(`real telnet closed after BASIC error; received ${output.toString('latin1')}`);
        }
        write('PRINT 6+7\r\n');
        await waitFor(' 13');
    } finally {
        child.kill('SIGTERM');
    }
}

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
            () => window.picomite?.proxy?.caps?.features?.telnet === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        await page.evaluate(() => {
            window.picomite.fsWrite('/sd/telnet_reset.bas',
                new TextEncoder().encode('10 PRINT "TELNET RESET RUN"\n20 END\n'));
        });
        await sleep(200);
        await page.keyboard.type('option telnet console on', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(600);

        const telnetPrelude = Buffer.from([
            255, 253, 3,
            255, 251, 3,
            255, 252, 34,
        ]);
        const firstSock = await telnetRoundTrip(
            Buffer.concat([telnetPrelude, Buffer.from('A=7+8:PRINT A\r\n', 'latin1')]),
            ' 15',
        );

        await page.keyboard.type('RUN "telnet_reset.bas"', { delay: 5 });
        await page.keyboard.press('Enter');
        const closedAfterRun = await waitForClose(firstSock, 1000);
        if (closedAfterRun) {
            fail(`active Telnet session closed after RUN.\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }
        firstSock.write('A=3+4:PRINT A\r');
        const afterRun = await readUntil(firstSock, Buffer.from(' 7', 'latin1'), 8000);
        if (!afterRun.includes(Buffer.from(' 7', 'latin1'))) {
            fail(`Telnet session did not survive RUN; received ${afterRun.toString('latin1')}`);
        }
        firstSock.destroy();

        await sleep(1000);
        const secondSock = await telnetRoundTrip('A=4+5:PRINT A\r', ' 9');
        secondSock.destroy();

        await realTelnetClientRoundTrip();

        console.log('OK - proxy mode detected Telnet capability.');
        console.log('OK - Telnet input merged into the WASM console and received PRINT output.');
        console.log('OK - RUN preserved the active Telnet session and listener.');
        console.log('OK - real telnet client stayed connected after a BASIC error.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
}
