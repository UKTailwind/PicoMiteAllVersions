// Headless smoke for host-WASM proxy-mode TFTP.
//
// The browser page uses ?tftp_port=... as a test-only override so the shared
// TFTP core binds an unprivileged UDP port through the proxy instead of port 69.

import { chromium } from '/Users/joshv/.local/state/yolobox/instances/pico-gamer-main/checkout/web/node_modules/playwright/index.mjs';
import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import { setTimeout as sleep } from 'node:timers/promises';

const PROXY_PORT = 8144;
const TFTP_PORT = 8145;
const PAGE_URL = `http://127.0.0.1:${PROXY_PORT}/?tftp_port=${TFTP_PORT}`;

function fail(msg) {
    console.error('FAIL -', msg);
    process.exit(1);
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
    const url = `http://127.0.0.1:${PROXY_PORT}/__picomite_proxy/caps`;
    for (let i = 0; i < 60; i++) {
        try {
            const r = await fetch(url);
            if (r.ok) {
                const caps = await r.json();
                if (caps?.features?.udp === true && caps?.features?.tftp === true) return;
            }
        } catch {}
        await sleep(200);
    }
    throw new Error('proxy did not come up with udp and tftp capabilities');
}

function makeTftpClient() {
    const sock = dgram.createSocket('udp4');
    const queue = [];
    const waiters = [];
    sock.on('message', (msg, rinfo) => {
        const item = { msg: Buffer.from(msg), rinfo };
        const waiter = waiters.shift();
        if (waiter) waiter(item);
        else queue.push(item);
    });
    return {
        sock,
        bind: () => new Promise((resolve, reject) => {
            sock.once('error', reject);
            sock.bind(0, '127.0.0.1', resolve);
        }),
        send: (packet, port = TFTP_PORT) => new Promise((resolve, reject) => {
            sock.send(packet, port, '127.0.0.1', (err) => err ? reject(err) : resolve());
        }),
        recv: (timeoutMs = 3000) => new Promise((resolve, reject) => {
            if (queue.length) {
                resolve(queue.shift());
                return;
            }
            const timer = setTimeout(() => {
                const idx = waiters.indexOf(done);
                if (idx >= 0) waiters.splice(idx, 1);
                reject(new Error('tftp timeout'));
            }, timeoutMs);
            const done = (item) => {
                clearTimeout(timer);
                resolve(item);
            };
            waiters.push(done);
        }),
        close: () => sock.close(),
    };
}

function requestPacket(opcode, filename, blockSize = null) {
    const parts = [
        Buffer.from([0, opcode]),
        Buffer.from(filename, 'utf8'),
        Buffer.from([0]),
        Buffer.from('octet', 'ascii'),
        Buffer.from([0]),
    ];
    if (blockSize) {
        parts.push(Buffer.from('blksize', 'ascii'), Buffer.from([0]));
        parts.push(Buffer.from(String(blockSize), 'ascii'), Buffer.from([0]));
    }
    return Buffer.concat([
        ...parts,
    ]);
}

function dataPacket(block, payload) {
    const header = Buffer.alloc(4);
    header.writeUInt16BE(3, 0);
    header.writeUInt16BE(block, 2);
    return Buffer.concat([header, Buffer.from(payload)]);
}

function ackPacket(block) {
    const packet = Buffer.alloc(4);
    packet.writeUInt16BE(4, 0);
    packet.writeUInt16BE(block, 2);
    return packet;
}

function describeTftp(packet) {
    if (!packet || packet.length < 2) return 'short packet';
    const opcode = packet.readUInt16BE(0);
    if (opcode === 5 && packet.length >= 5) {
        return `ERROR ${packet.readUInt16BE(2)} ${packet.subarray(4, -1).toString('utf8')}`;
    }
    if (packet.length >= 4) return `opcode=${opcode} block=${packet.readUInt16BE(2)}`;
    return `opcode=${opcode}`;
}

function parseOack(packet) {
    if (!packet || packet.length < 2 || packet.readUInt16BE(0) !== 6) return null;
    const parts = packet.subarray(2).toString('ascii').split('\0');
    const opts = new Map();
    for (let i = 0; i + 1 < parts.length; i += 2) {
        if (!parts[i]) break;
        opts.set(parts[i].toLowerCase(), parts[i + 1]);
    }
    return opts;
}

async function tftpWrite(filename, payload) {
    const client = makeTftpClient();
    await client.bind();
    try {
        await client.send(requestPacket(2, filename, 508));
        let { msg, rinfo } = await client.recv();
        let blockSize = 512;
        const oack = parseOack(msg);
        if (oack) {
            blockSize = Number.parseInt(oack.get('blksize') || '512', 10);
        } else if (msg.length < 4 || msg.readUInt16BE(0) !== 4 || msg.readUInt16BE(2) !== 0) {
            throw new Error(`expected ACK0, got ${describeTftp(msg)}`);
        }
        let block = 1;
        for (let offset = 0; offset < payload.length; offset += blockSize) {
            await client.send(dataPacket(block, payload.subarray(offset, offset + blockSize)), rinfo.port);
            ({ msg, rinfo } = await client.recv());
            if (msg.length < 4 || msg.readUInt16BE(0) !== 4 || msg.readUInt16BE(2) !== block) {
                throw new Error(`expected ACK${block}, got ${describeTftp(msg)}`);
            }
            block++;
        }
        if (payload.length % blockSize === 0) {
            await client.send(dataPacket(block, Buffer.alloc(0)), rinfo.port);
            ({ msg } = await client.recv());
            if (msg.length < 4 || msg.readUInt16BE(0) !== 4 || msg.readUInt16BE(2) !== block) {
                throw new Error(`expected final ACK${block}, got ${describeTftp(msg)}`);
            }
        }
    } finally {
        client.close();
    }
}

async function tftpRead(filename) {
    const client = makeTftpClient();
    await client.bind();
    try {
        await client.send(requestPacket(1, filename, 508));
        let { msg, rinfo } = await client.recv();
        let blockSize = 512;
        const oack = parseOack(msg);
        if (oack) {
            blockSize = Number.parseInt(oack.get('blksize') || '512', 10);
            await client.send(ackPacket(0), rinfo.port);
            ({ msg, rinfo } = await client.recv());
        }
        const chunks = [];
        let block = 1;
        while (true) {
            if (msg.length < 4 || msg.readUInt16BE(0) !== 3 || msg.readUInt16BE(2) !== block) {
                throw new Error(`expected DATA${block}, got ${describeTftp(msg)}`);
            }
            const chunk = msg.subarray(4);
            chunks.push(chunk);
            await client.send(ackPacket(block), rinfo.port);
            if (chunk.length < blockSize) break;
            block++;
            ({ msg, rinfo } = await client.recv());
        }
        return Buffer.concat(chunks);
    } finally {
        client.close();
    }
}

async function readOutput(page) {
    return page.evaluate(async () => {
        try {
            const bytes = await window.picomite.fsRead('/sd/net_proxy_tftp.txt');
            return new TextDecoder().decode(bytes);
        } catch {
            return '';
        }
    });
}

async function waitForOutput(page, marker, logs, loops = 80) {
    let output = '';
    for (let i = 0; i < loops; i++) {
        output = await readOutput(page);
        if (output.includes(marker)) return output;
        await sleep(200);
    }
    fail(`timed out waiting for ${marker}; last output:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
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
            () => window.picomite?.proxy?.caps?.features?.udp === true &&
                  window.picomite?.proxy?.caps?.features?.tftp === true,
            { timeout: 5000 },
        );
        await page.click('#screen');

        const prog = [
            '10 OPEN "net_proxy_tftp.txt" FOR OUTPUT AS #1',
            '20 PRINT #1,"TCPIP=";MM.INFO(TCPIP STATUS)',
            '30 PRINT #1,"TFTPREADY"',
            '40 CLOSE #1',
            '50 FOR I%=1 TO 300',
            '60 PAUSE 50',
            '70 NEXT I%',
        ].join('\n') + '\n';

        await page.evaluate((source) => {
            window.picomite.fsWrite('/sd/net_proxy_tftp.bas', new TextEncoder().encode(source));
        }, prog);
        await sleep(200);
        await page.keyboard.type('NEW', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(100);
        await page.keyboard.type('OPTION TFTP ON', { delay: 5 });
        await page.keyboard.press('Enter');
        await sleep(400);
        await page.keyboard.type('RUN "net_proxy_tftp.bas"', { delay: 5 });
        await page.keyboard.press('Enter');

        const output = await waitForOutput(page, 'TFTPREADY', logs);
        if (!/TCPIP=\s*1\b/.test(output)) {
            fail(`TCPIP status did not report online:\n${output}\nConsole tail:\n${logs.slice(-20).join('\n')}`);
        }

        const payload = Buffer.concat([
            Buffer.from('WASM_TFTP_PAYLOAD\n', 'utf8'),
            Buffer.from(Array.from({ length: 1100 }, (_, i) => i % 251)),
        ]);
        await tftpWrite('tftp_in.txt', payload);

        const persisted = await page.evaluate(async () => {
            const bytes = await window.picomite.fsRead('/sd/tftp_in.txt');
            return Array.from(bytes);
        });
        if (!Buffer.from(persisted).equals(payload)) {
            fail(`WRQ payload was not persisted through WASM filesystem: ${Buffer.from(persisted).toString('utf8')}`);
        }

        const readBack = await tftpRead('tftp_in.txt');
        if (!readBack.equals(payload)) {
            fail(`RRQ payload mismatch: ${readBack.toString('utf8')}`);
        }

        console.log('OK - proxy mode detected tftp capability.');
        console.log('OK - shared TFTP core served WRQ/RRQ over proxied UDP.');
        console.log('OK - TFTP file data persisted through the WASM /sd filesystem.');
    } finally {
        await browser.close();
    }
} finally {
    proxy.kill('SIGTERM');
}
